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
#include "ardour/region.h"
#include "ardour/region_factory.h"
#include "ardour/route.h"
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
