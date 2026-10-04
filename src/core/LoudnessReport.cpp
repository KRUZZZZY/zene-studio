/*
 * LoudnessReport.cpp - EBU R128 loudness report for a rendered project
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
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "LoudnessReport.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>

namespace lmms
{

namespace
{

const char* const TargetDescription =
	"EBU R128: -23.0 LUFS-I (tolerance +/-0.5 LU), true peak <= -1.0 dBTP";

//! BS.1770-4's absolute gate: a block at or below -70 LUFS is not measurable
//! loudness at all (LufsMeter's own AbsoluteGateLufs is the same number; it is
//! private there, and no block at or below it is ever counted).
constexpr float AbsoluteGateLufs = -70.0f;

//! "key = value" with the key column padded, so the report reads as a table.
QString field(const char* key, const QString& value)
{
	return QStringLiteral("%1 = %2").arg(QString::fromLatin1(key), -22).arg(value);
}

QString signedNumber(float value)
{
	const QString text = QString::number(value, 'f', 2);
	return value >= 0.0f ? QStringLiteral("+") + text : text;
}

/*! Calls @a feed(offset, count) over [0, frameCount) in pieces of at most @a piece frames - once even
 *  for an empty block, as the single call it replaces did - and @a afterEach after every piece. */
template <typename Feed, typename After>
void inPieces(f_cnt_t frameCount, f_cnt_t piece, Feed feed, After afterEach)
{
	f_cnt_t done = 0;
	do
	{
		const f_cnt_t count = std::min(piece, frameCount - done);
		feed(done, count);
		afterEach();
		done += count;
	}
	while (done < frameCount);
}

//! Raises @a max to @a meter's short-term loudness when that is louder.
void noteShortTerm(const LufsMeter& meter, float* max)
{
	const float shortTerm = meter.shortTermLufs();
	if (shortTerm > *max) { *max = shortTerm; }
}

} // namespace

LoudnessReport::LoudnessReport(sample_rate_t sampleRate, ch_cnt_t channelCount)
	: m_meter(sampleRate, channelCount)
{
}

void LoudnessReport::reset()
{
	m_meter.reset();
	m_shortTermMax = LufsMeter::MinusInfinity;
}

void LoudnessReport::addBlock(const SampleFrame* frames, f_cnt_t frameCount)
{
	// The passive tap. processBlock() takes const frames and only reads them;
	// nothing here can change what the renderer writes to the file.
	//
	// Worst-case short-term loudness over the render: the meter only holds the
	// current 3 s window, the report wants the loudest one. The block is fed one
	// 100 ms sub-block's worth at most, with a read after each piece: a piece
	// closes at most one sub-block, so no window falls between two reads however
	// long the caller's block is.
	// No frames is the meter's own refusal, passed through whole (an offset into nothing is no pointer).
	const auto piece = frames != nullptr ? static_cast<f_cnt_t>(m_meter.subBlockFrames())
		: std::numeric_limits<f_cnt_t>::max();
	inPieces(frameCount, piece, [&](f_cnt_t at, f_cnt_t count) { m_meter.processBlock(frames + at, count); },
		[this] { noteShortTerm(m_meter, &m_shortTermMax); });
}


void LoudnessReport::addPlanarBlock(const sample_t* const* channels, ch_cnt_t channelCount,
	f_cnt_t frameCount)
{
	// The same passive tap as addBlock(), for the layouts processBlock() cannot
	// serve: mono fed channel-by-channel, and 5.1, whose channel weights only
	// exist on the planar path. Reads only.
	// Fed in sub-block pieces as addBlock() is. A block the meter refuses (not its
	// channel count, or a missing channel) goes through whole: it is ignored anyway.
	const bool feedable = channels != nullptr && channelCount == m_meter.channelCount()
		&& std::none_of(channels, channels + channelCount, [](const sample_t* channel) { return channel == nullptr; });
	if (!feedable)
	{
		m_meter.processPlanar(channels, channelCount, frameCount);
		noteShortTerm(m_meter, &m_shortTermMax);
		return;
	}
	const sample_t* moved[LufsMeter::MaxChannels] = {};
	const auto piece = static_cast<f_cnt_t>(m_meter.subBlockFrames());
	inPieces(frameCount, piece, [&](f_cnt_t at, f_cnt_t count) {
		for (ch_cnt_t channel = 0; channel < channelCount; ++channel) { moved[channel] = channels[channel] + at; }
		m_meter.processPlanar(moved, channelCount, count);
	}, [this] { noteShortTerm(m_meter, &m_shortTermMax); });
}

