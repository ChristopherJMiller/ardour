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
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "pbd/memento_command.h"
#include "pbd/xml++.h"

#include "ardour/location.h"
#include "ardour/session.h"
#include "ardour/tempo.h"
#include "ardour/types.h"

#include "handlers/common.h"
#include "handlers/markers.h"

namespace ArdourSurface
{
namespace mcp
{
namespace
{

std::string
markers_list_json (ARDOUR::Session& session)
{
	/* Ensure this thread has a current tempo-map pointer for any beat->audio conversions. */
	Temporal::TempoMap::fetch ();

	ARDOUR::Locations* locations = session.locations ();
	if (!locations) {
		return "{\"markers\":[]}";
	}

	struct MarkerSnapshot {
		std::string             name;
		samplepos_t             entry_start_sample;
		samplepos_t             entry_end_sample;
		ARDOUR::Location::Flags flags;
		int32_t                 cue_id;
		bool                    have_cue;
		bool                    hidden;
		std::string             location_id;
		std::string             location_name;
		samplepos_t             location_start_sample;
		samplepos_t             location_end_sample;
		bool                    synthetic;
		bool                    boundary_start;
	};

	std::vector<MarkerSnapshot>           markers;
	const ARDOUR::Locations::LocationList location_list = locations->list ();
	for (ARDOUR::Locations::LocationList::const_iterator it = location_list.begin (); it != location_list.end (); ++it) {
		ARDOUR::Location* loc = *it;
		if (!loc) {
			continue;
		}

		const ARDOUR::Location::Flags flags        = loc->flags ();
		const std::string             name         = loc->name ();
		const samplepos_t             start_sample = loc->start_sample ();
		const samplepos_t             end_sample   = loc->end_sample ();
		const std::string             location_id  = loc->id ().to_s ();
		const bool                    have_cue     = loc->is_cue_marker ();
		const int32_t                 cue_id       = have_cue ? loc->cue_id () : 0;

		if (loc->is_session_range ()) {
			/* Match OSC behavior: expose session bounds as synthetic "start"/"end" markers. */
			MarkerSnapshot start_marker;
			start_marker.name                  = "start";
			start_marker.entry_start_sample    = start_sample;
			start_marker.entry_end_sample      = start_sample;
			start_marker.flags                 = flags;
			start_marker.cue_id                = 0;
			start_marker.have_cue              = false;
			start_marker.hidden                = false;
			start_marker.location_id           = location_id;
			start_marker.location_name         = name;
			start_marker.location_start_sample = start_sample;
			start_marker.location_end_sample   = end_sample;
			start_marker.synthetic             = true;
			start_marker.boundary_start        = true;
			markers.push_back (start_marker);

			MarkerSnapshot end_marker;
			end_marker.name                  = "end";
			end_marker.entry_start_sample    = end_sample;
			end_marker.entry_end_sample      = end_sample;
			end_marker.flags                 = flags;
			end_marker.cue_id                = 0;
			end_marker.have_cue              = false;
			end_marker.hidden                = false;
			end_marker.location_id           = location_id;
			end_marker.location_name         = name;
			end_marker.location_start_sample = start_sample;
			end_marker.location_end_sample   = end_sample;
			end_marker.synthetic             = true;
			end_marker.boundary_start        = false;
			markers.push_back (end_marker);
			continue;
		}

		if (!(loc->is_mark () || loc->is_range_marker () || loc->is_auto_loop () || loc->is_auto_punch ())) {
			continue;
		}

		MarkerSnapshot marker;
		marker.name                  = name;
		marker.entry_start_sample    = start_sample;
		marker.entry_end_sample      = end_sample;
		marker.flags                 = flags;
		marker.cue_id                = cue_id;
		marker.have_cue              = have_cue;
		marker.hidden                = loc->is_hidden ();
		marker.location_id           = location_id;
		marker.location_name         = name;
		marker.location_start_sample = start_sample;
		marker.location_end_sample   = end_sample;
		marker.synthetic             = false;
		marker.boundary_start        = true;
		markers.push_back (marker);
	}

	std::sort (
	    markers.begin (),
	    markers.end (),
	    [] (const MarkerSnapshot& a, const MarkerSnapshot& b) {
		    if (a.entry_start_sample == b.entry_start_sample) {
			    return a.name < b.name;
		    }
		    return a.entry_start_sample < b.entry_start_sample;
	    });

	std::ostringstream ss;
	ss << "{\"markers\":[";

	bool first = true;
	for (size_t i = 0; i < markers.size (); ++i) {
		const MarkerSnapshot& marker = markers[i];
		if (!first) {
			ss << ",";
		}
		first = false;

		const bool        is_mark             = (marker.flags & ARDOUR::Location::IsMark) != 0;
		const bool        is_cue              = (marker.flags & ARDOUR::Location::IsCueMarker) != 0;
		const bool        is_cd               = (marker.flags & ARDOUR::Location::IsCDMarker) != 0;
		const bool        is_xrun             = (marker.flags & ARDOUR::Location::IsXrun) != 0;
		const bool        is_section          = (marker.flags & ARDOUR::Location::IsSection) != 0;
		const bool        is_scene            = (marker.flags & ARDOUR::Location::IsScene) != 0;
		const bool        is_range_marker     = (marker.flags & ARDOUR::Location::IsRangeMarker) != 0;
		const bool        is_session_range    = (marker.flags & ARDOUR::Location::IsSessionRange) != 0;
		const bool        is_auto_loop        = (marker.flags & ARDOUR::Location::IsAutoLoop) != 0;
		const bool        is_auto_punch       = (marker.flags & ARDOUR::Location::IsAutoPunch) != 0;
		const bool        is_clock_origin     = (marker.flags & ARDOUR::Location::IsClockOrigin) != 0;
		const bool        is_skip             = (marker.flags & ARDOUR::Location::IsSkip) != 0;
		const bool        is_range            = is_session_range || is_range_marker || is_auto_loop || is_auto_punch || is_cd;
		const samplepos_t entry_end_sample    = std::max (marker.entry_start_sample, marker.entry_end_sample);
		const int64_t     distance_from_start = (int64_t)marker.entry_start_sample - (int64_t)marker.location_start_sample;
		const std::string marker_bbt          = bbt_json_at_sample (marker.entry_start_sample);
		const std::string marker_end_bbt      = bbt_json_at_sample (entry_end_sample);
		const std::string location_start_bbt  = bbt_json_at_sample (marker.location_start_sample);
		const std::string location_end_bbt    = bbt_json_at_sample (marker.location_end_sample);

		ss << "{\"name\":\"" << json_escape (marker.name) << "\""
		   << ",\"label\":\"" << json_escape (marker.name) << "\""
		   << ",\"source\":\"" << (marker.synthetic ? "session_range_boundary" : "location") << "\""
		   << ",\"boundary\":\"" << (marker.synthetic ? (marker.boundary_start ? "start" : "end") : (is_range ? "range" : "point")) << "\""
		   << ",\"sortIndex\":" << i
		   << ",\"isSynthetic\":" << (marker.synthetic ? "true" : "false");

		ss << ",\"locationId\":\"" << json_escape (marker.location_id) << "\"";

		ss << ",\"locationName\":\"" << json_escape (marker.location_name) << "\""
		   << ",\"locationStartSample\":" << marker.location_start_sample
		   << ",\"locationEndSample\":" << marker.location_end_sample
		   << ",\"locationStartBbt\":" << location_start_bbt
		   << ",\"locationEndBbt\":" << location_end_bbt
		   << ",\"distanceFromLocationStartSamples\":" << distance_from_start
		   << ",\"startSample\":" << marker.entry_start_sample
		   << ",\"endSample\":" << entry_end_sample
		   << ",\"bbt\":" << marker_bbt
		   << ",\"endBbt\":" << marker_end_bbt
		   << ",\"lengthSamples\":" << (entry_end_sample - marker.entry_start_sample)
		   << ",\"isHidden\":" << (marker.hidden ? "true" : "false")
		   << ",\"isRange\":" << (is_range ? "true" : "false")
		   << ",\"flagBits\":" << (uint32_t)marker.flags
		   << ",\"isMark\":" << (is_mark ? "true" : "false")
		   << ",\"isCue\":" << (is_cue ? "true" : "false")
		   << ",\"isCD\":" << (is_cd ? "true" : "false")
		   << ",\"isXrun\":" << (is_xrun ? "true" : "false")
		   << ",\"isSection\":" << (is_section ? "true" : "false")
		   << ",\"isScene\":" << (is_scene ? "true" : "false")
		   << ",\"isRangeMarker\":" << (is_range_marker ? "true" : "false")
		   << ",\"isSessionRange\":" << (is_session_range ? "true" : "false")
		   << ",\"isAutoLoop\":" << (is_auto_loop ? "true" : "false")
		   << ",\"isAutoPunch\":" << (is_auto_punch ? "true" : "false")
		   << ",\"isClockOrigin\":" << (is_clock_origin ? "true" : "false")
		   << ",\"isSkip\":" << (is_skip ? "true" : "false")
		   << ",\"types\":" << marker_type_json (marker.flags);

		if (is_cue && marker.have_cue) {
			ss << ",\"cueId\":" << marker.cue_id;
		} else {
			ss << ",\"cueId\":null";
		}

		ss << "}";
	}

	ss << "]}";
	return ss.str ();
}

std::string
marker_added_json (const ARDOUR::Location& location, bool used_default_name, const std::string& requested_name)
{
	const samplepos_t             sample = location.start_sample ();
	const ARDOUR::Location::Flags flags  = location.flags ();
	const std::string             bbt    = bbt_json_at_sample (sample);

	std::ostringstream ss;
	ss << "{\"locationId\":\"" << json_escape (location.id ().to_s ()) << "\""
	   << ",\"name\":\"" << json_escape (location.name ()) << "\""
	   << ",\"startSample\":" << sample
	   << ",\"endSample\":" << sample
	   << ",\"bbt\":" << bbt
	   << ",\"types\":" << marker_type_json (flags)
	   << ",\"usedDefaultName\":" << (used_default_name ? "true" : "false");

	if (requested_name.empty ()) {
		ss << ",\"requestedName\":null";
	} else {
		ss << ",\"requestedName\":\"" << json_escape (requested_name) << "\"";
	}

	ss << "}";
	return ss.str ();
}

std::string
range_added_json (const ARDOUR::Location& location, bool used_default_name, const std::string& requested_name)
{
	const samplepos_t             start_sample = location.start_sample ();
	const samplepos_t             end_sample   = std::max (start_sample, location.end_sample ());
	const ARDOUR::Location::Flags flags        = location.flags ();
	const std::string             start_bbt    = bbt_json_at_sample (start_sample);
	const std::string             end_bbt      = bbt_json_at_sample (end_sample);

	std::ostringstream ss;
	ss << "{\"locationId\":\"" << json_escape (location.id ().to_s ()) << "\""
	   << ",\"name\":\"" << json_escape (location.name ()) << "\""
	   << ",\"startSample\":" << start_sample
	   << ",\"endSample\":" << end_sample
	   << ",\"startBbt\":" << start_bbt
	   << ",\"endBbt\":" << end_bbt
	   << ",\"lengthSamples\":" << (end_sample - start_sample)
	   << ",\"types\":" << marker_type_json (flags)
	   << ",\"usedDefaultName\":" << (used_default_name ? "true" : "false");

	if (requested_name.empty ()) {
		ss << ",\"requestedName\":null";
	} else {
		ss << ",\"requestedName\":\"" << json_escape (requested_name) << "\"";
	}

	ss << "}";
	return ss.str ();
}

std::string
marker_deleted_json (
    const std::string&      location_id,
    const std::string&      name,
    samplepos_t             start_sample,
    samplepos_t             end_sample,
    ARDOUR::Location::Flags flags)
{
	const samplepos_t safe_end_sample = std::max (start_sample, end_sample);
	const std::string start_bbt       = bbt_json_at_sample (start_sample);
	const std::string end_bbt         = bbt_json_at_sample (safe_end_sample);

	std::ostringstream ss;
	ss << "{\"locationId\":\"" << json_escape (location_id) << "\""
	   << ",\"name\":\"" << json_escape (name) << "\""
	   << ",\"startSample\":" << start_sample
	   << ",\"endSample\":" << safe_end_sample
	   << ",\"bbt\":" << start_bbt
	   << ",\"endBbt\":" << end_bbt
	   << ",\"lengthSamples\":" << (safe_end_sample - start_sample)
	   << ",\"types\":" << marker_type_json (flags)
	   << "}";
	return ss.str ();
}

std::string
marker_renamed_json (
    const std::string&      location_id,
    const std::string&      old_name,
    const std::string&      new_name,
    samplepos_t             start_sample,
    samplepos_t             end_sample,
    ARDOUR::Location::Flags flags)
{
	const samplepos_t safe_end_sample = std::max (start_sample, end_sample);
	const std::string start_bbt       = bbt_json_at_sample (start_sample);
	const std::string end_bbt         = bbt_json_at_sample (safe_end_sample);

	std::ostringstream ss;
	ss << "{\"locationId\":\"" << json_escape (location_id) << "\""
	   << ",\"oldName\":\"" << json_escape (old_name) << "\""
	   << ",\"name\":\"" << json_escape (new_name) << "\""
	   << ",\"startSample\":" << start_sample
	   << ",\"endSample\":" << safe_end_sample
	   << ",\"bbt\":" << start_bbt
	   << ",\"endBbt\":" << end_bbt
	   << ",\"lengthSamples\":" << (safe_end_sample - start_sample)
	   << ",\"types\":" << marker_type_json (flags)
	   << "}";
	return ss.str ();
}

bool
is_marker_or_range_location (const ARDOUR::Location& location)
{
	return location.is_mark () || location.is_range_marker ();
}

ARDOUR::Location*
resolve_marker_location (
    ARDOUR::Locations&            locations,
    const std::string&            location_id,
    const std::string&            name,
    const std::optional<int64_t>& sample_opt,
    std::string&                  error)
{
	error.clear ();

	if (!location_id.empty ()) {
		ARDOUR::Location* by_id = location_by_mcp_id (locations, location_id);
		if (!by_id) {
			error = "Marker locationId not found";
			return 0;
		}
		if (!is_marker_or_range_location (*by_id)) {
			error = "Location is not a marker/range";
			return 0;
		}
		return by_id;
	}

	std::vector<ARDOUR::Location*>        candidates;
	const ARDOUR::Locations::LocationList list = locations.list ();
	for (ARDOUR::Locations::LocationList::const_iterator it = list.begin (); it != list.end (); ++it) {
		ARDOUR::Location* loc = *it;
		if (!loc || !is_marker_or_range_location (*loc)) {
			continue;
		}
		if (loc->name () != name) {
			continue;
		}
		if (sample_opt && loc->start_sample () != (samplepos_t)*sample_opt) {
			continue;
		}
		candidates.push_back (loc);
	}

	if (candidates.empty ()) {
		error = "Marker not found by name/sample";
		return 0;
	}
	if (candidates.size () > 1) {
		error = "Ambiguous marker name; provide locationId or sample";
		return 0;
	}

	return candidates[0];
}

std::string
handle_markers_list_tool (ARDOUR::Session& session, const std::string& id)
{
	std::string structured = markers_list_json (session);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Markers listed\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_markers_add_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::optional<std::string> marker_name_opt = get_optional<std::string> (root, "params.arguments.name");
	const std::optional<std::string> marker_type_opt = get_optional<std::string> (root, "params.arguments.type");
	const std::string                requested_name  = marker_name_opt ? *marker_name_opt : std::string ();
	const std::string                marker_type     = marker_type_opt ? *marker_type_opt : std::string ("mark");

	ARDOUR::Location::Flags marker_flags      = ARDOUR::Location::IsMark;
	std::string             default_name_base = "mark";

	if (marker_type == "mark") {
		marker_flags      = ARDOUR::Location::IsMark;
		default_name_base = "mark";
	} else if (marker_type == "section" || marker_type == "arrangement") {
		marker_flags      = (ARDOUR::Location::Flags) ((uint32_t)ARDOUR::Location::IsMark | (uint32_t)ARDOUR::Location::IsSection);
		default_name_base = "section";
	} else if (marker_type == "scene") {
		marker_flags      = (ARDOUR::Location::Flags) ((uint32_t)ARDOUR::Location::IsMark | (uint32_t)ARDOUR::Location::IsScene);
		default_name_base = "scene";
	} else {
		return jsonrpc_error (id, -32602, "Invalid marker type (expected: mark, section, scene, arrangement)");
	}

	samplepos_t target_sample   = session.audible_sample ();
	bool        have_bbt_target = false;
	std::string bbt_error;
	if (!parse_optional_bbt_target_sample (root, "params.arguments", target_sample, have_bbt_target, bbt_error)) {
		return jsonrpc_error (id, -32602, bbt_error);
	}

	ARDOUR::Locations* locations = session.locations ();
	if (!locations) {
		return jsonrpc_error (id, -32602, "Session locations unavailable");
	}

	std::string marker_name = requested_name;
	if (marker_name.empty ()) {
		locations->next_available_name (marker_name, default_name_base);
	}
	const bool used_default_name = requested_name.empty ();

	const Temporal::timepos_t where (target_sample);
	ARDOUR::Location*         location = new ARDOUR::Location (session, where, where, marker_name, marker_flags);

	/* Match BasicUI::add_marker/OSC behavior: add an undoable marker operation. */
	session.begin_reversible_command ("add marker");
	XMLNode& before = locations->get_state ();
	locations->add (location, true);
	XMLNode& after = locations->get_state ();
	session.add_command (new MementoCommand<ARDOUR::Locations> (*locations, &before, &after));
	session.commit_reversible_command ();

	std::string structured = marker_added_json (*location, used_default_name, requested_name);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Marker added\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_markers_add_range_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::optional<std::string> marker_name_opt = get_optional<std::string> (root, "params.arguments.name");
	const std::string                requested_name  = marker_name_opt ? *marker_name_opt : std::string ();
	samplepos_t                      start_sample    = 0;
	samplepos_t                      end_sample      = 0;
	std::string                      range_error;
	if (!parse_range_endpoints (root, "params.arguments", start_sample, end_sample, range_error)) {
		return jsonrpc_error (id, -32602, range_error);
	}

	ARDOUR::Locations* locations = session.locations ();
	if (!locations) {
		return jsonrpc_error (id, -32602, "Session locations unavailable");
	}

	std::string marker_name = requested_name;
	if (marker_name.empty ()) {
		locations->next_available_name (marker_name, "range");
	}
	const bool used_default_name = requested_name.empty ();

	const Temporal::timepos_t start_where (start_sample);
	const Temporal::timepos_t end_where (end_sample);
	ARDOUR::Location*         location = new ARDOUR::Location (session, start_where, end_where, marker_name, ARDOUR::Location::IsRangeMarker);

	session.begin_reversible_command ("add range marker");
	XMLNode& before = locations->get_state ();
	locations->add (location, true);
	XMLNode& after = locations->get_state ();
	session.add_command (new MementoCommand<ARDOUR::Locations> (*locations, &before, &after));
	session.commit_reversible_command ();

	std::string structured = range_added_json (*location, used_default_name, requested_name);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Range marker added\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_markers_set_auto_loop_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id, const std::string& tool_name)
{
	samplepos_t start_sample = 0;
	samplepos_t end_sample   = 0;
	std::string range_error;
	if (!parse_range_endpoints (root, "params.arguments", start_sample, end_sample, range_error)) {
		return jsonrpc_error (id, -32602, range_error);
	}
	if (end_sample <= start_sample) {
		return jsonrpc_error (id, -32602, "Auto-loop range must have positive length");
	}

	ARDOUR::Locations* locations = session.locations ();
	if (!locations) {
		return jsonrpc_error (id, -32602, "Session locations unavailable");
	}

	ARDOUR::Location* loop_loc = locations->auto_loop_location ();
	if (loop_loc &&
	    loop_loc->start_sample () == start_sample &&
	    loop_loc->end_sample () == end_sample &&
	    !loop_loc->is_hidden ()) {
		std::string       structured     = special_range_json (*loop_loc, "auto_loop");
		const std::string unchanged_text = (tool_name == "transport/loop_location") ? "Loop location unchanged" : "Auto-loop range unchanged";
		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + json_escape (unchanged_text) + "\"}],\"structuredContent\":" + structured + "}");
	}

