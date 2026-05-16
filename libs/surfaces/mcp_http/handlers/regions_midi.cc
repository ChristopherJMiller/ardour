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
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "pbd/basename.h"
#include "pbd/memento_command.h"
#include "pbd/properties.h"
#include "pbd/stateful_diff_command.h"
#include "pbd/xml++.h"

#include "evoral/Event.h"
#include "evoral/Note.h"
#include "evoral/Sequence.h"

#include "ardour/audioregion.h"
#include "ardour/dB.h"
#include "ardour/midi_model.h"
#include "ardour/midi_region.h"
#include "ardour/midi_source.h"
#include "ardour/midi_track.h"
#include "ardour/playlist.h"
#include "ardour/region.h"
#include "ardour/region_factory.h"
#include "ardour/route.h"
#include "ardour/selection.h"
#include "ardour/session.h"
#include "ardour/source.h"
#include "ardour/tempo.h"
#include "ardour/track.h"
#include "ardour/types.h"

#include "handlers/common.h"
#include "handlers/regions_midi.h"

namespace ArdourSurface
{
namespace mcp
{
namespace
{

static std::string
midi_region_json (const std::shared_ptr<ARDOUR::Region>& region, const std::shared_ptr<ARDOUR::Track>& track)
{
	const samplepos_t start_sample = region->position_sample ();
	const samplepos_t end_sample   = start_sample + region->length_samples ();

	std::ostringstream ss;
	ss << "{\"regionId\":\"" << json_escape (region->id ().to_s ()) << "\""
	   << ",\"name\":\"" << json_escape (region->name ()) << "\""
	   << ",\"type\":\"midi\""
	   << ",\"trackId\":\"" << json_escape (track->id ().to_s ()) << "\""
	   << ",\"trackName\":\"" << json_escape (track->name ()) << "\"";

	const std::shared_ptr<ARDOUR::Playlist> playlist = region->playlist ();
	if (playlist) {
		ss << ",\"playlistId\":\"" << json_escape (playlist->id ().to_s ()) << "\"";
	} else {
		ss << ",\"playlistId\":null";
	}

	ss << ",\"startSample\":" << start_sample
	   << ",\"endSample\":" << end_sample
	   << ",\"lengthSamples\":" << region->length_samples ()
	   << ",\"startBbt\":" << bbt_json_at_sample (start_sample)
	   << ",\"endBbt\":" << bbt_json_at_sample (end_sample)
	   << ",\"locked\":" << (region->locked () ? "true" : "false")
	   << ",\"positionLocked\":" << (region->position_locked () ? "true" : "false")
	   << "}";

	return ss.str ();
}

struct MidiJsonEventDef {
	int         bar;
	double      beat;
	int         tick;
	int         note;
	int         velocity;
	int         channel;
	std::string type;
};

struct MidiJsonNoteDef {
	double start_quarters;
	double length_quarters;
	int    note;
	int    velocity;
	int    channel;
};

static bool
parse_time_signature (const std::string& sig, int& numerator, int& denominator)
{
	const std::string::size_type slash = sig.find ('/');
	if (slash == std::string::npos || slash == 0 || (slash + 1) >= sig.size ()) {
		return false;
	}

	const std::string num    = sig.substr (0, slash);
	const std::string den    = sig.substr (slash + 1);
	char*             endptr = 0;
	long              n      = std::strtol (num.c_str (), &endptr, 10);
	if (!endptr || *endptr != '\0') {
		return false;
	}
	endptr = 0;
	long d = std::strtol (den.c_str (), &endptr, 10);
	if (!endptr || *endptr != '\0') {
		return false;
	}

	if (n <= 0 || d <= 0 || n > 64 || d > 64) {
		return false;
	}

	numerator   = (int)n;
	denominator = (int)d;
	return true;
}

static std::string
json_string_array (const std::vector<std::string>& values)
{
	std::ostringstream ss;
	ss << "[";
	for (size_t i = 0; i < values.size (); ++i) {
		if (i > 0) {
			ss << ",";
		}
		ss << "\"" << json_escape (values[i]) << "\"";
	}
	ss << "]";
	return ss.str ();
}

static double
midi_json_event_quarters (const MidiJsonEventDef& e, int numerator, int denominator, int ticks_per_quarter)
{
	const double quarters_per_bar       = (double)numerator * (4.0 / (double)denominator);
	const double quarters_per_beat_unit = 4.0 / (double)denominator;
	return ((double)(e.bar - 1) * quarters_per_bar) + ((e.beat - 1.0) * quarters_per_beat_unit) + ((double)e.tick / (double)ticks_per_quarter);
}

static double
beats_to_double (const Temporal::Beats& beats)
{
	return beats.get_beats () + (beats.get_ticks () / (double)Temporal::ticks_per_beat);
}

static bool
quarters_to_midi_json_time (
    double quarters,
    int    time_sig_num,
    int    time_sig_den,
    int    ticks_per_quarter,
    int&   bar_out,
    int&   beat_out,
    int&   tick_out)
{
	if (!std::isfinite (quarters) || quarters < 0.0 || ticks_per_quarter <= 0 || time_sig_num <= 0 || time_sig_den <= 0) {
		return false;
	}

	const double quarters_per_bar       = (double)time_sig_num * (4.0 / (double)time_sig_den);
	const double quarters_per_beat_unit = 4.0 / (double)time_sig_den;
	if (!std::isfinite (quarters_per_bar) || quarters_per_bar <= 0.0 || !std::isfinite (quarters_per_beat_unit) || quarters_per_beat_unit <= 0.0) {
		return false;
	}

	const int ticks_per_beat_unit = std::max (1, (int)std::llround (quarters_per_beat_unit * (double)ticks_per_quarter));

	int    bar_index = (int)std::floor (quarters / quarters_per_bar);
	double in_bar    = quarters - ((double)bar_index * quarters_per_bar);
	if (in_bar < 0.0) {
		in_bar = 0.0;
	}

	int beat_index = (int)std::floor (in_bar / quarters_per_beat_unit);
	if (beat_index < 0) {
		beat_index = 0;
	}
	if (beat_index >= time_sig_num) {
		beat_index = time_sig_num - 1;
	}

	double in_beat = in_bar - ((double)beat_index * quarters_per_beat_unit);
	if (in_beat < 0.0) {
		in_beat = 0.0;
	}
	if (in_beat > quarters_per_beat_unit) {
		in_beat = quarters_per_beat_unit;
	}

	int tick = (int)std::llround (in_beat * (double)ticks_per_quarter);
	if (tick >= ticks_per_beat_unit) {
		tick -= ticks_per_beat_unit;
		++beat_index;
	}
	if (beat_index >= time_sig_num) {
		beat_index = 0;
		++bar_index;
	}

	bar_out  = bar_index + 1;
	beat_out = beat_index + 1;
	tick_out = std::max (0, tick);
	return true;
}

static bool
parse_midi_json_channel_base (
    const pt::ptree& midi_root,
    bool&            one_based,
    bool&            explicit_base,
    std::string&     error)
{
	error.clear ();
	one_based     = true;
	explicit_base = false;

	const std::optional<std::string> base_opt = get_optional<std::string> (midi_root, "channel_base");
	if (!base_opt) {
		return true;
	}

	explicit_base    = true;
	std::string base = *base_opt;
	std::transform (base.begin (), base.end (), base.begin (), ::tolower);
	if (base == "one" || base == "1") {
		one_based = true;
		return true;
	}
	if (base == "zero" || base == "0") {
		one_based = false;
		return true;
	}

	error = "Invalid midi.channel_base (expected \"one\" or \"zero\")";
	return false;
}

static bool
parse_midi_json_channel_value (
    int64_t      in,
    bool         one_based,
    const char*  field_name,
    int&         raw_channel,
    std::string& error)
{
	if (one_based) {
		if (in < 1 || in > 16) {
			std::ostringstream ss;
			ss << "Invalid " << field_name << " (expected 1..16 for channel_base=\"one\")";
			error = ss.str ();
			return false;
		}
		raw_channel = (int)in - 1;
		return true;
	}

	if (in < 0 || in > 15) {
		std::ostringstream ss;
		ss << "Invalid " << field_name << " (expected 0..15 for channel_base=\"zero\")";
		error = ss.str ();
		return false;
	}
	raw_channel = (int)in;
	return true;
}

static bool
parse_midi_json_events (
    const pt::ptree&               midi_root,
    std::vector<MidiJsonEventDef>& expanded_events,
    int&                           channel,
    bool&                          one_based_channel_input,
    bool&                          is_drum_mode,
    int&                           ticks_per_quarter,
    int&                           time_sig_num,
    int&                           time_sig_den,
    std::vector<std::string>&      warnings,
    std::string&                   error)
{
	expanded_events.clear ();
	warnings.clear ();
	error.clear ();

	bool explicit_channel_base = false;
	if (!parse_midi_json_channel_base (midi_root, one_based_channel_input, explicit_channel_base, error)) {
		return false;
	}

	const int64_t default_channel = one_based_channel_input ? 10 : 9;
	const int64_t channel_in      = midi_root.get<int64_t> ("channel", default_channel);
	if (!parse_midi_json_channel_value (channel_in, one_based_channel_input, "midi.channel", channel, error)) {
		if (!explicit_channel_base) {
			error += "; set midi.channel_base to \"zero\" for legacy 0..15 payloads";
		}
		return false;
	}

	is_drum_mode = midi_root.get<bool> ("is_drum_mode", true);

	const int64_t tpq_in = midi_root.get<int64_t> ("ticks_per_quarter", 480);
	if (tpq_in <= 0 || tpq_in > 96000) {
		error = "Invalid midi.ticks_per_quarter (expected 1..96000)";
		return false;
	}
	ticks_per_quarter = (int)tpq_in;

	const std::string time_signature = midi_root.get<std::string> ("time_signature", "4/4");
	if (!parse_time_signature (time_signature, time_sig_num, time_sig_den)) {
		error = "Invalid midi.time_signature (expected format N/D)";
		return false;
	}

	const boost::optional<const pt::ptree&> midi_events = midi_root.get_child_optional ("midi_events");
	if (!midi_events) {
		error = "Missing midi.midi_events (expected an array of event objects)";
		return false;
	}

	std::map<int, std::vector<MidiJsonEventDef>> events_by_bar;
	for (pt::ptree::const_iterator it = midi_events->begin (); it != midi_events->end (); ++it) {
		const pt::ptree& ev = it->second;

		if (get_optional<std::string> (ev, "comment")) {
			continue;
		}

		const std::optional<int64_t> bar_opt = get_optional<int64_t> (ev, "bar");
		if (!bar_opt || *bar_opt < 1 || *bar_opt > 1000000) {
			error = "Each midi event must provide bar >= 1";
			return false;
		}
		const int bar = (int)*bar_opt;

		const std::optional<int64_t> repeat_opt = get_optional<int64_t> (ev, "repeat");
		if (repeat_opt) {
			if (*repeat_opt < 0 || *repeat_opt > 1000000) {
				error = "Invalid repeat value (expected >= 0)";
				return false;
			}

			const int                                                    source_bar = (*repeat_opt == 0) ? (bar - 1) : (int)*repeat_opt;
			std::map<int, std::vector<MidiJsonEventDef>>::const_iterator src        = events_by_bar.find (source_bar);
			if (src == events_by_bar.end ()) {
				std::ostringstream w;
				w << "Repeat skipped at bar " << bar << ": source bar " << source_bar << " not found";
				warnings.push_back (w.str ());
				continue;
			}

			for (size_t i = 0; i < src->second.size (); ++i) {
				MidiJsonEventDef copy = src->second[i];
				copy.bar              = bar;
				expanded_events.push_back (copy);
				events_by_bar[bar].push_back (copy);
			}
			continue;
		}

		const std::optional<double> beat_opt = get_optional<double> (ev, "b");
		if (!beat_opt || !std::isfinite (*beat_opt) || *beat_opt < 1.0) {
			error = "Each normal midi event must provide b >= 1";
			return false;
		}

		const std::optional<int64_t> note_opt = get_optional<int64_t> (ev, "n");
		if (!note_opt || *note_opt < 0 || *note_opt > 127) {
			error = "Each normal midi event must provide n (0..127)";
			return false;
		}

		const int64_t tick_in = ev.get<int64_t> ("t", 0);
		if (tick_in < 0) {
			error = "Invalid midi event t (expected >= 0)";
			return false;
		}

		const int64_t velocity_in = ev.get<int64_t> ("v", 64);
		if (velocity_in < 0 || velocity_in > 127) {
			error = "Invalid midi event v (expected 0..127)";
			return false;
		}

		const std::optional<int64_t> event_channel_opt = get_optional<int64_t> (ev, "channel");
		int                          event_channel     = channel;
		if (event_channel_opt) {
			if (!parse_midi_json_channel_value (*event_channel_opt, one_based_channel_input, "midi event channel", event_channel, error)) {
				if (!explicit_channel_base) {
					error += "; set midi.channel_base to \"zero\" for legacy 0..15 payloads";
				}
				return false;
			}
		}

		std::string type = ev.get<std::string> ("type", "note_on");
		std::transform (type.begin (), type.end (), type.begin (), ::tolower);

		MidiJsonEventDef out;
		out.bar      = bar;
		out.beat     = *beat_opt;
		out.tick     = (int)tick_in;
		out.note     = (int)*note_opt;
		out.velocity = (int)velocity_in;
		out.channel  = event_channel;
		out.type     = type;

		expanded_events.push_back (out);
		events_by_bar[bar].push_back (out);
	}

	return true;
}

static bool
build_midi_json_note_defs (
    const std::vector<MidiJsonEventDef>& expanded_events,
    bool                                 is_drum_mode,
    int                                  ticks_per_quarter,
    int                                  time_sig_num,
    int                                  time_sig_den,
    std::vector<MidiJsonNoteDef>&        notes,
    std::vector<std::string>&            warnings,
    std::string&                         error)
{
	notes.clear ();
	error.clear ();

	struct TimedEvent {
		double           quarters;
		size_t           ordinal;
		MidiJsonEventDef ev;
	};

	std::vector<TimedEvent> events;
	events.reserve (expanded_events.size ());
	for (size_t i = 0; i < expanded_events.size (); ++i) {
		const double q = midi_json_event_quarters (expanded_events[i], time_sig_num, time_sig_den, ticks_per_quarter);
		if (!std::isfinite (q) || q < 0.0) {
			error = "Invalid event timing produced by midi_events";
			return false;
		}

		TimedEvent te;
		te.quarters = q;
		te.ordinal  = i;
		te.ev       = expanded_events[i];
		events.push_back (te);
	}

	std::sort (
	    events.begin (),
	    events.end (),
	    [] (const TimedEvent& a, const TimedEvent& b) {
		    if (a.quarters != b.quarters) {
			    return a.quarters < b.quarters;
		    }
		    return a.ordinal < b.ordinal;
	    });

	if (is_drum_mode) {
		const double default_length = 0.0;

		for (size_t i = 0; i < events.size (); ++i) {
			MidiJsonNoteDef n;
			n.start_quarters  = events[i].quarters;
			n.length_quarters = default_length;
			n.note            = events[i].ev.note;
			n.velocity        = events[i].ev.velocity;
			n.channel         = events[i].ev.channel;
			notes.push_back (n);
		}
		return true;
	}

	struct PendingOn {
		double quarters;
		int    velocity;
	};
	std::map<int, std::vector<PendingOn>> active_by_note;

	for (size_t i = 0; i < events.size (); ++i) {
		const MidiJsonEventDef& ev       = events[i].ev;
		const bool              is_off   = (ev.type == "note_off") || (ev.velocity == 0);
		const bool              is_on    = (ev.type.empty () || ev.type == "note_on");
		const int               note_key = (ev.channel * 128) + ev.note;

		if (is_off) {
			std::vector<PendingOn>& stack = active_by_note[note_key];
			if (stack.empty ()) {
				std::ostringstream w;
				w << "Unmatched note_off skipped for note " << ev.note << " on channel " << (ev.channel + 1) << " at bar " << ev.bar;
				warnings.push_back (w.str ());
				continue;
			}

			const PendingOn on = stack.back ();
			stack.pop_back ();

			const double len = events[i].quarters - on.quarters;
			if (!std::isfinite (len) || len <= 0.0) {
				std::ostringstream w;
				w << "Non-positive note length skipped for note " << ev.note << " at bar " << ev.bar;
				warnings.push_back (w.str ());
				continue;
			}

			MidiJsonNoteDef n;
			n.start_quarters  = on.quarters;
			n.length_quarters = len;
			n.note            = ev.note;
			n.velocity        = on.velocity;
			n.channel         = ev.channel;
			notes.push_back (n);
			continue;
		}

		if (!is_on) {
			std::ostringstream w;
			w << "Unknown event type '" << ev.type << "' skipped at bar " << ev.bar;
			warnings.push_back (w.str ());
			continue;
		}

		PendingOn on;
		on.quarters = events[i].quarters;
		on.velocity = ev.velocity;
		active_by_note[note_key].push_back (on);
	}

	for (std::map<int, std::vector<PendingOn>>::const_iterator it = active_by_note.begin (); it != active_by_note.end (); ++it) {
		if (!it->second.empty ()) {
			const int          channel = it->first / 128;
			const int          note    = it->first % 128;
			std::ostringstream w;
			w << "Unclosed note_on skipped for note " << note << " on channel " << (channel + 1)
			  << " (" << it->second.size () << " pending)";
			warnings.push_back (w.str ());
		}
	}

	return true;
}

static bool
create_midi_region_on_track (
    ARDOUR::Session&                      session,
    const std::shared_ptr<ARDOUR::Track>& track,
    samplepos_t                           start_sample,
    samplepos_t                           end_sample,
    const std::string&                    requested_name,
    std::shared_ptr<ARDOUR::Region>&      out_region,
    std::string&                          error)
{
	error.clear ();
	out_region.reset ();

	const std::shared_ptr<ARDOUR::MidiTrack> midi_track = std::dynamic_pointer_cast<ARDOUR::MidiTrack> (track);
	if (!midi_track) {
		error = "trackId is not a MIDI track";
		return false;
	}

	const std::shared_ptr<ARDOUR::Playlist> playlist = midi_track->playlist ();
	if (!playlist) {
		error = "Track has no playlist";
		return false;
	}

	const Temporal::timepos_t start_pos (start_sample);
	const Temporal::timepos_t end_pos (end_sample);
	const Temporal::timecnt_t region_length = start_pos.distance (end_pos);
	if (region_length.samples () <= 0) {
		error = "Invalid region length";
		return false;
	}

	std::shared_ptr<ARDOUR::MidiSource> midi_src;
	try {
		midi_src = session.create_midi_source_by_stealing_name (track);
	} catch (...) {
		midi_src.reset ();
	}
	if (!midi_src) {
		error = "Failed to create MIDI source";
		return false;
	}

	ARDOUR::SourceList srcs;
	srcs.push_back (std::dynamic_pointer_cast<ARDOUR::Source> (midi_src));
	if (srcs.empty () || !srcs.front ()) {
		error = "Failed to resolve MIDI source";
		return false;
	}

	const Temporal::timecnt_t source_start (Temporal::BeatTime); /* zero beats */

	PBD::PropertyList whole_file_props;
	whole_file_props.add (ARDOUR::Properties::start, source_start);
	whole_file_props.add (ARDOUR::Properties::length, region_length);
	whole_file_props.add (ARDOUR::Properties::automatic, true);
	whole_file_props.add (ARDOUR::Properties::whole_file, true);
	whole_file_props.add (ARDOUR::Properties::name, PBD::basename_nosuffix (midi_src->name ()));
	whole_file_props.add (ARDOUR::Properties::opaque, session.config.get_draw_opaque_midi_regions ());

	std::shared_ptr<ARDOUR::Region> whole_file_region = ARDOUR::RegionFactory::create (srcs, whole_file_props);
	if (!whole_file_region) {
		error = "Failed to create whole-file MIDI region";
		return false;
	}

	PBD::PropertyList playlist_region_props;
	if (requested_name.empty ()) {
		playlist_region_props.add (ARDOUR::Properties::name, whole_file_region->name ());
	} else {
		playlist_region_props.add (ARDOUR::Properties::name, requested_name);
	}

	std::shared_ptr<ARDOUR::Region> region = ARDOUR::RegionFactory::create (whole_file_region, playlist_region_props);
	if (!region) {
		error = "Failed to create MIDI playlist region";
		return false;
	}

	session.begin_reversible_command ("add midi region");
	playlist->clear_changes ();
	playlist->clear_owned_changes ();
	region->set_position (start_pos);
	playlist->add_region (region, start_pos, 1.0, false);
	playlist->rdiff_and_add_command (&session);
	session.commit_reversible_command ();

	out_region = region;
	return true;
}

static std::string
midi_region_brief_json (const std::shared_ptr<ARDOUR::Region>& region)
{
	const samplepos_t start_sample = region->position_sample ();
	const samplepos_t end_sample   = start_sample + region->length_samples ();

	std::ostringstream ss;
	ss << "{\"regionId\":\"" << json_escape (region->id ().to_s ()) << "\""
	   << ",\"name\":\"" << json_escape (region->name ()) << "\""
	   << ",\"type\":\"midi\"";

	const std::shared_ptr<ARDOUR::Playlist> playlist = region->playlist ();
	if (playlist) {
		ss << ",\"playlistId\":\"" << json_escape (playlist->id ().to_s ()) << "\"";
	} else {
		ss << ",\"playlistId\":null";
	}

	ss << ",\"startSample\":" << start_sample
	   << ",\"endSample\":" << end_sample
	   << ",\"lengthSamples\":" << region->length_samples ()
	   << ",\"startBbt\":" << bbt_json_at_sample (start_sample)
	   << ",\"endBbt\":" << bbt_json_at_sample (end_sample)
	   << "}";
	return ss.str ();
}

static std::string
midi_note_json (
    const std::shared_ptr<ARDOUR::Region>&                region,
    const std::shared_ptr<Evoral::Note<Temporal::Beats>>& note,
    const std::string&                                    position_origin)
{
	const Temporal::Beats start_source_beats        = note->time ();
	const Temporal::Beats length_beats              = note->length ();
	const Temporal::Beats end_source_beats          = start_source_beats + length_beats;
	const Temporal::Beats start_region_beats        = region->source_beats_to_region_time (start_source_beats).beats ();
	const Temporal::Beats end_region_beats          = region->source_beats_to_region_time (end_source_beats).beats ();
	const double          start_source_beats_double = start_source_beats.get_beats () + (start_source_beats.get_ticks () / (double)Temporal::ticks_per_beat);
	const double          start_region_beats_double = start_region_beats.get_beats () + (start_region_beats.get_ticks () / (double)Temporal::ticks_per_beat);
	const double          length_beats_double       = length_beats.get_beats () + (length_beats.get_ticks () / (double)Temporal::ticks_per_beat);
	const double          end_source_beats_double   = end_source_beats.get_beats () + (end_source_beats.get_ticks () / (double)Temporal::ticks_per_beat);
	const double          end_region_beats_double   = end_region_beats.get_beats () + (end_region_beats.get_ticks () / (double)Temporal::ticks_per_beat);

	const samplepos_t start_sample = region->source_beats_to_absolute_time (start_source_beats).samples ();
	const samplepos_t end_sample   = region->source_beats_to_absolute_time (end_source_beats).samples ();

	std::ostringstream ss;
	ss << "{\"regionId\":\"" << json_escape (region->id ().to_s ()) << "\""
	   << ",\"regionName\":\"" << json_escape (region->name ()) << "\""
	   << ",\"noteId\":" << note->id ()
	   << ",\"note\":" << (int)note->note ()
	   << ",\"velocity\":" << (int)note->velocity ()
	   << ",\"channel\":" << ((int)note->channel () + 1)
	   << ",\"channelRaw\":" << (int)note->channel ()
	   << ",\"startRegionBeats\":" << start_region_beats_double
	   << ",\"startSourceBeats\":" << start_source_beats_double
	   << ",\"lengthBeats\":" << length_beats_double
	   << ",\"endRegionBeats\":" << end_region_beats_double
	   << ",\"endSourceBeats\":" << end_source_beats_double
	   << ",\"startSample\":" << start_sample
	   << ",\"endSample\":" << end_sample
	   << ",\"startBbt\":" << bbt_json_at_sample (start_sample)
	   << ",\"endBbt\":" << bbt_json_at_sample (end_sample)
	   << ",\"positionOrigin\":\"" << json_escape (position_origin) << "\""
	   << "}";

	return ss.str ();
}




static bool
resolve_audio_region_argument_or_selected_at_playhead (
    ARDOUR::Session&                      session,
    const pt::ptree&                      root,
    const std::string&                    args_path,
    std::shared_ptr<ARDOUR::Region>&      region,
    std::shared_ptr<ARDOUR::AudioRegion>& audio_region,
    std::string&                          resolved_via,
    std::string&                          error)
{
	if (!resolve_region_argument_or_selected_at_playhead (session, root, args_path, region, resolved_via, error)) {
		return false;
	}

	audio_region = std::dynamic_pointer_cast<ARDOUR::AudioRegion> (region);
	if (!audio_region) {
		error = "Region is not an audio region";
		return false;
	}

	return true;
}

static double
safe_region_gain_db (double linear_gain)
{
	const double magnitude = std::fabs (linear_gain);
	if (magnitude <= 0.0 || !std::isfinite (magnitude)) {
		return -193.0;
	}

	double db = accurate_coefficient_to_dB (magnitude);
	if (!std::isfinite (db)) {
		db = -193.0;
	}
	return db;
}

static std::string
region_info_json (
    const std::shared_ptr<ARDOUR::Region>& region,
    const std::string&                     resolved_via,
    bool                                   include_analysis,
    const std::optional<double>&           maximum_amplitude,
    const std::optional<double>&           rms)
{
	const samplepos_t                          start_sample = region->position_sample ();
	const samplepos_t                          end_sample   = start_sample + region->length_samples ();
	const std::shared_ptr<ARDOUR::Playlist>    playlist     = region->playlist ();
	const std::shared_ptr<ARDOUR::AudioRegion> audio_region = std::dynamic_pointer_cast<ARDOUR::AudioRegion> (region);

	std::ostringstream ss;
	ss << "{\"regionId\":\"" << json_escape (region->id ().to_s ()) << "\""
	   << ",\"name\":\"" << json_escape (region->name ()) << "\""
	   << ",\"type\":\"" << json_escape (region->data_type ().to_string ()) << "\""
	   << ",\"resolvedVia\":\"" << json_escape (resolved_via) << "\"";

	if (playlist) {
		ss << ",\"playlistId\":\"" << json_escape (playlist->id ().to_s ()) << "\"";
	} else {
		ss << ",\"playlistId\":null";
	}

	ss << ",\"startSample\":" << start_sample
	   << ",\"endSample\":" << end_sample
	   << ",\"lengthSamples\":" << region->length_samples ()
	   << ",\"startBbt\":" << bbt_json_at_sample (start_sample)
	   << ",\"endBbt\":" << bbt_json_at_sample (end_sample)
	   << ",\"hidden\":" << (region->hidden () ? "true" : "false")
	   << ",\"muted\":" << (region->muted () ? "true" : "false")
	   << ",\"locked\":" << (region->locked () ? "true" : "false")
	   << ",\"positionLocked\":" << (region->position_locked () ? "true" : "false")
	   << ",\"isAudio\":" << (audio_region ? "true" : "false")
	   << ",\"includeAnalysis\":" << (include_analysis ? "true" : "false");

	if (audio_region) {
		const double scale_amplitude = audio_region->scale_amplitude ();
		const double magnitude       = std::fabs (scale_amplitude);
		ss << ",\"audio\":{\"scaleAmplitude\":" << scale_amplitude
		   << ",\"gainLinearAbs\":" << magnitude
		   << ",\"gainDb\":" << safe_region_gain_db (scale_amplitude)
		   << ",\"polarityInverted\":" << (scale_amplitude < 0.0 ? "true" : "false");

		if (include_analysis) {
			ss << ",\"maximumAmplitude\":";
			if (maximum_amplitude && std::isfinite (*maximum_amplitude) && *maximum_amplitude >= 0.0) {
				ss << *maximum_amplitude;
			} else {
				ss << "null";
			}
			ss << ",\"rms\":";
			if (rms && std::isfinite (*rms) && *rms >= 0.0) {
				ss << *rms;
			} else {
				ss << "null";
			}
		}

		ss << "}";
	} else {
		ss << ",\"audio\":null";
	}

	ss << "}";
	return ss.str ();
}

static std::string
handle_midi_region_add_tool (ARDOUR::Session& session, pt::ptree& root, const std::string& id)
{
	ARDOUR::Session& _session = session;

	const std::string track_id       = root.get<std::string> ("params.arguments.trackId", "");
	const std::string requested_name = root.get<std::string> ("params.arguments.name", "");

	if (track_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing trackId");
	}

	const std::shared_ptr<ARDOUR::Route>     route      = route_by_mcp_id (_session, track_id);
	const std::shared_ptr<ARDOUR::Track>     track      = std::dynamic_pointer_cast<ARDOUR::Track> (route);
	const std::shared_ptr<ARDOUR::MidiTrack> midi_track = std::dynamic_pointer_cast<ARDOUR::MidiTrack> (track);
	if (!midi_track) {
		return jsonrpc_error (id, -32602, "trackId is not a MIDI track");
	}

	samplepos_t start_sample = 0;
	samplepos_t end_sample   = 0;
	std::string range_error;
	if (!parse_range_endpoints (root, "params.arguments", start_sample, end_sample, range_error)) {
		return jsonrpc_error (id, -32602, range_error);
	}
	if (end_sample <= start_sample) {
		return jsonrpc_error (id, -32602, "Invalid range: end must be greater than start");
	}

	const std::shared_ptr<ARDOUR::Playlist> playlist = midi_track->playlist ();
	if (!playlist) {
		return jsonrpc_error (id, -32602, "Track has no playlist");
	}

	const Temporal::timepos_t start_pos (start_sample);
	const Temporal::timepos_t end_pos (end_sample);
	const Temporal::timecnt_t region_length = start_pos.distance (end_pos);
	if (region_length.samples () <= 0) {
		return jsonrpc_error (id, -32602, "Invalid region length");
	}

	std::shared_ptr<ARDOUR::MidiSource> midi_src;
	try {
		midi_src = _session.create_midi_source_by_stealing_name (track);
	} catch (...) {
		midi_src.reset ();
	}
	if (!midi_src) {
		return jsonrpc_error (id, -32000, "Failed to create MIDI source");
	}

	ARDOUR::SourceList srcs;
	srcs.push_back (std::dynamic_pointer_cast<ARDOUR::Source> (midi_src));
	if (srcs.empty () || !srcs.front ()) {
		return jsonrpc_error (id, -32000, "Failed to resolve MIDI source");
	}

	const Temporal::timecnt_t source_start (Temporal::BeatTime); /* zero beats */

	PBD::PropertyList whole_file_props;
	whole_file_props.add (ARDOUR::Properties::start, source_start);
	whole_file_props.add (ARDOUR::Properties::length, region_length);
	whole_file_props.add (ARDOUR::Properties::automatic, true);
	whole_file_props.add (ARDOUR::Properties::whole_file, true);
	whole_file_props.add (ARDOUR::Properties::name, PBD::basename_nosuffix (midi_src->name ()));
	whole_file_props.add (ARDOUR::Properties::opaque, _session.config.get_draw_opaque_midi_regions ());

	std::shared_ptr<ARDOUR::Region> whole_file_region = ARDOUR::RegionFactory::create (srcs, whole_file_props);
	if (!whole_file_region) {
		return jsonrpc_error (id, -32000, "Failed to create whole-file MIDI region");
	}

	PBD::PropertyList playlist_region_props;
	if (requested_name.empty ()) {
		playlist_region_props.add (ARDOUR::Properties::name, whole_file_region->name ());
	} else {
		playlist_region_props.add (ARDOUR::Properties::name, requested_name);
	}

	std::shared_ptr<ARDOUR::Region> region = ARDOUR::RegionFactory::create (whole_file_region, playlist_region_props);
	if (!region) {
		return jsonrpc_error (id, -32000, "Failed to create MIDI playlist region");
	}

	_session.begin_reversible_command ("add midi region");
	playlist->clear_changes ();
	playlist->clear_owned_changes ();
	region->set_position (start_pos);
	playlist->add_region (region, start_pos, 1.0, false);
	playlist->rdiff_and_add_command (&_session);
	_session.commit_reversible_command ();

	std::ostringstream structured;
	structured << "{\"created\":" << midi_region_json (region, track)
	           << ",\"usedDefaultName\":" << (requested_name.empty () ? "true" : "false");
	if (requested_name.empty ()) {
		structured << ",\"requestedName\":null";
	} else {
		structured << ",\"requestedName\":\"" << json_escape (requested_name) << "\"";
	}
	structured << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"MIDI region added\"}],\"structuredContent\":") + structured.str () + "}");
}

static std::string
handle_midi_note_add_tool (ARDOUR::Session& session, pt::ptree& root, const std::string& id)
{
	ARDOUR::Session& _session = session;

	const std::string region_id = root.get<std::string> ("params.arguments.regionId", "");
	if (region_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing regionId");
	}

	const std::optional<int64_t> note_opt = get_optional<int64_t> (root, "params.arguments.note");
	if (!note_opt || *note_opt < 0 || *note_opt > 127) {
		return jsonrpc_error (id, -32602, "Invalid note (expected 0..127)");
	}

	const std::optional<double> length_beats_opt = get_optional<double> (root, "params.arguments.lengthBeats");
	if (!length_beats_opt || !std::isfinite (*length_beats_opt) || *length_beats_opt <= 0.0) {
		return jsonrpc_error (id, -32602, "Invalid lengthBeats (expected > 0)");
	}

	const int64_t velocity_in = root.get<int64_t> ("params.arguments.velocity", 100);
	if (velocity_in < 0 || velocity_in > 127) {
		return jsonrpc_error (id, -32602, "Invalid velocity (expected 0..127)");
	}

	const int64_t channel_in = root.get<int64_t> ("params.arguments.channel", 1);
	if (channel_in < 1 || channel_in > 16) {
		return jsonrpc_error (id, -32602, "Invalid channel (expected 1..16)");
	}

	const std::optional<double>  region_beat_opt = get_optional<double> (root, "params.arguments.regionBeat");
	const std::optional<int64_t> sample_opt      = get_optional<int64_t> (root, "params.arguments.sample");

	samplepos_t bbt_sample      = 0;
	bool        have_bbt_target = false;
	std::string bbt_error;
	if (!parse_optional_bbt_target_sample (root, "params.arguments", bbt_sample, have_bbt_target, bbt_error)) {
		return jsonrpc_error (id, -32602, bbt_error);
	}

	if (region_beat_opt && (!std::isfinite (*region_beat_opt) || *region_beat_opt < 0.0)) {
		return jsonrpc_error (id, -32602, "Invalid regionBeat (expected >= 0)");
	}

	if (sample_opt && *sample_opt < 0) {
		return jsonrpc_error (id, -32602, "Invalid sample (expected >= 0)");
	}

	int position_arg_count = 0;
	if (region_beat_opt) {
		++position_arg_count;
	}
	if (sample_opt) {
		++position_arg_count;
	}
	if (have_bbt_target) {
		++position_arg_count;
	}
	if (position_arg_count != 1) {
		return jsonrpc_error (id, -32602, "Provide exactly one position: regionBeat, sample, or bar+beat");
	}

	const std::shared_ptr<ARDOUR::Region>     region      = region_by_mcp_id (region_id);
	const std::shared_ptr<ARDOUR::MidiRegion> midi_region = std::dynamic_pointer_cast<ARDOUR::MidiRegion> (region);
	if (!midi_region) {
		return jsonrpc_error (id, -32602, "regionId is not a MIDI region");
	}

	const std::shared_ptr<ARDOUR::MidiModel> model = midi_region->model ();
	if (!model) {
		return jsonrpc_error (id, -32000, "MIDI region model not available");
	}

	Temporal::Beats start_source_beats;
	std::string     position_origin;
	if (region_beat_opt) {
		start_source_beats = midi_region->region_beats_to_source_beats (Temporal::Beats::from_double (*region_beat_opt));
		position_origin    = "regionBeat";
	} else {
		samplepos_t target_sample = 0;
		if (sample_opt) {
			target_sample   = (samplepos_t)*sample_opt;
			position_origin = "sample";
		} else {
			target_sample   = bbt_sample;
			position_origin = "bbt";
		}
		start_source_beats = midi_region->absolute_time_to_source_beats (Temporal::timepos_t (target_sample));
	}

	if (start_source_beats < Temporal::Beats ()) {
		return jsonrpc_error (id, -32602, "Target position is before source start");
	}

	const Temporal::Beats length_beats = Temporal::Beats::from_double (*length_beats_opt);
	if (length_beats < Temporal::Beats::one_tick ()) {
		return jsonrpc_error (id, -32602, "lengthBeats too small (minimum is one tick)");
	}

	std::shared_ptr<Evoral::Note<Temporal::Beats>> note (
	    new Evoral::Note<Temporal::Beats> (
	        (uint8_t)(channel_in - 1),
	        start_source_beats,
	        length_beats,
	        (uint8_t)*note_opt,
	        (uint8_t)velocity_in));

	ARDOUR::MidiModel::NoteDiffCommand* cmd = model->new_note_diff_command ("add midi note");
	cmd->add (note);
	model->apply_diff_command_as_commit (_session, cmd);

	const std::shared_ptr<Evoral::Note<Temporal::Beats>> inserted = model->find_note (note->id ());
	if (!inserted) {
		return jsonrpc_error (id, -32000, "MIDI note insertion was rejected by overlap policy");
	}

	std::ostringstream structured;
	structured << "{\"added\":" << midi_note_json (region, inserted, position_origin)
	           << ",\"requested\":{"
	           << "\"note\":" << *note_opt
	           << ",\"velocity\":" << velocity_in
	           << ",\"channel\":" << channel_in
	           << ",\"lengthBeats\":" << *length_beats_opt;
	if (region_beat_opt) {
		structured << ",\"regionBeat\":" << *region_beat_opt
		           << ",\"sample\":null"
		           << ",\"bar\":null"
		           << ",\"beat\":null";
	} else if (sample_opt) {
		structured << ",\"regionBeat\":null"
		           << ",\"sample\":" << *sample_opt
		           << ",\"bar\":null"
		           << ",\"beat\":null";
	} else {
		const int    req_bar  = root.get<int> ("params.arguments.bar", 0);
		const double req_beat = root.get<double> ("params.arguments.beat", 0.0);
		structured << ",\"regionBeat\":null"
		           << ",\"sample\":null"
		           << ",\"bar\":" << req_bar
		           << ",\"beat\":" << req_beat;
	}
	structured << "}}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"MIDI note added\"}],\"structuredContent\":") + structured.str () + "}");
}

