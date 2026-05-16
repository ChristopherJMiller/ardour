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
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "pbd/controllable.h"

#include "ardour/amp.h"
#include "ardour/audioregion.h"
#include "ardour/dB.h"
#include "ardour/internal_send.h"
#include "ardour/playlist.h"
#include "ardour/processor.h"
#include "ardour/region.h"
#include "ardour/route.h"
#include "ardour/selection.h"
#include "ardour/session.h"
#include "ardour/track.h"
#include "ardour/types.h"

#include "handlers/common.h"
#include "handlers/track.h"

namespace ArdourSurface
{
namespace mcp
{
namespace
{

std::string
track_fader_json (const std::shared_ptr<ARDOUR::Route>& route)
{
	std::shared_ptr<ARDOUR::AutomationControl> gain     = route ? route->gain_control () : std::shared_ptr<ARDOUR::AutomationControl> ();
	const double                               position = gain ? gain->internal_to_interface (gain->get_value ()) : 0.0;
	double                                     db       = -193.0;

	if (gain && gain->get_value () > 0.0) {
		db = accurate_coefficient_to_dB (gain->get_value ());
		if (!std::isfinite (db)) {
			db = -193.0;
		}
	}

	std::ostringstream ss;
	ss << "{\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
	   << ",\"name\":\"" << json_escape (route->name ()) << "\""
	   << ",\"position\":" << position
	   << ",\"db\":" << normalized_db_value (db)
	   << "}";
	return ss.str ();
}

std::string
send_list_json (const std::shared_ptr<ARDOUR::Route>& route)
{
	std::ostringstream ss;
	ss << "[";

	bool first = true;
	for (uint32_t i = 0;; ++i) {
		std::shared_ptr<ARDOUR::Processor> p = route->nth_send (i);
		if (!p) {
			break;
		}

		std::shared_ptr<ARDOUR::AutomationControl> gain     = route->send_level_controllable (i);
		const double                               position = gain ? gain->internal_to_interface (gain->get_value ()) : 0.0;
		double                                     db       = -193.0;
		if (gain && gain->get_value () > 0.0) {
			db = accurate_coefficient_to_dB (gain->get_value ());
			if (!std::isfinite (db)) {
				db = -193.0;
			}
		}

		if (!first) {
			ss << ",";
		}
		first = false;

		ss << "{\"index\":" << i
		   << ",\"name\":\"" << json_escape (route->send_name (i)) << "\""
		   << ",\"active\":" << (p->active () ? "true" : "false")
		   << ",\"preFader\":" << (p->get_pre_fader () ? "true" : "false")
		   << ",\"postFader\":" << (p->get_pre_fader () ? "false" : "true")
		   << ",\"position\":" << position
		   << ",\"db\":" << normalized_db_value (db);

		std::shared_ptr<ARDOUR::InternalSend> isend = std::dynamic_pointer_cast<ARDOUR::InternalSend> (p);
		if (isend) {
			std::shared_ptr<ARDOUR::Route> target = isend->target_route ();
			if (target) {
				ss << ",\"targetRouteId\":\"" << json_escape (target->id ().to_s ()) << "\""
				   << ",\"targetRouteName\":\"" << json_escape (target->name ()) << "\"";
			}
		}

		ss << "}";
	}

	ss << "]";
	return ss.str ();
}

std::string
send_level_json (const std::shared_ptr<ARDOUR::Route>& route, uint32_t send_index)
{
	std::shared_ptr<ARDOUR::Processor>         p        = route ? route->nth_send (send_index) : std::shared_ptr<ARDOUR::Processor> ();
	std::shared_ptr<ARDOUR::AutomationControl> gain     = route ? route->send_level_controllable (send_index) : std::shared_ptr<ARDOUR::AutomationControl> ();
	const double                               position = gain ? gain->internal_to_interface (gain->get_value ()) : 0.0;
	double                                     db       = -193.0;

	if (gain && gain->get_value () > 0.0) {
		db = accurate_coefficient_to_dB (gain->get_value ());
		if (!std::isfinite (db)) {
			db = -193.0;
		}
	}

	std::ostringstream ss;
	ss << "{\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
	   << ",\"routeName\":\"" << json_escape (route->name ()) << "\""
	   << ",\"sendIndex\":" << send_index
	   << ",\"name\":\"" << json_escape (route->send_name (send_index)) << "\""
	   << ",\"active\":" << ((p && p->active ()) ? "true" : "false")
	   << ",\"preFader\":" << ((p && p->get_pre_fader ()) ? "true" : "false")
	   << ",\"postFader\":" << ((p && p->get_pre_fader ()) ? "false" : "true")
	   << ",\"position\":" << position
	   << ",\"db\":" << normalized_db_value (db);

	std::shared_ptr<ARDOUR::InternalSend> isend = std::dynamic_pointer_cast<ARDOUR::InternalSend> (p);
	if (isend) {
		std::shared_ptr<ARDOUR::Route> target = isend->target_route ();
		if (target) {
			ss << ",\"targetRouteId\":\"" << json_escape (target->id ().to_s ()) << "\""
			   << ",\"targetRouteName\":\"" << json_escape (target->name ()) << "\"";
		}
	}

	ss << "}";
	return ss.str ();
}

bool
find_internal_send_index (
    const std::shared_ptr<ARDOUR::Route>& source_route,
    const std::shared_ptr<ARDOUR::Route>& target_route,
    uint32_t&                             send_index_out)
{
	if (!source_route || !target_route) {
		return false;
	}

	for (uint32_t i = 0;; ++i) {
		std::shared_ptr<ARDOUR::Processor> p = source_route->nth_send (i);
		if (!p) {
			break;
		}

		std::shared_ptr<ARDOUR::InternalSend> isend = std::dynamic_pointer_cast<ARDOUR::InternalSend> (p);
		if (!isend) {
			continue;
		}

		std::shared_ptr<ARDOUR::Route> target = isend->target_route ();
		if (target && target->id () == target_route->id ()) {
			send_index_out = i;
			return true;
		}
	}

	return false;
}

bool
recreate_aux_send_with_position (
    const std::shared_ptr<ARDOUR::Route>& source_route,
    uint32_t                              send_index,
    bool                                  post_fader,
    uint32_t&                             resolved_send_index,
    std::string&                          error)
{
	if (!source_route) {
		error = "Route not found";
		return false;
	}

	std::shared_ptr<ARDOUR::Processor> send_proc = source_route->nth_send (send_index);
	if (!send_proc) {
		error = "Send not found";
		return false;
	}

	std::shared_ptr<ARDOUR::InternalSend> isend = std::dynamic_pointer_cast<ARDOUR::InternalSend> (send_proc);
	if (!isend) {
		error = "Only internal aux sends are supported";
		return false;
	}

	std::shared_ptr<ARDOUR::Route> target_route = isend->target_route ();
	if (!target_route) {
		error = "Send target route not found";
		return false;
	}

	if (((!send_proc->get_pre_fader ()) && post_fader) || (send_proc->get_pre_fader () && !post_fader)) {
		resolved_send_index = send_index;
		return true;
	}

	std::shared_ptr<ARDOUR::AutomationControl> send_gain       = source_route->send_level_controllable (send_index);
	std::shared_ptr<ARDOUR::AutomationControl> send_enable     = source_route->send_enable_controllable (send_index);
	std::shared_ptr<ARDOUR::AutomationControl> send_pan        = source_route->send_pan_azimuth_controllable (send_index);
	std::shared_ptr<ARDOUR::AutomationControl> send_pan_enable = source_route->send_pan_azimuth_enable_controllable (send_index);

	const bool   have_gain         = !!send_gain;
	const bool   have_enable       = !!send_enable;
	const bool   have_pan          = !!send_pan;
	const bool   have_pan_enable   = !!send_pan_enable;
	const bool   processor_enabled = send_proc->enabled ();
	const double gain_value        = have_gain ? send_gain->get_value () : 0.0;
	const double enable_value      = have_enable ? send_enable->get_value () : 0.0;
	const double pan_value         = have_pan ? send_pan->get_value () : 0.0;
	const double pan_enable_value  = have_pan_enable ? send_pan_enable->get_value () : 0.0;

	if (source_route->remove_processor (send_proc) != 0) {
		error = "Failed to remove existing send";
		return false;
	}

	std::shared_ptr<ARDOUR::Processor> before = source_route->before_processor_for_placement (post_fader ? ARDOUR::PostFader : ARDOUR::PreFader);
	if (source_route->add_aux_send (target_route, before) != 0) {
		error = "Failed to re-create send at requested position";
		return false;
	}

	if (!find_internal_send_index (source_route, target_route, resolved_send_index)) {
		error = "Send not found after re-create";
		return false;
	}

	if (have_gain) {
		if (std::shared_ptr<ARDOUR::AutomationControl> c = source_route->send_level_controllable (resolved_send_index)) {
			c->set_value (gain_value, PBD::Controllable::NoGroup);
		}
	}
	if (have_enable) {
		if (std::shared_ptr<ARDOUR::AutomationControl> c = source_route->send_enable_controllable (resolved_send_index)) {
			c->set_value (enable_value, PBD::Controllable::NoGroup);
		}
	} else {
		if (std::shared_ptr<ARDOUR::Processor> p = source_route->nth_send (resolved_send_index)) {
			p->enable (processor_enabled);
		}
	}
	if (have_pan) {
		if (std::shared_ptr<ARDOUR::AutomationControl> c = source_route->send_pan_azimuth_controllable (resolved_send_index)) {
			c->set_value (pan_value, PBD::Controllable::NoGroup);
		}
	}
	if (have_pan_enable) {
		if (std::shared_ptr<ARDOUR::AutomationControl> c = source_route->send_pan_azimuth_enable_controllable (resolved_send_index)) {
			c->set_value (pan_enable_value, PBD::Controllable::NoGroup);
		}
	}

	return true;
}

std::string
track_info_json (const std::shared_ptr<ARDOUR::Route>& route)
{
	const bool        hidden = route->is_hidden ();
	const std::string type   = route_type_string (route);

	std::shared_ptr<ARDOUR::AutomationControl> gain       = route->gain_control ();
	std::shared_ptr<ARDOUR::AutomationControl> pan        = route->pan_azimuth_control ();
	std::shared_ptr<ARDOUR::AutomationControl> mute       = route->mute_control ();
	std::shared_ptr<ARDOUR::AutomationControl> solo       = route->solo_control ();
	std::shared_ptr<ARDOUR::Track>             track      = std::dynamic_pointer_cast<ARDOUR::Track> (route);
	std::shared_ptr<ARDOUR::AutomationControl> rec_enable = track ? track->rec_enable_control () : std::shared_ptr<ARDOUR::AutomationControl> ();
	std::shared_ptr<ARDOUR::AutomationControl> rec_safe   = track ? track->rec_safe_control () : std::shared_ptr<ARDOUR::AutomationControl> ();

	std::ostringstream ss;
	ss << "{\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
	   << ",\"name\":\"" << json_escape (route->name ()) << "\""
	   << ",\"type\":\"" << type << "\""
	   << ",\"trackNumber\":" << route->track_number ()
	   << ",\"presentationOrder\":" << route->presentation_info ().order ()
	   << ",\"hidden\":" << (hidden ? "true" : "false");

	if (gain) {
		double db = -193.0;
		if (gain->get_value () > 0.0) {
			db = accurate_coefficient_to_dB (gain->get_value ());
			if (!std::isfinite (db)) {
				db = -193.0;
			}
		}

		ss << ",\"fader\":{\"position\":" << gain->internal_to_interface (gain->get_value ())
		   << ",\"db\":" << normalized_db_value (db) << "}";
	} else {
		ss << ",\"fader\":null";
	}

	if (pan) {
		ss << ",\"pan\":{\"position\":" << pan->internal_to_interface (pan->get_value ())
		   << ",\"convention\":\"0.0=right, 0.5=center, 1.0=left\"}";
	} else {
		ss << ",\"pan\":null";
	}

	ss << ",\"mute\":";
	if (mute) {
		ss << (mute->get_value () > 0.5 ? "true" : "false");
	} else {
		ss << "null";
	}

	ss << ",\"solo\":";
	if (solo) {
		ss << (solo->get_value () > 0.5 ? "true" : "false");
	} else {
		ss << "null";
	}

	ss << ",\"recEnabled\":";
	if (rec_enable) {
		ss << (rec_enable->get_value () > 0.5 ? "true" : "false");
	} else {
		ss << "null";
	}

	ss << ",\"recSafe\":";
	if (rec_safe) {
		ss << (rec_safe->get_value () > 0.5 ? "true" : "false");
	} else {
		ss << "null";
	}

	ss << ",\"sends\":" << send_list_json (route);
	ss << ",\"plugins\":" << plugin_list_json (route);

	ss << "}";
	return ss.str ();
}

std::string
playlist_regions_json (const std::shared_ptr<ARDOUR::Playlist>& playlist, bool include_hidden)
{
	std::vector<std::shared_ptr<ARDOUR::Region>> regions;
	const ARDOUR::RegionList&                    all_regions = playlist->region_list_property ().rlist ();
	for (ARDOUR::RegionList::const_iterator it = all_regions.begin (); it != all_regions.end (); ++it) {
		if (!*it) {
			continue;
		}
		if (!include_hidden && (*it)->hidden ()) {
			continue;
		}
		regions.push_back (*it);
	}

	std::sort (
	    regions.begin (),
	    regions.end (),
	    [] (const std::shared_ptr<ARDOUR::Region>& a, const std::shared_ptr<ARDOUR::Region>& b) {
		    if (a->position_sample () != b->position_sample ()) {
			    return a->position_sample () < b->position_sample ();
		    }
		    if (a->length_samples () != b->length_samples ()) {
			    return a->length_samples () < b->length_samples ();
		    }
		    return a->id () < b->id ();
	    });

	std::ostringstream ss;
	ss << "[";
	for (size_t i = 0; i < regions.size (); ++i) {
		const std::shared_ptr<ARDOUR::Region>& region = regions[i];
		if (i > 0) {
			ss << ",";
		}

		const samplepos_t start_sample = region->position_sample ();
		const samplepos_t end_sample   = start_sample + region->length_samples ();

		ss << "{\"regionId\":\"" << json_escape (region->id ().to_s ()) << "\""
		   << ",\"name\":\"" << json_escape (region->name ()) << "\""
		   << ",\"type\":\"" << region->data_type ().to_string () << "\""
		   << ",\"startSample\":" << start_sample
		   << ",\"endSample\":" << end_sample
		   << ",\"lengthSamples\":" << region->length_samples ()
		   << ",\"startBbt\":" << bbt_json_at_sample (start_sample)
		   << ",\"endBbt\":" << bbt_json_at_sample (end_sample)
		   << ",\"hidden\":" << (region->hidden () ? "true" : "false")
		   << ",\"muted\":" << (region->muted () ? "true" : "false")
		   << ",\"locked\":" << (region->locked () ? "true" : "false")
		   << ",\"positionLocked\":" << (region->position_locked () ? "true" : "false")
		   << "}";
	}
	ss << "]";
	return ss.str ();
}

std::string
handle_track_get_info_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string route_id = root.get<std::string> ("params.arguments.id", "");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing track id");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}

