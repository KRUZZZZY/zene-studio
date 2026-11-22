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

#include <atomic>
#include <memory>
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
 *
 * The handoff to the unit is per block and lock free: the audio path calls pushChangedValues() before it renders,
 * which reads each model's value and hands a changed one to the unit (ParamTarget::setParam, an atomic store the
 * same block applies). Nothing listens to dataChanged() to reach the unit, and each model defers that signal when it
 * changes on the render thread (an automation tick): flushDeferred(), on the GUI thread, emits it there. So no Qt
 * signal is emitted on the render thread for an automated Audio Unit parameter (Q050-491 review round 1).
 */
class AuParamModelList
{
public:
	AuParamModelList(au::ParamTarget* unit, Model* parent);
	//! GUI thread, once after load(): one model per parameter the unit reported.
	void build();
	const std::vector<AuParamModel*>& models() const { return m_models; }
	//! Audio thread, once per block before rendering: every model whose value changed since the last block is sent to
	//! the unit. Allocation and lock free; the model list is fixed after build().
	void pushChangedValues();
	//! GUI thread: the dataChanged() each model held back while it changed on the render thread.
	void flushDeferred();
	//! GUI thread: the unit's values into the models (its own editor, a restored state), for a value further than
	//! @a tolerance of a model's range from it. A model whose value the audio path has not handed over yet is left alone.
	void pullValues(float tolerance);
	//! Every model under <parameters>, with its automation link (AutomatableModel::saveSettings).
	void saveValues(QDomDocument& doc, QDomElement& element);
	//! The saved models back, after the unit's own state: their values go to the unit with the next block.
	void restoreValues(const QDomElement& element);

private:
	au::ParamTarget* m_unit;
	Model* m_parent;
	std::vector<AuParamModel*> m_models;
	//! Per model, the value last handed to the unit. Written by the audio path, read by pullValues().
	std::unique_ptr<std::atomic<float>[]> m_pushed;
};

//! The state element both AU hosts write: kAudioUnitProperty_ClassInfo as a binary plist, base64, under <state>.
void saveAuState(au::HostedUnit* unit, QDomDocument& doc, QDomElement& element);
//! The saved state back into @a unit, while the engine is held. False when the document carries none.
bool loadAuState(au::HostedUnit* unit, const QDomElement& element);

} // namespace lmms

#endif // LMMS_AU_PARAMETER_H
