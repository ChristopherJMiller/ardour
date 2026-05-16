/*
 * Copyright (C) 2026 Frank Povazanj <frank.povazanj@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "pbd/basename.h"
#include "pbd/controllable.h"
#include "pbd/enumwriter.h"
#include "pbd/error.h"
#include "pbd/event_loop.h"
#include "pbd/id.h"
#include "pbd/memento_command.h"
#include "pbd/pthread_utils.h"
#include "pbd/stateful_diff_command.h"
#include "pbd/xml++.h"

#include "ardour/amp.h"
#include "ardour/audio_track.h"
#include "ardour/audioregion.h"
#include "ardour/dB.h"
#include "ardour/internal_send.h"
#include "ardour/location.h"
#include "ardour/midi_model.h"
#include "ardour/midi_region.h"
#include "ardour/midi_source.h"
#include "ardour/midi_track.h"
#include "ardour/playlist.h"
#include "ardour/plugin.h"
#include "ardour/plugin_insert.h"
#include "ardour/plugin_manager.h"
#include "ardour/presentation_info.h"
#include "ardour/processor.h"
#include "ardour/rc_configuration.h"
#include "ardour/region.h"
#include "ardour/region_factory.h"
#include "ardour/route.h"
#include "ardour/selection.h"
#include "ardour/session.h"
#include "ardour/session_event.h"
#include "ardour/source.h"
#include "ardour/stripable.h"
#include "ardour/tempo.h"
#include "ardour/track.h"

#include "handlers/common.h"
#include "handlers/markers.h"
#include "handlers/plugins.h"
#include "handlers/regions_midi.h"
#include "handlers/session.h"
#include "handlers/track.h"
#include "handlers/tracks.h"
#include "handlers/transport.h"
#include "mcp_http_server.h"

namespace pt = boost::property_tree;

using namespace ArdourSurface;

namespace
{

template <typename T>
static std::optional<T>
get_optional_std (const pt::ptree& tree, const std::string& path)
{
	const boost::optional<T> value = tree.get_optional<T> (path);
	if (value) {
		return *value;
	}
	return std::nullopt;
}


static std::string
json_escape (const std::string& s)
{
	std::ostringstream o;

	for (std::string::const_iterator it = s.begin (); it != s.end (); ++it) {
		if (*it == '"' || *it == '\\' || ('\x00' <= *it && *it <= '\x1f')) {
			o << "\\u" << std::hex << std::setw (4) << std::setfill ('0') << static_cast<int> (*it);
		} else {
			o << *it;
		}
	}

	return o.str ();
}

static std::string
canonical_tool_name (std::string tool_name)
{
	/* Some MCP clients only support function-safe identifiers, so accept
	 * underscore and dotted aliases in addition to slash-delimited names.
	 */
	std::replace (tool_name.begin (), tool_name.end (), '.', '/');

	if (tool_name.find ('/') != std::string::npos) {
		return tool_name;
	}

	static const char* known_groups[] = {
		"session",
		"transport",
		"markers",
		"tracks",
		"buses",
		"track",
		"region",
		"plugin",
		"midi_region",
		"midi_note"
	};

	for (size_t i = 0; i < (sizeof (known_groups) / sizeof (known_groups[0])); ++i) {
		const std::string group (known_groups[i]);
		const std::string prefix = group + "_";
		if (tool_name.size () <= prefix.size ()) {
			continue;
		}
		if (tool_name.compare (0, prefix.size (), prefix) == 0) {
			tool_name[group.size ()] = '/';
			return tool_name;
		}
	}

	return tool_name;
}

static bool
is_decimal_pbd_id_string (const std::string& s)
{
	if (s.empty ()) {
		return false;
	}

	for (std::string::const_iterator i = s.begin (); i != s.end (); ++i) {
		if (!std::isdigit ((unsigned char)*i)) {
			return false;
		}
	}

	return true;
}

