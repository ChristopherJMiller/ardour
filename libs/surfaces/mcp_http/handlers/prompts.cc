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

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "pbd/file_utils.h"
#include "pbd/search_path.h"

#include "ardour/filesystem_paths.h"

#include "handlers/common.h"
#include "handlers/prompts.h"

namespace ArdourSurface
{
namespace mcp
{
namespace
{

struct PromptArg {
	std::string name;
	std::string description;
	bool        required = false;
};

struct Prompt {
	std::string            name;
	std::string            title;
	std::string            description;
	std::vector<PromptArg> arguments;
	std::string            body;
};

std::string
trim (const std::string& s)
{
	const auto a = s.find_first_not_of (" \t\r\n");
	if (a == std::string::npos) {
		return "";
	}
	const auto b = s.find_last_not_of (" \t\r\n");
	return s.substr (a, b - a + 1);
}

std::string
strip_quotes (std::string s)
{
	s = trim (s);
	if (s.size () >= 2 && ((s.front () == '"' && s.back () == '"') || (s.front () == '\'' && s.back () == '\''))) {
		return s.substr (1, s.size () - 2);
	}
	return s;
}

bool
parse_prompt_file (const std::string& path, Prompt& out)
{
	std::ifstream f (path);
	if (!f) {
		return false;
	}

	std::string line;
	if (!std::getline (f, line) || trim (line) != "---") {
		return false;
	}

	std::vector<std::string> fm_lines;
	while (std::getline (f, line)) {
		if (trim (line) == "---") {
			std::ostringstream rest;
			rest << f.rdbuf ();
			out.body = rest.str ();
			break;
		}
		fm_lines.push_back (line);
	}

	bool       in_arguments = false;
	PromptArg* current_arg  = nullptr;

	for (size_t i = 0; i < fm_lines.size (); ++i) {
		const std::string& l = fm_lines[i];

		if (l.size () > 2 && l[0] == ' ' && l[1] == ' ') {
			/* indented — argument key */
			if (!in_arguments || !current_arg) {
				continue;
			}
			const std::string body  = trim (l);
			const auto        colon = body.find (':');
			if (colon == std::string::npos) {
				continue;
			}
			std::string k = trim (body.substr (0, colon));
			std::string v = strip_quotes (trim (body.substr (colon + 1)));
			if (k == "- name" || k == "name") {
				if (k == "- name") {
					out.arguments.push_back (PromptArg ());
					current_arg = &out.arguments.back ();
				}
				if (current_arg) {
					current_arg->name = v;
				}
			} else if (k == "description") {
				if (current_arg) {
					current_arg->description = v;
				}
			} else if (k == "required") {
				if (current_arg) {
					current_arg->required = (v == "true");
				}
			}
			continue;
		}

		if (l.size () > 0 && l[0] == '-') {
			/* list item — new argument */
			if (!in_arguments) {
				continue;
			}
			out.arguments.push_back (PromptArg ());
			current_arg = &out.arguments.back ();

			const std::string body  = trim (l.substr (1));
			const auto        colon = body.find (':');
			if (colon != std::string::npos) {
				std::string k = trim (body.substr (0, colon));
				std::string v = strip_quotes (trim (body.substr (colon + 1)));
				if (k == "name") {
					current_arg->name = v;
				} else if (k == "description") {
					current_arg->description = v;
				} else if (k == "required") {
					current_arg->required = (v == "true");
				}
			}
			continue;
		}

		const auto colon = l.find (':');
		if (colon == std::string::npos) {
			continue;
		}
		const std::string key = trim (l.substr (0, colon));
		const std::string raw = trim (l.substr (colon + 1));

		if (key == "arguments") {
			in_arguments = true;
			current_arg  = nullptr;
			if (raw == "[]") {
				/* explicit empty list — leave arguments empty */
			}
			continue;
		}

		in_arguments = false;
		current_arg  = nullptr;

		const std::string v = strip_quotes (raw);
		if (key == "name") {
			out.name = v;
		} else if (key == "title") {
			out.title = v;
		} else if (key == "description") {
			out.description = v;
		}
	}

	if (out.name.empty ()) {
		return false;
	}

	return true;
}

std::vector<Prompt>
load_all_prompts ()
{
	std::vector<Prompt> prompts;

	std::vector<std::string> files;
	PBD::Searchpath          sp (ARDOUR::ardour_data_search_path ());
	for (const std::string& dir : sp) {
		const std::string promptdir = Glib::build_filename (dir, "mcp_http", "prompts");
		if (!Glib::file_test (promptdir, Glib::FILE_TEST_IS_DIR)) {
			continue;
		}
		PBD::find_files_matching_pattern (files, promptdir, "*.md");
	}

	std::sort (files.begin (), files.end ());

	for (const std::string& path : files) {
		Prompt p;
		if (parse_prompt_file (path, p)) {
			prompts.push_back (p);
		}
	}

	return prompts;
}

std::string
substitute_arguments (std::string body, const pt::ptree& args)
{
	static const std::regex placeholder (R"(\$\{([A-Za-z_][A-Za-z0-9_]*)(?:\|([^}]*))?\})");
	std::string             out;
	out.reserve (body.size ());

	std::sregex_iterator it (body.begin (), body.end (), placeholder);
	std::sregex_iterator end;
	size_t               last = 0;

	for (; it != end; ++it) {
		const auto& m = *it;
		out.append (body, last, m.position () - last);

		const std::string  name        = m[1].str ();
		const std::string  default_val = m.size () > 2 ? m[2].str () : "";
		const auto         opt         = args.get_child_optional (name);
		std::string        val;
		if (opt) {
			val = opt->data ();
		}
		if (val.empty ()) {
			val = default_val;
		}
		out.append (val);
		last = m.position () + m.length ();
	}
	out.append (body, last, std::string::npos);
	return out;
}

std::string
prompt_summary_json (const Prompt& p)
{
	std::ostringstream ss;
	ss << "{\"name\":\"" << json_escape (p.name) << "\""
	   << ",\"title\":\"" << json_escape (p.title) << "\""
	   << ",\"description\":\"" << json_escape (p.description) << "\""
	   << ",\"arguments\":[";
	for (size_t i = 0; i < p.arguments.size (); ++i) {
		if (i > 0) {
			ss << ",";
		}
		ss << "{\"name\":\"" << json_escape (p.arguments[i].name) << "\""
		   << ",\"description\":\"" << json_escape (p.arguments[i].description) << "\""
		   << ",\"required\":" << (p.arguments[i].required ? "true" : "false")
		   << "}";
	}
	ss << "]}";
	return ss.str ();
}

} /* anonymous namespace */

std::string
handle_prompts_list (const std::string& id)
{
	const std::vector<Prompt> prompts = load_all_prompts ();

	std::ostringstream ss;
	ss << "{\"prompts\":[";
	for (size_t i = 0; i < prompts.size (); ++i) {
		if (i > 0) {
			ss << ",";
		}
		ss << prompt_summary_json (prompts[i]);
	}
	ss << "]}";

	return jsonrpc_result (id, ss.str ());
}

std::string
handle_prompts_get (const pt::ptree& root, const std::string& id)
{
	const std::string name = root.get<std::string> ("params.name", "");
	if (name.empty ()) {
		return jsonrpc_error (id, -32602, "Missing prompt name");
	}

	const std::vector<Prompt> prompts = load_all_prompts ();
	const Prompt*             found   = nullptr;
	for (const Prompt& p : prompts) {
		if (p.name == name) {
			found = &p;
			break;
		}
	}
	if (!found) {
		return jsonrpc_error (id, -32602, std::string ("Unknown prompt: ") + name);
	}

	pt::ptree empty_args;
	const auto args_opt = root.get_child_optional ("params.arguments");
	const pt::ptree& args = args_opt ? *args_opt : empty_args;

	const std::string substituted = substitute_arguments (found->body, args);

	std::ostringstream ss;
	ss << "{\"description\":\"" << json_escape (found->description) << "\""
	   << ",\"messages\":[{\"role\":\"user\",\"content\":{\"type\":\"text\",\"text\":\""
	   << json_escape (substituted) << "\"}}]}";

	return jsonrpc_result (id, ss.str ());
}

} /* namespace mcp */
} /* namespace ArdourSurface */
