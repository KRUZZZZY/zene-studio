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
#include "EffectControls.h"

#include "WasmWorker.h"

#include <QString>

#include <cstdint>

namespace lmms::wasm
{

class WasmEffect;

//! v0 has no generated UI and no persistent parameters; the worker's
//! parameter array is driven programmatically (and by tests) instead.
class WasmEffectControls : public EffectControls
{
	Q_OBJECT
public:
	explicit WasmEffectControls(WasmEffect* effect);
	~WasmEffectControls() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	QString nodeName() const override { return "WasmEffectControls"; }
	gui::EffectControlDialog* createView() override { return nullptr; }
	int controlCount() override { return 0; }
};

//! Effect that renders a WASM DSP module. All module work happens on
//! WasmWorker's thread; processImpl() only submits the dry block and collects
//! the previously rendered block, so the audio thread never enters wasmtime.
//!
//! Output latency is one block (the collect-after-submit pipeline).
class WasmEffect : public Effect
{
	Q_OBJECT
public:
	WasmEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~WasmEffect() override;

	EffectControls* controls() override { return &m_controls; }

	//! Control thread: load (or replace) the module under test.
	bool loadModule(const QString& path, QString* error = nullptr);
	bool isModuleLoaded() const { return m_loaded; }
	bool isModuleCorrupted() const { return m_worker.isCorrupted(); }
	QString lastError() const { return QString::fromStdString(m_worker.lastError()); }

	//! Control thread: drive a module parameter exposed via host_get_param().
	void setModuleParam(std::uint32_t index, float value) { m_worker.setParam(index, value); }

	//! Test/inspection hooks.
	WasmWorker& worker() { return m_worker; }
	const WasmWorker& worker() const { return m_worker; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

private:
	WasmEffectControls m_controls;
	WasmWorker m_worker;
	bool m_loaded = false;
};

} // namespace lmms::wasm

#endif // LMMS_WASM_EFFECT_H