static std::string
handle_midi_note_list_tool (ARDOUR::Session& session, pt::ptree& root, const std::string& id)
{
	(void)session;

	const std::string region_id = root.get<std::string> ("params.arguments.regionId", "");
	if (region_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing regionId");
	}

	const std::shared_ptr<ARDOUR::Region> region = region_by_mcp_id (region_id);
	if (!region) {
		return jsonrpc_error (id, -32602, "regionId not found");
	}
	const std::shared_ptr<ARDOUR::MidiRegion> midi_region = std::dynamic_pointer_cast<ARDOUR::MidiRegion> (region);
	if (!midi_region) {
		return jsonrpc_error (id, -32602, "regionId is not a MIDI region");
	}

	const std::shared_ptr<ARDOUR::MidiModel> model = midi_region->model ();
	if (!model) {
		return jsonrpc_error (id, -32000, "MIDI region model not available");
	}

	std::vector<std::string>        notes_json;
	const ARDOUR::MidiModel::Notes& notes = model->notes ();
	for (ARDOUR::MidiModel::Notes::const_iterator it = notes.begin (); it != notes.end (); ++it) {
		const std::shared_ptr<Evoral::Note<Temporal::Beats>>& note = *it;
		if (!note) {
			continue;
		}
		notes_json.push_back (midi_note_json (region, note, "list"));
	}

	std::ostringstream structured;
	structured << "{\"region\":" << midi_region_brief_json (region)
	           << ",\"count\":" << notes_json.size ()
	           << ",\"notes\":[";
	for (size_t i = 0; i < notes_json.size (); ++i) {
		if (i > 0) {
			structured << ",";
		}
		structured << notes_json[i];
	}
	structured << "]}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"MIDI notes listed\"}],\"structuredContent\":") + structured.str () + "}");
}

