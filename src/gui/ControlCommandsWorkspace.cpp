/*
 * ControlCommandsWorkspace.cpp - the 0.5.0 W3 window commands: windows, clock, help, focus
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

/*! 0.5.0 W3 L13 / L14, the window.* verbs of the lane - interface state, recorded nowhere:
 *  - window.stay_on_top / window.tidy (WF-105): the main window kept above others; the floating windows
 *    (detached panels, dialogs, plug-in windows) cascaded or tiled over the main window's screen;
 *  - window.large_clock (WF-101), window.tuner (WF-147), window.ask (WF-142), window.whats_new (WF-141, shown once per version
 *    at start by installWorkspaceMenus);
 *  - window.focus_panel (WF-132): keyboard focus to a panel, or to the next / previous open one (F6 and
 *    Shift+F6 in the Window menu), so the whole shell is reachable without a pointer;
 *  - window.accessibility_report (WF-129): the controls a screen reader cannot name, and names for them;
 *  - view.crosshair (WF-109): the arranger's cross-hair cursor (ArrangerAids.h), a preference. */

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QJsonArray>
#include <QMenu>
#include <QScreen>

#include "AccessibilityAudit.h"
#include "ArrangerAids.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "MainWindow.h"
#include "Shell.h"
#include "ShellPanel.h"
#include "UiConfigStore.h"
#include "WorkspaceWindows.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int CascadeStep = 32;

ControlResult stayOnTop(const QJsonObject& args)
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return noInterface(QStringLiteral("window.stay_on_top")); }
	const bool before = window->windowFlags().testFlag(Qt::WindowStaysOnTopHint);
	const bool wanted = args.contains(QStringLiteral("enabled")) ? args.value(QStringLiteral("enabled")).toBool() : !before;
	if (wanted != before)
	{
		// Changing a window flag re-creates the native window: keep its state (maximised, fullscreen).
		const Qt::WindowStates state = window->windowState();
		window->setWindowFlag(Qt::WindowStaysOnTopHint, wanted);
		window->setWindowState(state);
		window->show();
	}
	return ControlResult::success({{QStringLiteral("on_top"), wanted}, {QStringLiteral("changed"), wanted != before}});
}

//! The visible top-level windows other than the main one: floating panels, dialogs, plug-in windows. A modal
//! prompt is left where it is, over the window that asked.
QList<QWidget*> floatingWindows(const QWidget* main)
{
	QList<QWidget*> out;
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		const bool window = widget->windowType() == Qt::Window || widget->windowType() == Qt::Dialog || widget->windowType() == Qt::Tool;
		const bool tidyable = window && !widget->isModal() && qobject_cast<QMenu*>(widget) == nullptr;
		if (widget != main && widget->isVisible() && tidyable) { out.append(widget); }
	}
	return out;
}

void cascade(const QList<QWidget*>& windows, const QRect& area)
{
	// The steps start again from the top left before they reach the middle of the screen, so every
	// window's title bar stays on it however many windows there are.
	const int steps = std::max(1, std::min(area.width(), area.height()) / 2 / CascadeStep);
	for (int i = 0; i < windows.size(); ++i)
	{
		const QPoint at = area.topLeft() + QPoint(CascadeStep, CascadeStep) * (i % steps + 1);
		windows[i]->move(at);
		windows[i]->raise();
	}
}

void tile(const QList<QWidget*>& windows, const QRect& area)
{
	const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(windows.size())))));
	const int rows = std::max(1, static_cast<int>((windows.size() + columns - 1) / columns));
	const QSize cell(area.width() / columns, area.height() / rows);
	for (int i = 0; i < windows.size(); ++i)
	{
		windows[i]->setGeometry(QRect(area.topLeft() + QPoint((i % columns) * cell.width(), (i / columns) * cell.height()), cell));
		windows[i]->raise();
	}
}

ControlResult tidy(const QJsonObject& args)
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return noInterface(QStringLiteral("window.tidy")); }
	const QList<QWidget*> windows = floatingWindows(window);
	const QScreen* screen = window->screen() != nullptr ? window->screen() : QGuiApplication::primaryScreen();
	const QRect area = screen != nullptr ? screen->availableGeometry() : window->geometry();
	const QString mode = args.value(QStringLiteral("mode")).toString(QStringLiteral("cascade"));
	if (mode == QLatin1String("tile")) { tile(windows, area); } else { cascade(windows, area); }
	return ControlResult::success({{QStringLiteral("mode"), mode}, {QStringLiteral("windows"), static_cast<int>(windows.size())}});
}

