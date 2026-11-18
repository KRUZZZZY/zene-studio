/*
 * NoteTransformActions.cpp - the piano roll's randomise, humanise and velocity-scale tools
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

#include "NoteTransformActions.h"

#include <QAction>
#include <QApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>

#include <cmath>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "MidiClip.h"
#include "MpeExpression.h"
#include "Note.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

NoteTransformPrompt& prompt()
{
	static NoteTransformPrompt s_prompt;
	return s_prompt;
}

std::optional<double> ask(const QString& title, const QString& label, double value, double min, double max, int decimals)
{
	if (prompt()) { return prompt()(title, label, value, min, max, decimals); }
	bool ok = false;
	const double chosen = QInputDialog::getDouble(QApplication::activeWindow(), title, label, value, min, max, decimals, &ok);
	return ok ? std::optional<double>(chosen) : std::nullopt;
}

//! The scope for @a clip: "selection" when the piano roll has notes selected (handed to the command
//! surface first), else "clip".
QString scopeOf(const MidiClip* clip)
{
	return handOverNoteSelection(clip) ? QStringLiteral("selection") : QStringLiteral("clip");
}

//! One item: asks with @a title/@a label, then runs @a command with the clip, the scope and
//! { @a argument: the value } (plus @a extra).
QAction* transform(const QString& text, const QString& command, const QString& argument, double initial,
	double min, double max, int decimals, QJsonObject extra, std::function<const MidiClip*()> clip, QObject* parent)
{
	auto* action = new QAction(text, parent);
	action->setData(command);
	QObject::connect(action, &QAction::triggered, action, [=] {
		const MidiClip* target = clip();
		if (target == nullptr || target->notes().empty()) { return; }
		// The prompt is labelled with the item's own text: the argument is the command's parameter
		// name ('velocity_jitter'), which is nothing the user reading the dialog can act on.
		const std::optional<double> value = ask(text, text, initial, min, max, decimals);
		if (!value) { return; }
		QJsonObject args = extra;
		args.insert(QStringLiteral("clip"), control::clipIdOf(target));
		args.insert(QStringLiteral("scope"), scopeOf(target));
		args.insert(argument, *value);
		const ControlResult result = ControlRegistry::instance()->invoke(command, args);
		if (result.ok) { return; }
		if (isUnattendedRun()) { qWarning("note transform: %s refused: %s", qPrintable(command), qPrintable(result.errorMessage)); }
		else { QMessageBox::warning(QApplication::activeWindow(), text, result.errorMessage); }
	});
	return action;
}

//! The notes an MPE stream item acts on: the selected ones, or every note of
//! the clip when none is selected (the same scope rule the transform items
//! above hand to the command surface, resolved here because
//! note.expression_stream_set addresses one note per call).
QList<const Note*> streamTargetNotes(const MidiClip* clip)
{
	QList<const Note*> targets;
	bool anySelected = false;
	for (const Note* note : clip->notes())
	{
		if (note->selected()) { anySelected = true; }
	}
	for (const Note* note : clip->notes())
	{
		if (!anySelected || note->selected()) { targets.append(note); }
	}
	return targets;
}

//! One note.expression_stream_set call: @a pitchCents writes a {0:0, len:cents}
//! pitch ramp (the call omits the other axes, so they are kept); @a clear
//! empties every axis instead. One MidiClip-checkpoint undo step per note,
//! like every other command in this menu.
ControlResult invokeStreamSet(const MidiClip* clip, const Note* note,
	std::optional<int> pitchCents, bool clear)
{
	QJsonObject args{
		{QStringLiteral("clip"), control::clipIdOf(clip)},
		{QStringLiteral("note"), control::noteIdOf(note)},
	};
	if (clear)
	{
		args.insert(QStringLiteral("pitch"), QJsonArray{});
		args.insert(QStringLiteral("pressure"), QJsonArray{});
		args.insert(QStringLiteral("timbre"), QJsonArray{});
	}
	else if (pitchCents.has_value())
	{
		const int end = note->length().getTicks();
		args.insert(QStringLiteral("pitch"), QJsonArray{
			QJsonObject{{QStringLiteral("tick"), 0}, {QStringLiteral("value"), 0}},
			QJsonObject{{QStringLiteral("tick"), end}, {QStringLiteral("value"), *pitchCents}},
		});
	}
	return ControlRegistry::instance()->invoke(
		QStringLiteral("note.expression_stream_set"), args);
}

void warnStreamRefusal(const QString& title, const QString& command, const ControlResult& result)
{
	if (isUnattendedRun()) { qWarning("note stream tool: %s refused: %s", qPrintable(command), qPrintable(result.errorMessage)); }
	else { QMessageBox::warning(QApplication::activeWindow(), title, result.errorMessage); }
}

//! An MPE stream item: runs @a edit (one note.expression_stream_set per target
//! note) over the stream targets, warning once on the first refusal.
QAction* streamItem(const QString& text, std::function<const MidiClip*()> clip, QObject* parent,
	std::function<ControlResult(const MidiClip*, const Note*)> edit)
{
	auto* action = new QAction(text, parent);
	QObject::connect(action, &QAction::triggered, action, [=] {
		const MidiClip* target = clip();
		if (target == nullptr || target->notes().empty()) { return; }
		for (const Note* note : streamTargetNotes(target))
		{
			const ControlResult result = edit(target, note);
			if (result.ok) { continue; }
			warnStreamRefusal(text, QStringLiteral("note.expression_stream_set"), result);
			return;
		}
	});
	return action;
}

} // namespace

bool handOverNoteSelection(const MidiClip* clip)
{
	if (clip == nullptr) { return false; }
	QJsonArray selected;
	for (const Note* note : clip->notes())
	{
		if (note->selected()) { selected.append(control::noteIdOf(note)); }
	}
	if (selected.isEmpty()) { return false; }
	ControlRegistry::instance()->invoke(QStringLiteral("note.select"),
		{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("notes"), selected}});
	return true;
}

void setNoteTransformPrompt(NoteTransformPrompt replacement)
{
	prompt() = std::move(replacement);
}

QList<QAction*> makeNoteTransformActions(std::function<const MidiClip*()> clip, QObject* parent)
{
	auto* separator = new QAction(parent);
	separator->setSeparator(true);
	auto* streamSeparator = new QAction(parent);
	streamSeparator->setSeparator(true);
	// Q050-016: the piano roll's GUI path to the per-note MPE expression
	// streams. Both items run the same note.expression_stream_set command the
	// agent surface uses (one MidiClip-checkpoint undo step per note): the
	// ramp writes a {0:0, len:value} pitch stream and keeps the other axes, the
	// clear empties every axis of the notes that carry a stream.
	auto* ramp = new QAction(QObject::tr("MPE pitch ramp..."), parent);
	QObject::connect(ramp, &QAction::triggered, ramp, [=] {
		const MidiClip* target = clip();
		if (target == nullptr || target->notes().empty()) { return; }
		const QString text = QObject::tr("MPE pitch ramp...");
		const std::optional<double> value = ask(text, text, 1200.0,
			-MpeNoteExpression::MaxPitchCents, MpeNoteExpression::MaxPitchCents, 0);
		if (!value) { return; }
		const int cents = static_cast<int>(std::lround(*value));
		for (const Note* note : streamTargetNotes(target))
		{
			const ControlResult result = invokeStreamSet(target, note, cents, false);
			if (result.ok) { continue; }
			warnStreamRefusal(text, QStringLiteral("note.expression_stream_set"), result);
			return;
		}
	});
	return {separator,
		transform(QObject::tr("Randomize velocities..."), QStringLiteral("note.randomize"),
			QStringLiteral("velocity_jitter"), 0.2, 0.0, 1.0, 2, {}, clip, parent),
		transform(QObject::tr("Humanize timing..."), QStringLiteral("note.randomize"),
			QStringLiteral("position_jitter"), 2.0, 0.0, 48.0, 0,
			{{QStringLiteral("velocity_jitter"), 0.0}}, clip, parent),
		transform(QObject::tr("Scale velocities..."), QStringLiteral("note.velocity_scale"),
			QStringLiteral("factor"), 1.2, 0.05, 4.0, 2, {}, clip, parent),
		streamSeparator,
		ramp,
		streamItem(QObject::tr("Clear MPE expression streams"), clip, parent,
			[](const MidiClip* owned, const Note* note) {
				if (!note->hasMpeStream()) { return ControlResult::success(QJsonObject{}); }
				return invokeStreamSet(owned, note, std::nullopt, true);
			})};
}

} // namespace lmms::gui