	std::string structured = track_info_json (route);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Route info\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_track_get_regions_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string route_id       = root.get<std::string> ("params.arguments.id", "");
	const bool        include_hidden = root.get<bool> ("params.arguments.includeHidden", false);

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing track id");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}

	const std::shared_ptr<ARDOUR::Track> track = std::dynamic_pointer_cast<ARDOUR::Track> (route);
	if (!track) {
		return jsonrpc_error (id, -32602, "Route is not a track");
	}

	const std::shared_ptr<ARDOUR::Playlist> playlist = track->playlist ();
	if (!playlist) {
		return jsonrpc_error (id, -32000, "Track has no playlist");
	}

	const std::string  regions = playlist_regions_json (playlist, include_hidden);
	std::ostringstream structured;
	structured << "{\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
	           << ",\"name\":\"" << json_escape (route->name ()) << "\""
	           << ",\"type\":\"" << json_escape (route_type_string (route)) << "\""
	           << ",\"playlistId\":\"" << json_escape (playlist->id ().to_s ()) << "\""
	           << ",\"includeHidden\":" << (include_hidden ? "true" : "false")
	           << ",\"regions\":" << regions
	           << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Track regions listed\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_track_get_fader_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string route_id = root.get<std::string> ("params.arguments.id", "");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing track id");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}