static std::string
handle_midi_note_edit_tool (ARDOUR::Session& session, pt::ptree& root, const std::string& id)
{
	ARDOUR::Session& _session = session;

	const std::string region_id = root.get<std::string> ("params.arguments.regionId", "");
	if (region_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing regionId");
	}

	const std::shared_ptr<ARDOUR::Region> region = region_by_mcp_id (region_id);
	if (!region) {
		return jsonrpc_error (id, -32602, "regionId not found");
	}
	const std::shared_ptr<ARDOUR::MidiRegion> midi_region = std::dynamic_pointer_cast<ARDOUR::MidiRegion> (region);
	if (!midi_region) {
		return jsonrpc_error (id, -32602, "regionId is not a MIDI region");
	}

	const std::shared_ptr<ARDOUR::MidiModel> model = midi_region->model ();
	if (!model) {
		return jsonrpc_error (id, -32000, "MIDI region model not available");
	}

	boost::optional<pt::ptree&> edits_opt = root.get_child_optional ("params.arguments.edits");
	if (!edits_opt) {
		return jsonrpc_error (id, -32602, "Missing edits array");
	}
	if (edits_opt->empty ()) {
		return jsonrpc_error (id, -32602, "edits must contain at least one item");
	}

	struct PlannedNoteEdit {
		Evoral::event_id_t                             note_id;
		std::shared_ptr<Evoral::Note<Temporal::Beats>> note;
		bool                                           remove;
		bool                                           change_note;
		bool                                           change_velocity;
		bool                                           change_channel;
		bool                                           change_start;
		bool                                           change_length;
		uint8_t                                        note_value;
		uint8_t                                        velocity_value;
		uint8_t                                        channel_value;
		Temporal::Beats                                start_value;
		Temporal::Beats                                length_value;
	};

	std::vector<PlannedNoteEdit>    planned;
	std::vector<Evoral::event_id_t> not_found_ids;
	std::vector<std::string>        warnings;

	size_t requested_count = 0;
	size_t unchanged_count = 0;
	size_t invalid_count   = 0;

	for (pt::ptree::const_iterator it = edits_opt->begin (); it != edits_opt->end (); ++it) {
		++requested_count;
		const pt::ptree& edit       = it->second;
		const size_t     edit_index = requested_count;

		const std::optional<int64_t> note_id_opt = get_optional<int64_t> (edit, "noteId");
		if (!note_id_opt) {
			return jsonrpc_error (id, -32602, "Each edit item must include noteId");
		}
		if (*note_id_opt < INT32_MIN || *note_id_opt > INT32_MAX) {
			std::ostringstream w;
			w << "Edit " << edit_index << " skipped: noteId out of range";
			warnings.push_back (w.str ());
			++invalid_count;
			continue;
		}

		const Evoral::event_id_t                             note_id = (Evoral::event_id_t)*note_id_opt;
		const std::shared_ptr<Evoral::Note<Temporal::Beats>> note    = model->find_note (note_id);
		if (!note) {
			not_found_ids.push_back (note_id);
			continue;
		}

		const bool remove_note = edit.get<bool> ("delete", false);

		const std::optional<int64_t> note_value_opt      = get_optional<int64_t> (edit, "note");
		const std::optional<int64_t> delta_semitones_opt = get_optional<int64_t> (edit, "deltaSemitones");
		if (note_value_opt && delta_semitones_opt) {
			return jsonrpc_error (id, -32602, "Each edit item may include only one of: note or deltaSemitones");
		}

		const std::optional<int64_t> velocity_value_opt = get_optional<int64_t> (edit, "velocity");
		const std::optional<int64_t> delta_velocity_opt = get_optional<int64_t> (edit, "deltaVelocity");
		if (velocity_value_opt && delta_velocity_opt) {
			return jsonrpc_error (id, -32602, "Each edit item may include only one of: velocity or deltaVelocity");
		}

		const std::optional<double> start_beats_opt = get_optional<double> (edit, "startBeats");
		const std::optional<double> delta_beats_opt = get_optional<double> (edit, "deltaBeats");
		if (start_beats_opt && delta_beats_opt) {
			return jsonrpc_error (id, -32602, "Each edit item may include only one of: startBeats or deltaBeats");
		}

		const std::optional<int64_t> channel_opt      = get_optional<int64_t> (edit, "channel");
		const std::optional<double>  length_beats_opt = get_optional<double> (edit, "lengthBeats");

		if (start_beats_opt && (!std::isfinite (*start_beats_opt) || *start_beats_opt < 0.0)) {
			return jsonrpc_error (id, -32602, "Invalid startBeats (expected finite >= 0)");
		}
		if (delta_beats_opt && !std::isfinite (*delta_beats_opt)) {
			return jsonrpc_error (id, -32602, "Invalid deltaBeats (expected finite number)");
		}
		if (length_beats_opt && (!std::isfinite (*length_beats_opt) || *length_beats_opt <= 0.0)) {
			return jsonrpc_error (id, -32602, "Invalid lengthBeats (expected > 0)");
		}

		PlannedNoteEdit planned_edit;
		planned_edit.note_id         = note_id;
		planned_edit.note            = note;
		planned_edit.remove          = remove_note;
		planned_edit.change_note     = false;
		planned_edit.change_velocity = false;
		planned_edit.change_channel  = false;
		planned_edit.change_start    = false;
		planned_edit.change_length   = false;
		planned_edit.note_value      = note->note ();
		planned_edit.velocity_value  = note->velocity ();
		planned_edit.channel_value   = note->channel ();
		planned_edit.start_value     = note->time ();
		planned_edit.length_value    = note->length ();

		if (remove_note) {
			planned.push_back (planned_edit);
			continue;
		}

		bool        invalid_edit = false;
		std::string invalid_reason;

		int note_number = (int)note->note ();
		if (note_value_opt) {
			note_number = (int)*note_value_opt;
		} else if (delta_semitones_opt) {
			note_number += (int)*delta_semitones_opt;
		}
		if (note_number < 0 || note_number > 127) {
			invalid_edit   = true;
			invalid_reason = "note out of range after edit";
		}

		int velocity = (int)note->velocity ();
		if (!invalid_edit) {
			if (velocity_value_opt) {
				velocity = (int)*velocity_value_opt;
			} else if (delta_velocity_opt) {
				velocity += (int)*delta_velocity_opt;
			}
			if (velocity < 0 || velocity > 127) {
				invalid_edit   = true;
				invalid_reason = "velocity out of range after edit";
			}
		}

		int channel = (int)note->channel ();
		if (!invalid_edit && channel_opt) {
			channel = (int)*channel_opt - 1;
			if (channel < 0 || channel > 15) {
				invalid_edit   = true;
				invalid_reason = "channel out of range (expected 1..16)";
			}
		}

		Temporal::Beats start_source = note->time ();
		if (!invalid_edit && (start_beats_opt || delta_beats_opt)) {
			const Temporal::Beats current_region_beats = midi_region->source_beats_to_region_time (note->time ()).beats ();
			double                target_region_beats  = beats_to_double (current_region_beats);
			if (start_beats_opt) {
				target_region_beats = *start_beats_opt;
			} else {
				target_region_beats += *delta_beats_opt;
			}

			if (!std::isfinite (target_region_beats) || target_region_beats < 0.0) {
				invalid_edit   = true;
				invalid_reason = "start position out of range after edit";
			} else {
				start_source = midi_region->region_beats_to_source_beats (Temporal::Beats::from_double (target_region_beats));
				if (start_source < Temporal::Beats ()) {
					invalid_edit   = true;
					invalid_reason = "start position maps before source start";
				}
			}
		}

		Temporal::Beats length_source = note->length ();
		if (!invalid_edit && length_beats_opt) {
			length_source = Temporal::Beats::from_double (*length_beats_opt);
			if (length_source < Temporal::Beats::one_tick ()) {
				invalid_edit   = true;
				invalid_reason = "lengthBeats too small (minimum is one tick)";
			}
		}

		if (invalid_edit) {
			std::ostringstream w;
			w << "Edit " << edit_index << " for noteId " << note_id << " skipped: " << invalid_reason;
			warnings.push_back (w.str ());
			++invalid_count;
			continue;
		}

		planned_edit.note_value     = (uint8_t)note_number;
		planned_edit.velocity_value = (uint8_t)velocity;
		planned_edit.channel_value  = (uint8_t)channel;
		planned_edit.start_value    = start_source;
		planned_edit.length_value   = length_source;

		planned_edit.change_note     = (planned_edit.note_value != note->note ());
		planned_edit.change_velocity = (planned_edit.velocity_value != note->velocity ());
		planned_edit.change_channel  = (planned_edit.channel_value != note->channel ());
		planned_edit.change_start    = (planned_edit.start_value != note->time ());
		planned_edit.change_length   = (planned_edit.length_value != note->length ());

		if (!planned_edit.change_note && !planned_edit.change_velocity && !planned_edit.change_channel && !planned_edit.change_start && !planned_edit.change_length) {
			++unchanged_count;
			continue;
		}

		planned.push_back (planned_edit);
	}

	const size_t queued_count = planned.size ();
	if (queued_count > 0) {
		ARDOUR::MidiModel::NoteDiffCommand* cmd = model->new_note_diff_command ("edit midi notes");
		for (size_t i = 0; i < planned.size (); ++i) {
			const PlannedNoteEdit& e = planned[i];
			if (e.remove) {
				cmd->remove (e.note);
				continue;
			}
			if (e.change_note) {
				cmd->change (e.note, ARDOUR::MidiModel::NoteDiffCommand::NoteNumber, e.note_value);
			}
			if (e.change_velocity) {
				cmd->change (e.note, ARDOUR::MidiModel::NoteDiffCommand::Velocity, e.velocity_value);
			}
			if (e.change_channel) {
				cmd->change (e.note, ARDOUR::MidiModel::NoteDiffCommand::Channel, e.channel_value);
			}
			if (e.change_start) {
				cmd->change (e.note, ARDOUR::MidiModel::NoteDiffCommand::StartTime, e.start_value);
			}
			if (e.change_length) {
				cmd->change (e.note, ARDOUR::MidiModel::NoteDiffCommand::Length, e.length_value);
			}
		}
		model->apply_diff_command_as_commit (_session, cmd);
	}

	size_t                          changed_count  = 0;
	size_t                          deleted_count  = 0;
	size_t                          rejected_count = 0;
	std::vector<std::string>        changed_notes_json;
	std::vector<Evoral::event_id_t> deleted_ids;

	for (size_t i = 0; i < planned.size (); ++i) {
		const PlannedNoteEdit&                               e     = planned[i];
		const std::shared_ptr<Evoral::Note<Temporal::Beats>> after = model->find_note (e.note_id);

		if (e.remove) {
			if (after) {
				++rejected_count;
				std::ostringstream w;
				w << "Delete for noteId " << e.note_id << " was rejected";
				warnings.push_back (w.str ());
			} else {
				++deleted_count;
				deleted_ids.push_back (e.note_id);
			}
			continue;
		}

		if (!after) {
			++rejected_count;
			std::ostringstream w;
			w << "Edit for noteId " << e.note_id << " was rejected (note no longer present)";
			warnings.push_back (w.str ());
			continue;
		}

		bool mismatch = false;
		if (e.change_note && after->note () != e.note_value) {
			mismatch = true;
		}
		if (e.change_velocity && after->velocity () != e.velocity_value) {
			mismatch = true;
		}
		if (e.change_channel && after->channel () != e.channel_value) {
			mismatch = true;
		}
		if (e.change_start && after->time () != e.start_value) {
			mismatch = true;
		}
		if (e.change_length && after->length () != e.length_value) {
			mismatch = true;
		}

		if (mismatch) {
			++rejected_count;
			std::ostringstream w;
			w << "Edit for noteId " << e.note_id << " did not fully apply";
			warnings.push_back (w.str ());
			continue;
		}

		++changed_count;
		changed_notes_json.push_back (midi_note_json (region, after, "edit"));
	}

	std::ostringstream structured;
	structured << "{\"region\":" << midi_region_brief_json (region)
	           << ",\"summary\":{"
	           << "\"requested\":" << requested_count
	           << ",\"queued\":" << queued_count
	           << ",\"changed\":" << changed_count
	           << ",\"deleted\":" << deleted_count
	           << ",\"unchanged\":" << unchanged_count
	           << ",\"notFound\":" << not_found_ids.size ()
	           << ",\"invalid\":" << invalid_count
	           << ",\"rejected\":" << rejected_count
	           << "}"
	           << ",\"changed\":[";
	for (size_t i = 0; i < changed_notes_json.size (); ++i) {
		if (i > 0) {
			structured << ",";
		}
		structured << changed_notes_json[i];
	}
	structured << "],\"deletedNoteIds\":[";
	for (size_t i = 0; i < deleted_ids.size (); ++i) {
		if (i > 0) {
			structured << ",";
		}
		structured << deleted_ids[i];
	}
	structured << "],\"notFoundNoteIds\":[";
	for (size_t i = 0; i < not_found_ids.size (); ++i) {
		if (i > 0) {
			structured << ",";
		}
		structured << not_found_ids[i];
	}
	structured << "],\"warnings\":" << json_string_array (warnings) << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"MIDI notes edited\"}],\"structuredContent\":") + structured.str () + "}");
}

