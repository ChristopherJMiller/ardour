/*
 * Copyright (C) 2026 Christopher Miller
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

#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

#include "ardour/session.h"
#include "ardour/types.h"

#include "temporal/tempo.h"

#include "handlers/common.h"
#include "handlers/tempo.h"

namespace ArdourSurface
{
namespace mcp
{
namespace
{

std::string
tempo_point_json (const Temporal::TempoPoint& tp)
{
	const Temporal::BBT_Time bbt = tp.bbt ();
	std::ostringstream       ss;
	ss << "{\"type\":\"tempo\""
	   << ",\"bbt\":{\"bars\":" << bbt.bars
	   << ",\"beats\":" << bbt.beats
	   << ",\"ticks\":" << bbt.ticks << "}"
	   << ",\"bpm\":" << tp.note_types_per_minute ()
	   << ",\"endBpm\":" << tp.end_note_types_per_minute ()
	   << ",\"noteType\":" << (int)tp.note_type ()
	   << ",\"ramped\":" << (tp.type () == Temporal::Tempo::Ramped ? "true" : "false")
	   << ",\"continuing\":" << (tp.continuing () ? "true" : "false")
	   << ",\"sample\":" << tp.time ().samples ()
	   << "}";
	return ss.str ();
}

std::string
meter_point_json (const Temporal::MeterPoint& mp)
{
	const Temporal::BBT_Time bbt = mp.bbt ();
	std::ostringstream       ss;
	ss << "{\"type\":\"meter\""
	   << ",\"bbt\":{\"bars\":" << bbt.bars
	   << ",\"beats\":" << bbt.beats
	   << ",\"ticks\":" << bbt.ticks << "}"
	   << ",\"divisionsPerBar\":" << mp.divisions_per_bar ()
	   << ",\"noteValue\":" << mp.note_value ()
	   << ",\"sample\":" << mp.time ().samples ()
	   << "}";
	return ss.str ();
}

bool
parse_bbt_required (const pt::ptree& root, const std::string& args_path,
                    Temporal::BBT_Argument& out, std::string& error)
{
	const std::optional<int>    bar_opt  = get_optional<int> (root, args_path + ".bar");
	const std::optional<double> beat_opt = get_optional<double> (root, args_path + ".beat");

	if (!bar_opt || !beat_opt) {
		error = "Missing required bar and beat";
		return false;
	}
	if (*bar_opt < 1 || *beat_opt < 1.0 || !std::isfinite (*beat_opt)) {
		error = "Invalid bar/beat (bar>=1, beat>=1.0)";
		return false;
	}

	const int32_t whole_beats = (int32_t)std::floor (*beat_opt);
	const double  fractional  = *beat_opt - (double)whole_beats;
	int32_t       ticks       = (int32_t)std::llround (fractional * (double)Temporal::ticks_per_beat);
	int32_t       beats       = whole_beats;
	if (ticks >= Temporal::ticks_per_beat) {
		ticks = 0;
		++beats;
	}

	out = Temporal::BBT_Argument ((int32_t)*bar_opt, beats, ticks);
	return true;
}

std::string
handle_tempo_list_tool (ARDOUR::Session& /*session*/, const std::string& id)
{
	Temporal::TempoMap::fetch ();
	Temporal::TempoMap::SharedPtr map = Temporal::TempoMap::use ();

	std::ostringstream ss;
	ss << "{\"tempos\":[";
	bool first = true;
	for (const Temporal::TempoPoint& tp : map->tempos ()) {
		if (!first) {
			ss << ",";
		}
		first = false;
		ss << tempo_point_json (tp);
	}
	ss << "],\"meters\":[";
	first = true;
	for (const Temporal::MeterPoint& mp : map->meters ()) {
		if (!first) {
			ss << ",";
		}
		first = false;
		ss << meter_point_json (mp);
	}
	ss << "]}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Tempo map listed\"}],\"structuredContent\":") + ss.str () + "}");
}

std::string
handle_tempo_add_tempo_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	Temporal::TempoMap::fetch ();

	const std::optional<double> bpm_opt       = get_optional<double> (root, "params.arguments.bpm");
	const std::optional<double> end_bpm_opt   = get_optional<double> (root, "params.arguments.endBpm");
	const std::optional<int>    note_type_opt = get_optional<int> (root, "params.arguments.noteType");

	if (!bpm_opt) {
		return jsonrpc_error (id, -32602, "Missing bpm");
	}
	if (*bpm_opt <= 0.0 || *bpm_opt > 999.0 || !std::isfinite (*bpm_opt)) {
		return jsonrpc_error (id, -32602, "Invalid bpm (expected 0 < bpm <= 999)");
	}
	const int note_type = note_type_opt ? *note_type_opt : 4;
	if (note_type < 1 || note_type > 64) {
		return jsonrpc_error (id, -32602, "Invalid noteType (expected 1..64)");
	}

	Temporal::BBT_Argument bbt;
	std::string            err;
	if (!parse_bbt_required (root, "params.arguments.bbt", bbt, err)) {
		return jsonrpc_error (id, -32602, err);
	}

	const double end_bpm = end_bpm_opt && std::isfinite (*end_bpm_opt) && *end_bpm_opt > 0.0 ? *end_bpm_opt : *bpm_opt;

	session.begin_reversible_command ("add tempo");
	Temporal::TempoMap::WritableSharedPtr map = Temporal::TempoMap::write_copy ();
	const Temporal::Tempo                 t (*bpm_opt, end_bpm, (uint8_t)note_type);
	const Temporal::TempoPoint&           added = map->set_tempo (t, bbt);
	std::string                           structured = tempo_point_json (added);
	Temporal::TempoMap::update (map);
	session.commit_reversible_command ();

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Tempo added\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_tempo_add_meter_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	Temporal::TempoMap::fetch ();

	const std::optional<int> num_opt = get_optional<int> (root, "params.arguments.divisionsPerBar");
	const std::optional<int> den_opt = get_optional<int> (root, "params.arguments.noteValue");
	if (!num_opt || !den_opt) {
		return jsonrpc_error (id, -32602, "Missing divisionsPerBar and noteValue");
	}
	if (*num_opt < 1 || *num_opt > 64 || *den_opt < 1 || *den_opt > 64) {
		return jsonrpc_error (id, -32602, "Invalid meter (each value must be 1..64)");
	}

	Temporal::BBT_Argument bbt;
	std::string            err;
	if (!parse_bbt_required (root, "params.arguments.bbt", bbt, err)) {
		return jsonrpc_error (id, -32602, err);
	}

	session.begin_reversible_command ("add meter");
	Temporal::TempoMap::WritableSharedPtr map = Temporal::TempoMap::write_copy ();
	const Temporal::Meter                 m ((int8_t)*num_opt, (int8_t)*den_opt);
	const Temporal::MeterPoint&           added = map->set_meter (m, bbt);
	std::string                           structured = meter_point_json (added);
	Temporal::TempoMap::update (map);
	session.commit_reversible_command ();

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Meter added\"}],\"structuredContent\":") + structured + "}");
}