static ARDOUR::Location*
location_by_mcp_id (ARDOUR::Locations& locations, const std::string& id)
{
	/* Defensive guard:
	 * PBD::ID(string) does not fail-closed on parse errors, so reject
	 * non-decimal MCP IDs before constructing an ID object.
	 */
	if (!is_decimal_pbd_id_string (id)) {
		return 0;
	}

	return locations.get_location_by_id (PBD::ID (id));
}

static std::shared_ptr<ARDOUR::Region>
region_by_mcp_id (const std::string& id)
{
	if (!is_decimal_pbd_id_string (id)) {
		return std::shared_ptr<ARDOUR::Region> ();
	}

	return ARDOUR::RegionFactory::region_by_id (PBD::ID (id));
}

static std::shared_ptr<ARDOUR::Route>
route_by_mcp_id (ARDOUR::Session& session, const std::string& id)
{
	if (!is_decimal_pbd_id_string (id)) {
		return std::shared_ptr<ARDOUR::Route> ();
	}

	return session.route_by_id (PBD::ID (id));
}

static bool
is_number_literal (const std::string& s)
{
	if (s.empty ()) {
		return false;
	}

	char* endptr = 0;
	std::strtod (s.c_str (), &endptr);
	return endptr && *endptr == '\0';
}

static int
clamp_debug_level (int level)
{
	if (level < 0) {
		return 0;
	}
	if (level > 2) {
		return 2;
	}
	return level;
}

static std::string
tool_result_with_structured_text_fallback (const std::string& result_json)
{
	/* Compatibility policy: whenever structuredContent is present, mirror it
	 * as serialized JSON in content[0].text for clients that ignore structure.
	 *
	 * This helper assumes the tool result shape used in this server:
	 * {"content":[...],"structuredContent":<json>}
	 */
	static const std::string     key     = "\"structuredContent\":";
	const std::string::size_type key_pos = result_json.find (key);
	if (key_pos == std::string::npos) {
		return result_json;
	}

	std::string::size_type obj_end = result_json.find_last_not_of (" \t\r\n");
	if (obj_end == std::string::npos || result_json[obj_end] != '}') {
		return result_json;
	}

	std::string::size_type value_start = key_pos + key.size ();
	while (value_start < result_json.size () && std::isspace ((unsigned char)result_json[value_start])) {
		++value_start;
	}
	if (value_start >= obj_end) {
		return result_json;
	}

	const std::string structured_json = result_json.substr (value_start, obj_end - value_start);
	return std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + json_escape (structured_json) + "\"}],\"structuredContent\":" + structured_json + "}";
}

static std::string
jsonrpc_id (const pt::ptree& root)
{
	boost::optional<const pt::ptree&> id_node = root.get_child_optional ("id");
	if (!id_node) {
		return "null";
	}

	if (!id_node->empty ()) {
		return "null";
	}

	std::string id = id_node->data ();
	if (id.empty () || id == "null") {
		return "null";
	}

	if (id == "true" || id == "false" || is_number_literal (id)) {
		return id;
	}

	return std::string ("\"") + json_escape (id) + "\"";
}

static bool
has_jsonrpc_id (const pt::ptree& root)
{
	return root.get_child_optional ("id").is_initialized ();
}

static std::string
jsonrpc_result (const std::string& id, const std::string& result_json)
{
	const std::string normalized_result_json = tool_result_with_structured_text_fallback (result_json);
	return std::string ("{\"jsonrpc\":\"2.0\",\"id\":") + id + ",\"result\":" + normalized_result_json + "}";
}

static std::string
jsonrpc_error (const std::string& id, int code, const std::string& message)
{
	std::ostringstream ss;
	ss << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"error\":{\"code\":" << code << ",\"message\":\""
	   << json_escape (message) << "\"}}";
	return ss.str ();
}

static std::string
transport_state_string (ARDOUR::Session& session)
{
	if (session.transport_locating ()) {
		return "locating";
	}

	if (session.transport_rolling ()) {
		return "rolling";
	}

	return "stopped";
}