static std::string
handle_midi_note_import_json_tool (ARDOUR::Session& session, pt::ptree& root, const std::string& id)
{
	ARDOUR::Session& _session = session;

	boost::optional<pt::ptree&> midi_opt = root.get_child_optional ("params.arguments.midi");
	if (!midi_opt) {
		return jsonrpc_error (id, -32602, "Missing midi object");
	}

	const std::string region_id      = root.get<std::string> ("params.arguments.regionId", "");
	const std::string track_id_arg   = root.get<std::string> ("params.arguments.trackId", "");
	const std::string requested_name = root.get<std::string> ("params.arguments.name", "");

	bool                            created_region = false;
	std::shared_ptr<ARDOUR::Track>  target_track;
	std::shared_ptr<ARDOUR::Region> target_region;

	if (!region_id.empty ()) {
		target_region = region_by_mcp_id (region_id);
		if (!target_region) {
			return jsonrpc_error (id, -32602, "regionId not found");
		}

		if (!track_id_arg.empty ()) {
			std::shared_ptr<ARDOUR::Route> r = route_by_mcp_id (_session, track_id_arg);
			target_track                     = std::dynamic_pointer_cast<ARDOUR::Track> (r);
		}
	} else {
		if (track_id_arg.empty ()) {
			return jsonrpc_error (id, -32602, "Provide regionId, or trackId with range endpoints");
		}

		std::shared_ptr<ARDOUR::Route> route                = route_by_mcp_id (_session, track_id_arg);
		target_track                                        = std::dynamic_pointer_cast<ARDOUR::Track> (route);
		const std::shared_ptr<ARDOUR::MidiTrack> midi_track = std::dynamic_pointer_cast<ARDOUR::MidiTrack> (target_track);
		if (!midi_track) {
			return jsonrpc_error (id, -32602, "trackId is not a MIDI track");
		}

		samplepos_t start_sample = 0;
		samplepos_t end_sample   = 0;
		std::string range_error;
		if (!parse_range_endpoints (root, "params.arguments", start_sample, end_sample, range_error)) {
			return jsonrpc_error (id, -32602, range_error);
		}
		if (end_sample <= start_sample) {
			return jsonrpc_error (id, -32602, "Invalid range: end must be greater than start");
		}

		std::string create_error;
		if (!create_midi_region_on_track (_session, target_track, start_sample, end_sample, requested_name, target_region, create_error)) {
			return jsonrpc_error (id, -32000, create_error);
		}
		created_region = true;
	}

	const std::shared_ptr<ARDOUR::MidiRegion> midi_region = std::dynamic_pointer_cast<ARDOUR::MidiRegion> (target_region);
	if (!midi_region) {
		return jsonrpc_error (id, -32602, "Target region is not a MIDI region");
	}

	const std::shared_ptr<ARDOUR::MidiModel> model = midi_region->model ();
	if (!model) {
		return jsonrpc_error (id, -32000, "MIDI region model not available");
	}

	std::vector<MidiJsonEventDef> expanded_events;
	std::vector<MidiJsonNoteDef>  note_defs;
	std::vector<std::string>      warnings;
	int                           channel                 = 9;
	bool                          one_based_channel_input = true;
	bool                          is_drum_mode            = true;
	int                           ticks_per_quarter       = 480;
	int                           time_sig_num            = 4;
	int                           time_sig_den            = 4;
	std::string                   parse_error;

	if (!parse_midi_json_events (*midi_opt, expanded_events, channel, one_based_channel_input, is_drum_mode, ticks_per_quarter, time_sig_num, time_sig_den, warnings, parse_error)) {
		return jsonrpc_error (id, -32602, parse_error);
	}
	if (!build_midi_json_note_defs (expanded_events, is_drum_mode, ticks_per_quarter, time_sig_num, time_sig_den, note_defs, warnings, parse_error)) {
		return jsonrpc_error (id, -32602, parse_error);
	}

	ARDOUR::MidiModel::NoteDiffCommand*                         cmd = model->new_note_diff_command ("import midi json");
	std::vector<std::shared_ptr<Evoral::Note<Temporal::Beats>>> requested_notes;
	requested_notes.reserve (note_defs.size ());

	bool uses_per_event_channels = false;
	for (size_t i = 0; i < expanded_events.size (); ++i) {
		if (expanded_events[i].channel != channel) {
			uses_per_event_channels = true;
			break;
		}
	}

	for (size_t i = 0; i < note_defs.size (); ++i) {
		if (note_defs[i].note < 0 || note_defs[i].note > 127 || note_defs[i].velocity < 0 || note_defs[i].velocity > 127 || note_defs[i].channel < 0 || note_defs[i].channel > 15) {
			std::ostringstream w;
			w << "Skipped invalid note/velocity/channel values at index " << i;
			warnings.push_back (w.str ());
			continue;
		}

		const Temporal::Beats start_source_beats = midi_region->region_beats_to_source_beats (Temporal::Beats::from_double (note_defs[i].start_quarters));
		if (start_source_beats < Temporal::Beats ()) {
			std::ostringstream w;
			w << "Skipped note before source start at index " << i;
			warnings.push_back (w.str ());
			continue;
		}

		const Temporal::Beats length_beats = Temporal::Beats::from_double (note_defs[i].length_quarters);
		if (length_beats < Temporal::Beats ()) {
			std::ostringstream w;
			w << "Skipped note with negative length at index " << i;
			warnings.push_back (w.str ());
			continue;
		}
		if (!is_drum_mode && length_beats < Temporal::Beats::one_tick ()) {
			std::ostringstream w;
			w << "Skipped note shorter than one tick at index " << i;
			warnings.push_back (w.str ());
			continue;
		}

		std::shared_ptr<Evoral::Note<Temporal::Beats>> note (
		    new Evoral::Note<Temporal::Beats> (
		        (uint8_t)note_defs[i].channel,
		        start_source_beats,
		        length_beats,
		        (uint8_t)note_defs[i].note,
		        (uint8_t)note_defs[i].velocity));
		cmd->add (note);
		requested_notes.push_back (note);
	}

	if (!requested_notes.empty ()) {
		model->apply_diff_command_as_commit (_session, cmd);
	} else {
		delete cmd;
	}

	size_t inserted_count = 0;
	for (size_t i = 0; i < requested_notes.size (); ++i) {
		if (model->find_note (requested_notes[i]->id ())) {
			++inserted_count;
		}
	}

	const size_t rejected_count = (note_defs.size () >= inserted_count) ? (note_defs.size () - inserted_count) : 0;

	std::ostringstream structured;
	structured << "{\"createdRegion\":" << (created_region ? "true" : "false")
	           << ",\"region\":" << midi_region_brief_json (target_region)
	           << ",\"summary\":{"
	           << "\"channelBase\":\"" << (one_based_channel_input ? "one" : "zero") << "\""
	           << ","
	           << "\"channel\":" << (channel + 1)
	           << ",\"defaultChannel\":" << (channel + 1)
	           << ",\"usesPerEventChannels\":" << (uses_per_event_channels ? "true" : "false")
	           << ",\"isDrumMode\":" << (is_drum_mode ? "true" : "false")
	           << ",\"ticksPerQuarter\":" << ticks_per_quarter
	           << ",\"timeSignature\":{\"numerator\":" << time_sig_num << ",\"denominator\":" << time_sig_den << "}"
	           << ",\"eventsExpanded\":" << expanded_events.size ()
	           << ",\"notesRequested\":" << note_defs.size ()
	           << ",\"notesAttempted\":" << requested_notes.size ()
	           << ",\"notesInserted\":" << inserted_count
	           << ",\"notesRejected\":" << rejected_count
	           << "}"
	           << ",\"warnings\":" << json_string_array (warnings);
	if (target_track) {
		structured << ",\"trackId\":\"" << json_escape (target_track->id ().to_s ()) << "\""
		           << ",\"trackName\":\"" << json_escape (target_track->name ()) << "\"";
	} else {
		structured << ",\"trackId\":null"
		           << ",\"trackName\":null";
	}
	if (requested_name.empty ()) {
		structured << ",\"requestedName\":null";
	} else {
		structured << ",\"requestedName\":\"" << json_escape (requested_name) << "\"";
	}
	structured << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"MIDI JSON imported\"}],\"structuredContent\":") + structured.str () + "}");
}