LoudnessReport::Verdict LoudnessReport::verdict() const
{
	Verdict result;
	const float integrated = m_meter.integratedLufs();
	// Silence (or a render shorter than the first gating block) measures the
	// meter's -inf sentinel: no verdict is claimed at all in that case.
	if (!std::isfinite(integrated)) { return result; }

	result.measured = true;
	result.deviationLu = integrated - EbuR128TargetLufs;
	result.loudnessOk = std::fabs(result.deviationLu) <= EbuR128ToleranceLu;
	const float truePeak = m_meter.truePeakDbtp();
	result.truePeakOk = std::isfinite(truePeak) && truePeak <= EbuR128TruePeakCeilingDbtp;
	return result;
}

QString LoudnessReport::verdictName(const Verdict& verdict)
{
	if (!verdict.measured) { return QStringLiteral("NOT MEASURED (no measurable signal)"); }
	if (verdict.pass()) { return QStringLiteral("PASS"); }
	if (!verdict.loudnessOk && !verdict.truePeakOk) { return QStringLiteral("WARN (loudness and true peak)"); }
	return verdict.loudnessOk ? QStringLiteral("WARN (true peak)")
							  : QStringLiteral("WARN (loudness)");
}

QString LoudnessReport::summary() const
{
	const Verdict result = verdict();
	QString line = QStringLiteral("Loudness: %1 LUFS-I (target %2), short-term max %3, true peak %4 dBTP -> %5")
		.arg(formatValue(integratedLufs()),
			 QString::number(EbuR128TargetLufs, 'f', 1),
			 formatValue(shortTermMaxLufs()),
			 formatValue(truePeakDbtp()),
			 verdictName(result));
	if (result.measured)
	{
		line += QStringLiteral(" [deviation %1 LU]").arg(signedNumber(result.deviationLu));
	}
	return line;
}

QString LoudnessReport::reportText(const QString& sourcePath) const
{
	const Verdict result = verdict();

	QStringList lines;
	lines << QStringLiteral("Zene Studio loudness report (EBU R128 / ITU-R BS.1770-4)");
	lines << QStringLiteral("");
	lines << field("file", sourcePath);
	lines << field("sample_rate", QStringLiteral("%1 Hz").arg(sampleRate()));
	lines << field("channels", QString::number(channelCount()));
	lines << QStringLiteral("");
	lines << field("integrated_lufs", formatValue(integratedLufs()));
	lines << field("short_term_max_lufs", formatValue(shortTermMaxLufs()));
	lines << field("momentary_lufs", formatValue(momentaryLufs()));
	lines << field("true_peak_dbtp", formatValue(truePeakDbtp()));
	lines << QStringLiteral("");
	lines << field("target", QString::fromLatin1(TargetDescription));
	lines << field("deviation_lu", result.measured ? signedNumber(result.deviationLu)
												   : QStringLiteral("n/a"));
	lines << field("streaming_reference", QStringLiteral("%1 LUFS-I (informational: platform "
														 "normalisation convention, not graded)")
											  .arg(QString::number(StreamingReferenceLufs, 'f', 1)));
	lines << field("verdict", verdictName(result));
	lines << QStringLiteral("");
	return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString LoudnessReport::sidecarPathFor(const QString& renderedFilePath)
{
	return renderedFilePath + QStringLiteral(".loudness.txt");
}

bool LoudnessReport::writeSidecar(const QString& renderedFilePath, QString* errorString) const
{
	const QString path = sidecarPathFor(renderedFilePath);
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		if (errorString != nullptr)
		{
			*errorString = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
		}
		return false;
	}

	QTextStream stream(&file);
	stream << reportText(renderedFilePath);
	stream.flush();
	const bool ok = file.error() == QFileDevice::NoError;
	if (!ok && errorString != nullptr)
	{
		*errorString = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
	}
	file.close();
	return ok;
}

QString LoudnessReport::formatValue(float value)
{
	// The meter's sentinel is -inf; printing it as a number would be a lie, and
	// a silent render must not report a plausible loudness. Anything at or below
	// BS.1770's -70 LUFS absolute gate is equally unmeasurable - a filter ringing
	// down into digital silence reads a denormal, not a level - so it reads as
	// the sentinel too.
	if (!std::isfinite(value) || value <= AbsoluteGateLufs) { return QStringLiteral("-inf"); }
	return QString::number(value, 'f', 2);
}

} // namespace lmms
