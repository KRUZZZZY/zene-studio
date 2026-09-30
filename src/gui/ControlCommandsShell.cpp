/*
 * ControlCommandsShell.cpp - the main window's own verbs: fullscreen, attach/detach all,
 *                            the settings and About dialogs, online help, a new version
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

/*! M3.2 (registry-first actions). The View and Help menus' actions, and File > Save as New
 *  Version and the command palette (M3.4), each resolve to one of these commands and dispatch it (SPEC A11/A15: one action,
 *  one implementation). Every one that needs an interface refuses `requires` in a process
 *  without a main window. Dialogs open NON-modally: a command must answer, and a modal exec
 *  would hold the control surface until a person closed it.
 */

#include <QApplication>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QPixmap>
#include <QJsonObject>
#include <QUrl>

#include "AboutDialog.h"
#include "CommandPalette.h"
#include "ConfigManager.h"
#include "ShortcutsPage.h"
#include "StartHub.h"
#include "ModulatorPanel.h"
#include "TempoMapPanel.h"
#include "UndoHistoryPanel.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "SetupDialog.h"
#include "Song.h"
#include "VersionedSaveDialog.h"

namespace lmms
{

using namespace control;

namespace
{

//! The documentation the Help menu has always pointed at.
const char* const kHelpUrl = "https://lmms.io/documentation/";

ControlResult noInterface(const QString& id)
{
	return ControlResult::failure(ControlErrorKind::Requires,
		QStringLiteral("command '%1' requires an interface: this instance has no main window").arg(id));
}

gui::MainWindow* mainWindow()
{
	gui::GuiApplication* g = gui::getGUI();
	return g != nullptr ? g->mainWindow() : nullptr;
}

ControlCommand command(const char* group, const char* verb)
{
	ControlCommand cmd;
	cmd.group = QLatin1String(group);
	cmd.verb = QLatin1String(verb);
	cmd.id = cmd.group + QLatin1Char('.') + cmd.verb;
	cmd.mutating = false;
	cmd.requiresEngine = false;
	return cmd;
}

void registerFullscreen(ControlRegistry& registry)
{
	ControlCommand cmd = command("window", "fullscreen");
	cmd.description = QStringLiteral("Put the main window into fullscreen or take it out - View > "
		"Fullscreen dispatches this. With no `enabled` it toggles. Leaving fullscreen restores the "
		"maximised or normal state the window had. Interface state only.");
	cmd.argsSchema = objectSchema({{QStringLiteral("enabled"), booleanProperty()}});
	cmd.resultSchema = objectSchema({{QStringLiteral("fullscreen"), booleanProperty()},
		{QStringLiteral("changed"), booleanProperty()}});
	cmd.handler = [](const QJsonObject& args) {
		gui::MainWindow* window = mainWindow();
		if (window == nullptr) { return noInterface(QStringLiteral("window.fullscreen")); }
		const bool before = window->isFullScreen();
		const bool wanted = args.contains(QStringLiteral("enabled"))
			? args.value(QStringLiteral("enabled")).toBool() : !before;
		if (wanted != before) { window->toggleFullscreen(); }
		return ControlResult::success(QJsonObject{{QStringLiteral("fullscreen"), window->isFullScreen()},
			{QStringLiteral("changed"), window->isFullScreen() != before}});
	};
	registry.registerCommand(cmd);
}

void registerDetachAll(ControlRegistry& registry, const char* verb, bool detached)
{
	ControlCommand cmd = command("window", verb);
	cmd.description = detached
		? QStringLiteral("Detach every editor sub-window into its own top-level window - View > "
			"Detach all subwindows dispatches this. Interface state only.")
		: QStringLiteral("Attach every detached editor window back into the main window's "
			"workspace - View > Attach all subwindows dispatches this. Interface state only.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("detached"), booleanProperty()}});
	const QString id = cmd.id;
	cmd.handler = [detached, id](const QJsonObject&) {
		gui::MainWindow* window = mainWindow();
		if (window == nullptr) { return noInterface(id); }
		window->setAllSubWindowsDetached(detached);
		return ControlResult::success(QJsonObject{{QStringLiteral("detached"), detached}});
	};
	registry.registerCommand(cmd);
}

void registerSettingsDialog(ControlRegistry& registry)
{
	ControlCommand cmd = command("window", "settings");
	cmd.description = QStringLiteral("Open the Settings dialog (non-modally) - Edit > Settings "
		"dispatches this. What the dialog changes on OK is what settings.set writes; this only "
		"shows it.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("shown"), booleanProperty()}});
	cmd.handler = [](const QJsonObject&) {
		if (mainWindow() == nullptr) { return noInterface(QStringLiteral("window.settings")); }
		auto* dialog = new gui::SetupDialog();
		dialog->setAttribute(Qt::WA_DeleteOnClose);
		dialog->setModal(false);
		dialog->show();
		return ControlResult::success(QJsonObject{{QStringLiteral("shown"), true}});
	};
	registry.registerCommand(cmd);
}

void registerAbout(ControlRegistry& registry)
{
	ControlCommand cmd = command("app", "about");
	cmd.description = QStringLiteral("Open the About dialog (non-modally) - Help > About dispatches "
		"this. app.version answers the same facts as data.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("shown"), booleanProperty()}});
	cmd.handler = [](const QJsonObject&) {
		gui::MainWindow* window = mainWindow();
		if (window == nullptr) { return noInterface(QStringLiteral("app.about")); }
		auto* dialog = new gui::AboutDialog(window);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
		dialog->setModal(false);
		dialog->show();
		return ControlResult::success(QJsonObject{{QStringLiteral("shown"), true}});
	};
	registry.registerCommand(cmd);
}

void registerOnlineHelp(ControlRegistry& registry)
{
	ControlCommand cmd = command("app", "online_help");
	cmd.description = QStringLiteral("Open the online documentation in the system browser - Help > "
		"Online Help dispatches this. It hands a URL to the desktop, which opens a program outside "
		"this one, so it declares `requires: human`: an unattended caller is refused rather than "
		"having a browser launched for it. `url` is where it points.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("url"), stringProperty()},
		{QStringLiteral("opened"), booleanProperty()}});
	cmd.requiresDecl = QStringList{QStringLiteral("display"), QStringLiteral("human")};
	cmd.handler = [](const QJsonObject&) {
		if (mainWindow() == nullptr) { return noInterface(QStringLiteral("app.online_help")); }
		const QString url = QString::fromLatin1(kHelpUrl);
		const bool opened = QDesktopServices::openUrl(QUrl(url));
		return ControlResult::success(QJsonObject{{QStringLiteral("url"), url},
			{QStringLiteral("opened"), opened}});
	};
	registry.registerCommand(cmd);
}

void registerCommandPalette(ControlRegistry& registry)
{
	ControlCommand cmd = command("window", "command_palette");
	cmd.description = QStringLiteral("Open the command palette (non-modally) - View > Command Palette "
		"and Ctrl+Shift+P dispatch this. It lists every enabled menu action with its menu path and "
		"shortcut, plus every command that needs no argument, filters them as you type and runs the "
		"highlighted one: a menu entry triggers the real action (its dialog included), a command "
		"entry is dispatched through the registry. `entries` is how many it offers.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("shown"), booleanProperty()},
		{QStringLiteral("entries"), integerProperty()}});
	cmd.handler = [](const QJsonObject&) {
		gui::MainWindow* window = mainWindow();
		if (window == nullptr) { return noInterface(QStringLiteral("window.command_palette")); }
		auto* palette = new gui::CommandPalette(window->menuBar(), window);
		palette->setAttribute(Qt::WA_DeleteOnClose);
		palette->setModal(false);
		palette->show();
		return ControlResult::success(QJsonObject{{QStringLiteral("shown"), true},
			{QStringLiteral("entries"), static_cast<int>(palette->entries().size())}});
	};
	registry.registerCommand(cmd);
}

void registerShortcuts(ControlRegistry& registry)
{
	ControlCommand cmd = command("window", "shortcuts");
	cmd.description = QStringLiteral("Open Help > Keyboard Shortcuts (non-modally): every menu action "
		"that carries a shortcut, with its menu path and the registry command it declares, read from "
		"the live menus and filterable. Read-only: it lists shortcuts, it does not remap them. "
		"`entries` is how many shortcuts it lists.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("shown"), booleanProperty()},
		{QStringLiteral("entries"), integerProperty()}});
	cmd.handler = [](const QJsonObject&) {
		gui::MainWindow* window = mainWindow();
		if (window == nullptr) { return noInterface(QStringLiteral("window.shortcuts")); }
		auto* page = new gui::ShortcutsPage(window->menuBar(), window);
		page->setAttribute(Qt::WA_DeleteOnClose);
		page->setModal(false);
		page->show();
		return ControlResult::success(QJsonObject{{QStringLiteral("shown"), true},
			{QStringLiteral("entries"), page->rowCount()}});
	};
	registry.registerCommand(cmd);
}

void registerUndoHistory(ControlRegistry& registry)
{
	ControlCommand cmd = command("window", "undo_history");
	cmd.description = QStringLiteral("Open Edit > Undo History (non-modally): the undo stack as "
		"control.undo_depth reports it, Undo and Redo buttons that run control.undo / control.redo, and "
		"the command record control.transactions keeps, newest first. An edit made directly in an "
		"editor is undone by Undo but is not in that record, and the panel says so. `rows` is how many "
		"recorded commands it lists.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("shown"), booleanProperty()},
		{QStringLiteral("rows"), integerProperty()}});
	cmd.handler = [](const QJsonObject&) {
		gui::MainWindow* window = mainWindow();
		if (window == nullptr) { return noInterface(QStringLiteral("window.undo_history")); }
		auto* panel = new gui::UndoHistoryPanel(window);
		panel->setAttribute(Qt::WA_DeleteOnClose);
		panel->setModal(false);
		panel->show();
		return ControlResult::success(QJsonObject{{QStringLiteral("shown"), true},
			{QStringLiteral("rows"), panel->rowCount()}});
	};
	registry.registerCommand(cmd);
}

void registerTempoMapPanel(ControlRegistry& registry)
{
	ControlCommand cmd = command("window", "tempo_map");
	cmd.description = QStringLiteral("Open Edit > Tempo Map (non-modally): the tempo map's events as "
		"transport.tempo_map_get reports them, its Active switch, and Add at playhead / Remove / Clear, "
		"each running the transport.tempo_map_* command it names. `rows` is how many events it lists.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("shown"), booleanProperty()},
		{QStringLiteral("rows"), integerProperty()}});
	cmd.handler = [](const QJsonObject&) {
		gui::MainWindow* window = mainWindow();
		if (window == nullptr) { return noInterface(QStringLiteral("window.tempo_map")); }
		auto* panel = new gui::TempoMapPanel(window);
		panel->setAttribute(Qt::WA_DeleteOnClose);
		panel->setModal(false);
		panel->show();
		return ControlResult::success(QJsonObject{{QStringLiteral("shown"), true},
			{QStringLiteral("rows"), panel->rowCount()}});
	};
	registry.registerCommand(cmd);
}

