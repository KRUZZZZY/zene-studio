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

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "MidiClip.h"
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

//! The scope for @a clip: "selection" when the piano roll has notes selected - which it hands to
//! the command surface first (note.select), whose own selection that scope reads - else "clip".
QString scopeOf(const MidiClip* clip)
{
	QJsonArray selected;
	for (const Note* note : clip->notes())
	{
		if (note->selected()) { selected.append(control::noteIdOf(note)); }
	}
	if (selected.isEmpty()) { return QStringLiteral("clip"); }
	ControlRegistry::instance()->invoke(QStringLiteral("note.select"),
		{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("notes"), selected}});
	return QStringLiteral("selection");
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
		const std::optional<double> value = ask(text, argument, initial, min, max, decimals);
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

} // namespace

void setNoteTransformPrompt(NoteTransformPrompt replacement)
{
	prompt() = std::move(replacement);
}

QList<QAction*> makeNoteTransformActions(std::function<const MidiClip*()> clip, QObject* parent)
{
	auto* separator = new QAction(parent);
	separator->setSeparator(true);
	return {separator,
		transform(QObject::tr("Randomize velocities..."), QStringLiteral("note.randomize"),
			QStringLiteral("velocity_jitter"), 0.2, 0.0, 1.0, 2, {}, clip, parent),
		transform(QObject::tr("Humanize timing..."), QStringLiteral("note.randomize"),
			QStringLiteral("position_jitter"), 2.0, 0.0, 48.0, 0,
			{{QStringLiteral("velocity_jitter"), 0.0}}, clip, parent),
		transform(QObject::tr("Scale velocities..."), QStringLiteral("note.velocity_scale"),
			QStringLiteral("factor"), 1.2, 0.05, 4.0, 2, {}, clip, parent)};
}

} // namespace lmms::gui
