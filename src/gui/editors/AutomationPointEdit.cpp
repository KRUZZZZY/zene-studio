/*
 * AutomationPointEdit.cpp - one automation point moved from the editor's keys or its item list
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


#include "AutomationPointEdit.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>

#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "ControlAutomationSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"

namespace lmms::gui::automationedit
{

namespace
{

QString tr(const char* text) { return QCoreApplication::translate("AutomationEditor", text); }

AutomatableModel* controlOf(const AutomationClip* clip)
{
	return clip != nullptr && !clip->objects().empty() ? clip->objects().front().data() : nullptr;
}

//! The command's way: automation.move_point, in song ticks and the control's own unit.
bool moveByCommand(AutomationClip* clip, AutomatableModel* model, const control::ParameterAddress& address, int tick,
	int toTick, float toValue, QString* error)
{
	const int origin = clip->startPosition().getTicks();
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("automation.move_point"),
		{{QStringLiteral("track"), address.target}, {QStringLiteral("parameter"), address.parameter.id()},
			{QStringLiteral("ticks"), origin + tick}, {QStringLiteral("new_ticks"), origin + toTick},
			{QStringLiteral("new_value"), static_cast<double>(model->scaledValue(toValue))}});
	if (!result.ok) { *error = result.errorMessage; }
	return result.ok;
}

//! In place, for a control no command can name: what automation.move_point does, behind one journal checkpoint.
bool moveInPlace(AutomationClip* clip, int tick, int toTick, float toValue, QString* error)
{
	if (toTick != tick && clip->getTimeMap().contains(TimePos(toTick)))
	{
		*error = tr("there is already a point at that position");
		return false;
	}
	const AutomationNode& node = clip->getTimeMap()[TimePos(tick)];
	const auto curve = node.curve();
	const float bend = node.bend();
	clip->addJournalCheckPoint();
	clip->removeNode(TimePos(tick));
	clip->putValue(TimePos(toTick), toValue, false);
	clip->getTimeMap()[TimePos(toTick)].setCurve(curve, bend);
	Engine::getSong()->setModified();
	return true;
}

} // namespace

bool rangeOf(const AutomationClip* clip, Range* range)
{
	const AutomatableModel* model = controlOf(clip);
	if (model == nullptr) { return false; }
	*range = {model->minValue<float>(), model->maxValue<float>(), model->step<float>()};
	return true;
}

bool movePoint(AutomationClip* clip, int tick, int toTick, float toValue, QString* error)
{
	AutomatableModel* model = controlOf(clip);
	if (model == nullptr || !clip->getTimeMap().contains(TimePos(tick)))
	{
		*error = tr("there is no point there");
		return false;
	}
	// std::clamp hands NaN back unchanged, and a NaN stored as a point's value poisons the curve: refuse it first.
	if (!std::isfinite(toValue))
	{
		*error = tr("the value is not a finite number");
		return false;
	}
	toTick = std::max(0, toTick);
	toValue = std::clamp(toValue, model->minValue<float>(), model->maxValue<float>());
	control::ParameterAddress address;
	if (control::existingAutomationClip(model) == clip && control::addressableParameterForModel(model, &address))
	{
		return moveByCommand(clip, model, address, tick, toTick, toValue, error);
	}
	return moveInPlace(clip, tick, toTick, toValue, error);
}

float valueStep(const AutomationClip* clip, bool coarse)
{
	Range range;
	if (!rangeOf(clip, &range)) { return 0.f; }
	const float span = (range.maximum - range.minimum) / (coarse ? 10.f : 100.f);
	return std::max(span, range.step);
}

} // namespace lmms::gui::automationedit