static std::string
handle_midi_note_get_json_tool (ARDOUR::Session& session, pt::ptree& root, const std::string& id)
{
	(void)session;

	const std::string region_id = root.get<std::string> ("params.arguments.regionId", "");
	if (region_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing regionId");
	}

	const int64_t ticks_per_quarter_in = root.get<int64_t> ("params.arguments.ticksPerQuarter", 480);
	if (ticks_per_quarter_in <= 0 || ticks_per_quarter_in > 96000) {
		return jsonrpc_error (id, -32602, "Invalid ticksPerQuarter (expected 1..96000)");
	}
	const int ticks_per_quarter = (int)ticks_per_quarter_in;

	const std::string time_signature = root.get<std::string> ("params.arguments.timeSignature", "4/4");
	int               time_sig_num   = 4;
	int               time_sig_den   = 4;
	if (!parse_time_signature (time_signature, time_sig_num, time_sig_den)) {
		return jsonrpc_error (id, -32602, "Invalid timeSignature (expected format N/D)");
	}

	const std::shared_ptr<ARDOUR::Region> region = region_by_mcp_id (region_id);
	if (!region) {
		return jsonrpc_error (id, -32602, "regionId not found");
	}
	const std::shared_ptr<ARDOUR::MidiRegion> midi_region = std::dynamic_pointer_cast<ARDOUR::MidiRegion> (region);
	if (!midi_region) {
		return jsonrpc_error (id, -32602, "regionId is not a MIDI region");
	}

	const std::shared_ptr<ARDOUR::MidiModel> model = midi_region->model ();
	if (!model) {
		return jsonrpc_error (id, -32000, "MIDI region model not available");
	}

	struct ExportEvent {
		double      quarters;
		size_t      ordinal;
		int         note;
		int         velocity;
		int         channel;
		const char* type;
	};

	std::vector<ExportEvent> events;
	std::vector<std::string> warnings;
	std::map<int, size_t>    channel_counts;
	size_t                   notes_seen     = 0;
	size_t                   notes_exported = 0;

	const ARDOUR::MidiModel::Notes& notes = model->notes ();
	for (ARDOUR::MidiModel::Notes::const_iterator it = notes.begin (); it != notes.end (); ++it) {
		const std::shared_ptr<Evoral::Note<Temporal::Beats>>& note = *it;
		if (!note) {
			continue;
		}
		++notes_seen;

		const int note_num = (int)note->note ();
		const int velocity = (int)note->velocity ();
		const int channel  = (int)note->channel ();
		if (note_num < 0 || note_num > 127 || velocity < 0 || velocity > 127 || channel < 0 || channel > 15) {
			std::ostringstream w;
			w << "Skipped note with invalid note/velocity/channel (noteId " << note->id () << ")";
			warnings.push_back (w.str ());
			continue;
		}

		const Temporal::Beats start_source_beats = note->time ();
		const Temporal::Beats end_source_beats   = start_source_beats + note->length ();
		if (end_source_beats < start_source_beats) {
			std::ostringstream w;
			w << "Skipped note with negative length (noteId " << note->id () << ")";
			warnings.push_back (w.str ());
			continue;
		}

		const Temporal::Beats start_region_beats = midi_region->source_beats_to_region_time (start_source_beats).beats ();
		const Temporal::Beats end_region_beats   = midi_region->source_beats_to_region_time (end_source_beats).beats ();
		const double          start_quarters     = beats_to_double (start_region_beats);
		const double          end_quarters       = beats_to_double (end_region_beats);
		if (!std::isfinite (start_quarters) || !std::isfinite (end_quarters) || end_quarters < start_quarters) {
			std::ostringstream w;
			w << "Skipped note with invalid export timing (noteId " << note->id () << ")";
			warnings.push_back (w.str ());
			continue;
		}

		const size_t base_ordinal = events.size ();

		ExportEvent on;
		on.quarters = start_quarters;
		on.ordinal  = base_ordinal;
		on.note     = note_num;
		on.velocity = velocity;
		on.channel  = channel;
		on.type     = "note_on";
		events.push_back (on);

		ExportEvent off;
		off.quarters = end_quarters;
		off.ordinal  = base_ordinal + 1;
		off.note     = note_num;
		off.velocity = 0;
		off.channel  = channel;
		off.type     = "note_off";
		events.push_back (off);

		channel_counts[channel] += 1;
		++notes_exported;
	}

	std::sort (
	    events.begin (),
	    events.end (),
	    [] (const ExportEvent& a, const ExportEvent& b) {
		    if (a.quarters != b.quarters) {
			    return a.quarters < b.quarters;
		    }
		    if (a.channel == b.channel && a.note == b.note && a.ordinal != b.ordinal) {
			    return a.ordinal < b.ordinal;
		    }
		    const int a_type_order = (std::strcmp (a.type, "note_off") == 0) ? 0 : 1;
		    const int b_type_order = (std::strcmp (b.type, "note_off") == 0) ? 0 : 1;
		    if (a_type_order != b_type_order) {
			    return a_type_order < b_type_order;
		    }
		    if (a.channel != b.channel) {
			    return a.channel < b.channel;
		    }
		    if (a.note != b.note) {
			    return a.note < b.note;
		    }
		    return a.ordinal < b.ordinal;
	    });

	int    default_channel = 9;
	size_t best_count      = 0;
	for (std::map<int, size_t>::const_iterator it = channel_counts.begin (); it != channel_counts.end (); ++it) {
		if (it->second > best_count) {
			best_count      = it->second;
			default_channel = it->first;
		}
	}

	std::ostringstream events_json;
	events_json << "[";
	bool   first_event     = true;
	size_t events_exported = 0;
	for (size_t i = 0; i < events.size (); ++i) {
		int bar  = 0;
		int beat = 0;
		int tick = 0;
		if (!quarters_to_midi_json_time (events[i].quarters, time_sig_num, time_sig_den, ticks_per_quarter, bar, beat, tick)) {
			std::ostringstream w;
			w << "Skipped event with unrepresentable timing at event index " << i;
			warnings.push_back (w.str ());
			continue;
		}

		if (!first_event) {
			events_json << ",";
		}
		first_event = false;
		events_json << "{\"bar\":" << bar
		            << ",\"b\":" << beat
		            << ",\"t\":" << tick
		            << ",\"n\":" << events[i].note
		            << ",\"v\":" << events[i].velocity
		            << ",\"type\":\"" << events[i].type << "\"";
		if (events[i].channel != default_channel) {
			events_json << ",\"channel\":" << (events[i].channel + 1);
		}
		events_json << "}";
		++events_exported;
	}
	events_json << "]";

	std::ostringstream midi_json;
	midi_json << "{\"channel_base\":\"one\""
	          << ",\"channel\":" << (default_channel + 1)
	          << ",\"is_drum_mode\":false"
	          << ",\"time_signature\":\"" << json_escape (time_signature) << "\""
	          << ",\"ticks_per_quarter\":" << ticks_per_quarter
	          << ",\"midi_events\":" << events_json.str ()
	          << "}";

	std::ostringstream structured;
	structured << "{\"region\":" << midi_region_brief_json (region)
	           << ",\"midi\":" << midi_json.str ()
	           << ",\"summary\":{"
	           << "\"notesInRegion\":" << notes_seen
	           << ",\"notesExported\":" << notes_exported
	           << ",\"eventsExported\":" << events_exported
	           << ",\"defaultChannel\":" << (default_channel + 1)
	           << ",\"defaultChannelRaw\":" << default_channel
	           << "}"
	           << ",\"warnings\":" << json_string_array (warnings)
	           << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"MIDI JSON exported\"}],\"structuredContent\":") + structured.str () + "}");
}

static std::string
handle_region_get_info_tool (ARDOUR::Session& session, pt::ptree& root, const std::string& id)
{
	ARDOUR::Session& _session = session;

	const bool include_analysis = root.get<bool> ("params.arguments.includeAnalysis", false);

	std::shared_ptr<ARDOUR::Region> region;
	std::string                     region_resolved_via;
	std::string                     region_error;
	if (!resolve_region_argument_or_selected_at_playhead (_session, root, "params.arguments", region, region_resolved_via, region_error)) {
		return jsonrpc_error (id, -32602, region_error);
	}

	std::optional<double> analyzed_maximum_amplitude;
	std::optional<double> analyzed_rms;
	if (include_analysis) {
		const std::shared_ptr<ARDOUR::AudioRegion> audio_region = std::dynamic_pointer_cast<ARDOUR::AudioRegion> (region);
		if (audio_region) {
			const double max_amp = audio_region->maximum_amplitude ();
			if (std::isfinite (max_amp) && max_amp >= 0.0) {
				analyzed_maximum_amplitude = max_amp;
			}

			const double rms = audio_region->rms ();
			if (std::isfinite (rms) && rms >= 0.0) {
				analyzed_rms = rms;
			}
		}
	}

	const std::string structured = region_info_json (
	    region,
	    region_resolved_via,
	    include_analysis,
	    analyzed_maximum_amplitude,
	    analyzed_rms);

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Region info\"}],\"structuredContent\":") + structured + "}");
}

