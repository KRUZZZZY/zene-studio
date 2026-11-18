/*
 * ControlCommandsAccessibleEditing.cpp - window.item_list and view.announcements
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

/*! 0.5.0 UX decisions, themes 10 and 17:
 *  - window.item_list: open (or close) the editable item list (ItemList.h) of a canvas - the note area, the controller
 *    lane, the XY pad, the comp strip, the arranger's clips, the automation editor's points (ED-194). Interface
 *    state; its edits are the canvases' own commands.
 *  - view.announcements: what a screen reader is told unasked - all transitions, errors only, or nothing
 *    (AccessibleWidgets.h's announcement policy). A preference in the config file; one undo step. */

#include <functional>

#include <QApplication>
#include <QTableView>

#include "AccessibleWidgets.h"
#include "AutomationEditor.h"
#include "CompSwipeStrip.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "GuiApplication.h"
#include "ItemList.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "SongEditor.h"
#include "UiConfigStore.h"
#include "XyPad.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxRowsAnswered = 200;

const QStringList& surfaceNames()
{
	static const QStringList names{QStringLiteral("notes"), QStringLiteral("controller"), QStringLiteral("xy"),
		QStringLiteral("comp"), QStringLiteral("clips"), QStringLiteral("automation")};
	return names;
}

//! The open @a T to list: the one holding the keyboard focus, else a shown one, else any.
template<typename T>
T* openOne()
{
	for (QWidget* up = QApplication::focusWidget(); up != nullptr; up = up->parentWidget())
	{
		if (auto* found = qobject_cast<T*>(up)) { return found; }
	}
	T* any = nullptr;
	for (QWidget* widget : QApplication::allWidgets())
	{
		auto* found = qobject_cast<T*>(widget);
		if (found != nullptr && found->isVisible()) { return found; }
		if (any == nullptr) { any = found; }
	}
	return any;
}

//! Where a surface's list lives (the canvas widget it belongs to) and how its source is made.
struct Surface
{
	QWidget* host = nullptr;
	std::function<gui::ItemListSource*(QObject*)> make;
	QString missing;  //!< why there is none
};

Surface editorSurface(bool controller)
{
	gui::NoteEditor* editor = openOne<gui::NoteEditor>();
	if (editor == nullptr) { return {nullptr, {}, QStringLiteral("no note editor is open")}; }
	QWidget* lane = editor->findChild<QWidget*>(QStringLiteral("noteControllerLane"));
	if (controller)
	{
		return {lane != nullptr ? lane : editor, [editor](QObject* parent) { return gui::itemlists::controllerPoints(editor, parent); }, {}};
	}
	return {editor->canvas(), [editor](QObject* parent) { return gui::itemlists::notes(editor, parent); }, {}};
}

Surface arrangerSurface()
{
	gui::GuiApplication* g = gui::getGUI();
	gui::SongEditor* editor = g != nullptr && g->songEditor() != nullptr ? g->songEditor()->m_editor : nullptr;
	if (editor == nullptr) { return {nullptr, {}, QStringLiteral("there is no arranger: this instance has no interface")}; }
	return {editor, [](QObject* parent) { return gui::itemlists::clips(parent); }, {}};
}

Surface automationSurface()
{
	gui::GuiApplication* g = gui::getGUI();
	gui::AutomationEditor* editor = g != nullptr && g->automationEditor() != nullptr ? g->automationEditor()->m_editor : nullptr;
	if (editor == nullptr) { return {nullptr, {}, QStringLiteral("there is no automation editor: this instance has no interface")}; }
	if (!editor->validClip()) { return {nullptr, {}, QStringLiteral("the automation editor shows no clip")}; }
	return {editor, [editor](QObject* parent) { return gui::itemlists::automationPoints(editor, parent); }, {}};
}

Surface surfaceFor(const QString& name)
{
	if (name == QLatin1String("automation")) { return automationSurface(); }
	if (name == QLatin1String("notes") || name == QLatin1String("controller")) { return editorSurface(name == QLatin1String("controller")); }
	if (name == QLatin1String("clips")) { return arrangerSurface(); }
	if (name == QLatin1String("xy"))
	{
		gui::XyPadDialog* pad = openOne<gui::XyPadDialog>();
		if (pad == nullptr) { return {nullptr, {}, QStringLiteral("no XY pad is open")}; }
		return {pad, [pad](QObject* parent) { return gui::itemlists::xyAxes(pad, parent); }, {}};
	}
	gui::CompSwipeStrip* strip = openOne<gui::CompSwipeStrip>();
	if (strip == nullptr) { return {nullptr, {}, QStringLiteral("no take lanes are open")}; }
	return {strip, [strip](QObject* parent) { return gui::itemlists::compSegments(strip, parent); }, {}};
}