	session.begin_reversible_command ("set auto loop range");

	if (!loop_loc) {
		const Temporal::timepos_t start_where (start_sample);
		const Temporal::timepos_t end_where (end_sample);
		ARDOUR::Location*         loc    = new ARDOUR::Location (session, start_where, end_where, "Loop", ARDOUR::Location::IsAutoLoop);
		XMLNode&                  before = locations->get_state ();
		locations->add (loc, true);
		session.set_auto_loop_location (loc);
		XMLNode& after = locations->get_state ();
		session.add_command (new MementoCommand<ARDOUR::Locations> (*locations, &before, &after));
		loop_loc = loc;
	} else {
		XMLNode& before = loop_loc->get_state ();
		loop_loc->set_hidden (false, 0);
		loop_loc->set (Temporal::timepos_t (start_sample), Temporal::timepos_t (end_sample));
		XMLNode& after = loop_loc->get_state ();
		session.add_command (new MementoCommand<ARDOUR::Location> (*loop_loc, &before, &after));
	}

	session.commit_reversible_command ();

	std::string       structured   = special_range_json (*loop_loc, "auto_loop");
	const std::string updated_text = (tool_name == "transport/loop_location") ? "Loop location updated" : "Auto-loop range updated";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + json_escape (updated_text) + "\"}],\"structuredContent\":" + structured + "}");
}

std::string
handle_markers_hide_auto_loop_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const bool hidden = root.get<bool> ("params.arguments.hidden", true);