static std::string
handle_region_set_gain_tool (ARDOUR::Session& session, pt::ptree& root, const std::string& id)
{
	ARDOUR::Session& _session = session;

	const std::optional<double> linear          = get_optional<double> (root, "params.arguments.linear");
	const std::optional<double> db              = get_optional<double> (root, "params.arguments.db");
	const bool                  invert_polarity = root.get<bool> ("params.arguments.invertPolarity", false);

	if (!linear && !db) {
		return jsonrpc_error (id, -32602, "Provide one of: linear or db");
	}
	if (linear && db) {
		return jsonrpc_error (id, -32602, "Provide only one of: linear or db");
	}

	if (linear) {
		if (!std::isfinite (*linear) || *linear < 0.0) {
			return jsonrpc_error (id, -32602, "Invalid linear gain (expected finite >= 0)");
		}
	}
	if (db) {
		if (!std::isfinite (*db)) {
			return jsonrpc_error (id, -32602, "Invalid dB value");
		}
	}

	std::shared_ptr<ARDOUR::Region>      region;
	std::shared_ptr<ARDOUR::AudioRegion> audio_region;
	std::string                          region_resolved_via;
	std::string                          region_error;
	if (!resolve_audio_region_argument_or_selected_at_playhead (
	        _session,
	        root,
	        "params.arguments",
	        region,
	        audio_region,
	        region_resolved_via,
	        region_error)) {
		return jsonrpc_error (id, -32602, region_error);
	}

	const double previous_gain = audio_region->scale_amplitude ();
	const double previous_db   = safe_region_gain_db (previous_gain);
	double       new_magnitude = std::fabs (previous_gain);

	if (linear) {
		new_magnitude = *linear;
	} else {
		new_magnitude = (*db <= -192.0) ? 0.0 : dB_to_coefficient (*db);
	}

	if (!std::isfinite (new_magnitude) || new_magnitude < 0.0) {
		return jsonrpc_error (id, -32602, "Invalid mapped region gain");
	}

	double sign = (previous_gain < 0.0) ? -1.0 : 1.0;
	if (invert_polarity) {
		sign *= -1.0;
	}
	const double new_gain = (new_magnitude == 0.0) ? 0.0 : (new_magnitude * sign);
	const bool   updated  = (new_gain != previous_gain);

	if (updated) {
		_session.begin_reversible_command ("set region gain");
		region->clear_changes ();
		audio_region->set_scale_amplitude ((ARDOUR::gain_t)new_gain);
		_session.add_command (new PBD::StatefulDiffCommand (region));
		_session.commit_reversible_command ();
	}

	std::ostringstream structured;
	structured << "{\"regionId\":\"" << json_escape (region->id ().to_s ()) << "\""
	           << ",\"name\":\"" << json_escape (region->name ()) << "\""
	           << ",\"resolvedVia\":\"" << json_escape (region_resolved_via) << "\""
	           << ",\"updated\":" << (updated ? "true" : "false")
	           << ",\"requested\":{"
	           << "\"linear\":";
	if (linear) {
		structured << *linear;
	} else {
		structured << "null";
	}
	structured << ",\"db\":";
	if (db) {
		structured << *db;
	} else {
		structured << "null";
	}
	structured << ",\"invertPolarity\":" << (invert_polarity ? "true" : "false")
	           << "},\"gain\":{"
	           << "\"previousLinear\":" << previous_gain
	           << ",\"previousDb\":" << previous_db
	           << ",\"linear\":" << audio_region->scale_amplitude ()
	           << ",\"db\":" << safe_region_gain_db (audio_region->scale_amplitude ())
	           << ",\"polarityInverted\":" << (audio_region->scale_amplitude () < 0.0 ? "true" : "false")
	           << "}}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + (updated ? "Region gain updated" : "Region gain unchanged") + "\"}],\"structuredContent\":" + structured.str () + "}");
}

static std::string
handle_region_normalize_tool (ARDOUR::Session& session, pt::ptree& root, const std::string& id)
{
	ARDOUR::Session& _session = session;

	const double target_db = root.get<double> ("params.arguments.targetDb", 0.0);
	if (!std::isfinite (target_db)) {
		return jsonrpc_error (id, -32602, "Invalid targetDb");
	}

	std::shared_ptr<ARDOUR::Region>      region;
	std::shared_ptr<ARDOUR::AudioRegion> audio_region;
	std::string                          region_resolved_via;
	std::string                          region_error;
	if (!resolve_audio_region_argument_or_selected_at_playhead (
	        _session,
	        root,
	        "params.arguments",
	        region,
	        audio_region,
	        region_resolved_via,
	        region_error)) {
		return jsonrpc_error (id, -32602, region_error);
	}

	const double peak_amplitude = audio_region->maximum_amplitude ();
	if (!std::isfinite (peak_amplitude) || peak_amplitude < 0.0) {
		return jsonrpc_error (id, -32000, "Failed to analyze region peak amplitude");
	}

	const double previous_gain = audio_region->scale_amplitude ();
	const double previous_db   = safe_region_gain_db (previous_gain);

	_session.begin_reversible_command ("normalize region");
	region->clear_changes ();
	audio_region->normalize ((float)peak_amplitude, (float)target_db);
	const double normalized_gain = audio_region->scale_amplitude ();
	const bool   updated         = (normalized_gain != previous_gain);
	_session.add_command (new PBD::StatefulDiffCommand (region));
	_session.commit_reversible_command ();

	std::ostringstream structured;
	structured << "{\"regionId\":\"" << json_escape (region->id ().to_s ()) << "\""
	           << ",\"name\":\"" << json_escape (region->name ()) << "\""
	           << ",\"resolvedVia\":\"" << json_escape (region_resolved_via) << "\""
	           << ",\"targetDb\":" << target_db
	           << ",\"peakAmplitude\":" << peak_amplitude
	           << ",\"updated\":" << (updated ? "true" : "false")
	           << ",\"gain\":{"
	           << "\"previousLinear\":" << previous_gain
	           << ",\"previousDb\":" << previous_db
	           << ",\"linear\":" << audio_region->scale_amplitude ()
	           << ",\"db\":" << safe_region_gain_db (audio_region->scale_amplitude ())
	           << ",\"polarityInverted\":" << (audio_region->scale_amplitude () < 0.0 ? "true" : "false")
	           << "}}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + (updated ? "Region normalized" : "Region unchanged") + "\"}],\"structuredContent\":" + structured.str () + "}");
}

static std::string
handle_region_split_tool (ARDOUR::Session& session, pt::ptree& root, const std::string& id)
{
	ARDOUR::Session& _session = session;

	Temporal::TempoMap::fetch ();

	const std::optional<int64_t> sample_opt = get_optional<int64_t> (root, "params.arguments.sample");
	if (sample_opt && *sample_opt < 0) {
		return jsonrpc_error (id, -32602, "Invalid sample (expected >= 0)");
	}

	samplepos_t bbt_sample      = 0;
	bool        have_bbt_target = false;
	std::string bbt_error;
	if (!parse_optional_bbt_target_sample (root, "params.arguments", bbt_sample, have_bbt_target, bbt_error)) {
		return jsonrpc_error (id, -32602, bbt_error);
	}

	if (sample_opt && have_bbt_target) {
		return jsonrpc_error (id, -32602, "Provide either sample or bar+beat, not both");
	}

	std::shared_ptr<ARDOUR::Region> region;
	std::string                     region_resolved_via;
	std::string                     region_error;
	if (!resolve_region_argument_or_selected_at_playhead (_session, root, "params.arguments", region, region_resolved_via, region_error)) {
		return jsonrpc_error (id, -32602, region_error);
	}

	const std::shared_ptr<ARDOUR::Playlist> playlist = region->playlist ();
	if (!playlist) {
		return jsonrpc_error (id, -32000, "Region has no playlist");
	}
	if (region->locked ()) {
		return jsonrpc_error (id, -32602, "Region is locked");
	}

	samplepos_t split_sample = _session.transport_sample ();
	std::string split_origin = "playhead";
	if (sample_opt) {
		split_sample = (samplepos_t)*sample_opt;
		split_origin = "sample";
	} else if (have_bbt_target) {
		split_sample = bbt_sample;
		split_origin = "bbt";
	}

	const samplepos_t region_start_sample = region->position_sample ();
	const samplepos_t region_end_sample   = region_start_sample + region->length_samples ();
	if (split_sample <= region_start_sample || split_sample >= region_end_sample) {
		std::ostringstream structured;
		structured << "{\"regionId\":\"" << json_escape (region->id ().to_s ()) << "\""
		           << ",\"name\":\"" << json_escape (region->name ()) << "\""
		           << ",\"type\":\"" << json_escape (region->data_type ().to_string ()) << "\""
		           << ",\"playlistId\":\"" << json_escape (playlist->id ().to_s ()) << "\""
		           << ",\"resolvedVia\":\"" << json_escape (region_resolved_via) << "\""
		           << ",\"split\":false"
		           << ",\"reason\":\"split point outside region body\""
		           << ",\"splitOrigin\":\"" << json_escape (split_origin) << "\""
		           << ",\"splitSample\":" << split_sample
		           << ",\"splitBbt\":" << bbt_json_at_sample (split_sample)
		           << ",\"regionStartSample\":" << region_start_sample
		           << ",\"regionEndSample\":" << region_end_sample
		           << ",\"regionStartBbt\":" << bbt_json_at_sample (region_start_sample)
		           << ",\"regionEndBbt\":" << bbt_json_at_sample (region_end_sample)
		           << ",\"createdRegions\":[]}";

		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Region unchanged\"}],\"structuredContent\":") + structured.str () + "}");
	}

	std::vector<std::string> playlist_region_ids_before;
	{
		const ARDOUR::RegionList& existing = playlist->region_list_property ().rlist ();
		for (ARDOUR::RegionList::const_iterator i = existing.begin (); i != existing.end (); ++i) {
			if (*i) {
				playlist_region_ids_before.push_back ((*i)->id ().to_s ());
			}
		}
	}

	_session.begin_reversible_command ("split region");
	playlist->clear_changes ();
	playlist->split_region (region, Temporal::timepos_t (split_sample));
	_session.add_command (new PBD::StatefulDiffCommand (playlist));
	_session.commit_reversible_command ();

	std::vector<std::shared_ptr<ARDOUR::Region>> created_regions;
	bool                                         source_region_remaining = false;
	{
		const ARDOUR::RegionList& after = playlist->region_list_property ().rlist ();
		for (ARDOUR::RegionList::const_iterator i = after.begin (); i != after.end (); ++i) {
			if (!*i) {
				continue;
			}

			const std::string candidate_id = (*i)->id ().to_s ();
			if (candidate_id == region->id ().to_s ()) {
				source_region_remaining = true;
			}

			if (std::find (playlist_region_ids_before.begin (), playlist_region_ids_before.end (), candidate_id) == playlist_region_ids_before.end ()) {
				created_regions.push_back (*i);
			}
		}
	}

	std::sort (
	    created_regions.begin (),
	    created_regions.end (),
	    [] (const std::shared_ptr<ARDOUR::Region>& a, const std::shared_ptr<ARDOUR::Region>& b) {
		    if (a->position_sample () != b->position_sample ()) {
			    return a->position_sample () < b->position_sample ();
		    }
		    return a->id () < b->id ();
	    });

	const bool         split_performed = (created_regions.size () >= 2) && !source_region_remaining;
	std::ostringstream structured;
	structured << "{\"regionId\":\"" << json_escape (region->id ().to_s ()) << "\""
	           << ",\"name\":\"" << json_escape (region->name ()) << "\""
	           << ",\"type\":\"" << json_escape (region->data_type ().to_string ()) << "\""
	           << ",\"playlistId\":\"" << json_escape (playlist->id ().to_s ()) << "\""
	           << ",\"resolvedVia\":\"" << json_escape (region_resolved_via) << "\""
	           << ",\"split\":" << (split_performed ? "true" : "false")
	           << ",\"splitOrigin\":\"" << json_escape (split_origin) << "\""
	           << ",\"splitSample\":" << split_sample
	           << ",\"splitBbt\":" << bbt_json_at_sample (split_sample)
	           << ",\"regionStartSample\":" << region_start_sample
	           << ",\"regionEndSample\":" << region_end_sample
	           << ",\"regionStartBbt\":" << bbt_json_at_sample (region_start_sample)
	           << ",\"regionEndBbt\":" << bbt_json_at_sample (region_end_sample)
	           << ",\"sourceRegionRemoved\":" << (source_region_remaining ? "false" : "true")
	           << ",\"createdRegions\":[";

	for (size_t i = 0; i < created_regions.size (); ++i) {
		if (i > 0) {
			structured << ",";
		}
		const samplepos_t created_start_sample = created_regions[i]->position_sample ();
		const samplepos_t created_end_sample   = created_start_sample + created_regions[i]->length_samples ();
		structured << "{\"regionId\":\"" << json_escape (created_regions[i]->id ().to_s ()) << "\""
		           << ",\"name\":\"" << json_escape (created_regions[i]->name ()) << "\""
		           << ",\"type\":\"" << json_escape (created_regions[i]->data_type ().to_string ()) << "\""
		           << ",\"startSample\":" << created_start_sample
		           << ",\"endSample\":" << created_end_sample
		           << ",\"lengthSamples\":" << created_regions[i]->length_samples ()
		           << ",\"startBbt\":" << bbt_json_at_sample (created_start_sample)
		           << ",\"endBbt\":" << bbt_json_at_sample (created_end_sample)
		           << "}";
	}
	structured << "]}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + (split_performed ? "Region split" : "Region unchanged") + "\"}],\"structuredContent\":" + structured.str () + "}");
}

