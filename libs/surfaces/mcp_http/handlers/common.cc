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
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

#include "pbd/enumwriter.h"
#include "pbd/id.h"

#include "ardour/amp.h"
#include "ardour/dB.h"
#include "ardour/location.h"
#include "ardour/playlist.h"
#include "ardour/region.h"
#include "ardour/selection.h"
#include "ardour/track.h"
#include "ardour/region_factory.h"
#include "ardour/route.h"
#include "ardour/processor.h"
#include "ardour/session.h"
#include "ardour/tempo.h"
#include "ardour/audio_track.h"
#include "ardour/midi_track.h"

#include "handlers/common.h"

namespace ArdourSurface
{
namespace mcp
{

std::string
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

std::string
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

bool
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

ARDOUR::Location*
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

std::shared_ptr<ARDOUR::Region>
region_by_mcp_id (const std::string& id)
{
	if (!is_decimal_pbd_id_string (id)) {
		return std::shared_ptr<ARDOUR::Region> ();
	}

	return ARDOUR::RegionFactory::region_by_id (PBD::ID (id));
}

std::shared_ptr<ARDOUR::Route>
route_by_mcp_id (ARDOUR::Session& session, const std::string& id)
{
	if (!is_decimal_pbd_id_string (id)) {
		return std::shared_ptr<ARDOUR::Route> ();
	}

	return session.route_by_id (PBD::ID (id));
}

namespace
{
bool
is_number_literal (const std::string& s)
{
	if (s.empty ()) {
		return false;
	}

	char* endptr = 0;
	std::strtod (s.c_str (), &endptr);
	return endptr && *endptr == '\0';
}

std::string
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
} /* anonymous namespace */

std::string
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

bool
has_jsonrpc_id (const pt::ptree& root)
{
	return root.get_child_optional ("id").is_initialized ();
}

std::string
jsonrpc_result (const std::string& id, const std::string& result_json)
{
	const std::string normalized_result_json = tool_result_with_structured_text_fallback (result_json);
	return std::string ("{\"jsonrpc\":\"2.0\",\"id\":") + id + ",\"result\":" + normalized_result_json + "}";
}

std::string
jsonrpc_error (const std::string& id, int code, const std::string& message)
{
	std::ostringstream ss;
	ss << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"error\":{\"code\":" << code << ",\"message\":\""
	   << json_escape (message) << "\"}}";
	return ss.str ();
}

std::string
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

std::string
transport_state_json (ARDOUR::Session& session)
{
	std::ostringstream ss;
	ss << "{\"rolling\":" << (session.transport_rolling () ? "true" : "false")
	   << ",\"speed\":" << session.transport_speed ()
	   << ",\"sample\":" << session.transport_sample ()
	   << ",\"state\":\"" << transport_state_string (session) << "\"}";
	return ss.str ();
}

const char*
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

double
transport_tempo_bpm (ARDOUR::Session& session)
{
	try {
		Temporal::TempoMap::SharedPtr tmap (Temporal::TempoMap::fetch ());
		return tmap->metric_at (Temporal::timepos_t (session.transport_sample ())).tempo ().quarter_notes_per_minute ();
	} catch (...) {
		return 120.0;
	}
}

bool
resolve_region_argument_or_selected_at_playhead (
    ARDOUR::Session&                 session,
    const pt::ptree&                 root,
    const std::string&               args_path,
    std::shared_ptr<ARDOUR::Region>& region,
    std::string&                     resolved_via,
    std::string&                     error)
{
	error.clear ();
	resolved_via.clear ();
	region.reset ();

	const std::string region_id = root.get<std::string> (args_path + ".regionId", "");
	if (!region_id.empty ()) {
		region = region_by_mcp_id (region_id);
		if (!region) {
			error = "regionId not found";
			return false;
		}
		resolved_via = "regionId";
		return true;
	}

	const std::shared_ptr<ARDOUR::Stripable> selected_stripable = session.selection ().first_selected_stripable ();
	if (!selected_stripable) {
		error = "Missing regionId and no selected track";
		return false;
	}

	const std::shared_ptr<ARDOUR::Route> selected_route = std::dynamic_pointer_cast<ARDOUR::Route> (selected_stripable);
	const std::shared_ptr<ARDOUR::Track> selected_track = std::dynamic_pointer_cast<ARDOUR::Track> (selected_route);
	if (!selected_track) {
		error = "Missing regionId and selected stripable is not a track";
		return false;
	}

	const std::shared_ptr<ARDOUR::Playlist> selected_playlist = selected_track->playlist ();
	if (!selected_playlist) {
		error = "Missing regionId and selected track has no playlist";
		return false;
	}

	const samplepos_t playhead_sample = session.transport_sample ();
	region                            = selected_playlist->top_unmuted_region_at (Temporal::timepos_t (playhead_sample));
	if (!region) {
		region = selected_playlist->top_region_at (Temporal::timepos_t (playhead_sample));
	}
	if (!region) {
		error = "Missing regionId and no region at playhead on selected track";
		return false;
	}

	resolved_via = "selectedTrackAtPlayhead";
	return true;
}

std::string
plugin_list_json (const std::shared_ptr<ARDOUR::Route>& route)
{
	std::ostringstream ss;
	ss << "[";

	bool first = true;
	for (uint32_t i = 0;; ++i) {
		std::shared_ptr<ARDOUR::Processor> p = route->nth_plugin (i);
		if (!p) {
			break;
		}
		if (!p->display_to_user ()) {
			continue;
		}

		if (!first) {
			ss << ",";
		}
		first = false;

		ss << "{\"index\":" << i
		   << ",\"name\":\"" << json_escape (p->name ()) << "\""
		   << ",\"displayName\":\"" << json_escape (p->display_name ()) << "\""
		   << ",\"preFader\":" << (p->get_pre_fader () ? "true" : "false")
		   << ",\"postFader\":" << (p->get_pre_fader () ? "false" : "true")
		   << ",\"active\":" << (p->active () ? "true" : "false")
		   << ",\"enabled\":" << (p->enabled () ? "true" : "false")
		   << "}";
	}

	ss << "]";
	return ss.str ();
}

std::string
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

std::string
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

std::string
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

std::string
special_range_json (const ARDOUR::Location& location, const std::string& mode)
{
	const samplepos_t             start_sample = location.start_sample ();
	const samplepos_t             end_sample   = std::max (start_sample, location.end_sample ());
	const ARDOUR::Location::Flags flags        = location.flags ();
	const std::string             start_bbt    = bbt_json_at_sample (start_sample);
	const std::string             end_bbt      = bbt_json_at_sample (end_sample);
	const bool                    is_hidden    = location.is_hidden ();

	std::ostringstream ss;
	ss << "{\"mode\":\"" << json_escape (mode) << "\""
	   << ",\"locationId\":\"" << json_escape (location.id ().to_s ()) << "\""
	   << ",\"name\":\"" << json_escape (location.name ()) << "\""
	   << ",\"startSample\":" << start_sample
	   << ",\"endSample\":" << end_sample
	   << ",\"startBbt\":" << start_bbt
	   << ",\"endBbt\":" << end_bbt
	   << ",\"lengthSamples\":" << (end_sample - start_sample)
	   << ",\"isHidden\":" << (is_hidden ? "true" : "false")
	   << ",\"types\":" << marker_type_json (flags)
	   << "}";
	return ss.str ();
}

bool
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

bool
parse_optional_bbt_target_sample (
    const pt::ptree&   root,
    const std::string& args_path,
    samplepos_t&       target_sample,
    bool&              have_target,
    std::string&       error)
{
	have_target = false;
	error.clear ();

	const std::optional<int>    bar_opt  = get_optional<int> (root, args_path + ".bar");
	const std::optional<double> beat_opt = get_optional<double> (root, args_path + ".beat");

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

bool
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

	const std::optional<int64_t> sample_opt = get_optional<int64_t> (root, args_path + "." + sample_key);
	const std::optional<int>     bar_opt    = get_optional<int> (root, args_path + "." + bar_key);
	const std::optional<double>  beat_opt   = get_optional<double> (root, args_path + "." + beat_key);

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

bool
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

	const std::optional<int64_t> start_sample_opt = get_optional<int64_t> (root, args_path + ".startSample");
	const std::optional<int64_t> end_sample_opt   = get_optional<int64_t> (root, args_path + ".endSample");
	const std::optional<int>     start_bar_opt    = get_optional<int> (root, args_path + ".startBar");
	const std::optional<double>  start_beat_opt   = get_optional<double> (root, args_path + ".startBeat");
	const std::optional<int>     end_bar_opt      = get_optional<int> (root, args_path + ".endBar");
	const std::optional<double>  end_beat_opt     = get_optional<double> (root, args_path + ".endBeat");

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

bool
valid_fader_position (double p)
{
	return std::isfinite (p) && p >= 0.0 && p <= 1.0;
}

bool
valid_fader_db (double d)
{
	if (std::isnan (d)) {
		return false;
	}

	if (std::isinf (d)) {
		return d < 0.0;
	}

	/* Match OSC silence floor and enforce explicit MCP dB bounds. */
	return d >= -193.0 && d <= 6.0;
}

bool
db_is_silence_floor (double db)
{
	return !std::isfinite (db) || db <= -192.0;
}

double
db_to_gain_with_floor (double db)
{
	return db_is_silence_floor (db) ? 0.0 : dB_to_coefficient (db);
}

double
normalized_db_value (double db)
{
	if (db_is_silence_floor (db)) {
		return -193.0;
	}

	/* Avoid scientific-notation near-zero noise in JSON output. */
	return (std::fabs (db) < 1e-6) ? 0.0 : db;
}

} /* namespace mcp */
} /* namespace ArdourSurface */