	ARDOUR::Locations* locations = session.locations ();
	if (!locations) {
		return jsonrpc_error (id, -32602, "Session locations unavailable");
	}

	ARDOUR::Location* loop_loc = locations->auto_loop_location ();
	if (!loop_loc) {
		return jsonrpc_error (id, -32602, "Auto-loop range not set");
	}

	if (loop_loc->is_hidden () == hidden) {
		std::string structured = special_range_json (*loop_loc, "auto_loop");
		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Auto-loop range visibility unchanged\"}],\"structuredContent\":") + structured + "}");
	}

	session.begin_reversible_command (hidden ? "hide auto loop range" : "show auto loop range");
	XMLNode& before = loop_loc->get_state ();
	loop_loc->set_hidden (hidden, 0);
	XMLNode& after = loop_loc->get_state ();
	session.add_command (new MementoCommand<ARDOUR::Location> (*loop_loc, &before, &after));
	session.commit_reversible_command ();

	std::string structured = special_range_json (*loop_loc, "auto_loop");
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Auto-loop range visibility updated\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_markers_set_auto_punch_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	samplepos_t start_sample = 0;
	samplepos_t end_sample   = 0;
	std::string range_error;
	if (!parse_range_endpoints (root, "params.arguments", start_sample, end_sample, range_error)) {
		return jsonrpc_error (id, -32602, range_error);
	}
	if (end_sample <= start_sample) {
		return jsonrpc_error (id, -32602, "Auto-punch range must have positive length");
	}

	ARDOUR::Locations* locations = session.locations ();
	if (!locations) {
		return jsonrpc_error (id, -32602, "Session locations unavailable");
	}

	ARDOUR::Location* punch_loc = locations->auto_punch_location ();
	if (punch_loc &&
	    punch_loc->start_sample () == start_sample &&
	    punch_loc->end_sample () == end_sample &&
	    !punch_loc->is_hidden ()) {
		std::string structured = special_range_json (*punch_loc, "auto_punch");
		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Auto-punch range unchanged\"}],\"structuredContent\":") + structured + "}");
	}

	session.begin_reversible_command ("set auto punch range");

	if (!punch_loc) {
		const Temporal::timepos_t start_where (start_sample);
		const Temporal::timepos_t end_where (end_sample);
		ARDOUR::Location*         loc    = new ARDOUR::Location (session, start_where, end_where, "Punch", ARDOUR::Location::IsAutoPunch);
		XMLNode&                  before = locations->get_state ();
		locations->add (loc, true);
		session.set_auto_punch_location (loc);
		XMLNode& after = locations->get_state ();
		session.add_command (new MementoCommand<ARDOUR::Locations> (*locations, &before, &after));
		punch_loc = loc;
	} else {
		XMLNode& before = punch_loc->get_state ();
		punch_loc->set_hidden (false, 0);
		punch_loc->set (Temporal::timepos_t (start_sample), Temporal::timepos_t (end_sample));
		XMLNode& after = punch_loc->get_state ();
		session.add_command (new MementoCommand<ARDOUR::Location> (*punch_loc, &before, &after));
	}

	session.commit_reversible_command ();

	std::string structured = special_range_json (*punch_loc, "auto_punch");
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Auto-punch range updated\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_markers_hide_auto_punch_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const bool hidden = root.get<bool> ("params.arguments.hidden", true);

	ARDOUR::Locations* locations = session.locations ();
	if (!locations) {
		return jsonrpc_error (id, -32602, "Session locations unavailable");
	}

	ARDOUR::Location* punch_loc = locations->auto_punch_location ();
	if (!punch_loc) {
		return jsonrpc_error (id, -32602, "Auto-punch range not set");
	}

	if (punch_loc->is_hidden () == hidden) {
		std::string structured = special_range_json (*punch_loc, "auto_punch");
		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Auto-punch range visibility unchanged\"}],\"structuredContent\":") + structured + "}");
	}

	session.begin_reversible_command (hidden ? "hide auto punch range" : "show auto punch range");
	XMLNode& before = punch_loc->get_state ();
	punch_loc->set_hidden (hidden, 0);
	XMLNode& after = punch_loc->get_state ();
	session.add_command (new MementoCommand<ARDOUR::Location> (*punch_loc, &before, &after));
	session.commit_reversible_command ();

	std::string structured = special_range_json (*punch_loc, "auto_punch");
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Auto-punch range visibility updated\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_markers_delete_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::optional<std::string> location_id_opt = get_optional<std::string> (root, "params.arguments.locationId");
	const std::optional<std::string> name_opt        = get_optional<std::string> (root, "params.arguments.name");
	const std::optional<int64_t>     sample_opt      = get_optional<int64_t> (root, "params.arguments.sample");
	const std::string                location_id     = location_id_opt ? *location_id_opt : std::string ();
	const std::string                name            = name_opt ? *name_opt : std::string ();

	if (location_id.empty () && name.empty ()) {
		return jsonrpc_error (id, -32602, "Provide one of: locationId or name");
	}
	if (sample_opt && *sample_opt < 0) {
		return jsonrpc_error (id, -32602, "Invalid sample (expected >= 0)");
	}

	ARDOUR::Locations* locations = session.locations ();
	if (!locations) {
		return jsonrpc_error (id, -32602, "Session locations unavailable");
	}

	std::string       resolve_error;
	ARDOUR::Location* target = resolve_marker_location (*locations, location_id, name, sample_opt, resolve_error);
	if (!target) {
		return jsonrpc_error (id, -32602, resolve_error);
	}

	const std::string             removed_id           = target->id ().to_s ();
	const std::string             removed_name         = target->name ();
	const samplepos_t             removed_start_sample = target->start_sample ();
	const samplepos_t             removed_end_sample   = target->end_sample ();
	const ARDOUR::Location::Flags removed_flags        = target->flags ();

	session.begin_reversible_command ("delete marker");
	XMLNode& before = locations->get_state ();
	locations->remove (target);
	XMLNode& after = locations->get_state ();
	session.add_command (new MementoCommand<ARDOUR::Locations> (*locations, &before, &after));
	session.commit_reversible_command ();

	std::string structured = marker_deleted_json (removed_id, removed_name, removed_start_sample, removed_end_sample, removed_flags);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Marker/range deleted\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_markers_rename_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::optional<std::string> location_id_opt = get_optional<std::string> (root, "params.arguments.locationId");
	const std::optional<std::string> name_opt        = get_optional<std::string> (root, "params.arguments.name");
	const std::optional<int64_t>     sample_opt      = get_optional<int64_t> (root, "params.arguments.sample");
	const std::string                new_name        = root.get<std::string> ("params.arguments.newName", "");
	const std::string                location_id     = location_id_opt ? *location_id_opt : std::string ();
	const std::string                name            = name_opt ? *name_opt : std::string ();

	if (new_name.empty ()) {
		return jsonrpc_error (id, -32602, "Missing newName");
	}
	if (location_id.empty () && name.empty ()) {
		return jsonrpc_error (id, -32602, "Provide one of: locationId or name");
	}
	if (sample_opt && *sample_opt < 0) {
		return jsonrpc_error (id, -32602, "Invalid sample (expected >= 0)");
	}

	ARDOUR::Locations* locations = session.locations ();
	if (!locations) {
		return jsonrpc_error (id, -32602, "Session locations unavailable");
	}

	std::string       resolve_error;
	ARDOUR::Location* target = resolve_marker_location (*locations, location_id, name, sample_opt, resolve_error);
	if (!target) {
		return jsonrpc_error (id, -32602, resolve_error);
	}

	const std::string             renamed_id          = target->id ().to_s ();
	const std::string             old_name            = target->name ();
	const samplepos_t             marker_start_sample = target->start_sample ();
	const samplepos_t             marker_end_sample   = target->end_sample ();
	const ARDOUR::Location::Flags marker_flags        = target->flags ();

	session.begin_reversible_command ("rename marker");
	XMLNode& before = locations->get_state ();
	target->set_name (new_name);
	XMLNode& after = locations->get_state ();
	session.add_command (new MementoCommand<ARDOUR::Locations> (*locations, &before, &after));
	session.commit_reversible_command ();

	std::string structured = marker_renamed_json (renamed_id, old_name, target->name (), marker_start_sample, marker_end_sample, marker_flags);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Marker/range renamed\"}],\"structuredContent\":") + structured + "}");
}

} /* anonymous namespace */