static std::string
handle_region_resize_tool (ARDOUR::Session& session, pt::ptree& root, const std::string& id)
{
	ARDOUR::Session& _session = session;

	Temporal::TempoMap::fetch ();

	samplepos_t requested_start_boundary = 0;
	samplepos_t requested_end_boundary   = 0;
	bool        have_start_boundary      = false;
	bool        have_end_boundary        = false;
	std::string boundary_error;

	if (!parse_optional_timeline_boundary_sample (
	        root, "params.arguments",
	        "startSample", "startBar", "startBeat",
	        requested_start_boundary, have_start_boundary, boundary_error)) {
		return jsonrpc_error (id, -32602, boundary_error);
	}
	if (!parse_optional_timeline_boundary_sample (
	        root, "params.arguments",
	        "endSample", "endBar", "endBeat",
	        requested_end_boundary, have_end_boundary, boundary_error)) {
		return jsonrpc_error (id, -32602, boundary_error);
	}
	if (!have_start_boundary && !have_end_boundary) {
		return jsonrpc_error (id, -32602, "Provide at least one boundary (startSample/startBar+startBeat and/or endSample/endBar+endBeat)");
	}

	std::shared_ptr<ARDOUR::Region> region;
	std::string                     region_resolved_via;
	std::string                     region_error;
	if (!resolve_region_argument_or_selected_at_playhead (_session, root, "params.arguments", region, region_resolved_via, region_error)) {
		return jsonrpc_error (id, -32602, region_error);
	}
	const std::shared_ptr<ARDOUR::Playlist> playlist = region->playlist ();
	if (!playlist) {
		return jsonrpc_error (id, -32000, "Region has no playlist");
	}
	if (region->locked ()) {
		return jsonrpc_error (id, -32602, "Region is locked");
	}

	const samplepos_t previous_start_sample  = region->position_sample ();
	const samplepos_t previous_end_sample    = previous_start_sample + region->length_samples ();
	const samplepos_t requested_start_sample = have_start_boundary ? requested_start_boundary : previous_start_sample;
	const samplepos_t requested_end_sample   = have_end_boundary ? requested_end_boundary : previous_end_sample;

	if (requested_end_sample <= requested_start_sample) {
		return jsonrpc_error (id, -32602, "Invalid region bounds: end must be greater than start");
	}

	const ARDOUR::Trimmable::CanTrim can_trim   = region->can_trim ();
	const int                        trim_flags = (int)can_trim;
	if (requested_start_sample < previous_start_sample && !(trim_flags & ARDOUR::Trimmable::FrontTrimEarlier)) {
		return jsonrpc_error (id, -32602, "Region front cannot be trimmed earlier");
	}
	if (requested_start_sample > previous_start_sample && !(trim_flags & ARDOUR::Trimmable::FrontTrimLater)) {
		return jsonrpc_error (id, -32602, "Region front cannot be trimmed later");
	}
	if (requested_end_sample < previous_end_sample && !(trim_flags & ARDOUR::Trimmable::EndTrimEarlier)) {
		return jsonrpc_error (id, -32602, "Region end cannot be trimmed earlier");
	}
	if (requested_end_sample > previous_end_sample && !(trim_flags & ARDOUR::Trimmable::EndTrimLater)) {
		return jsonrpc_error (id, -32602, "Region end cannot be trimmed later");
	}

	if (requested_start_sample != previous_start_sample || requested_end_sample != previous_end_sample) {
		_session.begin_reversible_command ("resize region");
		region->clear_changes ();
		region->trim_to (
		    Temporal::timepos_t (requested_start_sample),
		    Temporal::timepos_t (requested_start_sample).distance (Temporal::timepos_t (requested_end_sample)));
		_session.add_command (new PBD::StatefulDiffCommand (region));
		_session.commit_reversible_command ();
	}

	const samplepos_t resized_start_sample = region->position_sample ();
	const samplepos_t resized_end_sample   = resized_start_sample + region->length_samples ();
	const bool        resized              = (resized_start_sample != previous_start_sample) || (resized_end_sample != previous_end_sample);

	std::ostringstream structured;
	structured << "{\"regionId\":\"" << json_escape (region->id ().to_s ()) << "\""
	           << ",\"name\":\"" << json_escape (region->name ()) << "\""
	           << ",\"type\":\"" << json_escape (region->data_type ().to_string ()) << "\""
	           << ",\"playlistId\":\"" << json_escape (playlist->id ().to_s ()) << "\""
	           << ",\"resolvedVia\":\"" << json_escape (region_resolved_via) << "\""
	           << ",\"resized\":" << (resized ? "true" : "false")
	           << ",\"previousStartSample\":" << previous_start_sample
	           << ",\"previousEndSample\":" << previous_end_sample
	           << ",\"requestedStartSample\":" << requested_start_sample
	           << ",\"requestedEndSample\":" << requested_end_sample
	           << ",\"startSample\":" << resized_start_sample
	           << ",\"endSample\":" << resized_end_sample
	           << ",\"lengthSamples\":" << region->length_samples ()
	           << ",\"previousStartBbt\":" << bbt_json_at_sample (previous_start_sample)
	           << ",\"previousEndBbt\":" << bbt_json_at_sample (previous_end_sample)
	           << ",\"requestedStartBbt\":" << bbt_json_at_sample (requested_start_sample)
	           << ",\"requestedEndBbt\":" << bbt_json_at_sample (requested_end_sample)
	           << ",\"startBbt\":" << bbt_json_at_sample (resized_start_sample)
	           << ",\"endBbt\":" << bbt_json_at_sample (resized_end_sample)
	           << ",\"requested\":{";

	const std::optional<int64_t> req_start_sample_opt = get_optional<int64_t> (root, "params.arguments.startSample");
	const std::optional<int>     req_start_bar_opt    = get_optional<int> (root, "params.arguments.startBar");
	const std::optional<double>  req_start_beat_opt   = get_optional<double> (root, "params.arguments.startBeat");
	const std::optional<int64_t> req_end_sample_opt   = get_optional<int64_t> (root, "params.arguments.endSample");
	const std::optional<int>     req_end_bar_opt      = get_optional<int> (root, "params.arguments.endBar");
	const std::optional<double>  req_end_beat_opt     = get_optional<double> (root, "params.arguments.endBeat");

	structured << "\"start\":{"
	           << "\"sample\":";
	if (req_start_sample_opt) {
		structured << *req_start_sample_opt;
	} else {
		structured << "null";
	}
	structured << ",\"bar\":";
	if (req_start_bar_opt) {
		structured << *req_start_bar_opt;
	} else {
		structured << "null";
	}
	structured << ",\"beat\":";
	if (req_start_beat_opt) {
		structured << *req_start_beat_opt;
	} else {
		structured << "null";
	}
	structured << "},\"end\":{"
	           << "\"sample\":";
	if (req_end_sample_opt) {
		structured << *req_end_sample_opt;
	} else {
		structured << "null";
	}
	structured << ",\"bar\":";
	if (req_end_bar_opt) {
		structured << *req_end_bar_opt;
	} else {
		structured << "null";
	}
	structured << ",\"beat\":";
	if (req_end_beat_opt) {
		structured << *req_end_beat_opt;
	} else {
		structured << "null";
	}
	structured << "}}}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + (resized ? "Region resized" : "Region unchanged") + "\"}],\"structuredContent\":" + structured.str () + "}");
}

static std::string
handle_region_copy_tool (ARDOUR::Session& session, pt::ptree& root, const std::string& id)
{
	ARDOUR::Session& _session = session;

	Temporal::TempoMap::fetch ();

	const std::string target_track_id = root.get<std::string> ("params.arguments.trackId", "");

	const std::optional<int64_t> sample_opt        = get_optional<int64_t> (root, "params.arguments.sample");
	const std::optional<int64_t> delta_samples_opt = get_optional<int64_t> (root, "params.arguments.deltaSamples");
	const std::optional<double>  delta_beats_opt   = get_optional<double> (root, "params.arguments.deltaBeats");

	samplepos_t bbt_sample      = 0;
	bool        have_bbt_target = false;
	std::string bbt_error;
	if (!parse_optional_bbt_target_sample (root, "params.arguments", bbt_sample, have_bbt_target, bbt_error)) {
		return jsonrpc_error (id, -32602, bbt_error);
	}

	if (sample_opt && *sample_opt < 0) {
		return jsonrpc_error (id, -32602, "Invalid sample (expected >= 0)");
	}
	if (delta_beats_opt && !std::isfinite (*delta_beats_opt)) {
		return jsonrpc_error (id, -32602, "Invalid deltaBeats (expected finite number)");
	}

	int target_mode_count = 0;
	if (sample_opt) {
		++target_mode_count;
	}
	if (have_bbt_target) {
		++target_mode_count;
	}
	if (delta_samples_opt) {
		++target_mode_count;
	}
	if (delta_beats_opt) {
		++target_mode_count;
	}
	if (target_mode_count != 1) {
		return jsonrpc_error (id, -32602, "Provide exactly one target: sample, bar+beat, deltaSamples, or deltaBeats");
	}

	std::shared_ptr<ARDOUR::Region> region;
	std::string                     region_resolved_via;
	std::string                     region_error;
	if (!resolve_region_argument_or_selected_at_playhead (_session, root, "params.arguments", region, region_resolved_via, region_error)) {
		return jsonrpc_error (id, -32602, region_error);
	}

	const std::shared_ptr<ARDOUR::Playlist> source_playlist = region->playlist ();
	if (!source_playlist) {
		return jsonrpc_error (id, -32000, "Region has no playlist");
	}

	std::shared_ptr<ARDOUR::Track>    target_track;
	std::shared_ptr<ARDOUR::Playlist> target_playlist = source_playlist;
	if (!target_track_id.empty ()) {
		const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id (_session, target_track_id);
		target_track                               = std::dynamic_pointer_cast<ARDOUR::Track> (route);
		if (!target_track) {
			return jsonrpc_error (id, -32602, "trackId is not a track");
		}

		target_playlist = target_track->playlist ();
		if (!target_playlist) {
			return jsonrpc_error (id, -32000, "Destination track has no playlist");
		}
	}

	if (target_playlist->data_type () != region->data_type ()) {
		return jsonrpc_error (id, -32602, "Region type does not match destination track type");
	}

	const samplepos_t source_start_sample    = region->position_sample ();
	samplepos_t       requested_start_sample = source_start_sample;
	std::string       copy_origin            = "none";

	if (sample_opt) {
		requested_start_sample = (samplepos_t)*sample_opt;
		copy_origin            = "sample";
	} else if (have_bbt_target) {
		requested_start_sample = bbt_sample;
		copy_origin            = "bbt";
	} else if (delta_samples_opt) {
		const int64_t current = (int64_t)source_start_sample;
		const int64_t delta   = *delta_samples_opt;
		if ((delta > 0 && current > (LLONG_MAX - delta)) || (delta < 0 && current < (LLONG_MIN - delta))) {
			return jsonrpc_error (id, -32602, "deltaSamples overflow");
		}
		const int64_t target = current + delta;
		if (target < 0) {
			return jsonrpc_error (id, -32602, "Resulting position is before session start");
		}
		requested_start_sample = (samplepos_t)target;
		copy_origin            = "deltaSamples";
	} else {
		const Temporal::Beats current_quarters = Temporal::TempoMap::use ()->quarters_at (Temporal::timepos_t (source_start_sample));
		const Temporal::Beats target_quarters  = current_quarters + Temporal::Beats::from_double (*delta_beats_opt);
		if (target_quarters < Temporal::Beats ()) {
			return jsonrpc_error (id, -32602, "Resulting position is before session start");
		}
		requested_start_sample = Temporal::TempoMap::use ()->sample_at (target_quarters);
		copy_origin            = "deltaBeats";
	}

	const bool                            cross_track = target_playlist != source_playlist;
	const std::shared_ptr<ARDOUR::Region> region_copy = ARDOUR::RegionFactory::create (region, true);
	if (!region_copy) {
		return jsonrpc_error (id, -32000, "Failed to create region copy");
	}

	std::vector<std::string> target_region_ids_before;
	{
		const ARDOUR::RegionList& existing = target_playlist->region_list_property ().rlist ();
		for (ARDOUR::RegionList::const_iterator i = existing.begin (); i != existing.end (); ++i) {
			target_region_ids_before.push_back ((*i)->id ().to_s ());
		}
	}

	_session.begin_reversible_command ("copy region");
	target_playlist->clear_changes ();
	target_playlist->clear_owned_changes ();
	target_playlist->add_region (region_copy, Temporal::timepos_t (requested_start_sample), 1.0, false);
	target_playlist->rdiff_and_add_command (&_session);
	_session.commit_reversible_command ();

	std::shared_ptr<ARDOUR::Region> inserted_region;
	{
		const ARDOUR::RegionList& after = target_playlist->region_list_property ().rlist ();
		for (ARDOUR::RegionList::const_iterator i = after.begin (); i != after.end (); ++i) {
			const std::string candidate_id = (*i)->id ().to_s ();
			if (std::find (target_region_ids_before.begin (), target_region_ids_before.end (), candidate_id) == target_region_ids_before.end ()) {
				inserted_region = *i;
				break;
			}
		}
	}

	const std::shared_ptr<ARDOUR::Region> copied_region       = inserted_region ? inserted_region : region_copy;
	const samplepos_t                     copied_start_sample = copied_region->position_sample ();
	const samplepos_t                     copied_end_sample   = copied_start_sample + copied_region->length_samples ();

	std::ostringstream structured;
	structured << "{\"regionId\":\"" << json_escape (copied_region->id ().to_s ()) << "\""
	           << ",\"sourceRegionId\":\"" << json_escape (region->id ().to_s ()) << "\""
	           << ",\"name\":\"" << json_escape (copied_region->name ()) << "\""
	           << ",\"type\":\"" << json_escape (copied_region->data_type ().to_string ()) << "\""
	           << ",\"playlistId\":\"" << json_escape (target_playlist->id ().to_s ()) << "\""
	           << ",\"sourcePlaylistId\":\"" << json_escape (source_playlist->id ().to_s ()) << "\""
	           << ",\"resolvedVia\":\"" << json_escape (region_resolved_via) << "\""
	           << ",\"trackId\":";
	if (target_track) {
		structured << "\"" << json_escape (target_track->id ().to_s ()) << "\"";
	} else {
		structured << "null";
	}
	structured << ",\"lengthSamples\":" << copied_region->length_samples ()
	           << ",\"crossTrack\":" << (cross_track ? "true" : "false")
	           << ",\"copyOrigin\":\"" << json_escape (copy_origin) << "\""
	           << ",\"copied\":true"
	           << ",\"sourceStartSample\":" << source_start_sample
	           << ",\"requestedStartSample\":" << requested_start_sample
	           << ",\"startSample\":" << copied_start_sample
	           << ",\"endSample\":" << copied_end_sample
	           << ",\"sourceStartBbt\":" << bbt_json_at_sample (source_start_sample)
	           << ",\"requestedStartBbt\":" << bbt_json_at_sample (requested_start_sample)
	           << ",\"startBbt\":" << bbt_json_at_sample (copied_start_sample)
	           << ",\"endBbt\":" << bbt_json_at_sample (copied_end_sample)
	           << ",\"requested\":{";
	if (sample_opt) {
		structured << "\"sample\":" << *sample_opt
		           << ",\"bar\":null,\"beat\":null,\"deltaSamples\":null,\"deltaBeats\":null";
	} else if (have_bbt_target) {
		const int    req_bar  = root.get<int> ("params.arguments.bar", 0);
		const double req_beat = root.get<double> ("params.arguments.beat", 0.0);
		structured << "\"sample\":null"
		           << ",\"bar\":" << req_bar
		           << ",\"beat\":" << req_beat
		           << ",\"deltaSamples\":null,\"deltaBeats\":null";
	} else if (delta_samples_opt) {
		structured << "\"sample\":null,\"bar\":null,\"beat\":null"
		           << ",\"deltaSamples\":" << *delta_samples_opt
		           << ",\"deltaBeats\":null";
	} else {
		structured << "\"sample\":null,\"bar\":null,\"beat\":null,\"deltaSamples\":null"
		           << ",\"deltaBeats\":" << *delta_beats_opt;
	}
	structured << ",\"trackId\":";
	if (!target_track_id.empty ()) {
		structured << "\"" << json_escape (target_track_id) << "\"";
	} else {
		structured << "null";
	}
	structured << "}}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Region copied\"}],\"structuredContent\":") + structured.str () + "}");
}