//! The open @a Dialog of @a window, brought to the front, or nullptr: each of these windows is opened once,
//! and asking again raises it (two tuners would share one tap, and closing either would switch it off).
template <typename Dialog>
Dialog* raiseOpen(gui::MainWindow* window)
{
	for (Dialog* open : window->findChildren<Dialog*>(QString(), Qt::FindDirectChildrenOnly))
	{
		if (!open->isVisible()) { continue; }  // closed, waiting for its deferred delete
		open->raise();
		open->activateWindow();
		return open;
	}
	return nullptr;
}

template <typename Dialog>
Dialog* openDialog(gui::MainWindow* window)
{
	if (Dialog* open = raiseOpen<Dialog>(window)) { return open; }
	auto* dialog = new Dialog(window);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setModal(false);
	dialog->show();
	return dialog;
}

ControlResult largeClock(const QJsonObject&)
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return noInterface(QStringLiteral("window.large_clock")); }
	auto* clock = openDialog<gui::LargeClock>(window);
	return ControlResult::success({{QStringLiteral("shown"), true}, {QStringLiteral("position"), clock->refresh()}});
}

ControlResult tunerWindow(const QJsonObject&)
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return noInterface(QStringLiteral("window.tuner")); }
	auto* tuner = openDialog<gui::TunerWindow>(window);
	return ControlResult::success({{QStringLiteral("shown"), true}, {QStringLiteral("note"), tuner->refresh()}});
}

ControlResult askWindow(const QJsonObject& args)
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return noInterface(QStringLiteral("window.ask")); }
	auto* dialog = openDialog<gui::AssistantDialog>(window);
	const QString question = args.value(QStringLiteral("question")).toString();
	const int answers = question.trimmed().isEmpty() ? 0 : dialog->ask(question);
	return ControlResult::success({{QStringLiteral("shown"), true}, {QStringLiteral("answers"), answers}});
}

ControlResult whatsNewWindow(const QJsonObject&)
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return noInterface(QStringLiteral("window.whats_new")); }
	const ControlResult notes = ControlRegistry::instance()->invoke(QStringLiteral("app.whats_new"));
	if (!notes.ok) { return notes; }
	if (raiseOpen<gui::WhatsNewDialog>(window) == nullptr)
	{
		auto* dialog = new gui::WhatsNewDialog(notes.result, window);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
		dialog->show();
	}
	markWhatsNewSeen();
	return ControlResult::success({{QStringLiteral("shown"), true}, {QStringLiteral("version"), notes.result.value(QStringLiteral("version"))}});
}

//! The first widget inside @a root that takes keyboard focus, or nullptr.
QWidget* firstFocusable(QWidget* root)
{
	for (QWidget* child : root->findChildren<QWidget*>())
	{
		if (child->isVisible() && child->isEnabled() && (child->focusPolicy() & Qt::TabFocus)) { return child; }
	}
	return nullptr;
}

//! The first widget inside @a frame's content that takes keyboard focus; when the content has none (an empty
//! Inspector), the panel's own header button, so the focus does move into the panel and F6 goes on from it.
QWidget* focusTarget(gui::ShellPanel* frame)
{
	QWidget* content = frame->content() != nullptr ? frame->content() : frame;
	if (QWidget* inside = firstFocusable(content)) { return inside; }
	QWidget* header = firstFocusable(frame);
	return header != nullptr ? header : content;
}

//! The open panels, in the register's order.
QStringList openPanels(const gui::Shell* shell)
{
	QStringList out;
	for (const gui::ShellPanelSpec& spec : gui::ShellLayout::panels())
	{
		if (shell->model().isVisible(spec.id) && shell->panel(spec.id) != nullptr) { out.append(spec.id); }
	}
	return out;
}

//! The panel 'panel' names: an id, or next / previous from the focused one.
QString panelToFocus(const gui::Shell* shell, const QString& panel)
{
	if (panel != QLatin1String("next") && panel != QLatin1String("previous")) { return panel; }
	const QStringList open = openPanels(shell);
	if (open.isEmpty()) { return QString(); }
	const int at = static_cast<int>(open.indexOf(shell->model().focused()));
	const int step = panel == QLatin1String("next") ? 1 : -1;
	const int size = static_cast<int>(open.size());
	if (at < 0) { return step > 0 ? open.first() : open.last(); }  // from outside the panels: the first, or the last
	return open.at(((at + step) % size + size) % size);
}

