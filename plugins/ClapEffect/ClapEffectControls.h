/*
 * ClapEffectControls.h - controls and parameter models for the CLAP effect host
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

#ifndef LMMS_CLAP_EFFECT_CONTROLS_H
#define LMMS_CLAP_EFFECT_CONTROLS_H

#include <cstdint>
#include <vector>

#include <QDomDocument>
#include <QDomElement>

#include "ClapParameter.h"
#include "EffectControls.h"

class QTimer;

namespace lmms
{

class ClapEffect;

namespace gui
{
class EffectControlDialog;
}

//! Exposes every CLAP parameter as an AutomatableModel and persists the
//! plug-in's own state (clap.state) into the .mmp. The models follow the
//! plug-in's parameter rescans (W6, ClapParamModelList).
class ClapEffectControls : public EffectControls
{
	Q_OBJECT
public:
	explicit ClapEffectControls(ClapEffect* effect);
	~ClapEffectControls() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;

	auto nodeName() const -> QString override { return "clap"; }
	auto controlCount() -> int override
	{
		return static_cast<int>(m_params.models().size());
	}
	auto createView() -> gui::EffectControlDialog* override;

	auto paramModels() const -> const std::vector<ClapParamModel*>&
	{
		return m_params.models();
	}
	auto modelForParam(std::uint32_t id) -> ClapParamModel*;

signals:
	//! W6: the plug-in rescanned its parameters and the models were rebuilt; a view redraws its knobs.
	void parametersChanged();

private slots:
	//! GUI thread: pull plug-in side changes into the models and service the
	//! audio thread's re-prepare request
	void poll();

private:
	ClapEffect* m_effect;
	ClapParamModelList m_params;
	QTimer* m_pollTimer = nullptr;
};

} // namespace lmms

#endif // LMMS_CLAP_EFFECT_CONTROLS_H