static std::string
handle_region_move_tool (ARDOUR::Session& session, pt::ptree& root, const std::string& id)
{
	ARDOUR::Session& _session = session;

	Temporal::TempoMap::fetch ();

	const std::string target_track_id = root.get<std::string> ("params.arguments.trackId", "");

	const std::optional<int64_t> sample_opt        = get_optional<int64_t> (root, "params.arguments.sample");
	const std::optional<int64_t> delta_samples_opt = get_optional<int64_t> (root, "params.arguments.deltaSamples");
	const std::optional<double>  delta_beats_opt   = get_optional<double> (root, "params.arguments.deltaBeats");

	samplepos_t bbt_sample      = 0;
	bool        have_bbt_target = false;
	std::string bbt_error;
	if (!parse_optional_bbt_target_sample (root, "params.arguments", bbt_sample, have_bbt_target, bbt_error)) {
		return jsonrpc_error (id, -32602, bbt_error);
	}

	if (sample_opt && *sample_opt < 0) {
		return jsonrpc_error (id, -32602, "Invalid sample (expected >= 0)");
	}
	if (delta_beats_opt && !std::isfinite (*delta_beats_opt)) {
		return jsonrpc_error (id, -32602, "Invalid deltaBeats (expected finite number)");
	}

	int target_mode_count = 0;
	if (sample_opt) {
		++target_mode_count;
	}
	if (have_bbt_target) {
		++target_mode_count;
	}
	if (delta_samples_opt) {
		++target_mode_count;
	}
	if (delta_beats_opt) {
		++target_mode_count;
	}
	if (target_mode_count != 1) {
		return jsonrpc_error (id, -32602, "Provide exactly one target: sample, bar+beat, deltaSamples, or deltaBeats");
	}

	std::shared_ptr<ARDOUR::Region> region;
	std::string                     region_resolved_via;
	std::string                     region_error;
	if (!resolve_region_argument_or_selected_at_playhead (_session, root, "params.arguments", region, region_resolved_via, region_error)) {
		return jsonrpc_error (id, -32602, region_error);
	}

	const std::shared_ptr<ARDOUR::Playlist> source_playlist = region->playlist ();
	if (!source_playlist) {
		return jsonrpc_error (id, -32000, "Region has no playlist");
	}
	if (!region->can_move ()) {
		return jsonrpc_error (id, -32602, "Region is locked or position-locked");
	}

	std::shared_ptr<ARDOUR::Track>    target_track;
	std::shared_ptr<ARDOUR::Playlist> target_playlist = source_playlist;
	if (!target_track_id.empty ()) {
		const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id (_session, target_track_id);
		target_track                               = std::dynamic_pointer_cast<ARDOUR::Track> (route);
		if (!target_track) {
			return jsonrpc_error (id, -32602, "trackId is not a track");
		}

		target_playlist = target_track->playlist ();
		if (!target_playlist) {
			return jsonrpc_error (id, -32000, "Destination track has no playlist");
		}
	}

	if (target_playlist->data_type () != region->data_type ()) {
		return jsonrpc_error (id, -32602, "Region type does not match destination track type");
	}

	const samplepos_t previous_start_sample  = region->position_sample ();
	samplepos_t       requested_start_sample = previous_start_sample;
	std::string       move_origin            = "none";

	if (sample_opt) {
		requested_start_sample = (samplepos_t)*sample_opt;
		move_origin            = "sample";
	} else if (have_bbt_target) {
		requested_start_sample = bbt_sample;
		move_origin            = "bbt";
	} else if (delta_samples_opt) {
		const int64_t current = (int64_t)previous_start_sample;
		const int64_t delta   = *delta_samples_opt;
		if ((delta > 0 && current > (LLONG_MAX - delta)) || (delta < 0 && current < (LLONG_MIN - delta))) {
			return jsonrpc_error (id, -32602, "deltaSamples overflow");
		}
		const int64_t target = current + delta;
		if (target < 0) {
			return jsonrpc_error (id, -32602, "Resulting position is before session start");
		}
		requested_start_sample = (samplepos_t)target;
		move_origin            = "deltaSamples";
	} else {
		const Temporal::Beats current_quarters = Temporal::TempoMap::use ()->quarters_at (Temporal::timepos_t (previous_start_sample));
		const Temporal::Beats target_quarters  = current_quarters + Temporal::Beats::from_double (*delta_beats_opt);
		if (target_quarters < Temporal::Beats ()) {
			return jsonrpc_error (id, -32602, "Resulting position is before session start");
		}
		requested_start_sample = Temporal::TempoMap::use ()->sample_at (target_quarters);
		move_origin            = "deltaBeats";
	}

	const bool                        cross_playlist_move = target_playlist != source_playlist;
	std::shared_ptr<ARDOUR::Region>   moved_region        = region;
	std::shared_ptr<ARDOUR::Playlist> moved_playlist      = source_playlist;

	if (cross_playlist_move) {
		const std::shared_ptr<ARDOUR::Region> region_copy = ARDOUR::RegionFactory::create (region, true);
		if (!region_copy) {
			return jsonrpc_error (id, -32000, "Failed to create region copy for cross-track move");
		}

		std::vector<std::string> target_region_ids_before;
		{
			const ARDOUR::RegionList& existing = target_playlist->region_list_property ().rlist ();
			for (ARDOUR::RegionList::const_iterator i = existing.begin (); i != existing.end (); ++i) {
				target_region_ids_before.push_back ((*i)->id ().to_s ());
			}
		}

		_session.begin_reversible_command ("move region");
		source_playlist->clear_changes ();
		source_playlist->clear_owned_changes ();
		target_playlist->clear_changes ();
		target_playlist->clear_owned_changes ();
		target_playlist->add_region (region_copy, Temporal::timepos_t (requested_start_sample), 1.0, false);
		source_playlist->remove_region (region);
		target_playlist->rdiff_and_add_command (&_session);
		source_playlist->rdiff_and_add_command (&_session);
		_session.commit_reversible_command ();

		std::shared_ptr<ARDOUR::Region> inserted_region;
		{
			const ARDOUR::RegionList& after = target_playlist->region_list_property ().rlist ();
			for (ARDOUR::RegionList::const_iterator i = after.begin (); i != after.end (); ++i) {
				const std::string candidate_id = (*i)->id ().to_s ();
				if (std::find (target_region_ids_before.begin (), target_region_ids_before.end (), candidate_id) == target_region_ids_before.end ()) {
					inserted_region = *i;
					break;
				}
			}
		}

		moved_region = inserted_region ? inserted_region : region_copy;
		/* RegionFactory::create() auto-generates a new name.
		 * For cross-track move we preserve the original name.
		 */
		moved_region->set_name (region->name ());
		moved_playlist = target_playlist;
	} else if (requested_start_sample != previous_start_sample) {
		_session.begin_reversible_command ("move region");
		region->clear_changes ();
		region->set_position (Temporal::timepos_t (requested_start_sample));
		_session.add_command (new PBD::StatefulDiffCommand (region));
		_session.commit_reversible_command ();
	}

	const samplepos_t moved_start_sample = moved_region->position_sample ();
	const samplepos_t moved_end_sample   = moved_start_sample + moved_region->length_samples ();
	const bool        moved              = cross_playlist_move || moved_start_sample != previous_start_sample;

	std::ostringstream structured;
	structured << "{\"regionId\":\"" << json_escape (moved_region->id ().to_s ()) << "\""
	           << ",\"sourceRegionId\":\"" << json_escape (region->id ().to_s ()) << "\""
	           << ",\"name\":\"" << json_escape (moved_region->name ()) << "\""
	           << ",\"type\":\"" << json_escape (moved_region->data_type ().to_string ()) << "\""
	           << ",\"playlistId\":\"" << json_escape (moved_playlist->id ().to_s ()) << "\""
	           << ",\"sourcePlaylistId\":\"" << json_escape (source_playlist->id ().to_s ()) << "\""
	           << ",\"resolvedVia\":\"" << json_escape (region_resolved_via) << "\""
	           << ",\"trackId\":";
	if (target_track) {
		structured << "\"" << json_escape (target_track->id ().to_s ()) << "\"";
	} else {
		structured << "null";
	}
	structured << ",\"lengthSamples\":" << moved_region->length_samples ()
	           << ",\"crossTrack\":" << (cross_playlist_move ? "true" : "false")
	           << ",\"moveOrigin\":\"" << json_escape (move_origin) << "\""
	           << ",\"moved\":" << (moved ? "true" : "false")
	           << ",\"previousStartSample\":" << previous_start_sample
	           << ",\"requestedStartSample\":" << requested_start_sample
	           << ",\"startSample\":" << moved_start_sample
	           << ",\"endSample\":" << moved_end_sample
	           << ",\"previousStartBbt\":" << bbt_json_at_sample (previous_start_sample)
	           << ",\"requestedStartBbt\":" << bbt_json_at_sample (requested_start_sample)
	           << ",\"startBbt\":" << bbt_json_at_sample (moved_start_sample)
	           << ",\"endBbt\":" << bbt_json_at_sample (moved_end_sample)
	           << ",\"requested\":{";
	if (sample_opt) {
		structured << "\"sample\":" << *sample_opt
		           << ",\"bar\":null,\"beat\":null,\"deltaSamples\":null,\"deltaBeats\":null";
	} else if (have_bbt_target) {
		const int    req_bar  = root.get<int> ("params.arguments.bar", 0);
		const double req_beat = root.get<double> ("params.arguments.beat", 0.0);
		structured << "\"sample\":null"
		           << ",\"bar\":" << req_bar
		           << ",\"beat\":" << req_beat
		           << ",\"deltaSamples\":null,\"deltaBeats\":null";
	} else if (delta_samples_opt) {
		structured << "\"sample\":null,\"bar\":null,\"beat\":null"
		           << ",\"deltaSamples\":" << *delta_samples_opt
		           << ",\"deltaBeats\":null";
	} else {
		structured << "\"sample\":null,\"bar\":null,\"beat\":null,\"deltaSamples\":null"
		           << ",\"deltaBeats\":" << *delta_beats_opt;
	}
	structured << ",\"trackId\":";
	if (!target_track_id.empty ()) {
		structured << "\"" << json_escape (target_track_id) << "\"";
	} else {
		structured << "null";
	}
	structured << "}}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + (moved ? "Region moved" : "Region unchanged") + "\"}],\"structuredContent\":" + structured.str () + "}");
}

} /* anonymous namespace */

bool
dispatch_regions_midi_tool_call (ARDOUR::Session& session, const std::string& tool_name, pt::ptree& root, const std::string& id, std::string& response)
{
	if (tool_name == "midi_region/add") {
		response = handle_midi_region_add_tool (session, root, id);
		return true;
	}
	if (tool_name == "midi_note/add") {
		response = handle_midi_note_add_tool (session, root, id);
		return true;
	}
	if (tool_name == "midi_note/list") {
		response = handle_midi_note_list_tool (session, root, id);
		return true;
	}
	if (tool_name == "midi_note/edit") {
		response = handle_midi_note_edit_tool (session, root, id);
		return true;
	}
	if (tool_name == "midi_note/import_json") {
		response = handle_midi_note_import_json_tool (session, root, id);
		return true;
	}
	if (tool_name == "midi_note/get_json") {
		response = handle_midi_note_get_json_tool (session, root, id);
		return true;
	}
	if (tool_name == "region/get_info") {
		response = handle_region_get_info_tool (session, root, id);
		return true;
	}
	if (tool_name == "region/set_gain") {
		response = handle_region_set_gain_tool (session, root, id);
		return true;
	}
	if (tool_name == "region/normalize") {
		response = handle_region_normalize_tool (session, root, id);
		return true;
	}
	if (tool_name == "region/split") {
		response = handle_region_split_tool (session, root, id);
		return true;
	}
	if (tool_name == "region/resize") {
		response = handle_region_resize_tool (session, root, id);
		return true;
	}
	if (tool_name == "region/copy") {
		response = handle_region_copy_tool (session, root, id);
		return true;
	}
	if (tool_name == "region/move") {
		response = handle_region_move_tool (session, root, id);
		return true;
	}

	return false;
}

} /* namespace mcp */
} /* namespace ArdourSurface */
