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

#ifndef _ardour_surface_mcp_http_handlers_common_h_
#define _ardour_surface_mcp_http_handlers_common_h_

#include <memory>
#include <optional>
#include <string>

#include <boost/property_tree/ptree.hpp>

#include "ardour/location.h"
#include "ardour/types.h"

namespace ARDOUR
{
class Location;
class Locations;
class Region;
class Route;
class Session;
}

namespace ArdourSurface
{
namespace mcp
{

namespace pt = boost::property_tree;

/* --- JSON-RPC framing & encoding ------------------------------------------ */

std::string json_escape (const std::string&);
std::string canonical_tool_name (std::string tool_name);

std::string jsonrpc_id (const pt::ptree& root);
bool        has_jsonrpc_id (const pt::ptree& root);
std::string jsonrpc_result (const std::string& id, const std::string& result_json);
std::string jsonrpc_error (const std::string& id, int code, const std::string& message);

/* --- Optional ptree getters ----------------------------------------------- */

template <typename T>
std::optional<T>
get_optional (const pt::ptree& tree, const std::string& path)
{
	const boost::optional<T> value = tree.get_optional<T> (path);
	if (value) {
		return *value;
	}
	return std::nullopt;
}

/* --- ID resolution (PBD::ID hardening) ------------------------------------ */

bool is_decimal_pbd_id_string (const std::string&);

ARDOUR::Location*               location_by_mcp_id (ARDOUR::Locations&, const std::string& id);
std::shared_ptr<ARDOUR::Region> region_by_mcp_id (const std::string& id);
std::shared_ptr<ARDOUR::Route>  route_by_mcp_id (ARDOUR::Session&, const std::string& id);

/* --- Transport / session common JSON -------------------------------------- */

std::string transport_state_string (ARDOUR::Session&);
std::string transport_state_json (ARDOUR::Session&);
const char* record_state_string (ARDOUR::RecordState);
double      transport_tempo_bpm (ARDOUR::Session&);

/* --- Route / marker / BBT helpers ----------------------------------------- */

std::string route_type_string (const std::shared_ptr<ARDOUR::Route>&);
std::string plugin_list_json (const std::shared_ptr<ARDOUR::Route>&);
std::string bbt_json_at_sample (samplepos_t);
std::string marker_type_json (ARDOUR::Location::Flags);
std::string special_range_json (const ARDOUR::Location&, const std::string& mode);

/* --- BBT / timeline parsing helpers --------------------------------------- */

bool parse_bbt_target_sample (
    int bar, double beat,
    samplepos_t& target_sample,
    std::string& error);

bool parse_optional_bbt_target_sample (
    const pt::ptree&   root,
    const std::string& args_path,
    samplepos_t&       target_sample,
    bool&              have_target,
    std::string&       error);

bool parse_optional_timeline_boundary_sample (
    const pt::ptree&   root,
    const std::string& args_path,
    const std::string& sample_key,
    const std::string& bar_key,
    const std::string& beat_key,
    samplepos_t&       target_sample,
    bool&              have_target,
    std::string&       error);

bool parse_range_endpoints (
    const pt::ptree&   root,
    const std::string& args_path,
    samplepos_t&       start_sample,
    samplepos_t&       end_sample,
    std::string&       error);

/* --- Fader / dB helpers (shared by tracks + sends) ------------------------ */

bool   valid_fader_position (double);
bool   valid_fader_db (double);
bool   db_is_silence_floor (double);
double db_to_gain_with_floor (double);
double normalized_db_value (double);

} /* namespace mcp */
} /* namespace ArdourSurface */

#endif /* _ardour_surface_mcp_http_handlers_common_h_ */
