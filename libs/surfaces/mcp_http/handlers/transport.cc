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

#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "ardour/location.h"
#include "ardour/rc_configuration.h"
#include "ardour/session.h"
#include "ardour/session_event.h"
#include "ardour/types.h"

#include "handlers/common.h"
#include "handlers/transport.h"

namespace ArdourSurface
{
namespace mcp
{
namespace
{

std::string
handle_transport_get_state_tool (ARDOUR::Session& session, const std::string& id)
{
	std::string structured = transport_state_json (session);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Transport state\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_transport_locate_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::optional<int64_t> sample_opt      = get_optional<int64_t> (root, "params.arguments.sample");
	samplepos_t                  target_sample   = 0;
	bool                         have_bbt_target = false;
	std::string                  bbt_error;
	if (!parse_optional_bbt_target_sample (root, "params.arguments", target_sample, have_bbt_target, bbt_error)) {
		return jsonrpc_error (id, -32602, bbt_error);
	}

	if (sample_opt && have_bbt_target) {
		return jsonrpc_error (id, -32602, "Provide either sample or bar+beat, not both");
	}
	if (!sample_opt && !have_bbt_target) {
		return jsonrpc_error (id, -32602, "Missing target position (provide sample or bar+beat)");
	}

	if (sample_opt) {
		if (*sample_opt < 0) {
			return jsonrpc_error (id, -32602, "Invalid sample (expected >= 0)");
		}
		target_sample = (samplepos_t)*sample_opt;
	}

	session.request_locate (target_sample, false, ARDOUR::RollIfAppropriate);

	std::ostringstream structured;
	structured << "{\"requestedSample\":" << target_sample
	           << ",\"requestedBbt\":" << bbt_json_at_sample (target_sample)
	           << ",\"transport\":" << transport_state_json (session)
	           << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Transport locate requested\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_transport_goto_start_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const bool and_roll = root.get<bool> ("params.arguments.andRoll", false);
	session.goto_start (and_roll);
	std::string structured = transport_state_json (session);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Transport moved to start\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_transport_goto_end_tool (ARDOUR::Session& session, const std::string& id)
{
	session.goto_end ();
	std::string structured = transport_state_json (session);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Transport moved to end\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_transport_prev_marker_tool (ARDOUR::Session& session, const std::string& id)
{
	ARDOUR::Locations* locations = session.locations ();
	if (!locations) {
		return jsonrpc_error (id, -32602, "Session locations unavailable");
	}

	const samplepos_t   now_sample = session.transport_sample ();
	Temporal::timepos_t pos        = locations->first_mark_before_flagged (
            Temporal::timepos_t (now_sample),
            true,
            ARDOUR::Location::Flags (0),
            ARDOUR::Location::Flags (0),
            ARDOUR::Location::Flags (0));

	/* Match Editor behavior while rolling: skip the current/very-near mark. */
	if (pos != Temporal::timepos_t::max (Temporal::AudioTime) && session.transport_rolling ()) {
		if ((now_sample - pos.samples ()) < (session.sample_rate () / 2)) {
			Temporal::timepos_t prior = locations->first_mark_before (pos, true);
			if (prior != Temporal::timepos_t::max (Temporal::AudioTime)) {
				pos = prior;
			}
		}
	}

	std::ostringstream structured;
	if (pos == Temporal::timepos_t::max (Temporal::AudioTime)) {
		structured << "{\"moved\":false,\"transport\":" << transport_state_json (session) << "}";
		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"No previous marker\"}],\"structuredContent\":") + structured.str () + "}");
	}

	session.request_locate (pos.samples ());

	structured << "{\"moved\":true"
	           << ",\"targetSample\":" << pos.samples ()
	           << ",\"targetBbt\":" << bbt_json_at_sample (pos.samples ())
	           << ",\"transport\":" << transport_state_json (session)
	           << "}";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Moved to previous marker\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_transport_next_marker_tool (ARDOUR::Session& session, const std::string& id)
{
	ARDOUR::Locations* locations = session.locations ();
	if (!locations) {
		return jsonrpc_error (id, -32602, "Session locations unavailable");
	}

	Temporal::timepos_t pos = locations->first_mark_after_flagged (
	    Temporal::timepos_t (session.transport_sample () + 1),
	    true,
	    ARDOUR::Location::Flags (0),
	    ARDOUR::Location::Flags (0),
	    ARDOUR::Location::Flags (0));

	std::ostringstream structured;
	if (pos == Temporal::timepos_t::max (Temporal::AudioTime)) {
		structured << "{\"moved\":false,\"transport\":" << transport_state_json (session) << "}";
		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"No next marker\"}],\"structuredContent\":") + structured.str () + "}");
	}

	session.request_locate (pos.samples ());

	structured << "{\"moved\":true"
	           << ",\"targetSample\":" << pos.samples ()
	           << ",\"targetBbt\":" << bbt_json_at_sample (pos.samples ())
	           << ",\"transport\":" << transport_state_json (session)
	           << "}";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Moved to next marker\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_transport_loop_toggle_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	ARDOUR::Locations* locations = session.locations ();
	if (!locations) {
		return jsonrpc_error (id, -32602, "Session locations unavailable");
	}

	ARDOUR::Location* loop_loc = locations->auto_loop_location ();
	if (!loop_loc) {
		return jsonrpc_error (id, -32602, "Auto-loop range not set");
	}

	const std::optional<bool> enabled_opt       = get_optional<bool> (root, "params.arguments.enabled");
	const bool                requested_enabled = enabled_opt ? *enabled_opt : !session.get_play_loop ();
	const bool                loop_is_mode      = ARDOUR::Config->get_loop_is_mode ();
	const bool                was_rolling       = session.transport_rolling ();

