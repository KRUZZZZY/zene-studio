/*
 * FocusDeskPane.cpp - the Focus Desk as a page of the main window.
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

#include "FocusDeskPane.h"

#include <QAction>
#include <QBoxLayout>
#include <QJsonObject>
#include <QMenu>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QVBoxLayout>

// The editors are named through their own types: a pointer to a concrete window
// converts to QWidget* only where that window is complete, so this is what lets
// the header's forward declarations be enough for everyone else.
#include "AutomationEditor.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControllerRackView.h"
#include "FocusDesk.h"
#include "GuiApplication.h"
#include "MixerView.h"
#include "PianoRoll.h"
#include "ProjectNotes.h"
#include "SongEditor.h"

namespace lmms::gui
{

namespace
{

//! The config keys, in one place so a renamed key cannot be read under one
//! spelling and written under another.
QString deskEnabledKey() { return QStringLiteral("focusdesk"); }
QString deskFocusKey()   { return QStringLiteral("focusdesk.focus"); }
QString deskDensityKey() { return QStringLiteral("focusdesk.density"); }

} // namespace

FocusDeskPane::Mountables FocusDeskPane::editorsFromGui()
{
	// Only the rows with a live editor appear. The other five register rows
	// (browser, session, inspector, modulation, learn) are staged, and the desk
	// states why on their chips rather than being handed a placeholder.
	auto* gui = getGUI();
	if (gui == nullptr) { return {}; }

	return {
		{QStringLiteral("arrangement"), gui->songEditor()},
		{QStringLiteral("detaileditor"), gui->pianoRoll()},
		{QStringLiteral("mixer"), gui->mixerView()},
		{QStringLiteral("automation"), gui->automationEditor()},
		{QStringLiteral("devicechain"), gui->getControllerRackView()},
		{QStringLiteral("project"), gui->getProjectNotes()},
	};
}

FocusDeskPane::FocusDeskPane(QMdiArea* workspace, QWidget* workspacePage,
	QBoxLayout* hostLayout, QWidget* parent) :
	QWidget(parent),
	m_workspace(workspace),
	m_workspacePage(workspacePage),
	m_resolveFromGui(true)
{
	build(hostLayout);
	readConfig();
}

FocusDeskPane::FocusDeskPane(QMdiArea* workspace, QWidget* workspacePage,
	QBoxLayout* hostLayout, const Mountables& mountables, QWidget* parent) :
	QWidget(parent),
	m_workspace(workspace),
	m_workspacePage(workspacePage),
	m_mountables(mountables)
{
	build(hostLayout);
	readConfig();
}

FocusDeskPane::~FocusDeskPane()
{
	// Hand every editor back before the desk dies: a live editor left inside a
	// dying shell is a leak at best and a dangling pointer at worst.
	if (m_active) { release(); }
}

void FocusDeskPane::build(QBoxLayout* hostLayout)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	m_desk = new FocusDesk(this);
	layout->addWidget(m_desk);

	// The desk takes the workspace page's place in the page's own layout, so
	// the caller installs the pane by constructing it and nothing else.
	if (hostLayout != nullptr) { hostLayout->addWidget(this); }
	hide();

	connect(m_desk, &FocusDesk::focusChanged, this,
		[this](const QString&)
		{
			// A focus change made *while* the pane is switching is part of the
			// switch - mounting a module moves the focus - and the switch
			// persists its own finished state at the end. Persisting the
			// half-way state here would announce a desk that is neither.
			if (m_settling) { return; }
			saveToConfig();
		});

	// The desk's mode IS the `ui/focusdesk` setting, so the setting is what
	// drives it: writing that key enters or leaves the mode, whoever writes it -
	// the View menu's action (which dispatches the registry's settings.set), an
	// agent on the socket, or control.undo putting an earlier value back. This
	// is what makes the action's declaration TRUE rather than decorative
	// (SPEC A11: one action, one implementation), and it is why the key is
	// seeded in readConfig(): the first write to a key that does not exist yet
	// is an insert, and an insert does not announce itself.
	connect(ConfigManager::inst(), &ConfigManager::valueChanged, this,
		[this](const QString& cls, const QString& attribute, const QString& value)
		{
			// The pane writes this key too, and setValue emits synchronously,
			// so without this the observer would be answering a write it just
			// made - an unbounded recursion through setValue, not a slow loop.
			// Only the pane hearing *itself* is suppressed; every other
			// listener still sees what it wrote.
			if (m_settling) { return; }
			if (cls != QStringLiteral("ui") || attribute != deskEnabledKey()) { return; }
			const bool wanted = value.toInt() != 0;
			if (wanted == m_active) { return; }
			if (setDeskActive(wanted) == wanted) { return; }
			// The desk refused: nothing could be claimed, so it stayed off. Put
			// the state back rather than leave the setting describing a mode the
			// window is not in - the next start would read it, try again and
			// refuse again. saveToConfig() writes the state the pane is actually
			// in and, being guarded, does not come straight back here.
			saveToConfig();
		});
}

void FocusDeskPane::readConfig()
{
	auto* conf = ConfigManager::inst();
	m_wantActive = conf->value("ui", deskEnabledKey()).toInt() != 0;

	// The key has to EXIST before a write to it can announce itself:
	// ConfigManager::setValue emits valueChanged only when it REPLACES a value,
	// and nothing writes this one by default, so without this the first
	// `settings.set ui/focusdesk 1` - the View menu's action, or an agent on the
	// socket - would land silently and the desk would not follow it. Seeded in
	// memory only, with the state the window actually started in, so merely
	// opening the product still writes no config file.
	if (conf->value("ui", deskEnabledKey()).isEmpty())
	{
		conf->setValue("ui", deskEnabledKey(), QStringLiteral("0"));
	}

	const QString density = conf->value("ui", deskDensityKey());
	QVariantMap layout;
	if (!density.isEmpty()) { layout.insert(QStringLiteral("density"), density); }
	const QString focus = conf->value("ui", deskFocusKey());
	if (!focus.isEmpty()) { layout.insert(QStringLiteral("focus"), focus); }
	if (!layout.isEmpty()) { m_desk->restoreLayout(layout); }
}

bool FocusDeskPane::applyConfiguredState()
{
	// Deferring this past the constructor is not politeness: the editors are
	// built by GuiApplication *after* it builds MainWindow, so at construction
	// time their accessors hand back pointers that were never assigned. There
	// is no way to refuse them - only to not read them yet.
	if (m_wantActive && !m_active) { setDeskActive(true); }
	return m_active;
}

void FocusDeskPane::saveToConfig() const
{
	// Guarded, because this is the pane's own write to the key the pane also
	// follows: the emission below reaches the observer while this scope is
	// still active, which is what stops the pane from answering itself. The
	// rollback restores the caller's own value, so a write made inside a
	// switch does not end the switch's guard early.
	QScopedValueRollback<bool> settling(m_settling, true);
	auto* conf = ConfigManager::inst();
	conf->setValue("ui", deskEnabledKey(), QString::number(m_active ? 1 : 0));
	conf->setValue("ui", deskFocusKey(), m_desk->focused());
	conf->setValue("ui", deskDensityKey(), FocusDeskModules::densityName(m_desk->density()));
	conf->saveConfigFile();
}

void FocusDeskPane::claim()
{
	// Resolved here, not at construction: MainWindow builds this pane before
	// GuiApplication builds the editors, and until it has, their accessors
	// return uninitialised pointers - not nulls, so there is nothing to test
	// for. Only a caller that knows the editors exist reaches this.
	if (m_resolveFromGui) { m_mountables = editorsFromGui(); }

	for (const auto& entry : m_mountables)
	{
		QWidget* editor = entry.second;
		if (editor == nullptr) { continue; }

		auto* subWindow = qobject_cast<QMdiSubWindow*>(editor->parentWidget());
		// An editor the product is not hosting yet has no subwindow to come
		// back to, so it is left alone rather than adopted irreversibly.
		if (subWindow == nullptr || m_sources.contains(entry.first)) { continue; }
		if (m_workspace != nullptr && subWindow->mdiArea() != m_workspace) { continue; }
		if (!m_desk->mountModule(entry.first, editor)) { continue; }

		m_sources.insert(entry.first, Origin{subWindow, !subWindow->isHidden()});
		// Empty the subwindow only after the desk has taken the widget: if the
		// desk refused it, the product's own window must still be intact.
		subWindow->setWidget(nullptr);
		subWindow->hide();
	}
}

void FocusDeskPane::release()
{
	m_desk->releaseAll();
	for (auto it = m_sources.begin(); it != m_sources.end(); ++it)
	{
		for (const auto& entry : m_mountables)
		{
			if (entry.first != it.key()) { continue; }
			// The editor can already be gone. `MainWindow::~MainWindow` deletes
			// automationEditor, pianoRoll and songEditor before the pane it owns
			// is destroyed, so at shutdown there is nothing left to hand back;
			// claim() emptied the subwindow already, so skipping is the whole of
			// the correct action. Handing a freed widget to setWidget() would be
			// the same use-after-free the desk's own QPointer guards against.
			QWidget* editor = entry.second;
			if (editor == nullptr) { break; }
			if (it.value().window != nullptr)
			{
				it.value().window->setWidget(editor);
				// The window comes back in the state the product left it in,
				// not hidden: the workspace is about to be shown again.
				it.value().window->setVisible(it.value().wasVisible);
			}
			break;
		}
	}
	m_sources.clear();
}

bool FocusDeskPane::setDeskActive(bool active)
{
	if (active == m_active) { return m_active; }
	// The whole switch is one settling period. Claiming a module moves the
	// focus, and releasing one moves it back, so the desk speaks while this
	// runs; what it says is the state it is leaving, not the state it is
	// entering. The switch persists its own finished state below.
	QScopedValueRollback<bool> settling(m_settling, true);
	if (active)
	{
		claim();
		if (m_sources.isEmpty()) { return false; }
	}
	else
	{
		release();
	}

	m_active = active;
	if (m_workspacePage != nullptr) { m_workspacePage->setVisible(!active); }
	setVisible(active);
	saveToConfig();
	return m_active;
}

QStringList FocusDeskPane::mountedIds() const
{
	QStringList ids;
	if (m_desk == nullptr) { return ids; }
	for (const auto& row : m_desk->registerRows())
	{
		if (m_sources.contains(row.id)) { ids.append(row.id); }
	}
	return ids;
}

QMdiSubWindow* FocusDeskPane::sourceWindow(const QString& moduleId) const
{
	return m_sources.value(moduleId).window;
}

void addFocusDeskToggle(QMenu* menu, FocusDeskPane* pane)
{
	if (menu == nullptr) { return; }

	QAction* action = menu->addAction(QObject::tr("Focus Desk"));
	action->setCheckable(true);
	action->setStatusTip(QObject::tr("Lay the editors out as one desk: a stage, two rails and a dock."));
	// A11/A15: the action declares - AND DISPATCHES - the registry command it
	// implements. The desk's mode is the `ui/focusdesk` setting, so entering it
	// is settings.set for that key: the menu item and an agent run the same
	// command, and there is no second way into the mode. The declaration goes in
	// the dynamic property "controlCommand" rather than objectName() (the
	// surface's own key) or data() (the channel of last resort, accepted only
	// when it already resolves; see tests/agent-surface-gate.py).
	action->setProperty("controlCommand", QStringLiteral("settings.set"));
	if (pane == nullptr)
	{
		// No pane means no way to enter the mode, so the entry says so instead
		// of offering a toggle that would do nothing.
		action->setEnabled(false);
		return;
	}

	action->setChecked(pane->deskActive());
	QObject::connect(action, &QAction::toggled, pane, [pane, action](bool on)
	{
		ControlRegistry::instance()->invoke(QStringLiteral("settings.set"),
			QJsonObject{{QStringLiteral("key"), QStringLiteral("ui/") + deskEnabledKey()},
				{QStringLiteral("value"), on ? QStringLiteral("1") : QStringLiteral("0")}});
		// The pane follows the setting (its own valueChanged handler), so the
		// tick is synced to what actually happened rather than to what was
		// asked for: the desk refuses when it cannot claim an editor, and a menu
		// describing a mode the window is not in is worse than no menu.
		if (pane->deskActive() != on)
		{
			QSignalBlocker blocker(action);
			action->setChecked(pane->deskActive());
		}
	});
}

} // namespace lmms::gui
