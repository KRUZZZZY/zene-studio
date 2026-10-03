/*
 * ControlCommandsModulatorTools.cpp - a modulator's own formula, and modulators restarted by a track's notes
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
/*! 0.5.0 L10 (W3), two more kinds of modulator:
 *  - modulator.formula_set (MX-118): the modulator runs a formula of its cycle position and count
 *    (include/ModulationFormula.h), compiled here on the control thread, evaluated per block on the
 *    audio thread with no allocation;
 *  - modulator.trigger_set (MX-121): the modulator restarts its cycle on every note a track plays
 *    (an envelope or a step sequence per note), or runs free on the timeline again.
 *  Both are saved with the layer (attributes only when set) and each is one undo step. */

#include <functional>

#include "ControlDeviceTools.h"
#include "ControlEdit.h"
#include "ControlModulationSupport.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "ModulationFormula.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

//! Put @a modulator in slot @a index of the layer (routes included) and republish.
void putModulator(int index, const Modulator& modulator)
{
	editModulationLayer([index, modulator](ModulationLayer& layer, ModulationRuntime&) {
		if (Modulator* slot = layer.modulator(index)) { *slot = modulator; }
	});
}

using ModulatorEdit = std::function<bool(Modulator*, ControlResult*)>;

//! The modulator 'modulator' names, changed by @a edit, as one undo step (the whole modulator is
//! captured by value before and after, so the step never holds a pointer into the layer).
ControlResult editModulator(const QJsonObject& args, const ModulatorEdit& edit)
{
	const ModulationLayer& layer = songModulationLayer().layer();
	ControlResult error;
	const int index = resolveModulatorIndex(layer, args.value(QStringLiteral("modulator")).toString(), &error);
	if (index < 0) { return error; }
	const Modulator before = *layer.modulator(index);
	Modulator after = before;
	if (!edit(&after, &error)) { return error; }
	putModulator(index, after);
	addUndoStep([index, before]() { putModulator(index, before); }, [index, after]() { putModulator(index, after); });
	Engine::getSong()->setModified();
	return ControlResult::success(modulatorJson(*songModulationLayer().layer().modulator(index), index));
}

ControlResult formulaSet(const QJsonObject& args)
{
	return editModulator(args, [&args](Modulator* modulator, ControlResult* error) {
		const QString text = args.value(QStringLiteral("formula")).toString().trimmed();
		QString why;
		if (!compileFormula(text, &modulator->source.formula, &why))
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("the formula does not read: %1").arg(why));
			return false;
		}
		modulator->source.shape = ModulationShape::Formula;
		modulator->formula = text;
		return true;
	});
}

ControlResult triggerSet(const QJsonObject& args)
{
	return editModulator(args, [&args](Modulator* modulator, ControlResult* error) {
		const QString id = args.value(QStringLiteral("track")).toString();
		if (id.isEmpty() || id == QLatin1String("none")) { modulator->source.triggerTrack = -1; return true; }
		Track* track = resolveTrack(id, error);
		if (track == nullptr) { return false; }
		if (dynamic_cast<InstrumentTrack*>(track) == nullptr)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("%1 plays no notes: a modulator follows an "
				"instrument track").arg(id));
			return false;
		}
		modulator->source.triggerTrack = track->id();
		return true;
	});
}

} // namespace

void registerModulatorToolCommands(ControlRegistry& registry)
{
	addDeviceCommand(registry, "modulator.formula_set", QStringLiteral("Make a modulator run 'formula', a formula of t (where in its "
		"cycle it is, 0..1) and x (cycles since it started): numbers, pi, e, + - * / % ^, sin cos abs floor sqrt, tri saw square "
		"(waveforms of a cycle position), rand (a repeatable random value per whole number), min max. The result is kept to "
		"-1..1 and its rate is the modulator's own. Example: sin(2*pi*t) * (1 - t). Its shape becomes 'formula'. Saved with the "
		"layer; one undo step."),
		{{QStringLiteral("modulator"), stringProperty()}, {QStringLiteral("formula"), stringProperty()}},
		{QStringLiteral("modulator"), QStringLiteral("formula")}, true, formulaSet);
	addDeviceCommand(registry, "modulator.trigger_set", QStringLiteral("Restart a modulator's cycle on every note 'track' (an "
		"instrument track, trk-<n>) plays - an envelope or a step sequence per note; before the first note it waits at the start of "
		"its cycle. An empty 'track' (or 'none') lets it run free on the timeline again. Saved with the layer; one undo step."),
		{{QStringLiteral("modulator"), stringProperty()}, {QStringLiteral("track"), stringProperty()}},
		{QStringLiteral("modulator"), QStringLiteral("track")}, true, triggerSet);
}

} // namespace lmms
