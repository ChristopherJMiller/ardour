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
#include <ctime>
#include <sstream>
#include <string>

#include "ardour/session.h"

#include "handlers/common.h"
#include "handlers/session.h"

namespace ArdourSurface
{
namespace mcp
{
namespace
{

std::string
session_info_json (ARDOUR::Session& session)
{
	std::ostringstream ss;
	ss << "{\"sessionName\":\"" << json_escape (session.name ()) << "\""
	   << ",\"sampleRate\":" << session.nominal_sample_rate ()
	   << ",\"tempoBpm\":" << transport_tempo_bpm (session)
	   << ",\"transport\":" << transport_state_json (session)
	   << "}";
	return ss.str ();
}

std::string
quick_snapshot_name_localtime ()
{
	const std::time_t now = std::time (0);
	std::tm           tm_now;
#ifdef _WIN32
	localtime_s (&tm_now, &now);
#else
	localtime_r (&now, &tm_now);
#endif

	char buf[64];
	if (std::strftime (buf, sizeof (buf), "%Y-%m-%dT%H.%M.%S", &tm_now) == 0) {
		return "snapshot";
	}
	return std::string (buf);
}

std::string
handle_session_get_info_tool (ARDOUR::Session& session, const std::string& id)
{
	std::string structured = session_info_json (session);
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Session info\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_session_save_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string snapshot_name      = root.get<std::string> ("params.arguments.snapshotName", "");
	const bool        switch_to_snapshot = root.get<bool> ("params.arguments.switchToSnapshot", false);

	const int rc = session.save_state (snapshot_name, false, switch_to_snapshot);
	if (rc != 0) {
		return jsonrpc_error (id, -32000, "Failed to save session state");
	}

	std::ostringstream structured;
	structured << "{\"saved\":true";
	if (snapshot_name.empty ()) {
		structured << ",\"requestedSnapshotName\":null";
	} else {
		structured << ",\"requestedSnapshotName\":\"" << json_escape (snapshot_name) << "\"";
	}
	structured << ",\"switchToSnapshot\":" << (switch_to_snapshot ? "true" : "false")
	           << ",\"currentSnapshotName\":\"" << json_escape (session.snap_name ()) << "\""
	           << ",\"session\":" << session_info_json (session)
	           << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Session saved\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_session_undo_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const int64_t count_in = root.get<int64_t> ("params.arguments.count", 1);
	if (count_in < 1 || count_in > 1024) {
		return jsonrpc_error (id, -32602, "Invalid count (expected 1..1024)");
	}
	const uint32_t count = (uint32_t)count_in;

	const uint32_t undo_before = session.undo_depth ();
	const uint32_t redo_before = session.redo_depth ();
	session.undo (count);
	const uint32_t undo_after = session.undo_depth ();
	const uint32_t redo_after = session.redo_depth ();

	std::ostringstream structured;
	structured << "{\"countRequested\":" << count
	           << ",\"undoDepthBefore\":" << undo_before
	           << ",\"redoDepthBefore\":" << redo_before
	           << ",\"undoDepthAfter\":" << undo_after
	           << ",\"redoDepthAfter\":" << redo_after
	           << ",\"nextUndo\":\"" << json_escape (session.next_undo ()) << "\""
	           << ",\"nextRedo\":\"" << json_escape (session.next_redo ()) << "\""
	           << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Undo applied\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_session_redo_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const int64_t count_in = root.get<int64_t> ("params.arguments.count", 1);
	if (count_in < 1 || count_in > 1024) {
		return jsonrpc_error (id, -32602, "Invalid count (expected 1..1024)");
	}
	const uint32_t count = (uint32_t)count_in;

	const uint32_t undo_before = session.undo_depth ();
	const uint32_t redo_before = session.redo_depth ();
	session.redo (count);
	const uint32_t undo_after = session.undo_depth ();
	const uint32_t redo_after = session.redo_depth ();

	std::ostringstream structured;
	structured << "{\"countRequested\":" << count
	           << ",\"undoDepthBefore\":" << undo_before
	           << ",\"redoDepthBefore\":" << redo_before
	           << ",\"undoDepthAfter\":" << undo_after
	           << ",\"redoDepthAfter\":" << redo_after
	           << ",\"nextUndo\":\"" << json_escape (session.next_undo ()) << "\""
	           << ",\"nextRedo\":\"" << json_escape (session.next_redo ()) << "\""
	           << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Redo applied\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_session_rename_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string new_name = root.get<std::string> ("params.arguments.newName", "");
	if (new_name.empty ()) {
		return jsonrpc_error (id, -32602, "Missing newName");
	}

	const std::string illegal = ARDOUR::Session::session_name_is_legal (new_name);
	if (!illegal.empty ()) {
		return jsonrpc_error (id, -32602, std::string ("Session name contains illegal character: ") + illegal);
	}

	const std::string old_name = session.name ();
	const int         rc       = session.rename (new_name);
	if (rc == -1) {
		return jsonrpc_error (id, -32000, "Session name already exists");
	}
	if (rc != 0) {
		return jsonrpc_error (id, -32000, "Renaming session failed");
	}

	std::ostringstream structured;
	structured << "{\"oldName\":\"" << json_escape (old_name) << "\""
	           << ",\"newName\":\"" << json_escape (session.name ()) << "\""
	           << ",\"session\":" << session_info_json (session)
	           << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Session renamed\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_session_quick_snapshot_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const bool        switch_to_snapshot = root.get<bool> ("params.arguments.switchToSnapshot", false);
	const std::string before_snapshot    = session.snap_name ();
	const std::string snapshot_name      = quick_snapshot_name_localtime ();

	/* Match ARDOUR_UI::quick_snapshot_session behavior without invoking GUI actions. */
	if (switch_to_snapshot && session.dirty ()) {
		if (session.save_state ("") != 0) {
			return jsonrpc_error (id, -32000, "Failed to save current session before quick snapshot");
		}
	}
	if (session.save_state (snapshot_name, false, switch_to_snapshot) != 0) {
		return jsonrpc_error (id, -32000, "Quick snapshot save failed");
	}

	std::ostringstream structured;
	structured << "{\"switchToSnapshot\":" << (switch_to_snapshot ? "true" : "false")
	           << ",\"snapshotName\":\"" << json_escape (snapshot_name) << "\""
	           << ",\"snapshotNameBefore\":\"" << json_escape (before_snapshot) << "\""
	           << ",\"snapshotNameAfter\":\"" << json_escape (session.snap_name ()) << "\""
	           << ",\"session\":" << session_info_json (session)
	           << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Quick snapshot requested\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_session_store_mixer_scene_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const int64_t index_in = root.get<int64_t> ("params.arguments.index", -1);
	if (index_in < 0 || index_in > 1024) {
		return jsonrpc_error (id, -32602, "Invalid index (expected 0..1024)");
	}
	const size_t index = (size_t)index_in;
	session.store_nth_mixer_scene (index);

	std::ostringstream structured;
	structured << "{\"index\":" << index
	           << ",\"stored\":true}";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Mixer scene stored\"}],\"structuredContent\":") + structured.str () + "}");
}

std::string
handle_session_recall_mixer_scene_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const int64_t index_in = root.get<int64_t> ("params.arguments.index", -1);
	if (index_in < 0 || index_in > 1024) {
		return jsonrpc_error (id, -32602, "Invalid index (expected 0..1024)");
	}
	const size_t index = (size_t)index_in;
	const bool   ok    = session.apply_nth_mixer_scene (index);
	if (!ok) {
		return jsonrpc_error (id, -32000, "Mixer scene recall failed");
	}