ControlResult itemList(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("surface")).toString();
	if (!surfaceNames().contains(name))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'surface' is one of %1").arg(surfaceNames().join(QStringLiteral(", "))));
	}
	const Surface surface = surfaceFor(name);
	if (surface.host == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, surface.missing); }
	gui::ItemListPanel* shown = gui::ItemListPanel::find(surface.host, name);
	const bool wanted = args.contains(QStringLiteral("visible")) ? args.value(QStringLiteral("visible")).toBool()
		: shown == nullptr || !shown->isVisible();
	if (!wanted)
	{
		if (shown != nullptr) { shown->close(); }  // deletes it and its source
		return ControlResult::success({{QStringLiteral("surface"), name}, {QStringLiteral("visible"), false}});
	}
	gui::ItemListPanel* panel = gui::ItemListPanel::open(surface.host, name, surface.make);
	panel->show();
	panel->raise();
	panel->activateWindow();
	panel->table()->setFocus(Qt::ShortcutFocusReason);
	QJsonObject result = panel->source()->describe(MaxRowsAnswered);
	result.insert(QStringLiteral("surface"), name);
	result.insert(QStringLiteral("visible"), true);
	return ControlResult::success(result);
}

ControlResult announcements(const QJsonObject& args)
{
	const QString level = args.value(QStringLiteral("level")).toString();
	if (!a11y::announceLevelNames().contains(level))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'level' is one of %1").arg(a11y::announceLevelNames().join(QStringLiteral(", "))));
	}
	const QString before = a11y::announceLevelNames().at(static_cast<int>(a11y::announceLevel()));
	return gui::uistore::write(QStringLiteral("announcements"), {{QStringLiteral("level"), level}},
		{{QStringLiteral("level"), level}, {QStringLiteral("previous"), before}},
		{QStringLiteral("view.announcements"), {{QStringLiteral("level"), before}}}, [] {});
}

void add(ControlRegistry& registry, const char* group, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&), bool mutating)
{
	ControlCommand cmd = guiCommand(group, verb);
	cmd.mutating = mutating;
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = objectProperty();
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerAccessibleEditingCommands(ControlRegistry& registry)
{
	add(registry, "window", "item_list", QStringLiteral("Open the editable item list of a canvas beside it, or close it "
		"('visible'; toggles when left out) - 'surface': notes (the open note editor's notes, with the expression lane's "
		"value), controller (the controller lane's points), xy (the open XY pad's two axes), comp (the take lanes' comp "
		"segments), clips (the arrangement's clips) or automation (the automation editor's points: position and value as a "
		"share of the control's range). Every cell edit, Insert and Delete in the list runs the canvas's own "
		"command (note.edit, note.add, note.remove_many, clip.controllers_set, rack.macro_set, comp.select, clip.move, "
		"clip.resize, clip.add, clip.delete, automation.move_point), one undo step each; selection follows the canvas both ways. Answers the list: "
		"{surface, visible, title, columns: [{id, title, editable}], rows: [{id, <column>: text}] (at most 200), count}. "
		"Ctrl+Shift+L on a canvas. Interface state."),
		objectSchema({{QStringLiteral("surface"), enumProperty(surfaceNames())}, {QStringLiteral("visible"), booleanProperty()}},
			{QStringLiteral("surface")}), itemList, false);
	add(registry, "view", "announcements", QStringLiteral("What a screen reader is told without asking: 'level' all (errors, "
		"refused edits, recording starting and stopping, a project saved or opened, a finished job - the default), errors "
		"(errors and refusals only) or off. The same message is never spoken twice within a second, and values are spoken "
		"only for the focused control, never for meters or automation. A preference in the config file (ui/announcements); "
		"one undo step."),
		objectSchema({{QStringLiteral("level"), enumProperty(a11y::announceLevelNames())}}, {QStringLiteral("level")}), announcements, true);
}

} // namespace lmms
