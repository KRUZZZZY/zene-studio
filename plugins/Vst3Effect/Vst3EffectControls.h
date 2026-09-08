/*
 * Vst3EffectControls.h - controls and parameter models for the VST3 effect host
 *
 * Copyright (c) 2026 LMMS contributors
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

#ifndef LMMS_VST3_EFFECT_CONTROLS_H
#define LMMS_VST3_EFFECT_CONTROLS_H

#include <cstdint>
#include <vector>

#include <QDomDocument>
#include <QDomElement>

#include "EffectControls.h"
#include "Vst3Parameter.h"

class QTimer;

namespace lmms
{

class Vst3Effect;

namespace gui
{
class EffectControlDialog;
}

//! Exposes every VST3 parameter as an AutomatableModel and persists the
//! plug-in's own state (component + controller) into the .mmp.
class Vst3EffectControls : public EffectControls
{
	Q_OBJECT
public:
	explicit Vst3EffectControls(Vst3Effect* effect);
	~Vst3EffectControls() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;

	auto nodeName() const -> QString override { return "vst3"; }
	auto controlCount() -> int override
	{
		return static_cast<int>(m_paramModels.size());
	}
	auto createView() -> gui::EffectControlDialog* override;

	auto paramModels() const -> const std::vector<Vst3ParamModel*>&
	{
		return m_paramModels;
	}
	auto modelForParam(std::uint32_t id) -> Vst3ParamModel*;

private slots:
	//! GUI thread: pull plug-in side changes into the models and service the
	//! audio thread's re-prepare request
	void poll();

private:
	Vst3Effect* m_effect;
	std::vector<Vst3ParamModel*> m_paramModels;
	QTimer* m_pollTimer = nullptr;
	bool m_syncing = false;
};

} // namespace lmms

#endif // LMMS_VST3_EFFECT_CONTROLS_H
