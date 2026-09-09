/*
 * WasmEffect.h - an LMMS Effect backed by a sandboxed WASM DSP module
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

#ifndef LMMS_WASM_EFFECT_H
#define LMMS_WASM_EFFECT_H

#include "Effect.h"
#include "WasmEffectControls.h"
#include "WasmWorker.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace lmms
{

//! LMMS effect that hosts a WASM DSP module in the crash-isolated sandbox.
/*!
 * The module never runs on the audio thread: processImpl() hands interleaved
 * blocks to a WasmWorker, which owns the wasmtime instance on its own thread
 * (specs/SPEC-wasm-sandbox.md section 4). This class adds the plugin-side
 * pieces on top of the sandbox: registration, the control dialog, project
 * save/load of the module path and parameter values, sample-rate handling and
 * latency compensation.
 */
class WasmEffect : public Effect
{
	Q_OBJECT
public:
	WasmEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~WasmEffect() override;

	//! Covariant return: callers holding a WasmEffect get the concrete controls
	//! (parameter models, module path) without casting.
	WasmEffectControls* controls() override;

	//! Path of the loaded module (empty when none); persisted in the project.
	QString modulePath() const;

	//! Control thread: load (or replace) the sandboxed module. The module is
	//! compiled and instantiated on the worker thread; this blocks until it is
	//! ready (or failed). Returns false and sets \p error on failure.
	bool loadModule(const QString& path, QString* error = nullptr);

	bool isModuleLoaded() const { return m_loaded; }
	bool isModuleCorrupted() const { return m_worker.isCorrupted(); }
	QString lastError() const { return QString::fromStdString(m_worker.lastError()); }

	//! Control thread: push a parameter value straight to the module (exposed
	//! via host_get_param()). Does not touch the parameter models, so it is
	//! not saved with the project - use controls()->paramModel(i)->setValue()
	//! for a change that must persist.
	void setModuleParam(std::uint32_t index, float value) { m_worker.setParam(index, value); }

	//! Frames of latency the loaded module reports (0 when it reports none).
	int moduleLatency() const { return m_worker.declaredLatency(); }

	//! Total frames this effect adds to the signal path: the module's declared
	//! latency plus the host's one-block collect-after-submit pipeline. When
	//! the module declares latency the dry path is delayed to match, so this
	//! figure stays constant even if the module traps and is quarantined.
	int latencyFrames() const override;

	wasm::WasmWorker& worker() { return m_worker; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

private:
	//! Audio thread: append \p frames of dry input to the compensation line.
	void pushDry(const SampleFrame* buf, std::uint32_t frames);
	//! Audio thread: read back \p frames from \p delay frames in the past.
	void pullDelayed(SampleFrame* buf, std::uint32_t frames, std::uint64_t delay);
	//! Read offset for the compensated dry signal (see the definition).
	std::uint64_t dryReadOffset(std::uint32_t moduleLatency, std::uint32_t frames) const;

	WasmEffectControls* m_controls = nullptr;
	wasm::WasmWorker m_worker;
	bool m_loaded = false;

	//! Largest module latency the dry path compensates for.
	static constexpr std::uint32_t maxCompensatedLatency = wasm::WasmWorker::maxBlockFrames;
	//! Delay line capacity: the compensated latency plus two blocks (the block
	//! being written and the block being read back).
	static constexpr std::uint32_t delayLineCapacity =
		maxCompensatedLatency + 2 * wasm::WasmWorker::maxBlockFrames;

	//! Dry-path history. Allocated in the constructor (control thread) and only
	//! ever touched by the audio thread afterwards: no allocation, no locks.
	std::vector<SampleFrame> m_delayLine;
	//! One block of untouched dry input, kept so the module can be fed the dry
	//! signal after collect() has overwritten the host buffer. Sized in the
	//! constructor; audio thread only.
	std::vector<SampleFrame> m_dryScratch;
	//! Absolute write position, primed to the capacity so the zero-filled
	//! history can be read back without underflow before any audio arrives.
	std::uint64_t m_delayWrite = delayLineCapacity;
	//! Frames of the last block seen by the audio thread; the pipeline delay is
	//! one such block. Atomic because latencyFrames() is read from the GUI.
	std::atomic<int> m_lastBlockFrames{0};
};

} // namespace lmms

#endif // LMMS_WASM_EFFECT_H
