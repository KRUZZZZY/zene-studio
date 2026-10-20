/*
 * WasmEffect.cpp - an LMMS Effect backed by a sandboxed WASM DSP module
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "WasmEffect.h"

#include "AudioEngine.h"
#include "ControlWasmSupport.h"
#include "EffectChain.h"
#include "Engine.h"
#include "WasmEffectControls.h"
#include "embed.h"
#include "plugin_export.h"

#include <algorithm>
#include <cstring>

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT wasm_effect_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"WasmEffect",
	QT_TRANSLATE_NOOP("PluginBrowser",
		"Runs a sandboxed WebAssembly DSP module in your project"),
	"LMMS WASM DSP sandbox contributors",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("zene-plugin-logo"),
	nullptr,
	nullptr
};

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new WasmEffect(parent,
		static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

WasmEffect::WasmEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&wasm_effect_plugin_descriptor, parent, key),
	m_controls(new WasmEffectControls(this)),
	m_delayLine(delayLineCapacity),
	m_dryScratch(wasm::WasmWorker::maxBlockFrames)
{
}

WasmEffect::~WasmEffect()
{
	m_worker.stop();
	delete m_controls;
}

WasmEffectControls* WasmEffect::controls()
{
	return m_controls;
}

QString WasmEffect::modulePath() const
{
	return m_controls->modulePath();
}

bool WasmEffect::loadModule(const QString& path, QString* error)
{
	if (path.isEmpty())
	{
		stopWorker();
		m_loaded = false;
		m_preservedLatency.store(0, std::memory_order_relaxed);
		m_controls->setModulePath(QString());
		refreshChainLatency();
		return false;
	}

	// 0.5.0 E22: all or nothing. The module is compiled and instantiated on this thread first, with
	// the checks the worker's lane makes; one that fails them leaves the running module - and the
	// path the project saves - alone, where a failed load used to stop it and keep its path.
	QString why;
	if (!control::wasmProbeDeviceModule(path, &why))
	{
		return failLoad(error, why);
	}
	const QString previous = m_loaded ? modulePath() : QString();
	// The latency the running module declares. It is kept for the whole
	// operation: while the replacement loads, and while the old module is put
	// back, the effect has to keep compensating what it compensated before, or
	// the chain's PDC alignment jumps for as long as the load takes.
	const int runningLatency = m_worker.declaredLatency();
	if (startWorker(path, &why, runningLatency))
	{
		if (error != nullptr) { error->clear(); }
		return true;
	}
	// The module is sound but the worker could not host it (the pool is full, or the load timed
	// out): put back the module that was running rather than leave a device with nothing loaded.
	if (!previous.isEmpty()) { startWorker(previous, nullptr, runningLatency); }
	return failLoad(error, why);
}

bool WasmEffect::failLoad(QString* error, const QString& why)
{
	if (error != nullptr) { *error = why; }
	return false;
}

bool WasmEffect::startWorker(const QString& path, QString* error, int preservedLatency)
{
	// The worker forgets the declared latency when it is stopped, so the
	// latency of the module this one replaces is kept here until the load
	// resolved.
	m_preservedLatency.store(preservedLatency, std::memory_order_relaxed);
	stopWorker();
	std::string err;
	m_loaded = m_worker.start(path.toStdString(), err);
	if (m_loaded) { m_controls->setModulePath(path); }
	else if (error != nullptr) { *error = QString::fromStdString(err); }
	// The module that runs now declares its own latency, so the preserved one
	// is only needed while a replacement is in flight. A load that failed
	// keeps it: the old module is put back, and the effect has to keep
	// compensating its latency until that is done.
	if (m_loaded) { m_preservedLatency.store(0, std::memory_order_relaxed); }
	refreshChainLatency();
	return m_loaded;
}

void WasmEffect::stopWorker()
{
	// WasmWorker::stop() drains the block queues the audio thread pushes to (submit) and pops from
	// (collect); single-producer/single-consumer queues, so the drain must not run while
	// processImpl() does. The engine's model-change guard waits for the period to finish and holds
	// the next one off for the drain only - not for the compile, which start() waits on with the
	// worker in Loading, where processImpl() passes audio through without touching the queues.
	AudioEngine* engine = Engine::audioEngine();
	if (engine != nullptr) { engine->requestChangeInModel(); }
	m_worker.stop();
	if (engine != nullptr) { engine->doneChangeInModel(); }
}

void WasmEffect::refreshChainLatency()
{
	// A module's declared latency enters the PDC graph through the owning
	// chain's cache, so every path out of a load must refresh it: waiting for
	// the next unrelated refresh left the graph misaligned by one host period
	// (#605 audit C1b). Control thread only.
	if (EffectChain* chain = effectChain())
	{
		chain->refreshLatency();
	}
}

int WasmEffect::compensatedModuleLatency() const
{
	// One bound for the report and the dry path (round 6): the report used the module's raw value, which
	// could pass what the dry path delays by, and overflow an int once the block was added to INT_MAX.
	//
	// The module's declared latency while a module runs. A replacement that is
	// still loading has none yet, so the latency the module it replaces
	// declared is kept: the report and the dry path must not lose the
	// compensation while the new module compiles - the chain's PDC cache is
	// only refreshed once the load resolved.
	const int declared = m_worker.declaredLatency();
	const int frames = declared > 0
		? declared
		: m_preservedLatency.load(std::memory_order_relaxed);
	return std::clamp(frames, 0, static_cast<int>(maxCompensatedLatency));
}

int WasmEffect::latencyFrames() const
{
	const int moduleFrames = compensatedModuleLatency();
	if (moduleFrames == 0)
	{
		return 0;
	}
	// The audio thread submits block N and collects block N-1, so the host adds
	// exactly one block of pipeline delay on top of whatever the module reports.
	// That block is the engine's period: it is fixed when the engine is built,
	// so this report is correct before the first processImpl() runs and never
	// depends on how many blocks the audio thread has seen. Reading the last
	// block's size instead made the PDC graph stale by exactly one period until
	// an unrelated refresh (#605 audit C1b).
	const int blockFrames = Engine::audioEngine() != nullptr
		? static_cast<int>(Engine::audioEngine()->framesPerPeriod())
		: 0;
	return moduleFrames + std::max(0, blockFrames);
}

Effect::ProcessStatus WasmEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	if (frames <= 0)
	{
		return ProcessStatus::Continue;
	}
	const auto frameCount = static_cast<std::uint32_t>(frames);

	// The module's declared latency, clamped to what the dry path compensates.
	// A reload keeps the last value and a trap keeps it too, so the dry path
	// stays aligned with whatever the module was doing.
	const auto moduleLatency = static_cast<std::uint32_t>(compensatedModuleLatency());

	if (!m_worker.isReady())
	{
		// No module (or one that is loading, failed, or was quarantined after a
		// trap): audio passes through. When the module declared latency the dry
		// path is delayed by the same amount so the effect's latency does not
		// jump when the module dies.
		if (moduleLatency == 0)
		{
			return ProcessStatus::Continue;
		}
		pushDry(buf, frameCount);
		pullDelayed(buf, frameCount, dryReadOffset(moduleLatency, frameCount));
		return ProcessStatus::Continue;
	}

	if (frameCount > wasm::WasmWorker::maxBlockFrames)
	{
		// Larger than the sandbox can carry; leave the audio untouched.
		return ProcessStatus::Continue;
	}

	float sampleRate = 44100.0f;
	if (Engine::audioEngine() != nullptr)
	{
		sampleRate = static_cast<float>(Engine::audioEngine()->outputSampleRate());
	}

	// Keep the dry block: collect() replaces buf with the module's output for
	// the *previous* block and submit() must be handed the dry signal.
	std::memcpy(m_dryScratch.data(), buf, frameCount * sizeof(SampleFrame));
	if (moduleLatency > 0)
	{
		// Feed the compensation line while the module runs, so a later trap can
		// fall back to an aligned dry signal.
		pushDry(m_dryScratch.data(), frameCount);
	}

	// Collect before submitting so the result popped can never be the block
	// about to be handed over: the pipeline is deterministically one block.
	const bool haveOutput = m_worker.collect(buf, frameCount);
	if (!haveOutput && moduleLatency > 0)
	{
		// The module owes us the previous block; deliver the same position from
		// the dry history so the effect's latency stays put.
		pullDelayed(buf, frameCount, dryReadOffset(moduleLatency, frameCount));
	}

	// submit() notices a sample-rate change and asks the worker thread to
	// re-instantiate the module; nothing but atomics happens on the audio
	// thread (docs/specs/SPEC-wasm-sandbox.md section 4).
	m_worker.submit(m_dryScratch.data(), frameCount, sampleRate);
	return ProcessStatus::Continue;
}

std::uint64_t WasmEffect::dryReadOffset(std::uint32_t moduleLatency,
	std::uint32_t frames) const
{
	// pushDry() has already advanced the write cursor by one block, so reading
	// the dry signal of the block `moduleLatency + frames` ago costs an extra
	// block: latency + 2 * frames.
	return static_cast<std::uint64_t>(moduleLatency) + 2 * frames;
}

void WasmEffect::pushDry(const SampleFrame* buf, std::uint32_t frames)
{
	for (std::uint32_t i = 0; i < frames; ++i)
	{
		m_delayLine[(m_delayWrite + i) % delayLineCapacity] = buf[i];
	}
	m_delayWrite += frames;
}

void WasmEffect::pullDelayed(SampleFrame* buf, std::uint32_t frames, std::uint64_t delay)
{
	for (std::uint32_t i = 0; i < frames; ++i)
	{
		buf[i] = m_delayLine[(m_delayWrite + i - delay) % delayLineCapacity];
	}
}

} // namespace lmms