void registerModulatorPanel(ControlRegistry& registry)
{
	ControlCommand cmd = command("window", "modulators");
	cmd.description = QStringLiteral("Open Edit > Modulators (non-modally): the modulation layer's "
		"modulators as modulator.get_state reports them, Add LFO / Remove, the selected one's shape and "
		"rate, the parameters it drives and a Bind row over a mixer channel's effects, each running the "
		"modulator.* command it names. `rows` is how many modulators it lists.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("shown"), booleanProperty()},
		{QStringLiteral("rows"), integerProperty()}});
	cmd.handler = [](const QJsonObject&) {
		gui::MainWindow* window = mainWindow();
		if (window == nullptr) { return noInterface(QStringLiteral("window.modulators")); }
		auto* panel = new gui::ModulatorPanel(window);
		panel->setAttribute(Qt::WA_DeleteOnClose);
		panel->setModal(false);
		panel->show();
		return ControlResult::success(QJsonObject{{QStringLiteral("shown"), true},
			{QStringLiteral("rows"), panel->modulatorCount()}});
	};
	registry.registerCommand(cmd);
}

void registerStartHub(ControlRegistry& registry)
{
	ControlCommand cmd = command("window", "start_hub");
	cmd.description = QStringLiteral("Open File > Start Hub (non-modally): start a new project, empty or "
		"from a template, reopen a recent one, and the Learn section (the command palette, the keyboard "
		"shortcuts page, the online manual). Its buttons run project.new, project.open and those window "
		"verbs through the registry. It never opens by itself at startup. `recent` and `templates` are "
		"how many it offers.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("shown"), booleanProperty()},
		{QStringLiteral("recent"), integerProperty()}, {QStringLiteral("templates"), integerProperty()}});
	cmd.handler = [](const QJsonObject&) {
		gui::MainWindow* window = mainWindow();
		if (window == nullptr) { return noInterface(QStringLiteral("window.start_hub")); }
		const QStringList recent = ConfigManager::inst()->recentlyOpenedProjects();
		const QStringList templates = gui::startHubTemplates();
		auto* hub = new gui::StartHub(recent, templates, window);
		hub->setAttribute(Qt::WA_DeleteOnClose);
		hub->setModal(false);
		hub->show();
		return ControlResult::success(QJsonObject{{QStringLiteral("shown"), true},
			{QStringLiteral("recent"), static_cast<int>(recent.size())},
			{QStringLiteral("templates"), static_cast<int>(templates.size())}});
	};
	registry.registerCommand(cmd);
}

