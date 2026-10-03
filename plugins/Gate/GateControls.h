/*
 * GateControls.h - the Gate's parameters
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
#ifndef LMMS_GATE_CONTROLS_H
#define LMMS_GATE_CONTROLS_H

#include "ComboBoxModel.h"
#include "EffectControls.h"
#include "GateControlDialog.h"

namespace lmms
{

class GateEffect;

class GateControls : public EffectControls
{
	Q_OBJECT
public:
	enum class Mode
	{
		Gate = 0,
		Expander = 1,
		Duck = 2
	};

	explicit GateControls(GateEffect* effect);
	~GateControls() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	QString nodeName() const override { return QStringLiteral("GateControls"); }
	gui::EffectControlDialog* createView() override { return new gui::GateControlDialog(this); }
	int controlCount() override { return 7; }

private:
	GateEffect* m_effect;
	ComboBoxModel m_modeModel;
	FloatModel m_thresholdModel;
	FloatModel m_rangeModel;
	FloatModel m_ratioModel;
	FloatModel m_attackModel;
	FloatModel m_holdModel;
	FloatModel m_releaseModel;

	friend class gui::GateControlDialog;
	friend class GateEffect;
};

} // namespace lmms

#endif // LMMS_GATE_CONTROLS_H
