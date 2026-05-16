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

#ifndef _ardour_surface_mcp_http_handlers_prompts_h_
#define _ardour_surface_mcp_http_handlers_prompts_h_

#include <string>

#include <boost/property_tree/ptree.hpp>

namespace ArdourSurface
{
namespace mcp
{

std::string handle_prompts_list (const std::string& id);
std::string handle_prompts_get (const boost::property_tree::ptree& root, const std::string& id);

} /* namespace mcp */
} /* namespace ArdourSurface */

#endif /* _ardour_surface_mcp_http_handlers_prompts_h_ */
