/*
 * ControlCommandsMeterFile.cpp - the FILE half of the `meter.*` command group
 *                                (SPEC A11-A16): meter.measure_file.
 *
 * Feature row 24 of docs/FEATURE-LIST-0.3.0.md ("LUFS / loudness metering"). The
 * LIVE half (meter.get_state / meter.arm, the passive tap on the master) is in
 * src/core/ControlCommandsMeter.cpp; this file is the document reader behind
 * meter.measure_file, and it is its own translation unit because the group's file
 * crossed this fork's 500-line file ratchet - the seam the automation, warp,
 * rack, comp, vca and mastering groups use.
 *
 * WHAT IT MEASURES, and with what. A rendered file, now, from its own bytes: the
 * same gated integrated loudness, momentary, short-term, loudest-short-term and
 * true-peak values the live tap publishes, plus the EBU R 128 verdict the render
 * path already grades against. The measurement is the merged `LoudnessReport`
 * (which owns the merged `LufsMeter`) - the exact object the render path feeds
 * while it writes a file - so this group forks no DSP and adds no second meter.
 *
 * THE CHANNEL RULE, stated because it is where a plausible wrong number comes
 * from. `LufsMeter::processBlock()` measures channels 0 and 1 of INTERLEAVED
 * frames, so a mono file pushed through that path would be measured as one
 * channel plus one silent channel and read 3 LU quiet, and a 5.1 file would lose
 * four channels entirely. So: one and two channels go through the interleaved
 * path (correct as it stands), and anything wider is de-interleaved into planar
 * chunks and fed through `LoudnessReport::addPlanarBlock()`, which is
 * `LufsMeter::processPlanar()` - the entry point whose BS.1770-4 Table 3 weights
 * (1.0 for L/R/C, 1.41 for Ls/Rs, LFE unmetered) only exist for that shape.
 * Above six channels there is no meter that can weigh the layout, so the file is
 * REFUSED, typed, rather than measured with a guess.
 *
 * Copyright (c) 2026 Zene Studio contributors
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <algorithm>
#include <memory>
#include <vector>

#include <sndfile.h>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "ControlMeterSupport.h"

#include "ControlMasteringSupport.h"
#include "ControlRegistry.h"
#include "LoudnessReport.h"
#include "LufsMeter.h"

namespace lmms
{

namespace control
{

namespace
{

//! Frames per read. Large enough that a 5-minute render is a few thousand reads,
//! small enough that the working set stays a few dozen KB.
constexpr int FileChunkFrames = 4096;

/*! The layouts the file half measures. LufsMeter's Table 3 weights are defined
 *  for 1, 2 and 6 channels and any other count is measured with every channel at
 *  1.0 (documented in its header), but a file WIDER than the meter's storage
 *  cannot be fed at all - which is a refusal, not a measurement.
 */
constexpr int MaxMeasuredChannels = LufsMeter::MaxChannels;

} // namespace

QJsonValue meterReadingJson(float value)
{
	if (!std::isfinite(value)) { return QJsonValue(QJsonValue::Null); }
	return QJsonValue(static_cast<double>(value));
}

QJsonObject meterReadingsJson(float integratedLufs, float momentaryLufs, float shortTermLufs,
	float shortTermMaxLufs, float truePeakDbtp)
{
	QJsonObject out;
	out.insert(QStringLiteral("integrated_lufs"), meterReadingJson(integratedLufs));
	out.insert(QStringLiteral("momentary_lufs"), meterReadingJson(momentaryLufs));
	out.insert(QStringLiteral("short_term_lufs"), meterReadingJson(shortTermLufs));
	out.insert(QStringLiteral("short_term_max_lufs"), meterReadingJson(shortTermMaxLufs));
	out.insert(QStringLiteral("true_peak_dbtp"), meterReadingJson(truePeakDbtp));
	return out;
}

QJsonObject meterTargetJson()
{
	QJsonObject out;
	out.insert(QStringLiteral("standard"), QStringLiteral("EBU R 128 (EBU Tech 3343)"));
	out.insert(QStringLiteral("integrated_lufs"),
		static_cast<double>(LoudnessReport::EbuR128TargetLufs));
	out.insert(QStringLiteral("tolerance_lu"),
		static_cast<double>(LoudnessReport::EbuR128ToleranceLu));
	out.insert(QStringLiteral("true_peak_ceiling_dbtp"),
		static_cast<double>(LoudnessReport::EbuR128TruePeakCeilingDbtp));
	out.insert(QStringLiteral("streaming_reference_lufs"),
		static_cast<double>(LoudnessReport::StreamingReferenceLufs));
	return out;
}

