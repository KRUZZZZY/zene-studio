/*
 * ModulationControls.h - the Modulation device's parameters
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
#ifndef LMMS_MODULATION_CONTROLS_H
#define LMMS_MODULATION_CONTROLS_H

#include "ComboBoxModel.h"
#include "EffectControls.h"
#include "ModulationControlDialog.h"

namespace lmms
{

class ModulationEffect;

class ModulationControls : public EffectControls
{
	Q_OBJECT
public:
	enum class Mode
	{
		Chorus = 0,
		Phaser = 1,
		Tremolo = 2
	};

	explicit ModulationControls(ModulationEffect* effect);
	~ModulationControls() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	QString nodeName() const override { return QStringLiteral("ModulationControls"); }
	gui::EffectControlDialog* createView() override { return new gui::ModulationControlDialog(this); }
	int controlCount() override { return 5; }

private:
	ModulationEffect* m_effect;
	ComboBoxModel m_modeModel;
	FloatModel m_rateModel;
	FloatModel m_depthModel;
	FloatModel m_feedbackModel;
	FloatModel m_spreadModel;

	friend class gui::ModulationControlDialog;
	friend class ModulationEffect;
};

} // namespace lmms

#endif // LMMS_MODULATION_CONTROLS_H
