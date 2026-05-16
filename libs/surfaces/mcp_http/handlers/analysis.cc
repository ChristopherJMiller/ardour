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
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "ardour/audioregion.h"
#include "ardour/dsp_filter.h"
#include "ardour/ebur128_analysis.h"
#include "ardour/region.h"
#include "ardour/session.h"
#include "ardour/types.h"

#include "handlers/analysis.h"
#include "handlers/common.h"

namespace ArdourSurface
{
namespace mcp
{
namespace
{

std::shared_ptr<ARDOUR::AudioRegion>
resolve_audio_region (ARDOUR::Session& /*session*/, const pt::ptree& root, std::string& error)
{
	const std::string region_id = root.get<std::string> ("params.arguments.regionId", "");
	if (region_id.empty ()) {
		error = "Missing regionId";
		return {};
	}

	std::shared_ptr<ARDOUR::Region> region = region_by_mcp_id (region_id);
	if (!region) {
		error = "regionId not found";
		return {};
	}

	std::shared_ptr<ARDOUR::AudioRegion> ar = std::dynamic_pointer_cast<ARDOUR::AudioRegion> (region);
	if (!ar) {
		error = "Region is not an audio region";
		return {};
	}

	return ar;
}

std::string
handle_analysis_peaks_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	std::string                          err;
	std::shared_ptr<ARDOUR::AudioRegion> region = resolve_audio_region (session, root, err);
	if (!region) {
		return jsonrpc_error (id, -32602, err);
	}

	const int64_t npeaks_in = root.get<int64_t> ("params.arguments.npeaks", 256);
	if (npeaks_in < 1 || npeaks_in > 16384) {
		return jsonrpc_error (id, -32602, "Invalid npeaks (expected 1..16384)");
	}
	const ARDOUR::samplecnt_t npeaks = (ARDOUR::samplecnt_t)npeaks_in;
	const uint32_t    chan   = root.get<uint32_t> ("params.arguments.channel", 0);

	const ARDOUR::samplecnt_t length = region->length_samples ();
	if (length <= 0) {
		return jsonrpc_error (id, -32000, "Region has zero length");
	}

	const double                samples_per_pixel = (double)length / (double)npeaks;
	std::vector<ARDOUR::PeakData> peaks (npeaks);
	const ARDOUR::samplecnt_t got = region->read_peaks (peaks.data (), npeaks, 0, length, chan, samples_per_pixel);
	if (got <= 0) {
		return jsonrpc_error (id, -32000, "Failed to read peaks");
	}

	std::ostringstream ss;
	ss << "{\"regionId\":\"" << json_escape (region->id ().to_s ()) << "\""
	   << ",\"name\":\"" << json_escape (region->name ()) << "\""
	   << ",\"channel\":" << chan
	   << ",\"npeaks\":" << got
	   << ",\"samplesPerPeak\":" << samples_per_pixel
	   << ",\"lengthSamples\":" << length
	   << ",\"min\":[";
	for (ARDOUR::samplecnt_t i = 0; i < got; ++i) {
		if (i > 0) ss << ",";
		ss << peaks[i].min;
	}
	ss << "],\"max\":[";
	for (ARDOUR::samplecnt_t i = 0; i < got; ++i) {
		if (i > 0) ss << ",";
		ss << peaks[i].max;
	}
	ss << "]}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Peaks read\"}],\"structuredContent\":") + ss.str () + "}");
}

std::string
handle_analysis_rms_lufs_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	std::string                          err;
	std::shared_ptr<ARDOUR::AudioRegion> region = resolve_audio_region (session, root, err);
	if (!region) {
		return jsonrpc_error (id, -32602, err);
	}

	ARDOUR::EBUr128Analysis analyser ((float)session.nominal_sample_rate ());
	const int               rc = analyser.run (region.get ());
	if (rc != 0) {
		return jsonrpc_error (id, -32000, "EBU R128 analysis failed");
	}

	std::ostringstream ss;
	ss << "{\"regionId\":\"" << json_escape (region->id ().to_s ()) << "\""
	   << ",\"name\":\"" << json_escape (region->name ()) << "\""
	   << ",\"integratedLufs\":" << analyser.loudness ()
	   << ",\"loudnessRangeLu\":" << analyser.loudness_range ()
	   << ",\"lengthSamples\":" << region->length_samples ()
	   << "}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"EBU R128 analysis complete\"}],\"structuredContent\":") + ss.str () + "}");
}