bool
dispatch_markers_tool_call (ARDOUR::Session& session, const std::string& tool_name, const pt::ptree& root, const std::string& id, std::string& response)
{
	if (tool_name == "markers/list") {
		response = handle_markers_list_tool (session, id);
		return true;
	}
	if (tool_name == "markers/add") {
		response = handle_markers_add_tool (session, root, id);
		return true;
	}
	if (tool_name == "markers/add_range") {
		response = handle_markers_add_range_tool (session, root, id);
		return true;
	}
	if (tool_name == "markers/set_auto_loop" || tool_name == "transport/loop_location") {
		response = handle_markers_set_auto_loop_tool (session, root, id, tool_name);
		return true;
	}
	if (tool_name == "markers/hide_auto_loop") {
		response = handle_markers_hide_auto_loop_tool (session, root, id);
		return true;
	}
	if (tool_name == "markers/set_auto_punch") {
		response = handle_markers_set_auto_punch_tool (session, root, id);
		return true;
	}
	if (tool_name == "markers/hide_auto_punch") {
		response = handle_markers_hide_auto_punch_tool (session, root, id);
		return true;
	}
	if (tool_name == "markers/delete") {
		response = handle_markers_delete_tool (session, root, id);
		return true;
	}
	if (tool_name == "markers/rename") {
		response = handle_markers_rename_tool (session, root, id);
		return true;
	}

	return false;
}

} /* namespace mcp */
} /* namespace ArdourSurface */