static std::string
transport_state_json (ARDOUR::Session& session)
{
	std::ostringstream ss;
	ss << "{\"rolling\":" << (session.transport_rolling () ? "true" : "false")
	   << ",\"speed\":" << session.transport_speed ()
	   << ",\"sample\":" << session.transport_sample ()
	   << ",\"state\":\"" << transport_state_string (session) << "\"}";
	return ss.str ();
}

static const char*
record_state_string (ARDOUR::RecordState state)
{
	switch (state) {
		case ARDOUR::Disabled:
			return "disabled";
		case ARDOUR::Enabled:
			return "enabled";
		case ARDOUR::Recording:
			return "recording";
		default:
			return "unknown";
	}
}

static double
transport_tempo_bpm (ARDOUR::Session& session)
{
	try {
		Temporal::TempoMap::SharedPtr tmap (Temporal::TempoMap::fetch ());
		return tmap->metric_at (Temporal::timepos_t (session.transport_sample ())).tempo ().quarter_notes_per_minute ();
	} catch (...) {
		return 120.0;
	}
}

static std::string
route_type_string (const std::shared_ptr<ARDOUR::Route>& route)
{
	if (!route) {
		return "route";
	}

	if (std::dynamic_pointer_cast<ARDOUR::MidiTrack> (route)) {
		return "midi_track";
	}
	if (std::dynamic_pointer_cast<ARDOUR::AudioTrack> (route)) {
		return "audio_track";
	}
	if (route->is_track ()) {
		return "track";
	}
	return "bus";
}


static std::string
marker_type_json (ARDOUR::Location::Flags flags)
{
	static const struct TypeName {
		const char* enum_name;
		const char* wire_name;
	} names[] = {
		{ "IsMark", "mark" },
		{ "IsHidden", "hidden" },
		{ "IsCueMarker", "cue" },
		{ "IsCDMarker", "cd" },
		{ "IsXrun", "xrun" },
		{ "IsSection", "section" },
		{ "IsScene", "scene" },
		{ "IsRangeMarker", "range" },
		{ "IsSessionRange", "session_range" },
		{ "IsAutoLoop", "auto_loop" },
		{ "IsAutoPunch", "auto_punch" },
		{ "IsClockOrigin", "clock_origin" },
		{ "IsSkip", "skip" }
	};

	const std::string  flags_text = enum_2_string (flags);
	std::ostringstream ss;
	ss << "[";

	bool   first = true;
	size_t start = 0;
	while (start < flags_text.size ()) {
		size_t comma = flags_text.find (',', start);
		if (comma == std::string::npos) {
			comma = flags_text.size ();
		}

		size_t token_begin = flags_text.find_first_not_of (" \t", start);
		size_t token_end   = comma;
		while (token_end > start && (flags_text[token_end - 1] == ' ' || flags_text[token_end - 1] == '\t')) {
			--token_end;
		}
		if (token_begin == std::string::npos || token_begin >= token_end) {
			start = comma + 1;
			continue;
		}

		std::string token = flags_text.substr (token_begin, token_end - token_begin);
		for (size_t i = 0; i < (sizeof (names) / sizeof (names[0])); ++i) {
			if (token == names[i].enum_name) {
				token = names[i].wire_name;
				break;
			}
		}

		if (!first) {
			ss << ",";
		}
		first = false;
		ss << "\"" << json_escape (token) << "\"";

		start = comma + 1;
	}

	ss << "]";
	return ss.str ();
}

static std::string
bbt_json_at_sample (samplepos_t sample)
{
	Temporal::BBT_Time bbt = Temporal::TempoMap::use ()->bbt_at (Temporal::timepos_t (sample));

	std::ostringstream text;
	text << bbt.bars << "|" << bbt.beats << "|" << bbt.ticks;

	std::ostringstream ss;
	ss << "{\"bars\":" << bbt.bars
	   << ",\"beats\":" << bbt.beats
	   << ",\"ticks\":" << bbt.ticks
	   << ",\"text\":\"" << text.str () << "\"}";
	return ss.str ();
}