std::string
handle_analysis_spectrum_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	std::string                          err;
	std::shared_ptr<ARDOUR::AudioRegion> region = resolve_audio_region (session, root, err);
	if (!region) {
		return jsonrpc_error (id, -32602, err);
	}

	const int64_t window_in = root.get<int64_t> ("params.arguments.windowSize", 4096);
	if (window_in < 64 || window_in > 65536 || (window_in & (window_in - 1)) != 0) {
		return jsonrpc_error (id, -32602, "Invalid windowSize (expected power-of-two between 64 and 65536)");
	}
	const uint32_t window_size = (uint32_t)window_in;
	const uint32_t chan        = root.get<uint32_t> ("params.arguments.channel", 0);
	const int64_t  offset_in   = root.get<int64_t> ("params.arguments.offsetSamples", 0);
	if (offset_in < 0) {
		return jsonrpc_error (id, -32602, "Invalid offsetSamples (must be >= 0)");
	}

	const ARDOUR::samplecnt_t length = region->length_samples ();
	if ((ARDOUR::samplecnt_t)offset_in + (ARDOUR::samplecnt_t)window_size > length) {
		return jsonrpc_error (id, -32602, "Window extends past end of region");
	}

	std::vector<ARDOUR::Sample> buf (window_size);
	const ARDOUR::samplecnt_t           got = region->read (buf.data (), (ARDOUR::samplepos_t)offset_in, window_size, chan);
	if (got <= 0) {
		return jsonrpc_error (id, -32000, "Failed to read region audio");
	}

	const double              rate = (double)session.nominal_sample_rate ();
	ARDOUR::DSP::FFTSpectrum  fft (window_size, rate);
	fft.set_data_hann (buf.data (), got);
	fft.execute ();

	const uint32_t nbins = window_size / 2;

	std::ostringstream ss;
	ss << "{\"regionId\":\"" << json_escape (region->id ().to_s ()) << "\""
	   << ",\"channel\":" << chan
	   << ",\"windowSize\":" << window_size
	   << ",\"offsetSamples\":" << offset_in
	   << ",\"sampleRate\":" << session.nominal_sample_rate ()
	   << ",\"freqHz\":[";
	for (uint32_t b = 0; b < nbins; ++b) {
		if (b > 0) ss << ",";
		ss << fft.freq_at_bin (b);
	}
	ss << "],\"powerDb\":[";
	for (uint32_t b = 0; b < nbins; ++b) {
		if (b > 0) ss << ",";
		float p = fft.power_at_bin (b, 1.f, false);
		if (!std::isfinite (p)) {
			p = -193.f;
		}
		ss << p;
	}
	ss << "]}";

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Spectrum analysis complete\"}],\"structuredContent\":") + ss.str () + "}");
}

} /* anonymous namespace */

bool
dispatch_analysis_tool_call (ARDOUR::Session& session, const std::string& tool_name, const pt::ptree& root, const std::string& id, std::string& response)
{
	if (tool_name == "analysis/peaks") {
		response = handle_analysis_peaks_tool (session, root, id);
		return true;
	}
	if (tool_name == "analysis/rms_lufs") {
		response = handle_analysis_rms_lufs_tool (session, root, id);
		return true;
	}
	if (tool_name == "analysis/spectrum") {
		response = handle_analysis_spectrum_tool (session, root, id);
		return true;
	}

	return false;
}

} /* namespace mcp */
} /* namespace ArdourSurface */
