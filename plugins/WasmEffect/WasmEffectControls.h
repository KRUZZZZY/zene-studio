/*
 * WasmEffectControls.h - controls model for the WasmEffect plugin
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

#ifndef LMMS_WASM_EFFECT_CONTROLS_H
#define LMMS_WASM_EFFECT_CONTROLS_H

#include "AutomatableModel.h"
#include "EffectControls.h"
#include "WasmEffectControlDialog.h"

#include <QString>

#include <array>
#include <cstdint>

namespace lmms
{

class WasmEffect;

//! Controls model for WasmEffect.
/*!
 * Owns the module path, the generic parameter models and the status/error
 * text. saveSettings()/loadSettings() persist all of it in the project file;
 * loading a project without a <wasmeffectcontrols> element (or without the
 * module attribute) keeps the effect silent and dry - backward compatible.
 */
class WasmEffectControls : public EffectControls
{
	Q_OBJECT
public:
	//! Number of generic module parameters the host exposes.
	static constexpr int paramCount = 8;

	explicit WasmEffectControls(WasmEffect* effect);
	~WasmEffectControls() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& element) override;

	QString nodeName() const override
	{
		return "wasmeffectcontrols";
	}

	int controlCount() override
	{
		return paramCount;
	}

	gui::EffectControlDialog* createView() override
	{
		return new gui::WasmEffectControlDialog(this);
	}

	FloatModel* paramModel(int index);
	float paramValue(int index) const;

	QString modulePath() const
	{
		return m_modulePath;
	}

	bool isModuleLoaded() const;
	QString statusText() const;
	QString lastError() const
	{
		return m_error;
	}

	//! Control thread: load \p path and remember it for save/load.
	bool loadModule(const QString& path);

	//! Control thread: record the path of the module that is actually loaded.
	void setModulePath(const QString& path)
	{
		m_modulePath = path;
	}

private:
	WasmEffect* m_effect;
	std::array<FloatModel*, paramCount> m_params{};
	QString m_modulePath;
	QString m_error;
};

} // namespace lmms

#endif // LMMS_WASM_EFFECT_CONTROLS_H