static bool
parse_bbt_target_sample (int bar, double beat, samplepos_t& target_sample, std::string& error)
{
	error.clear ();

	if (bar < 1 || !std::isfinite (beat) || beat < 1.0) {
		error = "Invalid bar/beat (expected: bar>=1, beat>=1.0)";
		return false;
	}

	int32_t whole_beats = (int32_t)std::floor (beat);
	double  fractional  = beat - (double)whole_beats;

	if (whole_beats < 1 || fractional < 0.0) {
		error = "Invalid beat value";
		return false;
	}

	int32_t ticks = (int32_t)std::llround (fractional * (double)Temporal::ticks_per_beat);
	if (ticks >= Temporal::ticks_per_beat) {
		ticks = 0;
		++whole_beats;
	}

	Temporal::BBT_Argument bbt ((int32_t)bar, whole_beats, ticks);
	target_sample = Temporal::TempoMap::use ()->sample_at (bbt);
	return true;
}


static bool
parse_optional_bbt_target_sample (
    const pt::ptree&   root,
    const std::string& args_path,
    samplepos_t&       target_sample,
    bool&              have_target,
    std::string&       error)
{
	have_target = false;
	error.clear ();

	const std::optional<int>    bar_opt  = get_optional_std<int> (root, args_path + ".bar");
	const std::optional<double> beat_opt = get_optional_std<double> (root, args_path + ".beat");

	if ((bar_opt && !beat_opt) || (!bar_opt && beat_opt)) {
		error = "Provide both bar and beat, or neither";
		return false;
	}

	if (!bar_opt && !beat_opt) {
		return true;
	}

	const int    bar  = *bar_opt;
	const double beat = *beat_opt;
	if (!parse_bbt_target_sample (bar, beat, target_sample, error)) {
		return false;
	}

	have_target = true;
	return true;
}

static bool
parse_optional_timeline_boundary_sample (
    const pt::ptree&   root,
    const std::string& args_path,
    const std::string& sample_key,
    const std::string& bar_key,
    const std::string& beat_key,
    samplepos_t&       target_sample,
    bool&              have_target,
    std::string&       error)
{
	have_target = false;
	error.clear ();

	const std::optional<int64_t> sample_opt = get_optional_std<int64_t> (root, args_path + "." + sample_key);
	const std::optional<int>     bar_opt    = get_optional_std<int> (root, args_path + "." + bar_key);
	const std::optional<double>  beat_opt   = get_optional_std<double> (root, args_path + "." + beat_key);

	if ((bar_opt && !beat_opt) || (!bar_opt && beat_opt)) {
		error = std::string ("Provide both ") + bar_key + " and " + beat_key + ", or neither";
		return false;
	}

	if (sample_opt && (bar_opt || beat_opt)) {
		error = std::string ("Provide either ") + sample_key + " or " + bar_key + "+" + beat_key + ", not both";
		return false;
	}

	if (!sample_opt && !bar_opt && !beat_opt) {
		return true;
	}

	if (sample_opt) {
		if (*sample_opt < 0) {
			error = std::string ("Invalid ") + sample_key + " (expected >= 0)";
			return false;
		}
		target_sample = (samplepos_t)*sample_opt;
		have_target   = true;
		return true;
	}

	if (!parse_bbt_target_sample (*bar_opt, *beat_opt, target_sample, error)) {
		error = std::string ("Invalid ") + bar_key + "/" + beat_key + ": " + error;
		return false;
	}

	have_target = true;
	return true;
}