	std::shared_ptr<ARDOUR::AutomationControl> gain = route->gain_control ();
	if (!gain) {
		return jsonrpc_error (id, -32602, "Track has no gain control");
	}

	std::string structured = track_fader_json (route);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Track fader state\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_track_select_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string route_id = root.get<std::string> ("params.arguments.id", "");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing route id");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}

	/* Match OSC /strip/select behavior: set global stripable selection. */
	session.selection ().select_stripable_and_maybe_group (route, ARDOUR::SelectionSet);

	std::string structured = track_info_json (route);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Route selected\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_track_rename_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string route_id       = root.get<std::string> ("params.arguments.id", "");
	const std::string requested_name = root.get<std::string> ("params.arguments.newName", "");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing route id");
	}
	if (requested_name.empty ()) {
		return jsonrpc_error (id, -32602, "Missing newName");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}

	const std::string old_name = route->name ();
	if (!route->set_name (requested_name)) {
		return jsonrpc_error (id, -32000, "Failed to rename route");
	}

	std::ostringstream structured;
	structured << "{\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
	           << ",\"oldName\":\"" << json_escape (old_name) << "\""
	           << ",\"newName\":\"" << json_escape (route->name ()) << "\""
	           << ",\"track\":" << track_info_json (route)
	           << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Route renamed\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_track_set_mute_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string         route_id = root.get<std::string> ("params.arguments.id", "");
	const std::optional<bool> value    = get_optional<bool> (root, "params.arguments.value");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing route id");
	}
	if (!value) {
		return jsonrpc_error (id, -32602, "Missing boolean value");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}
	if (!route->mute_control ()) {
		return jsonrpc_error (id, -32602, "Route has no mute control");
	}