ControlResult meterMeasureFile(const QString& path)
{
	if (path.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'path' is required: the absolute path of the rendered file to measure"));
	}
	if (!path.startsWith(QLatin1Char('/')))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'path' must be an absolute path"));
	}
	const QFileInfo fileInfo(path);
	if (!fileInfo.exists() || !fileInfo.isFile())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("no such file: %1").arg(path));
	}

	SF_INFO audioInfo{};
	SNDFILE* file = sf_open(QFile::encodeName(path).constData(), SFM_READ, &audioInfo);
	if (file == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("cannot read %1: %2").arg(path, QString::fromUtf8(sf_strerror(nullptr))));
	}
	// The handle closes on every path out of this function, including the
	// refusals below.
	const std::unique_ptr<SNDFILE, int (*)(SNDFILE*)> handle(file, sf_close);

	if (audioInfo.channels < 1 || audioInfo.channels > MaxMeasuredChannels)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 has %2 channels; this meter measures 1 to %3 (the BS.1770-4 Table 3 "
				"weights are defined for mono, stereo and 5.1)").arg(path)
				.arg(audioInfo.channels).arg(MaxMeasuredChannels));
	}
	if (audioInfo.frames <= 0 || audioInfo.samplerate <= 0)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 holds no frames to measure").arg(path));
	}

	const auto channels = static_cast<ch_cnt_t>(audioInfo.channels);
	LoudnessReport report(static_cast<sample_rate_t>(audioInfo.samplerate), channels);

	// Two fixed-size buffers, reused for every chunk: the interleaved one for the
	// one- and two-channel path (SampleFrame IS two floats, which is libsndfile's
	// own layout) and the planar one for the wider layouts.
	std::vector<SampleFrame> frames(FileChunkFrames);
	std::vector<sample_t> raw(static_cast<std::size_t>(FileChunkFrames) * channels);
	std::vector<std::vector<sample_t>> planar(channels, std::vector<sample_t>(FileChunkFrames));
	std::vector<const sample_t*> planarPointers(channels);

	sf_count_t remaining = audioInfo.frames;
	while (remaining > 0)
	{
		const sf_count_t want = std::min<sf_count_t>(remaining, FileChunkFrames);
		const sf_count_t got = channels <= DEFAULT_CHANNELS
			? sf_readf_float(file, reinterpret_cast<float*>(frames.data()), want)
			: sf_readf_float(file, raw.data(), want);
		if (got <= 0) { break; }

		if (channels <= DEFAULT_CHANNELS)
		{
			report.addBlock(frames.data(), static_cast<f_cnt_t>(got));
		}
		else
		{
			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				for (sf_count_t frame = 0; frame < got; ++frame)
				{
					planar[channel][static_cast<std::size_t>(frame)] =
						raw[static_cast<std::size_t>(frame) * channels + channel];
				}
				planarPointers[channel] = planar[channel].data();
			}
			report.addPlanarBlock(planarPointers.data(), channels, static_cast<f_cnt_t>(got));
		}
		remaining -= got;
	}

	const LoudnessReport::Verdict verdict = report.verdict();

	QJsonObject result;
	result.insert(QStringLiteral("path"), path);
	result.insert(QStringLiteral("bytes"), static_cast<qint64>(fileInfo.size()));
	// The file's live facts, hashed by the tree's one hashing helper - so the
	// sha256 here is the same measurement render.render and freeze.* report.
	result.insert(QStringLiteral("source"), masteringFileFacts({path}));
	result.insert(QStringLiteral("sample_rate"), audioInfo.samplerate);
	result.insert(QStringLiteral("channels"), audioInfo.channels);
	result.insert(QStringLiteral("frames"), static_cast<qint64>(audioInfo.frames));
	result.insert(QStringLiteral("duration_seconds"),
		static_cast<double>(audioInfo.frames) / static_cast<double>(audioInfo.samplerate));
	// libsndfile's own container code (SF_FORMAT_WAV & SF_FORMAT_TYPEMASK, ...).
	// Reported as the library's value rather than as a name this file would have
	// to keep a second table for: an agent that wants "wav" reads it here as the
	// code libsndfile's own header documents.
	result.insert(QStringLiteral("format_major"),
		static_cast<int>(audioInfo.format & SF_FORMAT_TYPEMASK));

	QJsonObject readings = meterReadingsJson(report.integratedLufs(), report.momentaryLufs(),
		report.shortTermLufs(), report.shortTermMaxLufs(), report.truePeakDbtp());
	for (auto it = readings.constBegin(); it != readings.constEnd(); ++it)
	{
		result.insert(it.key(), it.value());
	}
	result.insert(QStringLiteral("measured"), verdict.measured);
	result.insert(QStringLiteral("deviation_lu"),
		verdict.measured ? QJsonValue(static_cast<double>(verdict.deviationLu))
			: QJsonValue(QJsonValue::Null));
	result.insert(QStringLiteral("loudness_pass"), verdict.loudnessOk);
	result.insert(QStringLiteral("true_peak_pass"), verdict.truePeakOk);
	result.insert(QStringLiteral("verdict"), LoudnessReport::verdictName(verdict));
	result.insert(QStringLiteral("target"), meterTargetJson());
	result.insert(QStringLiteral("note"),
		QStringLiteral("Measured from the file's own bytes, now, by the same BS.1770-4 meter the "
			"render path uses (LoudnessReport, which owns a LufsMeter); the file is only READ - its "
			"sha256 is in `source`, so a caller can check the file is byte-identical before and "
			"after measuring it. One and two channels are measured interleaved, wider layouts "
			"planar, so mono is not read 3 LU quiet and a 5.1 file keeps its channel weights. A "
			"silent file reports null for every reading and `measured` false with verdict NOT "
			"MEASURED rather than a plausible-looking number. `verdict` grades the integrated value "
			"against EBU R 128's -23.0 LUFS-I +/- 0.5 LU and the true peak against its -1.0 dBTP "
			"ceiling; those are the ONLY tolerances this release grades against (see `target`)."));
	return ControlResult::success(result);
}

} // namespace control

} // namespace lmms