static bool
parse_range_endpoints (
    const pt::ptree&   root,
    const std::string& args_path,
    samplepos_t&       start_sample,
    samplepos_t&       end_sample,
    std::string&       error)
{
	error.clear ();
	start_sample = 0;
	end_sample   = 0;

	const std::optional<int64_t> start_sample_opt = get_optional_std<int64_t> (root, args_path + ".startSample");
	const std::optional<int64_t> end_sample_opt   = get_optional_std<int64_t> (root, args_path + ".endSample");
	const std::optional<int>     start_bar_opt    = get_optional_std<int> (root, args_path + ".startBar");
	const std::optional<double>  start_beat_opt   = get_optional_std<double> (root, args_path + ".startBeat");
	const std::optional<int>     end_bar_opt      = get_optional_std<int> (root, args_path + ".endBar");
	const std::optional<double>  end_beat_opt     = get_optional_std<double> (root, args_path + ".endBeat");

	if ((start_bar_opt && !start_beat_opt) || (!start_bar_opt && start_beat_opt)) {
		error = "Provide both startBar and startBeat, or neither";
		return false;
	}
	if ((end_bar_opt && !end_beat_opt) || (!end_bar_opt && end_beat_opt)) {
		error = "Provide both endBar and endBeat, or neither";
		return false;
	}
	if ((start_sample_opt && !end_sample_opt) || (!start_sample_opt && end_sample_opt)) {
		error = "Provide both startSample and endSample, or neither";
		return false;
	}

	const bool have_samples = start_sample_opt && end_sample_opt;
	const bool have_bbt     = start_bar_opt && start_beat_opt && end_bar_opt && end_beat_opt;

	if (have_samples && (start_bar_opt || start_beat_opt || end_bar_opt || end_beat_opt)) {
		error = "Provide either sample pair or bar+beat pair, not both";
		return false;
	}
	if (!have_samples && !have_bbt) {
		error = "Missing range endpoints (provide sample pair or bar+beat pair)";
		return false;
	}

	if (have_samples) {
		if (*start_sample_opt < 0 || *end_sample_opt < 0) {
			error = "Invalid sample (expected >= 0)";
			return false;
		}
		start_sample = (samplepos_t)*start_sample_opt;
		end_sample   = (samplepos_t)*end_sample_opt;
	} else {
		std::string bbt_error;
		if (!parse_bbt_target_sample (*start_bar_opt, *start_beat_opt, start_sample, bbt_error)) {
			error = std::string ("Invalid start ") + bbt_error;
			return false;
		}
		if (!parse_bbt_target_sample (*end_bar_opt, *end_beat_opt, end_sample, bbt_error)) {
			error = std::string ("Invalid end ") + bbt_error;
			return false;
		}
	}

	if (end_sample < start_sample) {
		error = "Invalid range: end before start";
		return false;
	}

	return true;
}


} // namespace

MCPHttpServer::MCPHttpServer (ARDOUR::Session& session, uint16_t port, int debug_level, PBD::EventLoop* event_loop)
	: _session (session)
	, _port (port)
	, _debug_level (clamp_debug_level (debug_level))
	, _event_loop (event_loop)
	, _context (0)
	, _running (false)
{
	memset (_protocols, 0, sizeof (_protocols));
	memset (&_info, 0, sizeof (_info));
}

MCPHttpServer::~MCPHttpServer ()
{
	stop ();
}

int
MCPHttpServer::start ()
{
	if (_context) {
		return 0;
	}

	_protocols[0].name                  = "mcp-http";
	_protocols[0].callback              = MCPHttpServer::lws_callback;
	_protocols[0].per_session_data_size = 0;
	_protocols[0].rx_buffer_size        = 0;
	_protocols[0].id                    = 0;
	_protocols[0].user                  = 0;
#if LWS_LIBRARY_VERSION_MAJOR >= 3
	_protocols[0].tx_packet_size = 0;
#endif

	_info.port      = _port;
	_info.protocols = _protocols;
	_info.gid       = -1;
	_info.uid       = -1;
	_info.user      = this;

	_context = lws_create_context (&_info);
	if (!_context) {
		PBD::error << "MCPHttp: could not create libwebsockets context" << endmsg;
		return -1;
	}

	_running        = true;
	_service_thread = std::thread (&MCPHttpServer::run, this);

	return 0;
}