void registerScreenshot(ControlRegistry& registry)
{
	ControlCommand cmd = command("window", "screenshot");
	cmd.description = QStringLiteral("Write a PNG of the main window - or, with `window: \"active\"`, of "
		"the top-level window that has focus (a dialog, the palette) - to the absolute `path`. For an "
		"agent that has to SEE the interface it drives: layout, labels and state as rendered, which "
		"no read command reports. Works under the offscreen platform. `width`/`height` are the image's.");
	cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("window"), enumProperty({QStringLiteral("main"), QStringLiteral("active")})}},
		{QStringLiteral("path")});
	cmd.resultSchema = objectSchema({{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("width"), integerProperty()}, {QStringLiteral("height"), integerProperty()}});
	cmd.handler = [](const QJsonObject& args) {
		gui::MainWindow* window = mainWindow();
		if (window == nullptr) { return noInterface(QStringLiteral("window.screenshot")); }
		const QString path = args.value(QStringLiteral("path")).toString();
		if (!QFileInfo(path).isAbsolute())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'path' must be absolute"));
		}
		QWidget* target = window;
		if (args.value(QStringLiteral("window")).toString() == QLatin1String("active")
			&& QApplication::activeWindow() != nullptr)
		{
			target = QApplication::activeWindow();
		}
		const QPixmap image = target->grab();
		if (image.isNull() || !image.save(path, "PNG"))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the window could not be rendered to '%1'").arg(path));
		}
		return ControlResult::success(QJsonObject{{QStringLiteral("path"), path},
			{QStringLiteral("width"), image.width()}, {QStringLiteral("height"), image.height()}});
	};
	registry.registerCommand(cmd);
}

