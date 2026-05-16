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

#include <cstdint>
#include <list>
#include <memory>
#include <sstream>
#include <string>

#include "ardour/audio_track.h"
#include "ardour/midi_track.h"
#include "ardour/plugin.h"
#include "ardour/presentation_info.h"
#include "ardour/route.h"
#include "ardour/route_group.h"
#include "ardour/selection.h"
#include "ardour/session.h"
#include "ardour/stripable.h"
#include "ardour/types.h"

#include "handlers/common.h"
#include "handlers/tracks.h"

namespace ArdourSurface
{
namespace mcp
{
namespace
{

std::string
tracks_list_json (ARDOUR::Session& session, bool include_hidden)
{
	std::shared_ptr<ARDOUR::RouteList const> routes = session.get_routes ();
	ARDOUR::RouteList                        sorted;

	if (routes) {
		sorted = *routes;
		sorted.sort (ARDOUR::Stripable::Sorter ());
	}

	std::ostringstream ss;
	ss << "{\"tracks\":[";

	bool first = true;
	for (ARDOUR::RouteList::const_iterator it = sorted.begin (); it != sorted.end (); ++it) {
		const std::shared_ptr<ARDOUR::Route>& route = *it;
		if (!route) {
			continue;
		}

		const bool hidden = route->is_hidden ();
		if (hidden && !include_hidden) {
			continue;
		}

		std::string type = route_type_string (route);

		if (!first) {
			ss << ",";
		}
		first = false;

		ss << "{\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
		   << ",\"name\":\"" << json_escape (route->name ()) << "\""
		   << ",\"type\":\"" << type << "\""
		   << ",\"trackNumber\":" << route->track_number ()
		   << ",\"presentationOrder\":" << route->presentation_info ().order ()
		   << ",\"hidden\":" << (hidden ? "true" : "false")
		   << "}";
	}

	ss << "]}";
	return ss.str ();
}

std::string
tracks_list_text (ARDOUR::Session& session, bool include_hidden)
{
	std::shared_ptr<ARDOUR::RouteList const> routes = session.get_routes ();
	ARDOUR::RouteList                        sorted;

	if (routes) {
		sorted = *routes;
		sorted.sort (ARDOUR::Stripable::Sorter ());
	}

	std::ostringstream ss;
	int                count = 0;
	for (ARDOUR::RouteList::const_iterator it = sorted.begin (); it != sorted.end (); ++it) {
		const std::shared_ptr<ARDOUR::Route>& route = *it;
		if (!route) {
			continue;
		}
		if (route->is_hidden () && !include_hidden) {
			continue;
		}
		++count;
	}

	ss << "Tracks (" << count << "):";
	for (ARDOUR::RouteList::const_iterator it = sorted.begin (); it != sorted.end (); ++it) {
		const std::shared_ptr<ARDOUR::Route>& route = *it;
		if (!route) {
			continue;
		}
		if (route->is_hidden () && !include_hidden) {
			continue;
		}

		ss << "\n- " << route->name ()
		   << " (" << route_type_string (route)
		   << ", id " << route->id ().to_s ()
		   << ")";
	}

	return ss.str ();
}

std::string
route_list_json (const ARDOUR::RouteList& routes)
{
	std::ostringstream ss;
	ss << "{\"count\":" << routes.size () << ",\"routes\":[";

	bool first = true;
	for (ARDOUR::RouteList::const_iterator it = routes.begin (); it != routes.end (); ++it) {
		const std::shared_ptr<ARDOUR::Route>& route = *it;
		if (!route) {
			continue;
		}

		if (!first) {
			ss << ",";
		}
		first = false;

		ss << "{\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
		   << ",\"name\":\"" << json_escape (route->name ()) << "\""
		   << ",\"type\":\"" << route_type_string (route) << "\""
		   << ",\"trackNumber\":" << route->track_number ()
		   << ",\"presentationOrder\":" << route->presentation_info ().order ()
		   << ",\"hidden\":" << (route->is_hidden () ? "true" : "false")
		   << "}";
	}

	ss << "]}";
	return ss.str ();
}

std::string
handle_tracks_list_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const bool  include_hidden = root.get<bool> ("params.arguments.includeHidden", false);
	std::string structured     = tracks_list_json (session, include_hidden);
	std::string text           = tracks_list_text (session, include_hidden);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + json_escape (text) + "\"}],\"structuredContent\":" + structured + "}");
}

std::string
handle_tracks_add_or_buses_add_tool (ARDOUR::Session& session, const std::string& tool_name, const pt::ptree& root, const std::string& id)
{
	const bool        add_tracks          = (tool_name == "tracks/add");
	const std::string route_kind          = root.get<std::string> ("params.arguments.type", "audio");
	const int64_t     count_in            = root.get<int64_t> ("params.arguments.count", 1);
	const std::string name_template       = root.get<std::string> ("params.arguments.name", "");
	const bool        has_input_channels  = root.get_child_optional ("params.arguments.inputChannels").is_initialized ();
	const bool        has_output_channels = root.get_child_optional ("params.arguments.outputChannels").is_initialized ();
	const int64_t     input_channels_in   = root.get<int64_t> ("params.arguments.inputChannels", 2);
	const int64_t     output_channels_in  = root.get<int64_t> ("params.arguments.outputChannels", 2);
	const bool        strict_io           = root.get<bool> ("params.arguments.strictIo", false);
	const std::string insert_mode         = root.get<std::string> ("params.arguments.insert", "end");
	const std::string relative_to_id      = root.get<std::string> ("params.arguments.relativeToId", "");

	if (count_in < 1 || count_in > 256) {
		return jsonrpc_error (id, -32602, "Invalid count (expected 1..256)");
	}
	const uint32_t count = (uint32_t)count_in;

	if (insert_mode != "end" && insert_mode != "before" && insert_mode != "after") {
		return jsonrpc_error (id, -32602, "Invalid insert mode (expected: end, before, after)");
	}

	ARDOUR::PresentationInfo::order_t insert_order = ARDOUR::PresentationInfo::max_order;
	std::shared_ptr<ARDOUR::Route>    relative_route;
	bool                              relative_from_selection = false;
	ARDOUR::PresentationInfo::order_t relative_order          = 0;

	if (insert_mode != "end") {
		if (!relative_to_id.empty ()) {
			relative_route = route_by_mcp_id (session, relative_to_id);
			if (!relative_route) {
				return jsonrpc_error (id, -32602, "relativeToId route not found");
			}
		} else {
			relative_route          = std::dynamic_pointer_cast<ARDOUR::Route> (session.selection ().first_selected_stripable ());
			relative_from_selection = true;
			if (!relative_route) {
				return jsonrpc_error (id, -32602, "No selected route; provide relativeToId or select a route");
			}
		}

		relative_order = relative_route->presentation_info ().order ();
		insert_order   = relative_order + (insert_mode == "after" ? 1 : 0);
	}

	ARDOUR::RouteList created_routes;
	int               resolved_input_channels  = 0;
	int               resolved_output_channels = 0;

	if (route_kind == "audio") {
		if (input_channels_in < 1 || input_channels_in > 1024 || output_channels_in < 1 || output_channels_in > 1024) {
			return jsonrpc_error (id, -32602, "Invalid audio channel count (expected 1..1024)");
		}

		const int input_channels  = (int)input_channels_in;
		int       output_channels = (int)output_channels_in;
		resolved_input_channels   = input_channels;
		resolved_output_channels  = output_channels;

		if (add_tracks && input_channels == 1 && output_channels == 1 && !strict_io) {
			/* Match UI mono-track behavior (mono in, stereo out) so pan is available. */
			output_channels          = 2;
			resolved_output_channels = output_channels;
		}

		if (add_tracks) {
			std::list<std::shared_ptr<ARDOUR::AudioTrack>> tracks = session.new_audio_track (
			    input_channels,
			    output_channels,
			    std::shared_ptr<ARDOUR::RouteGroup> (),
			    count,
			    name_template,
			    insert_order,
			    ARDOUR::Normal,
			    true,
			    false);
			for (std::list<std::shared_ptr<ARDOUR::AudioTrack>>::const_iterator it = tracks.begin (); it != tracks.end (); ++it) {
				if (*it) {
					created_routes.push_back (*it);
				}
			}
		} else {
			created_routes = session.new_audio_route (
			    input_channels,
			    output_channels,
			    std::shared_ptr<ARDOUR::RouteGroup> (),
			    count,
			    name_template,
			    ARDOUR::PresentationInfo::AudioBus,
			    insert_order);
		}
	} else if (route_kind == "midi") {
		if (add_tracks) {
			const ARDOUR::ChanCount                       midi_io (ARDOUR::DataType::MIDI, 1);
			std::list<std::shared_ptr<ARDOUR::MidiTrack>> tracks = session.new_midi_track (
			    midi_io,
			    midi_io,
			    strict_io,
			    std::shared_ptr<ARDOUR::PluginInfo> (),
			    static_cast<ARDOUR::Plugin::PresetRecord*> (0),
			    std::shared_ptr<ARDOUR::RouteGroup> (),
			    count,
			    name_template,
			    insert_order,
			    ARDOUR::Normal,
			    true,
			    false);
			for (std::list<std::shared_ptr<ARDOUR::MidiTrack>>::const_iterator it = tracks.begin (); it != tracks.end (); ++it) {
				if (*it) {
					created_routes.push_back (*it);
				}
			}
		} else {
			created_routes = session.new_midi_route (
			    std::shared_ptr<ARDOUR::RouteGroup> (),
			    count,
			    name_template,
			    strict_io,
			    std::shared_ptr<ARDOUR::PluginInfo> (),
			    static_cast<ARDOUR::Plugin::PresetRecord*> (0),
			    ARDOUR::PresentationInfo::MidiBus,
			    insert_order);
		}
	} else {
		return jsonrpc_error (id, -32602, "Invalid type (expected: audio or midi)");
	}

	if (created_routes.empty ()) {
		return jsonrpc_error (id, -32000, add_tracks ? "Failed to add track(s)" : "Failed to add bus(es)");
	}

	std::ostringstream structured;
	structured << "{\"kind\":\"" << (add_tracks ? "track" : "bus") << "\""
	           << ",\"type\":\"" << json_escape (route_kind) << "\"";
	if (route_kind == "audio") {
		structured << ",\"io\":{"
		           << "\"requestedInputChannels\":" << (has_input_channels ? std::to_string ((int)input_channels_in) : "null")
		           << ",\"requestedOutputChannels\":" << (has_output_channels ? std::to_string ((int)output_channels_in) : "null")
		           << ",\"resolvedInputChannels\":" << resolved_input_channels
		           << ",\"resolvedOutputChannels\":" << resolved_output_channels
		           << "}";
	}
	structured
	    << ",\"insert\":{\"mode\":\"" << json_escape (insert_mode) << "\"";
	if (insert_mode == "end") {
		structured << ",\"order\":\"end\""
		           << ",\"relativeToId\":null"
		           << ",\"relativeToName\":null"
		           << ",\"relativeFromSelection\":false";
	} else {
		structured << ",\"order\":" << insert_order
		           << ",\"relativeToId\":\"" << json_escape (relative_route->id ().to_s ()) << "\""
		           << ",\"relativeToName\":\"" << json_escape (relative_route->name ()) << "\""
		           << ",\"relativeOrder\":" << relative_order
		           << ",\"relativeFromSelection\":" << (relative_from_selection ? "true" : "false");
	}
	structured << "}"
	           << ",\"created\":" << route_list_json (created_routes)
	           << ",\"transport\":" << transport_state_json (session)
	           << "}";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + (add_tracks ? "Track(s) added" : "Bus(es) added") + "\"}],\"structuredContent\":" + structured.str () + "}");
}

} /* anonymous namespace */

bool
dispatch_tracks_tool_call (ARDOUR::Session& session, const std::string& tool_name, const pt::ptree& root, const std::string& id, std::string& response)
{
	if (tool_name == "tracks/list") {
		response = handle_tracks_list_tool (session, root, id);
		return true;
	}
	if (tool_name == "tracks/add" || tool_name == "buses/add") {
		response = handle_tracks_add_or_buses_add_tool (session, tool_name, root, id);
		return true;
	}

	return false;
}

} /* namespace mcp */
} /* namespace ArdourSurface */