	/* Match OSC mute logic. */
	route->mute_control ()->set_value (*value ? 1.0 : 0.0, PBD::Controllable::NoGroup);

	/* Route mute state can lag briefly across threads; echo the requested value on success. */
	std::ostringstream structured;
	structured << "{"
	           << "\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
	           << ",\"name\":\"" << json_escape (route->name ()) << "\""
	           << ",\"mute\":" << (*value ? "true" : "false")
	           << ",\"submitted\":true"
	           << "}";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Route mute update submitted\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_track_set_solo_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string         route_id = root.get<std::string> ("params.arguments.id", "");
	const std::optional<bool> value    = get_optional<bool> (root, "params.arguments.value");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing route id");
	}
	if (!value) {
		return jsonrpc_error (id, -32602, "Missing boolean value");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}
	if (!route->solo_control ()) {
		return jsonrpc_error (id, -32602, "Route has no solo control");
	}
	if (route->is_master () || route->is_monitor ()) {
		return jsonrpc_error (id, -32602, "Solo is not supported for this route");
	}

	/* Match OSC solo logic: use Session::set_control for solo-state propagation. */
	session.set_control (route->solo_control (), *value ? 1.0 : 0.0, PBD::Controllable::NoGroup);

	/* Solo state can lag briefly across threads; echo the requested value on success. */
	std::ostringstream structured;
	structured << "{"
	           << "\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
	           << ",\"name\":\"" << json_escape (route->name ()) << "\""
	           << ",\"solo\":" << (*value ? "true" : "false")
	           << ",\"submitted\":true"
	           << "}";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Route solo update submitted\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_track_set_rec_enable_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string         route_id = root.get<std::string> ("params.arguments.id", "");
	const std::optional<bool> value    = get_optional<bool> (root, "params.arguments.value");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing track id");
	}
	if (!value) {
		return jsonrpc_error (id, -32602, "Missing boolean value");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	const std::shared_ptr<ARDOUR::Track> track = std::dynamic_pointer_cast<ARDOUR::Track> (route);
	if (!track) {
		return jsonrpc_error (id, -32602, "Track not found");
	}
	if (!track->rec_enable_control ()) {
		return jsonrpc_error (id, -32602, "Track has no record-enable control");
	}

	/* Match OSC recenable logic. */
	track->rec_enable_control ()->set_value (*value ? 1.0 : 0.0, PBD::Controllable::NoGroup);

	/* Record-enable state can lag briefly across threads; echo the requested value on success. */
	std::ostringstream structured;
	structured << "{"
	           << "\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
	           << ",\"name\":\"" << json_escape (route->name ()) << "\""
	           << ",\"recEnabled\":" << (*value ? "true" : "false")
	           << ",\"submitted\":true"
	           << "}";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Track rec-enable update submitted\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_track_set_rec_safe_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string         route_id = root.get<std::string> ("params.arguments.id", "");
	const std::optional<bool> value    = get_optional<bool> (root, "params.arguments.value");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing track id");
	}
	if (!value) {
		return jsonrpc_error (id, -32602, "Missing boolean value");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	const std::shared_ptr<ARDOUR::Track> track = std::dynamic_pointer_cast<ARDOUR::Track> (route);
	if (!track) {
		return jsonrpc_error (id, -32602, "Track not found");
	}
	if (!track->rec_safe_control ()) {
		return jsonrpc_error (id, -32602, "Track has no record-safe control");
	}

	/* Match OSC record_safe logic. */
	track->rec_safe_control ()->set_value (*value ? 1.0 : 0.0, PBD::Controllable::NoGroup);

	/* Record-safe state can lag briefly across threads; echo the requested value on success. */
	std::ostringstream structured;
	structured << "{"
	           << "\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
	           << ",\"name\":\"" << json_escape (route->name ()) << "\""
	           << ",\"recSafe\":" << (*value ? "true" : "false")
	           << ",\"submitted\":true"
	           << "}";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Track rec-safe update submitted\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_track_set_pan_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string           route_id = root.get<std::string> ("params.arguments.id", "");
	const std::optional<double> position = get_optional<double> (root, "params.arguments.position");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing route id");
	}
	if (!position || !valid_fader_position (*position)) {
		return jsonrpc_error (id, -32602, "Invalid pan position (expected 0.0 to 1.0)");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}
	std::shared_ptr<ARDOUR::AutomationControl> pan = route->pan_azimuth_control ();
	if (!pan) {
		return jsonrpc_error (id, -32602, "Route has no pan control");
	}

	/* Match OSC pan_stereo_position logic. */
	pan->set_value (route->pan_azimuth_control ()->interface_to_internal (*position), PBD::Controllable::NoGroup);

	std::string structured = track_info_json (route);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Route pan updated\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_track_set_send_level_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string           route_id   = root.get<std::string> ("params.arguments.id", "");
	const int                   send_index = root.get<int> ("params.arguments.sendIndex", -1);
	const std::optional<double> position   = get_optional<double> (root, "params.arguments.position");
	const std::optional<double> db         = get_optional<double> (root, "params.arguments.db");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing route id");
	}
	if (send_index < 0) {
		return jsonrpc_error (id, -32602, "Invalid sendIndex (expected >= 0)");
	}
	if (!position && !db) {
		return jsonrpc_error (id, -32602, "Provide one of: position (0.0 to 1.0) or db");
	}
	if (position && db) {
		return jsonrpc_error (id, -32602, "Provide only one of: position or db");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}

	const uint32_t                     sid       = (uint32_t)send_index;
	std::shared_ptr<ARDOUR::Processor> send_proc = route->nth_send (sid);
	if (!send_proc) {
		return jsonrpc_error (id, -32602, "Send not found");
	}

	std::shared_ptr<ARDOUR::AutomationControl> send_gain = route->send_level_controllable (sid);
	if (!send_gain) {
		return jsonrpc_error (id, -32602, "Send has no level control");
	}

	double internal_gain = send_gain->get_value ();
	if (position) {
		if (!valid_fader_position (*position)) {
			return jsonrpc_error (id, -32602, "Invalid send position (expected 0.0 to 1.0)");
		}

		/* Match OSC /strip/send/fader behavior. */
		internal_gain = send_gain->interface_to_internal (*position);
	} else {
		if (!valid_fader_db (*db)) {
			return jsonrpc_error (id, -32602, "Invalid dB value (expected -193.0 to +6.0 dB; use -193.0 for silence)");
		}

		/* Match OSC /strip/send/gain behavior. */
		internal_gain = db_to_gain_with_floor (*db);
	}

	if (!std::isfinite (internal_gain)) {
		return jsonrpc_error (id, -32602, "Invalid mapped send gain");
	}

	internal_gain = std::max (send_gain->lower (), std::min (send_gain->upper (), internal_gain));
	send_gain->set_value (internal_gain, PBD::Controllable::NoGroup);

	std::string structured = send_level_json (route, sid);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Send level updated\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_track_add_send_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string           route_id   = root.get<std::string> ("params.arguments.id", "");
	const std::string           target_id  = root.get<std::string> ("params.arguments.targetId", "");
	const std::optional<double> position   = get_optional<double> (root, "params.arguments.position");
	const std::optional<double> db         = get_optional<double> (root, "params.arguments.db");
	const std::optional<bool>   enabled    = get_optional<bool> (root, "params.arguments.enabled");
	const std::optional<bool>   post_fader = get_optional<bool> (root, "params.arguments.postFader");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing source route id");
	}
	if (target_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing target route id");
	}
	if (position && db) {
		return jsonrpc_error (id, -32602, "Provide only one of: position or db");
	}
	if (position && !valid_fader_position (*position)) {
		return jsonrpc_error (id, -32602, "Invalid send position (expected 0.0 to 1.0)");
	}
	if (db && !valid_fader_db (*db)) {
		return jsonrpc_error (id, -32602, "Invalid dB value (expected -193.0 to +6.0 dB; use -193.0 for silence)");
	}

	const std::shared_ptr<ARDOUR::Route> route        = route_by_mcp_id_or_selection (session, route_id);
	const std::shared_ptr<ARDOUR::Route> target_route = route_by_mcp_id_or_selection (session, target_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Source route not found");
	}
	if (!target_route) {
		return jsonrpc_error (id, -32602, "Target route not found");
	}
	if (route->id () == target_route->id ()) {
		return jsonrpc_error (id, -32602, "Cannot add send to the same route");
	}
	if (route->is_singleton ()) {
		return jsonrpc_error (id, -32602, "Cannot add send from singleton route");
	}
	if (target_route->is_singleton ()) {
		return jsonrpc_error (id, -32602, "Cannot add send to singleton route");
	}
	std::shared_ptr<ARDOUR::Route> monitor_out = session.monitor_out ();
	if (monitor_out && monitor_out->id () == target_route->id ()) {
		return jsonrpc_error (id, -32602, "Cannot add aux send to monitor route");
	}

	const bool                         existed_before = (route->internal_send_for (target_route) != 0);
	std::shared_ptr<ARDOUR::Processor> before;
	if (post_fader) {
		before = route->before_processor_for_placement (*post_fader ? ARDOUR::PostFader : ARDOUR::PreFader);
	}
	const int rc = route->add_aux_send (target_route, before);
	if (rc != 0) {
		return jsonrpc_error (id, -32000, "Failed to add send");
	}

	uint32_t sid = 0;
	if (!find_internal_send_index (route, target_route, sid)) {
		return jsonrpc_error (id, -32000, "Send not found after add");
	}

	std::shared_ptr<ARDOUR::AutomationControl> send_gain = route->send_level_controllable (sid);
	if (position || db) {
		if (!send_gain) {
			return jsonrpc_error (id, -32602, "Send has no level control");
		}

		double internal_gain = send_gain->get_value ();
		if (position) {
			internal_gain = send_gain->interface_to_internal (*position);
		} else {
			internal_gain = db_to_gain_with_floor (*db);
		}

		if (!std::isfinite (internal_gain)) {
			return jsonrpc_error (id, -32602, "Invalid mapped send gain");
		}

		internal_gain = std::max (send_gain->lower (), std::min (send_gain->upper (), internal_gain));
		send_gain->set_value (internal_gain, PBD::Controllable::NoGroup);
	}

	if (enabled) {
		std::shared_ptr<ARDOUR::AutomationControl> send_enable = route->send_enable_controllable (sid);
		if (send_enable) {
			send_enable->set_value (*enabled ? 1.0 : 0.0, PBD::Controllable::NoGroup);
		} else {
			std::shared_ptr<ARDOUR::Processor> send_proc = route->nth_send (sid);
			if (!send_proc) {
				return jsonrpc_error (id, -32000, "Send not found while setting enabled state");
			}
			send_proc->enable (*enabled);
		}
	}

	std::ostringstream structured;
	structured << "{\"created\":" << (existed_before ? "false" : "true")
	           << ",\"send\":" << send_level_json (route, sid)
	           << ",\"sends\":" << send_list_json (route)
	           << "}";

	const char* text = existed_before ? "Send already existed; state updated" : "Send added";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + text + "\"}],\"structuredContent\":" + structured.str () + "}");
}