int
MCPHttpServer::stop ()
{
	if (!_context) {
		return 0;
	}

	_running = false;
	lws_cancel_service (_context);

	if (_service_thread.joinable ()) {
		_service_thread.join ();
	}

	lws_context_destroy (_context);
	_context = 0;
	_clients.clear ();

	return 0;
}

void
MCPHttpServer::set_debug_level (int level)
{
	_debug_level.store (clamp_debug_level (level));
}

int
MCPHttpServer::debug_level () const
{
	return _debug_level.load ();
}

void
MCPHttpServer::run ()
{
	if (_event_loop) {
		PBD::EventLoop::set_event_loop_for_thread (_event_loop);
	}

	PBD::notify_event_loops_about_thread_creation (pthread_self (), "MCPHttp", 2048);

	/* Session transport requests allocate SessionEvent objects from a per-thread pool. */
	ARDOUR::SessionEvent::create_per_thread_pool ("MCPHttp events", 256);
	Temporal::TempoMap::fetch ();

	while (_running) {
		lws_service (_context, 100);
	}
}

MCPHttpServer::ClientContext&
MCPHttpServer::client (struct lws* wsi)
{
	ClientMap::iterator it = _clients.find (wsi);
	if (it == _clients.end ()) {
		ClientContext ctx;
		ctx.mcp_post      = false;
		ctx.have_response = false;
		it                = _clients.emplace (wsi, ctx).first;
	}

	return it->second;
}

void
MCPHttpServer::erase_client (struct lws* wsi)
{
	ClientMap::iterator it = _clients.find (wsi);
	if (it != _clients.end ()) {
		_clients.erase (it);
	}
}

int
MCPHttpServer::handle_http (struct lws* wsi, ClientContext& ctx)
{
	ctx.mcp_post      = false;
	ctx.have_response = false;
	ctx.request_body.clear ();
	ctx.response_body.clear ();

	char        uri[1024];
	std::string path;

	if (lws_hdr_copy (wsi, uri, sizeof (uri), WSI_TOKEN_GET_URI) > 0) {
		path = uri;

		/* HTTP-only MCP endpoint: POST /mcp */
		if (path == "/mcp") {
			return send_http_status (wsi, 405);
		}

		return send_http_status (wsi, 404);
	}

	if (lws_hdr_copy (wsi, uri, sizeof (uri), WSI_TOKEN_POST_URI) > 0) {
		path = uri;

		if (path == "/mcp") {
			ctx.mcp_post = true;
			return 0;
		}

		return send_http_status (wsi, 404);
	}

	return send_http_status (wsi, 400);
}

int
MCPHttpServer::handle_http_body (struct lws* /*wsi*/, ClientContext& ctx, void* in, size_t len)
{
	if (!ctx.mcp_post) {
		return 0;
	}

	ctx.request_body.append (static_cast<char*> (in), len);
	return 0;
}

int
MCPHttpServer::handle_http_body_completion (struct lws* wsi, ClientContext& ctx)
{
	if (!ctx.mcp_post) {
		return send_http_status (wsi, 400);
	}

	ctx.response_body = dispatch_jsonrpc (ctx.request_body);

	if (ctx.response_body.empty ()) {
		return send_http_status (wsi, 202);
	}

	ctx.have_response = true;

	if (send_json_headers (wsi)) {
		return 1;
	}

	lws_callback_on_writable (wsi);
	return 0;
}

int
MCPHttpServer::handle_http_writeable (struct lws* wsi, ClientContext& ctx)
{
	if (ctx.have_response) {
		return write_json_response (wsi, ctx);
	}

	return 0;
}

int
MCPHttpServer::send_http_status (struct lws* wsi, unsigned int status)
{
	lws_return_http_status (wsi, status, 0);
	return -1;
}

