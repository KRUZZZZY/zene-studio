/*
 * NoteEditorMenus.cpp - the note editor's Notes menu: quantise, velocity, timing, groove, event list, find and change
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

#include <QAction>
#include <QHBoxLayout>
#include <QMenu>

#include "Accessibility.h"
#include "CommandForm.h"
#include "CommandLists.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "NoteEditor.h"
#include "ShellChrome.h"

namespace lmms::gui
{

namespace
{

QJsonObject merged(QJsonObject base, const QJsonObject& extra)
{
	for (auto it = extra.begin(); it != extra.end(); ++it) { base.insert(it.key(), it.value()); }
	return base;
}

} // namespace

QString NoteEditor::noteScope() const
{
	if (m_state.clip == nullptr) { return QStringLiteral("clip"); }
	for (const Note* note : m_state.clip->notes())
	{
		if (note->selected()) { return QStringLiteral("selection"); }
	}
	return QStringLiteral("clip");
}

void NoteEditor::addNotesMenu(QWidget* bar, QHBoxLayout* row)
{
	// Every clip-wide note command (L07), each a registered command run through its form.
	auto* button = new ShellButton(bar);
	button->setText(tr("Notes"));
	button->setToolButtonStyle(Qt::ToolButtonTextOnly);
	button->setPopupMode(QToolButton::InstantPopup);
	button->setObjectName(QStringLiteral("noteEditorNotesMenu"));
	auto* menu = new QMenu(button);
	button->setMenu(menu);
	a11y::announce(button, tr("Notes"), tr("Quantise, velocity, timing, groove and the event list for the clip's notes."));
	connect(menu, &QMenu::aboutToShow, this, [this, menu]() { fillNotesMenu(menu); });
	// A key-names preset changes the keyboard's labels and the drum view's rows, not the clip.
	connect(menu, &QMenu::triggered, this, &NoteEditor::refresh);
	row->addWidget(button);
	row->addSpacing(tokens::space::md);
}

void NoteEditor::fillNotesMenu(QMenu* menu)
{
	menu->clear();
	if (m_state.clip == nullptr)
	{
		menu->addAction(tr("No clip open"))->setEnabled(false);
		return;
	}
	const QString clip = clipId();
	const QJsonObject where{{QStringLiteral("clip"), clip}};
	const QJsonObject scoped{{QStringLiteral("clip"), clip}, {QStringLiteral("scope"), noteScope()}};
	auto call = [](const QString& id, const QJsonObject& fixed, const QJsonObject& defaults = {}, bool ask = true) {
		return [id, fixed, defaults, ask]() { return CommandCall{id, fixed, defaults, {}, ask}; };
	};
	addCommandAction(menu, tr("Quantise..."), QStringLiteral("groove.quantize"), call(QStringLiteral("groove.quantize"),
		where, {{QStringLiteral("grid"), std::max(1, m_state.gridTicks)}, {QStringLiteral("scope"), noteScope()}}));
	fillVelocityMenu(menu->addMenu(tr("Velocity")), scoped);
	fillTimingMenu(menu->addMenu(tr("Timing")), scoped);
	QMenu* groove = menu->addMenu(tr("Groove"));
	addCommandAction(groove, tr("Apply Groove..."), QStringLiteral("groove.apply"), call(QStringLiteral("groove.apply"), where));
	addCommandAction(groove, tr("Extract Groove..."), QStringLiteral("groove.extract"), call(QStringLiteral("groove.extract"),
		where, {{QStringLiteral("grid"), std::max(1, m_state.gridTicks)}}));
	menu->addSeparator();
	QAction* list = menu->addAction(tr("MIDI Event List..."));
	list->setProperty("controlCommand", QStringLiteral("note.list"));
	connect(list, &QAction::triggered, this, [this, clip]() { showCommandList(this, CommandLists::noteEvents(clip)); });
	addCommandAction(menu, tr("Find && Change..."), QStringLiteral("note.transform_where"),
		call(QStringLiteral("note.transform_where"), where, {{QStringLiteral("filter"), QJsonObject{}}}));
	const QString track = control::trackIdOf(m_state.clip->getTrack());
	addCommandChoices(menu, tr("Key Names"), QStringLiteral("key_names.preset"), QStringLiteral("preset"),
		call(QStringLiteral("key_names.preset"), {{QStringLiteral("track"), track}}, {}, false));
	const QJsonObject view{{QStringLiteral("reveal"), false}};
	addCommandChoices(menu, tr("Colour By"), QStringLiteral("window.note_editor_set"), QStringLiteral("color_mode"),
		call(QStringLiteral("window.note_editor_set"), view, {}, false));
	addCommandChoices(menu, tr("Keep in Scale"), QStringLiteral("window.note_editor_set"), QStringLiteral("keep_in_scale"),
		call(QStringLiteral("window.note_editor_set"), view, {}, false));
	menu->addSeparator();
	addCommandAction(menu, tr("Run Script on Notes..."), QStringLiteral("script.run"), call(QStringLiteral("script.run"), {},
		{{QStringLiteral("source"), QStringLiteral("-- the open clip is %1\nlocal clip = \"%1\"\n").arg(clip)}}));
}

void NoteEditor::fillVelocityMenu(QMenu* menu, const QJsonObject& scoped)
{
	const auto run = [scoped](const char* id, const QJsonObject& fixed = {}, bool ask = true) {
		return [id, scoped, fixed, ask]() { return CommandCall{QLatin1String(id), merged(scoped, fixed), {}, {}, ask}; };
	};
	addCommandAction(menu, tr("Scale..."), QStringLiteral("note.velocity_scale"), run("note.velocity_scale"));
	addCommandAction(menu, tr("Offset..."), QStringLiteral("note.velocity_offset"), run("note.velocity_offset"));
	addCommandAction(menu, tr("Softer (x0.8)"), QStringLiteral("note.velocity_scale"),
		run("note.velocity_scale", {{QStringLiteral("factor"), 0.8}}, false));
	addCommandAction(menu, tr("Louder (x1.25)"), QStringLiteral("note.velocity_scale"),
		run("note.velocity_scale", {{QStringLiteral("factor"), 1.25}}, false));
	addCommandAction(menu, tr("Humanise..."), QStringLiteral("note.randomize"), run("note.randomize"));
}

void NoteEditor::fillTimingMenu(QMenu* menu, const QJsonObject& scoped)
{
	const auto run = [scoped](const char* id, const QJsonObject& fixed = {}, bool ask = false) {
		return [id, scoped, fixed, ask]() { return CommandCall{QLatin1String(id), merged(scoped, fixed), {}, {}, ask}; };
	};
	addCommandAction(menu, tr("Double Time (x2)"), QStringLiteral("note.scale"),
		run("note.scale", {{QStringLiteral("time_factor"), 2.0}}));
	addCommandAction(menu, tr("Half Time (x0.5)"), QStringLiteral("note.scale"),
		run("note.scale", {{QStringLiteral("time_factor"), 0.5}}));
	addCommandAction(menu, tr("Scale Time..."), QStringLiteral("note.scale"), run("note.scale", {}, true));
	menu->addSeparator();
	for (int count : {2, 3, 4})
	{
		addCommandAction(menu, tr("Ratchet x%1").arg(count), QStringLiteral("note.ratchet"),
			run("note.ratchet", {{QStringLiteral("count"), count}}));
	}
	addCommandAction(menu, tr("Ratchet..."), QStringLiteral("note.ratchet"), run("note.ratchet", {}, true));
	menu->addSeparator();
	addCommandAction(menu, tr("Transpose..."), QStringLiteral("note.transpose"), run("note.transpose", {}, true));
}

} // namespace lmms::gui