void registerSaveVersion(ControlRegistry& registry)
{
	ControlCommand cmd = command("project", "save_version");
	cmd.description = QStringLiteral("Save the session as the NEXT VERSION of its file - "
		"<name>-01.mmp, -02, ... the first free one - and make that the project's file; the "
		"previous version stays on disk untouched. File > Save as New Version dispatches this. "
		"Refused while the session has no file yet (save it once with project.save first). "
		"Needs no interface.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("previous_file"), stringProperty()}});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject&) {
		Song* song = Engine::getSong();
		const QString previous = song->projectFileName();
		if (previous.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the session has no file yet, so there is no version to follow: "
					"save it once with project.save {path}"));
		}
		QString next = previous;
		do { gui::VersionedSaveDialog::changeFileNameVersion(next, true); }
		while (QFile::exists(next));
		if (!song->guiSaveProjectAs(next))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the new version could not be written to '%1': %2")
					.arg(next, song->saveRefusal()));
		}
		return ControlResult::success(QJsonObject{{QStringLiteral("file"), song->projectFileName()},
			{QStringLiteral("previous_file"), previous}});
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerShellCommands(ControlRegistry& registry)
{
	registerFullscreen(registry);
	registerDetachAll(registry, "detach_all", true);
	registerDetachAll(registry, "attach_all", false);
	registerSettingsDialog(registry);
	registerAbout(registry);
	registerOnlineHelp(registry);
	registerCommandPalette(registry);
	registerShortcuts(registry);
	registerUndoHistory(registry);
	registerTempoMapPanel(registry);
	registerModulatorPanel(registry);
	registerStartHub(registry);
	registerScreenshot(registry);
	registerSaveVersion(registry);
}

void dispatchShellCommand(const QString& id, const QJsonObject& args)
{
	ControlRegistry::instance()->invoke(id, args);
}

} // namespace lmms