int
MCPHttpServer::send_json_headers (struct lws* wsi)
{
	unsigned char  out_buf[1024];
	unsigned char* start = out_buf;
	unsigned char* p     = start;
	unsigned char* end   = &out_buf[sizeof (out_buf) - 1];

#if LWS_LIBRARY_VERSION_MAJOR >= 3
	if (lws_add_http_common_headers (wsi, 200, "application/json", LWS_ILLEGAL_HTTP_CONTENT_LEN, &p, end) || lws_add_http_header_by_token (wsi, WSI_TOKEN_HTTP_CACHE_CONTROL, reinterpret_cast<const unsigned char*> ("no-store"), 8, &p, end) || lws_finalize_write_http_header (wsi, start, &p, end)) {
		return 1;
	}
#else
	if (lws_add_http_header_status (wsi, 200, &p, end) || lws_add_http_header_by_token (wsi, WSI_TOKEN_HTTP_CONTENT_TYPE, reinterpret_cast<const unsigned char*> ("application/json"), 16, &p, end) || lws_add_http_header_by_token (wsi, WSI_TOKEN_CONNECTION, reinterpret_cast<const unsigned char*> ("close"), 5, &p, end) || lws_add_http_header_by_token (wsi, WSI_TOKEN_HTTP_CACHE_CONTROL, reinterpret_cast<const unsigned char*> ("no-store"), 8, &p, end) || lws_finalize_http_header (wsi, &p, end)) {
		return 1;
	}

	int len = p - start;
	if (lws_write (wsi, start, len, LWS_WRITE_HTTP_HEADERS) != len) {
		return 1;
	}
#endif

	return 0;
}

int
MCPHttpServer::write_json_response (struct lws* wsi, ClientContext& ctx)
{
	std::vector<unsigned char> body (ctx.response_body.begin (), ctx.response_body.end ());
	if (!body.empty () && lws_write (wsi, body.data (), body.size (), LWS_WRITE_HTTP) != (int)body.size ()) {
		return 1;
	}

	ctx.have_response = false;
	ctx.mcp_post      = false;
	ctx.request_body.clear ();
	ctx.response_body.clear ();

	if (lws_http_transaction_completed (wsi)) {
		return -1;
	}

	return -1;
}