	std::ostringstream structured;
	structured << "{\"index\":" << index
	           << ",\"recalled\":true}";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Mixer scene recalled\"}],\"structuredContent\":") + structured.str () + "}");
}

} /* anonymous namespace */

bool
dispatch_session_tool_call (ARDOUR::Session& session, const std::string& tool_name, const pt::ptree& root, const std::string& id, std::string& response)
{
	if (tool_name == "session/get_info") {
		response = handle_session_get_info_tool (session, id);
		return true;
	}
	if (tool_name == "session/save") {
		response = handle_session_save_tool (session, root, id);
		return true;
	}
	if (tool_name == "session/undo") {
		response = handle_session_undo_tool (session, root, id);
		return true;
	}
	if (tool_name == "session/redo") {
		response = handle_session_redo_tool (session, root, id);
		return true;
	}
	if (tool_name == "session/rename") {
		response = handle_session_rename_tool (session, root, id);
		return true;
	}
	if (tool_name == "session/quick_snapshot") {
		response = handle_session_quick_snapshot_tool (session, root, id);
		return true;
	}
	if (tool_name == "session/store_mixer_scene") {
		response = handle_session_store_mixer_scene_tool (session, root, id);
		return true;
	}
	if (tool_name == "session/recall_mixer_scene") {
		response = handle_session_recall_mixer_scene_tool (session, root, id);
		return true;
	}

	return false;
}

} /* namespace mcp */
} /* namespace ArdourSurface */
