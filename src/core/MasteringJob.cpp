/*
 * MasteringJob.cpp - render-once/branch-many mastering candidate generation
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

#include "MasteringJob.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

#include <sndfile.h>

#include "MasteringChain.h"
#include "PatternStore.h"
#include "RenderManager.h"
#include "Song.h"
#include "lmms_constants.h"

namespace lmms
{

namespace
{

//! The one render is kept beside the candidates so "the render happened once"
//! is a file a reader can look at, not a claim in a comment.
constexpr auto SourceRenderName = "00_source-mix.wav";

//! The chain's true-peak trim lands the measured peak on the ceiling; this is
//! the float slack on the pass comparison, not a tolerance on the standard.
constexpr float TruePeakSlackDb = 0.05f;

//! Lane-defined level-consistency flag: the loudest 3 s window more than this
//! many LU above the programme target. No standard publishes a short-term
//! limit for music, so this is reported as a flag and never as a verdict.
constexpr float ShortTermFlagLu = 5.0f;

//! RAII around a libsndfile handle: every early return in the I/O helpers has
//! to close the file, and a missed close would leak an open WAV descriptor.
class SndFileHandle
{
public:
	SndFileHandle(const QString& path, SF_INFO& info, int mode) :
		m_file(sf_open(QFile::encodeName(path).constData(), mode, &info))
	{
	}

	~SndFileHandle()
	{
		if (m_file != nullptr)
		{
			sf_close(m_file);
		}
	}

	SndFileHandle(const SndFileHandle&) = delete;
	SndFileHandle& operator=(const SndFileHandle&) = delete;

	bool isOpen() const { return m_file != nullptr; }
	SNDFILE* get() const { return m_file; }

private:
	SNDFILE* m_file;
};

//! libsndfile format tag for an LMMS output bit depth; mirrors AudioFileWave.
int waveFormatFor(OutputSettings::BitDepth depth)
{
	switch (depth)
	{
	case OutputSettings::BitDepth::Depth32Bit:
		return SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	case OutputSettings::BitDepth::Depth24Bit:
		return SF_FORMAT_WAV | SF_FORMAT_PCM_24;
	case OutputSettings::BitDepth::Depth16Bit:
	default:
		return SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	}
}

//! Strips the characters that would escape the output directory, so a candidate
//! name from a caller cannot write outside it.
QString sanitiseName(const QString& name)
{
	QString cleaned;
	for (const QChar ch : name)
	{
		if (ch.isLetterOrNumber() || ch == '-' || ch == '_' || ch == '.')
		{
			cleaned.append(ch);
		}
	}
	cleaned = cleaned.trimmed();
	return cleaned.isEmpty() ? QStringLiteral("candidate") : cleaned;
}

} // namespace

MasteringChainSettings MasteringCandidate::chainSettings() const
{
	MasteringChainSettings settings;
	settings.targetLufs = target.integratedLufs;
	settings.ceilingDbtp = target.ceilingDbtp;
	settings.dynamicsEnabled = dynamicsEnabled;
	settings.dynamicsThresholdDb = dynamicsThresholdDb;
	settings.dynamicsRatio = dynamicsRatio;
	settings.dynamicsAttackMs = dynamicsAttackMs;
	settings.dynamicsReleaseMs = dynamicsReleaseMs;
	settings.dynamicsKneeDb = dynamicsKneeDb;
	return settings;
}

MasteringJob::MasteringJob(const OutputSettings& outputSettings,
	ProjectRenderer::ExportFileFormat format, const QString& outputDirectory,
	const QVector<MasteringCandidate>& candidates) :
	m_outputSettings(outputSettings),
	m_format(format),
	m_outputDirectory(outputDirectory),
	m_candidates(candidates)
{
}

QVector<MasteringCandidate> MasteringJob::defaultCandidates()
{
	MasteringTarget streaming;
	streaming.name = QStringLiteral("streaming-14");
	streaming.integratedLufs = -14.0f;
	streaming.toleranceLu = 1.0f;
	streaming.ceilingDbtp = -1.0f;
	streaming.standard = QStringLiteral(
		"Streaming normalisation: -14 LUFS-I (the figure Spotify documents) with the "
		"-1 dBTP ceiling its delivery guidance gives. No service publishes a tolerance, "
		"so this target states +/-1.0 LU.");

	MasteringTarget streamingQuiet = streaming;
	streamingQuiet.name = QStringLiteral("streaming-16");
	streamingQuiet.integratedLufs = -16.0f;
	streamingQuiet.standard = QStringLiteral(
		"A second streaming point at -16 LUFS-I, exactly 2 LU from streaming-14: the "
		"candidate set varies target loudness on purpose, and this is the quiet end of "
		"it. Ceiling -1 dBTP.");

	MasteringTarget tight = streaming;
	tight.name = QStringLiteral("streaming-14-ceiling-2");
	tight.ceilingDbtp = -2.0f;
	tight.standard = QStringLiteral(
		"A lane-chosen variant of streaming-14 that only moves the true-peak ceiling to "
		"-2 dBTP, so the ceiling is visible as a separate axis of the candidate set. "
		"The -2 dB headroom figure is this lane's choice, not a service's.");

	MasteringTarget broadcast;
	broadcast.name = QStringLiteral("ebu-r128");
	broadcast.integratedLufs = -23.0f;
	broadcast.toleranceLu = 0.5f;
	broadcast.ceilingDbtp = -1.0f;
	broadcast.standard = QStringLiteral(
		"EBU R 128 programme loudness: -23 LUFS-I +/- 0.5 LU with a maximum true peak of "
		"-1 dBTP, measured per ITU-R BS.1770-4.");

	const auto make = [](const QString& name, const MasteringTarget& target, bool dynamics)
	{
		MasteringCandidate candidate;
		candidate.name = name;
		candidate.target = target;
		candidate.dynamicsEnabled = dynamics;
		return candidate;
	};

	QVector<MasteringCandidate> candidates;
	candidates.append(make(QStringLiteral("streaming-14"), streaming, false));
	candidates.append(make(QStringLiteral("streaming-16"), streamingQuiet, false));
	candidates.append(make(QStringLiteral("streaming-14-ceiling-2"), tight, false));
	// The dynamics candidate rides on the streaming-16 target rather than on
	// streaming-14. That is not cosmetic: at -14 LUFS this programme's peaks sit
	// on the limiter, the limiter decides the output, and a glue compressor in
	// front of it changes nothing the meter can see (measured; see
	// docs/AUTO-MASTERING.md). At -16 LUFS the limiter stays out of the way and
	// the dynamics stage is the thing that shapes the peaks.
	candidates.append(make(QStringLiteral("streaming-16-dynamics"), streamingQuiet, true));
	candidates.append(make(QStringLiteral("ebu-r128"), broadcast, false));
	return candidates;
}

QString MasteringJob::pathForCandidate(int index, const QString& name) const
{
	const QString file = QStringLiteral("%1_%2.wav")
		.arg(index, 2, 10, QLatin1Char('0'))
		.arg(sanitiseName(name));
	return QDir(m_outputDirectory).filePath(file);
}

bool MasteringJob::renderSourceMix(QString* error)
{
	if (!QDir().mkpath(m_outputDirectory))
	{
		*error = QStringLiteral("cannot create output directory %1").arg(m_outputDirectory);
		return false;
	}

	m_sourceRenderFile = QDir(m_outputDirectory).filePath(QString::fromLatin1(SourceRenderName));

	// The one render, and the only ProjectRenderer this job starts. 32-bit float:
	// the render is an intermediate for the chains, not a deliverable, so the
	// candidates must not start from a 16-bit quantisation of the engine's output.
	OutputSettings sourceSettings(m_outputSettings.getSampleRate(), m_outputSettings.bitrate(),
		OutputSettings::BitDepth::Depth32Bit, OutputSettings::StereoMode::Stereo);
	RenderManager manager(sourceSettings, ProjectRenderer::ExportFileFormat::Wave, m_sourceRenderFile);

	// RenderManager hands the render off to ProjectRenderer's own thread and
	// reports completion through finished(); this job owns the wait, so it runs
	// its own event loop rather than quitting the application.
	QEventLoop loop;
	QObject::connect(&manager, &RenderManager::finished, &loop, &QEventLoop::quit);
	const int rendersBefore = ProjectRenderer::renderCount();
	manager.renderProject();
	loop.exec();
	// Counted by the render machinery itself, never by this job's bookkeeping.
	m_renderCount += ProjectRenderer::renderCount() - rendersBefore;

	if (!QFileInfo::exists(m_sourceRenderFile))
	{
		*error = QStringLiteral("the project render produced no file at %1").arg(m_sourceRenderFile);
		return false;
	}
	return true;
}

bool MasteringJob::loadSourceMix(QString* error)
{
	SF_INFO info{};
	SndFileHandle file(m_sourceRenderFile, info, SFM_READ);
	if (!file.isOpen())
	{
		*error = QStringLiteral("cannot read %1: %2").arg(m_sourceRenderFile)
			.arg(QString::fromUtf8(sf_strerror(nullptr)));
		return false;
	}
	if (info.channels != DEFAULT_CHANNELS)
	{
		*error = QStringLiteral("%1 has %2 channels; mastering is stereo only")
			.arg(m_sourceRenderFile).arg(info.channels);
		return false;
	}

	const auto frames = static_cast<std::size_t>(std::max<sf_count_t>(0, info.frames));
	std::vector<float> interleaved(frames * DEFAULT_CHANNELS);
	const sf_count_t read = sf_readf_float(file.get(), interleaved.data(), info.frames);
	if (read < 0)
	{
		*error = QStringLiteral("reading %1 failed").arg(m_sourceRenderFile);
		return false;
	}

	m_sampleRate = static_cast<sample_rate_t>(info.samplerate);
	const auto loaded = static_cast<std::size_t>(read);
	m_source.resize(loaded);
	for (std::size_t i = 0; i < loaded; ++i)
	{
		m_source[i].setLeft(interleaved[i * DEFAULT_CHANNELS]);
		m_source[i].setRight(interleaved[i * DEFAULT_CHANNELS + 1]);
	}
	return true;
}

bool MasteringJob::writeCandidate(const MasteringCandidate&, const std::vector<SampleFrame>& frames,
	const QString& path, QString* error) const
{
	SF_INFO info{};
	info.samplerate = static_cast<int>(m_sampleRate);
	info.channels = DEFAULT_CHANNELS;
	info.format = waveFormatFor(m_outputSettings.getBitDepth());
	info.sections = 1;

	SndFileHandle file(path, info, SFM_WRITE);
	if (!file.isOpen())
	{
		*error = QStringLiteral("cannot write %1: %2").arg(path)
			.arg(QString::fromUtf8(sf_strerror(nullptr)));
		return false;
	}
	sf_command(file.get(), SFC_SET_CLIPPING, nullptr, SF_TRUE);

	std::vector<float> interleaved(frames.size() * DEFAULT_CHANNELS);
	for (std::size_t i = 0; i < frames.size(); ++i)
	{
		interleaved[i * DEFAULT_CHANNELS] = frames[i].left();
		interleaved[i * DEFAULT_CHANNELS + 1] = frames[i].right();
	}
	if (sf_writef_float(file.get(), interleaved.data(), static_cast<sf_count_t>(frames.size()))
		!= static_cast<sf_count_t>(frames.size()))
	{
		*error = QStringLiteral("writing %1 failed").arg(path);
		return false;
	}
	return true;
}

MasteringCandidateReport MasteringJob::measureCandidate(const MasteringCandidate& candidate,
	const std::vector<SampleFrame>& frames) const
{
	MasteringCandidateReport report;
	report.name = candidate.name;
	report.targetName = candidate.target.name;
	report.targetLufs = candidate.target.integratedLufs;
	report.toleranceLu = candidate.target.toleranceLu;
	report.ceilingDbtp = candidate.target.ceilingDbtp;
	report.metrics = MasteringChain::measure(frames, m_sampleRate);

	if (std::isfinite(report.metrics.integratedLufs))
	{
		report.lufsResidual = report.metrics.integratedLufs - candidate.target.integratedLufs;
		report.lufsPass = std::fabs(report.lufsResidual) <= candidate.target.toleranceLu;
		report.shortTermWarn = std::isfinite(report.metrics.shortTermMaxLufs)
			&& report.metrics.shortTermMaxLufs > candidate.target.integratedLufs + ShortTermFlagLu;
	}
	if (std::isfinite(report.metrics.truePeakDbtp))
	{
		report.truePeakPass = report.metrics.truePeakDbtp
			<= candidate.target.ceilingDbtp + TruePeakSlackDb;
	}
	return report;
}

bool MasteringJob::run(QString* error)
{
	QString sink;
	if (error == nullptr)
	{
		error = &sink;
	}
	if (m_candidates.isEmpty())
	{
		*error = QStringLiteral("no mastering candidates were requested");
		return false;
	}
	if (m_format != ProjectRenderer::ExportFileFormat::Wave)
	{
		*error = QStringLiteral("wave-1 mastering writes wav candidates only");
		return false;
	}

	m_reports.clear();
	if (!renderSourceMix(error) || !loadSourceMix(error))
	{
		return false;
	}
	m_sourceMetrics = MasteringChain::measure(m_source, m_sampleRate);

	for (int i = 0; i < m_candidates.size(); ++i)
	{
		const MasteringCandidate& candidate = m_candidates.at(i);
		// The branch: a private copy of the one render, mastered by this
		// candidate's chain. No second render happens here or anywhere below.
		std::vector<SampleFrame> frames = m_source;
		MasteringChain(candidate.chainSettings()).process(frames, m_sampleRate);

		MasteringCandidateReport report = measureCandidate(candidate, frames);
		if (!std::isfinite(report.metrics.integratedLufs)
			|| !std::isfinite(report.metrics.truePeakDbtp))
		{
			*error = QStringLiteral("candidate %1 produced no measurable signal")
				.arg(candidate.name);
			return false;
		}
		report.outputFile = pathForCandidate(i + 1, candidate.name);
		if (!writeCandidate(candidate, frames, report.outputFile, error))
		{
			return false;
		}
		m_reports.append(report);
	}
	return true;
}

} // namespace lmms