	if (session.get_play_loop () != requested_enabled) {
		if (requested_enabled) {
			/* Match BasicUI/OSC semantics: loop-is-mode does not force roll. */
			if (loop_is_mode) {
				session.request_play_loop (true, false);
			} else {
				session.request_play_loop (true, true);
				/* Ensure transport-state UI updates if SetLoop and roll requests race. */
				if (!was_rolling) {
					session.request_roll ();
				}
			}
		} else {
			session.request_play_loop (false);
		}
	}

	/* Match BasicUI: loop toggle should unhide the loop range. */
	loop_loc->set_hidden (false, 0);

	std::ostringstream structured;
	structured << "{\"requestedEnabled\":" << (requested_enabled ? "true" : "false")
	           << ",\"loopIsMode\":" << (loop_is_mode ? "true" : "false")
	           << ",\"rollingBefore\":" << (was_rolling ? "true" : "false")
	           << ",\"playLoop\":" << (session.get_play_loop () ? "true" : "false")
	           << ",\"transport\":" << transport_state_json (session)
	           << ",\"loop\":" << special_range_json (*loop_loc, "auto_loop")
	           << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Loop playback updated\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_transport_set_record_enable_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const bool                requested_enabled = root.get<bool> ("params.arguments.enabled");
	const ARDOUR::RecordState status_before     = session.record_status ();

	if (requested_enabled) {
		session.maybe_enable_record ();
	} else {
		session.disable_record (false, true);
	}

	const ARDOUR::RecordState status_after = session.record_status ();

	std::ostringstream structured;
	structured << "{\"requestedEnabled\":" << (requested_enabled ? "true" : "false")
	           << ",\"recordEnabled\":" << (session.get_record_enabled () ? "true" : "false")
	           << ",\"recordStatusBefore\":\"" << record_state_string (status_before) << "\""
	           << ",\"recordStatusAfter\":\"" << record_state_string (status_after) << "\""
	           << ",\"transport\":" << transport_state_json (session)
	           << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Global record-enable updated\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_transport_set_speed_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::optional<double> speed_opt = get_optional<double> (root, "params.arguments.speed");
	if (!speed_opt || !std::isfinite (*speed_opt)) {
		return jsonrpc_error (id, -32602, "Missing or invalid speed");
	}
	if (std::abs (*speed_opt) < 1e-12) {
		return jsonrpc_error (id, -32602, "Invalid speed 0. Use transport/stop to stop playback");
	}

	const double speed = *speed_opt;
	/* Match OSC/BasicUI set_transport_speed behavior. */
	session.request_roll (ARDOUR::TRS_UI);
	session.request_transport_speed (speed, ARDOUR::TRS_UI);

	std::ostringstream structured;
	structured << "{\"requestedSpeed\":" << speed
	           << ",\"actualSpeed\":" << session.actual_speed ()
	           << ",\"transport\":" << transport_state_json (session)
	           << "}";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Transport speed updated\"}],\"structuredContent\":") + structured.str () + "}");
}

/* request_roll/request_stop are async — the realtime thread applies the change.
 * Briefly wait for the state to settle so the response reflects post-call state
 * instead of the stale pre-call snapshot. Caps at ~100 ms; on timeout we return
 * what we have. */
static void
wait_for_transport_state (ARDOUR::Session& session, bool target_rolling)
{
	using namespace std::chrono;
	const auto deadline = steady_clock::now () + milliseconds (100);
	while (steady_clock::now () < deadline) {
		if (session.transport_rolling () == target_rolling) {
			return;
		}
		std::this_thread::sleep_for (milliseconds (2));
	}
}

std::string
handle_transport_play_tool (ARDOUR::Session& session, const std::string& id)
{
	session.request_roll ();
	wait_for_transport_state (session, true);
	std::string structured = transport_state_json (session);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Transport play requested\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_transport_stop_tool (ARDOUR::Session& session, const std::string& id)
{
	session.request_stop ();
	wait_for_transport_state (session, false);
	std::string structured = transport_state_json (session);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Transport stop requested\"}],\"structuredContent\":") + structured + "}");
}

} /* anonymous namespace */

bool
dispatch_transport_tool_call (ARDOUR::Session& session, const std::string& tool_name, const pt::ptree& root, const std::string& id, std::string& response)
{
	if (tool_name == "transport/get_state") {
		response = handle_transport_get_state_tool (session, id);
		return true;
	}
	if (tool_name == "transport/locate") {
		response = handle_transport_locate_tool (session, root, id);
		return true;
	}
	if (tool_name == "transport/goto_start") {
		response = handle_transport_goto_start_tool (session, root, id);
		return true;
	}
	if (tool_name == "transport/goto_end") {
		response = handle_transport_goto_end_tool (session, id);
		return true;
	}
	if (tool_name == "transport/prev_marker") {
		response = handle_transport_prev_marker_tool (session, id);
		return true;
	}
	if (tool_name == "transport/next_marker") {
		response = handle_transport_next_marker_tool (session, id);
		return true;
	}
	if (tool_name == "transport/loop_toggle") {
		response = handle_transport_loop_toggle_tool (session, root, id);
		return true;
	}
	if (tool_name == "transport/set_record_enable") {
		response = handle_transport_set_record_enable_tool (session, root, id);
		return true;
	}
	if (tool_name == "transport/set_speed") {
		response = handle_transport_set_speed_tool (session, root, id);
		return true;
	}
	if (tool_name == "transport/play") {
		response = handle_transport_play_tool (session, id);
		return true;
	}
	if (tool_name == "transport/stop") {
		response = handle_transport_stop_tool (session, id);
		return true;
	}

	return false;
}

} /* namespace mcp */
} /* namespace ArdourSurface */