ControlResult focusPanel(const QJsonObject& args)
{
	gui::MainWindow* window = controlMainWindow();
	gui::Shell* shell = window != nullptr ? window->shell() : nullptr;
	if (shell == nullptr) { return noInterface(QStringLiteral("window.focus_panel")); }
	const QString id = panelToFocus(shell, args.value(QStringLiteral("panel")).toString(QStringLiteral("next")));
	if (!gui::ShellLayout::isPanel(id)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no panel '%1'").arg(id)); }
	if (!shell->model().isVisible(id)) { shell->revealPanel(id); }
	QWidget* target = focusTarget(shell->panel(id));
	target->setFocus(Qt::ShortcutFocusReason);
	return ControlResult::success({{QStringLiteral("panel"), id}, {QStringLiteral("focus"), target->accessibleName().isEmpty()
		? QString::fromLatin1(target->metaObject()->className()) : target->accessibleName()}});
}

ControlResult crosshair(const QJsonObject& args)
{
	const bool before = gui::uistore::read(QStringLiteral("crosshair")).value(QStringLiteral("enabled")).toBool();
	const bool wanted = args.contains(QStringLiteral("enabled")) ? args.value(QStringLiteral("enabled")).toBool() : !before;
	return gui::uistore::write(QStringLiteral("crosshair"), {{QStringLiteral("enabled"), wanted}}, {{QStringLiteral("enabled"), wanted}},
		{QStringLiteral("view.crosshair"), {{QStringLiteral("enabled"), before}}},
		[] { gui::showCrosshair(gui::uistore::read(QStringLiteral("crosshair")).value(QStringLiteral("enabled")).toBool()); });
}

ControlResult accessibilityReport(const QJsonObject& args)
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return noInterface(QStringLiteral("window.accessibility_report")); }
	return ControlResult::success(gui::auditAccessibility(window, args.value(QStringLiteral("fill")).toBool()));
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&), const char* group = "window", bool mutating = false)
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

void registerWorkspaceCommands(ControlRegistry& registry)
{
	add(registry, "stay_on_top", QStringLiteral("Keep the main window above other programs' windows, or not ('enabled'; "
		"toggles when left out). Its maximised or fullscreen state is kept. Interface state."),
		objectSchema({{QStringLiteral("enabled"), booleanProperty()}}), stayOnTop);
	add(registry, "tidy", QStringLiteral("Tidy the floating windows - detached panels, dialogs (not a modal prompt), plug-in windows - over the "
		"main window's screen: 'cascade' (the default: stepped from its top left corner, starting again before the middle) or "
		"'tile' (a grid). Interface state."),
		objectSchema({{QStringLiteral("mode"), enumProperty({QStringLiteral("cascade"), QStringLiteral("tile")})}}), tidy);
	add(registry, "large_clock", QStringLiteral("Open a large clock: the play position in bars, beats and ticks and in "
		"minutes and seconds, big enough to read across the room (an open one is raised instead). A display."), objectSchema({}), largeClock);
	add(registry, "tuner", QStringLiteral("Open the tuner: the note nearest the pitch of its source (the input, unless "
		"tuner.set chose another), the cents off it and a needle (an open one is raised instead); closing it switches the tuner "
		"off. A display."),
		objectSchema({}), tunerWindow);
	add(registry, "ask", QStringLiteral("Open Help > Ask (app.ask in a window), asking 'question' at once when given; "
		"'answers' is how many it found (an open one is raised and asked). Offline."), objectSchema({{QStringLiteral("question"), stringProperty()}}), askWindow);
	add(registry, "whats_new", QStringLiteral("Show this version's release notes (app.whats_new) and remember they were "
		"shown, so the start does not show them again until the next version."), objectSchema({}), whatsNewWindow);
	add(registry, "focus_panel", QStringLiteral("Move the keyboard focus into shell panel 'panel' (window.get_state's panel "
		"ids; a hidden one is shown first), or to the 'next' / 'previous' open panel from the focused one (the default "
		"is next) - F6 and Shift+F6. Interface state."),
		objectSchema({{QStringLiteral("panel"), stringProperty()}}), focusPanel);
	add(registry, "accessibility_report", QStringLiteral("Walk the main window's controls and count those a screen reader "
		"would announce with no name, no role (a bare client or pane) or, for a control that shows a value, no value, and "
		"those Tab cannot reach; with 'fill', give each nameless one the name it already carries (its constructed name, its "
		"parameter's, its tooltip, its text). Lists, at most 50 of each, the controls with nothing to name them by and those "
		"with no role or value. The interface fills names this way once at start. Interface state."), objectSchema({{QStringLiteral("fill"), booleanProperty()}}), accessibilityReport);
	add(registry, "crosshair", QStringLiteral("Show a cross-hair cursor across the arranger - a vertical and a horizontal guide "
		"through the pointer - or hide it ('enabled'; toggles when left out). A preference in the config file; one undo step."),
		objectSchema({{QStringLiteral("enabled"), booleanProperty()}}), crosshair, "view", true);
}

} // namespace lmms