std::string
handle_track_set_send_position_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string         route_id   = root.get<std::string> ("params.arguments.id", "");
	const int                 send_index = root.get<int> ("params.arguments.sendIndex", -1);
	const std::optional<bool> post_fader = get_optional<bool> (root, "params.arguments.postFader");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing route id");
	}
	if (send_index < 0) {
		return jsonrpc_error (id, -32602, "Invalid sendIndex (expected >= 0)");
	}
	if (!post_fader) {
		return jsonrpc_error (id, -32602, "Missing postFader boolean");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}

	uint32_t    sid = 0;
	std::string err;
	if (!recreate_aux_send_with_position (route, (uint32_t)send_index, *post_fader, sid, err)) {
		return jsonrpc_error (id, -32000, err.empty () ? "Failed to set send position" : err);
	}

	std::string structured = send_level_json (route, sid);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Send position updated\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_track_remove_send_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string route_id   = root.get<std::string> ("params.arguments.id", "");
	const int         send_index = root.get<int> ("params.arguments.sendIndex", -1);
	const std::string target_id  = root.get<std::string> ("params.arguments.targetId", "");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing route id");
	}
	if (send_index < 0) {
		return jsonrpc_error (id, -32602, "Invalid sendIndex (expected >= 0)");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}

	const uint32_t                     sid       = (uint32_t)send_index;
	std::shared_ptr<ARDOUR::Processor> send_proc = route->nth_send (sid);
	if (!send_proc) {
		return jsonrpc_error (id, -32602, "Send not found");
	}

	std::ostringstream removed;
	removed << "{\"routeId\":\"" << json_escape (route->id ().to_s ()) << "\""
	        << ",\"routeName\":\"" << json_escape (route->name ()) << "\""
	        << ",\"sendIndex\":" << sid
	        << ",\"name\":\"" << json_escape (route->send_name (sid)) << "\""
	        << ",\"active\":" << (send_proc->active () ? "true" : "false")
	        << ",\"preFader\":" << (send_proc->get_pre_fader () ? "true" : "false")
	        << ",\"postFader\":" << (send_proc->get_pre_fader () ? "false" : "true");

	std::shared_ptr<ARDOUR::InternalSend> isend = std::dynamic_pointer_cast<ARDOUR::InternalSend> (send_proc);
	if (isend) {
		std::shared_ptr<ARDOUR::Route> target_route = isend->target_route ();
		if (!target_id.empty ()) {
			if (!target_route) {
				return jsonrpc_error (id, -32602, "Internal send has no target route");
			}
			if (target_route->id ().to_s () != target_id) {
				return jsonrpc_error (id, -32602, "Send targetId mismatch");
			}
		}
		if (target_route) {
			removed << ",\"targetRouteId\":\"" << json_escape (target_route->id ().to_s ()) << "\""
			        << ",\"targetRouteName\":\"" << json_escape (target_route->name ()) << "\"";
		}
	} else if (!target_id.empty ()) {
		return jsonrpc_error (id, -32602, "targetId is only supported for internal sends");
	}
	removed << "}";

	if (route->remove_processor (send_proc) != 0) {
		return jsonrpc_error (id, -32000, "Failed to remove send");
	}

	std::ostringstream structured;
	structured << "{\"removed\":true"
	           << ",\"routeId\":\"" << json_escape (route->id ().to_s ()) << "\""
	           << ",\"routeName\":\"" << json_escape (route->name ()) << "\""
	           << ",\"sendIndex\":" << sid
	           << ",\"removedSend\":" << removed.str ()
	           << ",\"sends\":" << send_list_json (route)
	           << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Send removed\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_track_set_fader_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string           route_id = root.get<std::string> ("params.arguments.id", "");
	const std::optional<double> position = get_optional<double> (root, "params.arguments.position");
	const std::optional<double> db       = get_optional<double> (root, "params.arguments.db");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing track id");
	}
	if (!position && !db) {
		return jsonrpc_error (id, -32602, "Provide one of: position (0.0 to 1.0) or db");
	}
	if (position && db) {
		return jsonrpc_error (id, -32602, "Provide only one of: position or db");
	}

	const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}

	std::shared_ptr<ARDOUR::AutomationControl> gain = route->gain_control ();
	if (!gain) {
		return jsonrpc_error (id, -32602, "Track has no gain control");
	}

	double internal_gain = gain->get_value ();
	if (position) {
		if (!valid_fader_position (*position)) {
			return jsonrpc_error (id, -32602, "Invalid fader position (expected 0.0 to 1.0)");
		}

		/* Match OSC behavior for /fader: convert surface position to internal gain value. */
		internal_gain = gain->interface_to_internal (*position);
	} else {
		if (!valid_fader_db (*db)) {
			return jsonrpc_error (id, -32602, "Invalid dB value (expected -193.0 to +6.0 dB; use -193.0 for silence)");
		}

		/* Match OSC behavior for /gain: map dB to coefficient, then clamp to control bounds. */
		internal_gain = db_to_gain_with_floor (*db);
		internal_gain = std::max (gain->lower (), std::min (gain->upper (), internal_gain));
	}

	gain->set_value (internal_gain, PBD::Controllable::NoGroup);

	std::string structured = track_fader_json (route);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Track fader updated\"}],\"structuredContent\":") + structured + "}");
}

} /* anonymous namespace */

bool
dispatch_track_tool_call (ARDOUR::Session& session, const std::string& tool_name, const pt::ptree& root, const std::string& id, std::string& response)
{
	if (tool_name == "track/get_info") {
		response = handle_track_get_info_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/get_regions") {
		response = handle_track_get_regions_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/get_fader") {
		response = handle_track_get_fader_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/select") {
		response = handle_track_select_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/rename") {
		response = handle_track_rename_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/set_mute") {
		response = handle_track_set_mute_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/set_solo") {
		response = handle_track_set_solo_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/set_rec_enable") {
		response = handle_track_set_rec_enable_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/set_rec_safe") {
		response = handle_track_set_rec_safe_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/set_pan") {
		response = handle_track_set_pan_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/set_send_level") {
		response = handle_track_set_send_level_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/add_send") {
		response = handle_track_add_send_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/set_send_position") {
		response = handle_track_set_send_position_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/remove_send") {
		response = handle_track_remove_send_tool (session, root, id);
		return true;
	}
	if (tool_name == "track/set_fader") {
		response = handle_track_set_fader_tool (session, root, id);
		return true;
	}

	return false;
}

} /* namespace mcp */
} /* namespace ArdourSurface */
