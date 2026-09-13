/*
 * OutputSettings.h - Stores the settings for file rendering
 *
 * Copyright (c) 2008-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2017 Michael Gregorius <michael.gregorius.git/at/arcor[dot]de>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_OUTPUT_SETTINGS_H
#define LMMS_OUTPUT_SETTINGS_H

#include "ExportRenderSettings.h"
#include "LmmsTypes.h"
#include "SrcQuality.h"

namespace lmms
{


class OutputSettings
{
public:
	enum class BitDepth
	{
		Depth16Bit,
		Depth24Bit,
		Depth32Bit,
		Count
	};

	enum class StereoMode
	{
		Mono,
		Stereo,
		JointStereo,
		Count
	};

public:
	OutputSettings(sample_rate_t sampleRate, bitrate_t bitRate, BitDepth bitDepth, StereoMode stereoMode)
		: m_sampleRate(sampleRate)
		, m_bitRate(bitRate)
		, m_bitDepth(bitDepth)
		, m_stereoMode(stereoMode)
		, m_compressionLevel(0.625) // 5/8
		/*! The two render-scope choices start from the current selection
		 *  (ExportRenderSettings), so a caller that does not mention them - the
		 *  export dialog, the CLI's own construction - renders with the choice
		 *  an agent made through export.set_dither / export.set_src_quality, or
		 *  with the defaults (dither off, Linear) when nobody has chosen.
		 *  ProjectRenderer publishes this value back for the render's duration.
		 */
		, m_dither(ExportRenderSettings::dither())
		, m_srcQuality(ExportRenderSettings::srcQuality())
	{
	}

	OutputSettings(sample_rate_t sampleRate, bitrate_t bitRate, BitDepth bitDepth)
		: OutputSettings(sampleRate, bitRate, bitDepth, StereoMode::Stereo)
	{
	}

	sample_rate_t getSampleRate() const { return m_sampleRate; }
	void setSampleRate(sample_rate_t sampleRate) { m_sampleRate = sampleRate; }

	bitrate_t bitrate() const { return m_bitRate; }
	void setBitrate(bitrate_t bitrate) { m_bitRate = bitrate; }

	BitDepth getBitDepth() const { return m_bitDepth; }
	void setBitDepth(BitDepth bitDepth) { m_bitDepth = bitDepth; }

	StereoMode getStereoMode() const { return m_stereoMode; }
	void setStereoMode(StereoMode stereoMode) { m_stereoMode = stereoMode; }


	double getCompressionLevel() const{ return m_compressionLevel; }
	void setCompressionLevel(double level){
		// legal range is 0.0 to 1.0.
		m_compressionLevel = level;
	}

	/**
	 * Whether this render should also produce an EBU R128 loudness report
	 * (see LoudnessReport, include/LoudnessReport.h): measured from the blocks
	 * that are written to the file, reported on the export dialog and in a
	 * ".loudness.txt" sidecar beside the render.
	 *
	 * Off by default, and measure-only: turning it on changes the report, never
	 * the rendered audio.
	 */
	bool loudnessReport() const { return m_loudnessReport; }
	void setLoudnessReport(bool enabled) { m_loudnessReport = enabled; }

	/*! TPDF dither in the integer export path (include/ExportDither.h).
	 *
	 *  **Off by default, and that is load-bearing.** This release's
	 *  reproducibility claim is that 7 of the 9 bundled projects render
	 *  byte-identically (projects/lmms-fl-research/START-HERE.md §3.0); an
	 *  always-on dither would falsify it. A `[add dithering]` row does not
	 *  exist, so nothing turns it on implicitly: only the `--dither` flag or
	 *  `export.set_dither` over the control socket does, and a dithered render
	 *  is still reproducible because the dither's generator is seeded from a
	 *  constant.
	 *
	 *  It dithers 16- and 24-bit output. 32-bit float has no fixed quantisation
	 *  step, so it is left alone.
	 */
	bool dither() const { return m_dither; }
	void setDither(bool enabled) { m_dither = enabled; }

	/*! Which sample-rate-conversion converter the render's resampler uses
	 *  (include/SrcQuality.h). `Linear` is the converter the engine has always
	 *  used, so it is the default and a render that does not ask for more is
	 *  byte-for-byte what it was.
	 */
	SrcQuality srcQuality() const { return m_srcQuality; }
	void setSrcQuality(SrcQuality quality) { m_srcQuality = quality; }

private:
	sample_rate_t m_sampleRate;
	bitrate_t m_bitRate;
	BitDepth m_bitDepth;
	StereoMode m_stereoMode;
	double m_compressionLevel;
	bool m_loudnessReport = false;
	bool m_dither = false;
	SrcQuality m_srcQuality = SrcQuality::Linear;
};


} // namespace lmms

#endif // LMMS_OUTPUT_SETTINGS_H
