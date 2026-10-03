/*
 * ParameterControls.h - effect controls whose parameters are listed once: saved, loaded and drawn from the list
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

#ifndef LMMS_PARAMETER_CONTROLS_H
#define LMMS_PARAMETER_CONTROLS_H

#include <utility>
#include <vector>

#include <QString>

#include "EffectControls.h"
#include "lmms_export.h"

namespace lmms
{

class AutomatableModel;

/*! 0.5.0 L10 (W3): the controls of a native device whose parameters are a plain list. A device
 *  registers each model once, with the key it is saved under; saving, loading and the control
 *  dialog (a ParameterGridDialog: a knob per number, a menu per choice, a switch per toggle) all
 *  come from that list, so a new device writes its DSP and its list and nothing else. */
class LMMS_EXPORT ParameterControls : public EffectControls
{
public:
	ParameterControls(Effect* effect, const QString& nodeName);
	~ParameterControls() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	QString nodeName() const override { return m_nodeName; }
	gui::EffectControlDialog* createView() override;
	int controlCount() override { return static_cast<int>(m_parameters.size()); }

	//! The registered models, in the order they are drawn.
	const std::vector<std::pair<AutomatableModel*, QString>>& parameters() const { return m_parameters; }

protected:
	//! Register @a model, saved under @a key. Call from the constructor, in the order to draw.
	void addParameter(AutomatableModel* model, const QString& key);

private:
	QString m_nodeName;
	std::vector<std::pair<AutomatableModel*, QString>> m_parameters;
};

} // namespace lmms

#endif // LMMS_PARAMETER_CONTROLS_H
