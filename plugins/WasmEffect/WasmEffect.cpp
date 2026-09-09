/*
 * WasmEffect.cpp - an LMMS Effect backed by a sandboxed WASM DSP module
 *
 * Copyright (c) 2026 LMMS WASM DSP sandbox contributors
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
	new PixmapLoader("lmms-plugin-logo"),
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

EffectControls* WasmEffect::controls()
{
	return m_controls;
}

bool WasmEffect::loadModule(const QString& path, QString* error)
{
	if (path.isEmpty())
	{
		m_worker.stop();
		m_loaded = false;
		return false;
	}

	std::string err;
	if (!m_worker.start(path.toStdString(), err))
	{
		m_loaded = false;
		if (error != nullptr)
		{
			*error = QString::fromStdString(err);
		}
		return false;
	}

	m_loaded = true;
	if (error != nullptr)
	{
		error->clear();
	}
	return true;
}

int WasmEffect::latencyFrames() const
{
	const int moduleFrames = std::max(0, m_worker.declaredLatency());
	if (moduleFrames == 0)
	{
		return 0;
	}
	// The audio thread submits block N and collects block N-1, so the host adds
	// exactly one block of pipeline delay on top of whatever the module reports.
	return moduleFrames + std::max(0, m_lastBlockFrames.load(std::memory_order_relaxed));
}

Effect::ProcessStatus WasmEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	if (frames <= 0)
	{
		return ProcessStatus::Continue;
	}
	const auto frameCount = static_cast<std::uint32_t>(frames);
	m_lastBlockFrames.store(static_cast<int>(frameCount), std::memory_order_relaxed);

	// The module's declared latency, clamped to what the dry path compensates.
	// A reload keeps the last value and a trap keeps it too, so the dry path
	// stays aligned with whatever the module was doing.
	const auto moduleLatency = static_cast<std::uint32_t>(
		std::clamp(m_worker.declaredLatency(), 0, static_cast<int>(maxCompensatedLatency)));

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
	// thread (specs/SPEC-wasm-sandbox.md section 4).
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
