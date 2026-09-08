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
#include "Song.h"

namespace lmms::wasm
{

namespace
{

//! v0: the sandbox is exercised through WasmEffect directly and is not yet
//! registered as a loadable plugin, so the descriptor lives here.
Plugin::Descriptor wasmEffectDescriptor = {
	"wasm_effect",
	"WASM Effect",
	"Sandboxed WASM DSP module",
	"LMMS WASM DSP sandbox contributors",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr,
};

} // namespace

WasmEffectControls::WasmEffectControls(WasmEffect* effect) :
	EffectControls(effect)
{
}

void WasmEffectControls::saveSettings(QDomDocument&, QDomElement&)
{
	// v0: no persistent parameters yet; the module path is not serialized.
}

void WasmEffectControls::loadSettings(const QDomElement&)
{
}

WasmEffect::WasmEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&wasmEffectDescriptor, parent, key),
	m_controls(this)
{
}

WasmEffect::~WasmEffect()
{
	m_worker.stop();
}

bool WasmEffect::loadModule(const QString& path, QString* error)
{
	std::string message;
	if (!m_worker.start(path.toStdString(), message))
	{
		if (error != nullptr)
		{
			*error = QString::fromStdString(message);
		}
		m_loaded = false;
		return false;
	}
	m_loaded = true;
	return true;
}

Effect::ProcessStatus WasmEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	if (!m_worker.isReady())
	{
		// Not loaded yet, still loading, failed to load or quarantined after a
		// trap: pass audio through untouched.
		return ProcessStatus::Continue;
	}
	const auto frameCount = static_cast<std::uint32_t>(frames);
	float sampleRate = 44100.0f;
	if (Engine::audioEngine() != nullptr)
	{
		sampleRate = static_cast<float>(Engine::audioEngine()->outputSampleRate());
	}
	if (Engine::getSong() != nullptr && Engine::getSong()->isPlaying())
	{
		m_worker.setTransportState(abi::transportPlaying);
	}
	else
	{
		m_worker.setTransportState(abi::transportStopped);
	}

	// Submit the dry input first, then overwrite the buffer with the block the
	// worker rendered last time around. Both calls are allocation-free,
	// lock-free and syscall-free; the module itself runs on the worker thread.
	m_worker.submit(buf, frameCount, sampleRate);
	m_worker.collect(buf, frameCount);
	return ProcessStatus::Continue;
}

} // namespace lmms::wasm
