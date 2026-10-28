/*
 * AuParameter.h - an Audio Unit's parameters as LMMS models, and its saved state (Q050-491, macOS only)
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
 */

#ifndef LMMS_AU_PARAMETER_H
#define LMMS_AU_PARAMETER_H

#include <vector>

#include <QDomDocument>
#include <QDomElement>

#include "AuHost.h"
#include "AutomatableModel.h"

namespace lmms
{

//! One Audio Unit parameter, in the unit's own plain range. Its object name ("au-<id>") is the stable key.
class AuParamModel : public FloatModel
{
	Q_OBJECT
public:
	AuParamModel(const au::ParamInfo& info, int index, Model* parent);
	int index() const { return m_index; }
	const au::ParamInfo& info() const { return m_info; }

private:
	au::ParamInfo m_info;
	int m_index;
};

/*!
 * The models of one unit's parameters, children of the device, so plugin.param_get / param_set, automation,
 * controllers and modulators find them as they find any device's (findChildren<AutomatableModel*>).
 * A model's change is written to the unit by setParam() - an atomic store the next block applies - from
 * whichever thread changed it (automation: the audio thread), except the host's own pullValues(). GUI thread
 * otherwise.
 */
class AuParamModelList
{
public:
	AuParamModelList(au::HostedUnit* unit, Model* parent);
	//! One model per parameter the unit reported after load(). Once.
	void build();
	const std::vector<AuParamModel*>& models() const { return m_models; }
	//! The unit's values into the models (its own editor, a restored state), not echoed back; only values
	//! further than @a tolerance of a model's range from it.
	void pullValues(float tolerance);
	//! Every model under <parameters>, with its automation link (AutomatableModel::saveSettings).
	void saveValues(QDomDocument& doc, QDomElement& element);
	//! The saved models back, after the unit's own state: their values go to the unit with the next block.
	void restoreValues(const QDomElement& element);

private:
	au::HostedUnit* m_unit;
	Model* m_parent;
	std::vector<AuParamModel*> m_models;
	bool m_syncing = false;
};

//! The state element both AU hosts write: kAudioUnitProperty_ClassInfo as a binary plist, base64, under <state>.
void saveAuState(au::HostedUnit* unit, QDomDocument& doc, QDomElement& element);
//! The saved state back into @a unit, while the engine is held. False when the document carries none.
bool loadAuState(au::HostedUnit* unit, const QDomElement& element);

} // namespace lmms

#endif // LMMS_AU_PARAMETER_H
