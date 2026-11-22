/*
 * MixerBusFreeze.cpp - a mixer bus's frozen take (Q050-124); see include/MixerBusFreeze.h
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

#include "MixerBusFreeze.h"

#include "BounceInPlace.h"

#include <algorithm>
#include <atomic>
#include <cmath>

#include <QCoreApplication>
#include <QDir>
#include <QDomElement>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>

#include "AudioEngine.h"
#include "Engine.h"
#include "Mixer.h"
#include "SampleBuffer.h"
#include "Song.h"

namespace lmms::busfreeze
{

namespace
{

//! Where the period being mixed starts (render thread writes before the mixer jobs; workers read after the job
//! queue's release/acquire, the m_muted rule). Relaxed atomics, so the read is never a data race.
std::atomic<bool> s_songPlaying{false};
std::atomic<f_cnt_t> s_periodStart{0};

//! Render child only: the armed channel's capture buffer, one period, allocated by armCapture (control thread).
std::vector<SampleFrame> s_captureStorage;
SampleFrame* s_captureFrames = nullptr;

#ifdef LMMS_TESTING
RenderHook s_renderHook = nullptr;
#endif

const QString FrozenAttribute = QStringLiteral("frozenAudio");

bool tempoMapInForce()
{
	const TempoMap& map = Engine::getSong()->tempoMap().map();
	return map.active() && !map.empty();
}

//! Why @a bus cannot be frozen, or empty.
QString busRefusal(MixerChannel* bus)
{
	if (bus == nullptr) { return QStringLiteral("no mixer channel to freeze"); }
	if (!bus->isBus()) { return QStringLiteral("only a bus can be frozen; freeze.track freezes a track"); }
	if (bus->m_freeze.isFrozen()) { return QStringLiteral("the bus is already frozen; unfreeze it first"); }
	if (bus->m_receives.empty()) { return QStringLiteral("nothing is routed to the bus: there is nothing to render"); }
	if (Engine::getSong() == nullptr) { return QStringLiteral("no engine to render with"); }
	if (tempoMapInForce())
	{
		return QStringLiteral("a bus freeze plays on the project's one tempo; turn the tempo map off first");
	}
	return QString();
}

QString refusal(MixerChannel* bus, const QString& outPath)
{
	if (const QString problem = busRefusal(bus); !problem.isEmpty()) { return problem; }
	if (outPath.isEmpty() || !BounceInPlace::isAbsoluteOutputPath(outPath))
	{
		return QStringLiteral("the output path must be absolute");
	}
	const QString project = Engine::getSong()->projectFileName();
	if (!project.isEmpty() && BounceInPlace::namesTheSameFile(project, outPath))
	{
		return QStringLiteral("the output path is the open project file: a freeze would replace the session");
	}
	return QString();
}

//! The shipped CLI render, with the bus captured in place of the master (the render.render child, and for the
//! reason BounceInPlace's runCliRender gives: a plain blocking wait, no nested event loop).
bool runChild(const BounceInPlace::PreparedRender& prepared, int channelId, QString* error)
{
	QProcess renderer;
	renderer.setStandardOutputFile(QProcess::nullDevice());
	renderer.setStandardErrorFile(QProcess::nullDevice());
	renderer.start(QCoreApplication::applicationFilePath(),
		{QStringLiteral("render"), prepared.projectPath, QStringLiteral("-o"), prepared.stagedPath,
			QStringLiteral("-f"), QStringLiteral("wav"), QStringLiteral("-s"), QString::number(prepared.sampleRate),
			QStringLiteral("--float"), QStringLiteral("--capture-channel"), QString::number(channelId)});
	const bool finished = renderer.waitForStarted(30000) && renderer.waitForFinished(600000);
	if (!finished) { renderer.kill(); }
	const int exitCode = finished ? renderer.exitCode() : -1;
	if (!finished || exitCode != 0)
	{
		*error = QStringLiteral("the bus render failed (exit %1)").arg(exitCode);
		return false;
	}
	return true;
}

bool renderStaged(MixerChannel* bus, const BounceInPlace::PreparedRender& prepared, QString* error)
{
#ifdef LMMS_TESTING
	if (s_renderHook != nullptr) { return s_renderHook(bus, prepared.stagedPath, prepared.sampleRate, error); }
#endif
	return runChild(prepared, bus->id(), error);
}

BounceInPlace::Result measure(const BounceInPlace::PreparedRender& prepared)
{
	BounceInPlace::Result result;
	result.sampleRate = prepared.sampleRate;
	result.frames = BounceInPlace::wavFrameCount(prepared.stagedPath);
	result.bytes = QFileInfo(prepared.stagedPath).size();
	result.sha256 = BounceInPlace::sha256OfFile(prepared.stagedPath);
	result.path = prepared.outputPath;
	result.range.start = 0;
	result.range.end = static_cast<tick_t>(std::llround(
		static_cast<double>(std::max<qint64>(result.frames, 0)) / Engine::framesPerTick(prepared.sampleRate)));
	if (result.frames <= 0 || result.sha256.isEmpty())
	{
		return BounceInPlace::Result::failure(QStringLiteral("the bus render produced no audio"));
	}
	result.ok = true;
	return result;
}

} // namespace

// ---------------------------------------------------------------------------
// audio thread
// ---------------------------------------------------------------------------

void setPeriod(bool songPlaying, f_cnt_t startFrame) noexcept
{
	s_songPlaying.store(songPlaying, std::memory_order_relaxed);
	s_periodStart.store(startFrame, std::memory_order_relaxed);
}

bool substitute(MixerChannel& channel, f_cnt_t frames) noexcept
{
	const BusFreeze& freeze = channel.m_freeze;
	if (freeze.frames == nullptr || !s_songPlaying.load(std::memory_order_relaxed)) { return false; }
	if (freeze.rate != Engine::audioEngine()->outputSampleRate()) { return false; }
	const f_cnt_t start = s_periodStart.load(std::memory_order_relaxed);
	if (start >= freeze.frameCount) { return false; }

	const f_cnt_t available = std::min(frames, freeze.frameCount - start);
	std::copy_n(freeze.frames + start, available, channel.m_buffer);
	if (available < frames) { zeroSampleFrames(channel.m_buffer + available, frames - available); }
	// The take is audio this period: the bus's quiet flags must not let a device downstream sleep through it.
	channel.m_bus.quietChannels().reset(0);
	channel.m_bus.quietChannels().reset(1);
	channel.m_hasInput = true;
	return true;
}

void capture(MixerChannel& channel, f_cnt_t frames) noexcept
{
	if (channel.m_freeze.capture == nullptr) { return; }
	std::copy_n(channel.m_buffer, frames, channel.m_freeze.capture);
}

void replaceOutput(SampleFrame* out, f_cnt_t frames) noexcept
{
	if (s_captureFrames == nullptr) { return; }
	std::copy_n(s_captureFrames, frames, out);
	// A period in which the channel did not run must render silence, not repeat this block.
	zeroSampleFrames(s_captureFrames, frames);
}

// ---------------------------------------------------------------------------
// control thread
// ---------------------------------------------------------------------------

void install(MixerChannel& channel, const QString& path, std::shared_ptr<const SampleBuffer> take)
{
	std::shared_ptr<const SampleBuffer> previous;
	{
		const auto renderLock = Engine::audioEngine()->requestChangesGuard();
		previous = std::move(channel.m_freeze.take);
		channel.m_freeze.path = path;
		channel.m_freeze.take = std::move(take);
		const SampleBuffer* buffer = channel.m_freeze.take.get();
		channel.m_freeze.frames = buffer != nullptr ? buffer->data() : nullptr;
		channel.m_freeze.frameCount = buffer != nullptr ? static_cast<f_cnt_t>(buffer->size()) : 0;
		channel.m_freeze.rate = buffer != nullptr ? buffer->sampleRate() : 0;
	}
	// `previous` is released here, on the control thread, after the period lock is gone.
}

void clear(MixerChannel& channel)
{
	install(channel, QString(), nullptr);
}

std::shared_ptr<const SampleBuffer> loadTake(const QString& path, QString* error)
{
	std::shared_ptr<const SampleBuffer> take = path.isEmpty() ? nullptr : SampleBuffer::fromFile(path);
	if (take == nullptr || take->empty())
	{
		if (error != nullptr) { *error = QStringLiteral("'%1' could not be opened as audio").arg(path); }
		return nullptr;
	}
	return take;
}

void save(const MixerChannel& channel, QDomElement& element)
{
	if (channel.m_freeze.isFrozen()) { element.setAttribute(FrozenAttribute, channel.m_freeze.path); }
}

void load(MixerChannel& channel, const QDomElement& element)
{
	// Reset on absence: a journal restore or an older project without the attribute is an unfrozen bus.
	const QString path = element.attribute(FrozenAttribute);
	if (path.isEmpty() || !channel.isBus())
	{
		if (channel.m_freeze.isFrozen()) { clear(channel); }
		return;
	}
	QString error;
	std::shared_ptr<const SampleBuffer> take = loadTake(path, &error);
	if (take == nullptr && Engine::getSong() != nullptr)
	{
		// The bus stays frozen (the file is what the user asked for) but plays its live inputs; the load report
		// names the ways out, as a track's missing take does.
		Engine::getSong()->collectError(QCoreApplication::translate("MixerBusFreeze",
			"Frozen bus take not found: %1 (the bus plays its live inputs; freeze.unfreeze_bus, undoable, clears it)")
			.arg(path));
	}
	install(channel, path, std::move(take));
}

bool fileMissing(const MixerChannel& channel)
{
	return channel.m_freeze.isFrozen() && !QFileInfo(channel.m_freeze.path).isFile();
}

#ifdef LMMS_TESTING
void setRenderHookForTest(RenderHook render)
{
	s_renderHook = render;
}
#endif

bool armCapture(Mixer& mixer, int channelId, QString* error)
{
	for (int i = 0; i < static_cast<int>(mixer.numChannels()); ++i)
	{
		MixerChannel* channel = mixer.mixerChannel(i);
		if (channel == nullptr || channel->id() != channelId) { continue; }
		const auto renderLock = Engine::audioEngine()->requestChangesGuard();
		s_captureStorage.assign(Engine::audioEngine()->framesPerPeriod(), SampleFrame{});
		s_captureFrames = s_captureStorage.data();
		channel->m_freeze.capture = s_captureFrames;
		// A muted bus is still rendered as itself (a bounce of a muted track is its audio), and a muted master
		// would end the period before the bus is mixed.
		channel->m_muteModel.setValue(false);
		mixer.mixerChannel(0)->m_muteModel.setValue(false);
		return true;
	}
	*error = QStringLiteral("no mixer channel ch-%1 to capture").arg(channelId);
	return false;
}

void disarmCapture(Mixer& mixer)
{
	const auto renderLock = Engine::audioEngine()->requestChangesGuard();
	for (int i = 0; i < static_cast<int>(mixer.numChannels()); ++i) { mixer.mixerChannel(i)->m_freeze.capture = nullptr; }
	s_captureFrames = nullptr;
}

} // namespace lmms::busfreeze

namespace lmms
{

using namespace busfreeze;

BounceInPlace::Result BounceInPlace::renderBus(MixerChannel* bus, const QString& outPath)
{
	if (const QString problem = refusal(bus, outPath); !problem.isEmpty())
	{
		return BounceInPlace::Result::failure(problem);
	}
	const QString outDir = QFileInfo(outPath).absolutePath();
	if (!QDir().mkpath(outDir)) { return BounceInPlace::Result::failure(QStringLiteral("could not create '%1'").arg(outDir)); }

	BounceInPlace::PreparedRender prepared;
	prepared.scratch = std::make_shared<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/zene-busfreeze-XXXXXX"));
	if (!prepared.scratch->isValid())
	{
		return BounceInPlace::Result::failure(QStringLiteral("could not create a temporary directory for the render"));
	}
	prepared.outputPath = outPath;
	prepared.sampleRate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	prepared.projectPath = prepared.scratch->filePath(QStringLiteral("session.mmp"));
	prepared.stagedPath = outPath + QStringLiteral(".part.")
		+ QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".wav");

	QString error;
	if (!Engine::getSong()->saveProjectFile(prepared.projectPath))
	{
		error = QStringLiteral("could not serialise the session for rendering");
	}
	else if (renderStaged(bus, prepared, &error))
	{
		BounceInPlace::Result result = measure(prepared);
		if (result.ok && BounceInPlace::installPreparedOutput(&prepared, &error))
		{
			BounceInPlace::commitPreparedOutput(&prepared);
			BounceInPlace::discardPreparedRender(&prepared);
			return result;
		}
		if (!result.ok) { error = result.error; }
	}
	BounceInPlace::discardPreparedRender(&prepared);
	return BounceInPlace::Result::failure(error);
}

} // namespace lmms
