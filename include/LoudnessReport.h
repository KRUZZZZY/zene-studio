/*
 * LoudnessReport.h - EBU R128 loudness report for a rendered project
 *
 * Copyright (c) 2026 Zene Studio developers
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

#ifndef LMMS_LOUDNESS_REPORT_H
#define LMMS_LOUDNESS_REPORT_H

#include <QString>

#include "LufsMeter.h"
#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

/**
 * Loudness report for one render: the loudness work the render path needs,
 * wrapped around the measurement core of LufsMeter.
 *
 * Exists so the render path can carry a single optional object:
 *
 * @code
 *   LoudnessReport report(48000, DEFAULT_CHANNELS);
 *   ... per rendered period, before the period is written to the file:
 *   report.addBlock(buffer.data(), buffer.size());
 *   ... once the render is done:
 *   report.writeSidecar(outputPath);   // <outputPath>.loudness.txt
 * @endcode
 *
 * What it adds to LufsMeter:
 *  - the short-term maximum over the whole render (the meter itself only holds
 *    the current 3 s window; a delivery spec asks for the worst case),
 *  - a pass/warn verdict against a named published target,
 *  - the report text and its sidecar file.
 *
 * The tap is passive: addBlock() only reads the frames it is given, so the
 * bytes that reach the file are the bytes that were measured, unchanged. The
 * measured signal and the rendered signal cannot diverge.
 *
 * Target: EBU R128 (EBU Tech 3343 delivery guidance over the ITU-R BS.1770-4
 * measurement): programme loudness -23.0 LUFS-I with a tolerance of 0.5 LU, and
 * a maximum true peak of -1.0 dBTP. That is a published specification with a
 * stated tolerance, which is why it is the graded target here. The -14 LUFS-I
 * platform loudness-normalisation convention of the streaming services is
 * reported alongside it for information only: it is a platform convention, not
 * a specification, and it carries no tolerance to grade against.
 *
 * Nothing constructs one unless the render was asked for a report
 * (OutputSettings::loudnessReport()), so a render without a requested report
 * behaves exactly as before and allocates no meter.
 */
class LMMS_EXPORT LoudnessReport
{
public:
	//! EBU R128 programme loudness target, in LUFS (LUFS-I).
	static constexpr float EbuR128TargetLufs = -23.0f;
	//! EBU R128 programme loudness tolerance, in LU (EBU Tech 3343).
	static constexpr float EbuR128ToleranceLu = 0.5f;
	//! EBU R128 maximum true peak, in dBTP.
	static constexpr float EbuR128TruePeakCeilingDbtp = -1.0f;
	//! Informational only: the streaming services' -14 LUFS-I normalisation
	//! convention and their -1 dBTP ceiling. Not graded (no published tolerance).
	static constexpr float StreamingReferenceLufs = -14.0f;

	//! The graded verdict for one render.
	struct Verdict
	{
		//! False when nothing was measurable (digital silence): then no verdict
		//! is claimed at all, rather than a plausible-looking default.
		bool measured = false;
		bool loudnessOk = false;
		bool truePeakOk = false;
		//! integratedLUFS - EbuR128TargetLufs; only meaningful when measured.
		float deviationLu = 0.0f;

		bool pass() const { return measured && loudnessOk && truePeakOk; }
	};

	/**
	 * @param sampleRate   Rate of the blocks that will be fed, in Hz.
	 * @param channelCount Channels per frame (1, 2 or 6; see LufsMeter).
	 */
	explicit LoudnessReport(sample_rate_t sampleRate = 48000, ch_cnt_t channelCount = DEFAULT_CHANNELS);

	//! Drops everything measured so far; the report is as if just built.
	void reset();

	sample_rate_t sampleRate() const { return m_meter.sampleRate(); }
	ch_cnt_t channelCount() const { return m_meter.channelCount(); }

	/**
	 * Measures one rendered block. Reads \p frames only - the buffer and its
	 * contents are never modified, which is what makes this a passive tap.
	 * Allocates nothing (LufsMeter::processBlock() allocates nothing); safe to
	 * call from an audio thread.
	 */
	void addBlock(const SampleFrame* frames, f_cnt_t frameCount);

	//! Snapshot of the meter's four values at this instant.
	LufsMeter::Reading reading() const { return m_meter.read(); }
	//! Gated integrated loudness of the whole render (LUFS-I).
	float integratedLufs() const { return m_meter.integratedLufs(); }
	//! Loudness of the last 400 ms (LUFS-M).
	float momentaryLufs() const { return m_meter.momentaryLufs(); }
	//! Loudness of the last 3 s (LUFS-S).
	float shortTermLufs() const { return m_meter.shortTermLufs(); }
	//! Loudest 3 s window seen during the render (LUFS-S max).
	float shortTermMaxLufs() const { return m_shortTermMax; }
	//! Maximum true peak (dBTP) over the whole render.
	float truePeakDbtp() const { return m_meter.truePeakDbtp(); }

	//! Pass/warn against the EBU R128 target above.
	Verdict verdict() const;

	//! One line: what to tell the user the moment the render ends.
	QString summary() const;

	//! The full report, \p sourcePath being the file that was rendered.
	QString reportText(const QString& sourcePath) const;

	//! Path of the report written beside \p renderedFilePath:
	//! "/x/song.wav" -> "/x/song.wav.loudness.txt".
	static QString sidecarPathFor(const QString& renderedFilePath);

	/**
	 * Writes reportText() beside the rendered file. Returns false (and sets
	 * \p errorString, when given) if the file could not be written - a failed
	 * report is never silently swallowed.
	 */
	bool writeSidecar(const QString& renderedFilePath, QString* errorString = nullptr) const;

	//! Formats a loudness/peak value for display: "n/a" for anything that is
	//! not finite (the meter's -inf sentinel), two decimals otherwise.
	static QString formatValue(float value);
	//! Formats the verdict without a leading article: "PASS", "WARN", "NOT MEASURED".
	static QString verdictName(const Verdict& verdict);

private:
	LufsMeter m_meter;
	//! Loudest short-term window so far; -inf until 3 s have been fed.
	float m_shortTermMax = LufsMeter::MinusInfinity;
};

} // namespace lmms

#endif // LMMS_LOUDNESS_REPORT_H