std::string
MCPHttpServer::dispatch_jsonrpc (const std::string& payload) const
{
	pt::ptree          root;
	std::istringstream is (payload);
	const int          dbg = debug_level ();

	try {
		pt::read_json (is, root);
	} catch (...) {
		if (dbg >= 1) {
			PBD::warning << "MCPHttp: JSON parse error" << endmsg;
		}
		return jsonrpc_error ("null", -32700, "Parse error");
	}

	std::string method = root.get<std::string> ("method", "");
	std::string id     = jsonrpc_id (root);
	const bool  has_id = has_jsonrpc_id (root);

	if (dbg >= 2) {
		PBD::info << "MCPHttp: request payload: " << payload << endmsg;
	}
	if (dbg >= 1) {
		if (method == "tools/call") {
			const std::string requested_tool = canonical_tool_name (root.get<std::string> ("params.name", ""));
			PBD::info << "MCPHttp: tools/call " << requested_tool << endmsg;
		} else {
			PBD::info << "MCPHttp: " << method << endmsg;
		}
	}

	if (method.empty ()) {
		return jsonrpc_error (id, -32600, "Invalid Request");
	}

	if (method == "initialize") {
		return jsonrpc_result (
		    id,
		    "{\"protocolVersion\":\"2025-03-26\","
		    "\"capabilities\":{\"tools\":{\"listChanged\":false}},"
		    "\"serverInfo\":{\"name\":\"ardour-mcp-http\",\"version\":\"0.1.0\"}}");
	}

	if (method == "notifications/initialized" && !has_id) {
		/* JSON-RPC notification: no response body. */
		return std::string ();
	}

	if (method == "ping" || method == "notifications/initialized") {
		return jsonrpc_result (id, "{}");
	}

	if (method == "tools/list") {
		static constexpr std::string_view tools_list{
#include "tools_json.inc"
		};

		return jsonrpc_result (
		    id,
		    std::string (tools_list));
	}

	if (method == "tools/call") {
		std::string tool_name = canonical_tool_name (root.get<std::string> ("params.name", ""));
		if (tool_name == "hello_world") {
			std::string caller = root.get<std::string> ("params.arguments.name", "");
			std::string text   = "Hello from Ardour";
			if (!caller.empty ()) {
				text += ", " + caller;
			}
			text += " (session: " + _session.name () + ")";

			return jsonrpc_result (
			    id,
			    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + json_escape (text) + "\"}]}");
		}

		std::string track_tool_response;
		if (mcp::dispatch_track_tool_call (_session, tool_name, root, id, track_tool_response)) {
			return track_tool_response;
		}

		std::string session_tool_response;
		if (mcp::dispatch_session_tool_call (_session, tool_name, root, id, session_tool_response)) {
			return session_tool_response;
		}

		std::string transport_tool_response;
		if (mcp::dispatch_transport_tool_call (_session, tool_name, root, id, transport_tool_response)) {
			return transport_tool_response;
		}

		std::string markers_tool_response;
		if (mcp::dispatch_markers_tool_call (_session, tool_name, root, id, markers_tool_response)) {
			return markers_tool_response;
		}

		std::string tracks_tool_response;
		if (mcp::dispatch_tracks_tool_call (_session, tool_name, root, id, tracks_tool_response)) {
			return tracks_tool_response;
		}

		std::string plugin_tool_response;
		if (mcp::dispatch_plugin_tool_call (_session, _event_loop, tool_name, root, id, plugin_tool_response)) {
			return plugin_tool_response;
		}

		std::string midi_region_tool_response;
		if (mcp::dispatch_regions_midi_tool_call (_session, tool_name, root, id, midi_region_tool_response)) {
			return midi_region_tool_response;
		}

		return jsonrpc_error (id, -32602, "Unknown tool name");
	}

	return jsonrpc_error (id, -32601, "Method not found");
}

int
MCPHttpServer::callback (struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
{
	ClientContext& ctx = client (wsi);
	int            rc  = 0;

	switch (reason) {
		case LWS_CALLBACK_HTTP:
			rc = handle_http (wsi, ctx);
			break;
		case LWS_CALLBACK_HTTP_BODY:
			rc = handle_http_body (wsi, ctx, in, len);
			break;
		case LWS_CALLBACK_HTTP_BODY_COMPLETION:
			rc = handle_http_body_completion (wsi, ctx);
			break;
		case LWS_CALLBACK_HTTP_WRITEABLE:
			rc = handle_http_writeable (wsi, ctx);
			break;
		case LWS_CALLBACK_CLOSED:
#ifdef LWS_CALLBACK_CLOSED_HTTP
		case LWS_CALLBACK_CLOSED_HTTP:
#endif
#ifdef LWS_CALLBACK_WSI_DESTROY
		case LWS_CALLBACK_WSI_DESTROY:
#endif
			erase_client (wsi);
			rc = 0;
			break;
#if ((LWS_LIBRARY_VERSION_MAJOR * 1000000) + (LWS_LIBRARY_VERSION_MINOR * 1000)) >= 2001000
		default:
			rc = lws_callback_http_dummy (wsi, reason, user, in, len);
			break;
#else
		default:
			rc = 0;
			break;
#endif
	}

	return rc;
}

int
MCPHttpServer::lws_callback (struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
{
	void*          ctx_userdata = lws_context_user (lws_get_context (wsi));
	MCPHttpServer* server       = static_cast<MCPHttpServer*> (ctx_userdata);
	if (!server) {
		return 0;
	}

	return server->callback (wsi, reason, user, in, len);
}