template <typename PointType>
const PointType*
find_point_at_bbt (const Temporal::TempoMap& map, const Temporal::BBT_Time& bbt, const PointType* /*tag*/);

template <>
const Temporal::TempoPoint*
find_point_at_bbt<Temporal::TempoPoint> (const Temporal::TempoMap& map, const Temporal::BBT_Time& bbt, const Temporal::TempoPoint*)
{
	for (const Temporal::TempoPoint& p : map.tempos ()) {
		const Temporal::BBT_Time& pb = p.bbt ();
		if (pb.bars == bbt.bars && pb.beats == bbt.beats && pb.ticks == bbt.ticks) {
			return &p;
		}
	}
	return nullptr;
}

template <>
const Temporal::MeterPoint*
find_point_at_bbt<Temporal::MeterPoint> (const Temporal::TempoMap& map, const Temporal::BBT_Time& bbt, const Temporal::MeterPoint*)
{
	for (const Temporal::MeterPoint& p : map.meters ()) {
		const Temporal::BBT_Time& pb = p.bbt ();
		if (pb.bars == bbt.bars && pb.beats == bbt.beats && pb.ticks == bbt.ticks) {
			return &p;
		}
	}
	return nullptr;
}

std::string
handle_tempo_remove_tempo_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	Temporal::TempoMap::fetch ();

	Temporal::BBT_Argument bbt;
	std::string            err;
	if (!parse_bbt_required (root, "params.arguments.bbt", bbt, err)) {
		return jsonrpc_error (id, -32602, err);
	}

	Temporal::TempoMap::WritableSharedPtr map = Temporal::TempoMap::write_copy ();
	const Temporal::TempoPoint*           found =
	    find_point_at_bbt<Temporal::TempoPoint> (*map, (Temporal::BBT_Time)bbt, nullptr);
	if (!found) {
		Temporal::TempoMap::abort_update ();
		return jsonrpc_error (id, -32602, "No tempo point at the given bbt");
	}

	session.begin_reversible_command ("remove tempo");
	const std::string structured = tempo_point_json (*found);
	map->remove_tempo (*found);
	Temporal::TempoMap::update (map);
	session.commit_reversible_command ();

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Tempo removed\"}],\"structuredContent\":") + structured + "}");
}

std::string
handle_tempo_remove_meter_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	Temporal::TempoMap::fetch ();

	Temporal::BBT_Argument bbt;
	std::string            err;
	if (!parse_bbt_required (root, "params.arguments.bbt", bbt, err)) {
		return jsonrpc_error (id, -32602, err);
	}

	Temporal::TempoMap::WritableSharedPtr map = Temporal::TempoMap::write_copy ();
	const Temporal::MeterPoint*           found =
	    find_point_at_bbt<Temporal::MeterPoint> (*map, (Temporal::BBT_Time)bbt, nullptr);
	if (!found) {
		Temporal::TempoMap::abort_update ();
		return jsonrpc_error (id, -32602, "No meter point at the given bbt");
	}

	session.begin_reversible_command ("remove meter");
	const std::string structured = meter_point_json (*found);
	map->remove_meter (*found);
	Temporal::TempoMap::update (map);
	session.commit_reversible_command ();

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Meter removed\"}],\"structuredContent\":") + structured + "}");
}

} /* anonymous namespace */

bool
dispatch_tempo_tool_call (ARDOUR::Session& session, const std::string& tool_name, const pt::ptree& root, const std::string& id, std::string& response)
{
	if (tool_name == "tempo/list") {
		response = handle_tempo_list_tool (session, id);
		return true;
	}
	if (tool_name == "tempo/add_tempo") {
		response = handle_tempo_add_tempo_tool (session, root, id);
		return true;
	}
	if (tool_name == "tempo/add_meter") {
		response = handle_tempo_add_meter_tool (session, root, id);
		return true;
	}
	if (tool_name == "tempo/remove_tempo") {
		response = handle_tempo_remove_tempo_tool (session, root, id);
		return true;
	}
	if (tool_name == "tempo/remove_meter") {
		response = handle_tempo_remove_meter_tool (session, root, id);
		return true;
	}

	return false;
}

} /* namespace mcp */
} /* namespace ArdourSurface */
