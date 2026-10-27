/*
 * PanelShortcuts.cpp - keys bound to one panel's own actions, beside the global menu map
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

/*! WF-110/111, stage 1 (0.5.0 L13): the four editing panels get a shortcut scope of their own.
 *  One key can mean four different things - the arranger's tool, the note editor's tool, the
 *  automation editor's mode, Perform's transport - and only the panel that holds the keyboard acts
 *  on it. The routing is an application event filter (a panel claims its bound key over a window
 *  menu action through ShortcutOverride, the way XyPad claims its arrows) and every action is a
 *  registered command, so a bound key, the panel's own button and the agent surface run one
 *  implementation (SPEC A11). */

#include "PanelShortcuts.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QWidget>

#include "ControlRegistry.h"
#include "PerformView.h"
#include "ShellPanel.h"
#include "UiConfigStore.h"

namespace lmms::gui::panelshortcuts
{

namespace
{

const QString PanelsKey = QStringLiteral("panels");

//! The scopes, in the shortcuts list's order.
const QStringList kScopes{QStringLiteral("arranger"), QStringLiteral("note_editor"),
	QStringLiteral("automation_editor"), QStringLiteral("perform")};

//! Every bindable panel action. None has a key of its own: a panel's existing keys keep working
//! until the user binds a scoped action, so a user who never opens the shortcuts page is unaffected.
const QList<Action>& actionTable()
{
	static const QList<Action> table{
		// The arranger's view commands (arrangement.zoom / fit_tracks / set_tool): the View menu and
		// the app's keyboard zoom dispatch these already, so a bound key and the menu are one call.
		{QStringLiteral("arranger"), QStringLiteral("Arranger ▸ Zoom In"), QStringLiteral("Zoom In"),
			QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), 4}}},
		{QStringLiteral("arranger"), QStringLiteral("Arranger ▸ Zoom Out"), QStringLiteral("Zoom Out"),
			QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -4}}},
		{QStringLiteral("arranger"), QStringLiteral("Arranger ▸ Fit Tracks"), QStringLiteral("Fit Tracks"),
			QStringLiteral("arrangement.fit_tracks"), {}},
		{QStringLiteral("arranger"), QStringLiteral("Arranger ▸ Draw Tool"), QStringLiteral("Draw Tool"),
			QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("draw")}}},
		{QStringLiteral("arranger"), QStringLiteral("Arranger ▸ Knife Tool"), QStringLiteral("Knife Tool"),
			QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("knife")}}},
		{QStringLiteral("arranger"), QStringLiteral("Arranger ▸ Select Tool"), QStringLiteral("Select Tool"),
			QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("select")}}},
		// The note editor's tools and its zoom, as its header's buttons set them (window.note_editor_set).
		{QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Draw Tool"), QStringLiteral("Draw Tool"),
			QStringLiteral("window.note_editor_set"), {{QStringLiteral("tool"), QStringLiteral("draw")}}},
		{QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Select Tool"), QStringLiteral("Select Tool"),
			QStringLiteral("window.note_editor_set"), {{QStringLiteral("tool"), QStringLiteral("select")}}},
		{QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Slice Tool"), QStringLiteral("Slice Tool"),
			QStringLiteral("window.note_editor_set"), {{QStringLiteral("tool"), QStringLiteral("slice")}}},
		{QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Erase Tool"), QStringLiteral("Erase Tool"),
			QStringLiteral("window.note_editor_set"), {{QStringLiteral("tool"), QStringLiteral("erase")}}},
		{QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Paint Tool"), QStringLiteral("Paint Tool"),
			QStringLiteral("window.note_editor_set"), {{QStringLiteral("tool"), QStringLiteral("paint")}}},
		{QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Mute Tool"), QStringLiteral("Mute Tool"),
			QStringLiteral("window.note_editor_set"), {{QStringLiteral("tool"), QStringLiteral("mute")}}},
		{QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Audition Tool"), QStringLiteral("Audition Tool"),
			QStringLiteral("window.note_editor_set"), {{QStringLiteral("tool"), QStringLiteral("audition")}}},
		{QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Zoom to Selection"),
			QStringLiteral("Zoom to Selection"), QStringLiteral("window.note_editor_set"),
			{{QStringLiteral("zoom_to_selection"), true}}},
		// The automation editor's four edit modes - the toolbar's own buttons (automation.editor_mode).
		{QStringLiteral("automation_editor"), QStringLiteral("Automation Editor ▸ Draw Mode"),
			QStringLiteral("Draw Mode"), QStringLiteral("automation.editor_mode"),
			{{QStringLiteral("mode"), QStringLiteral("draw")}}},
		{QStringLiteral("automation_editor"), QStringLiteral("Automation Editor ▸ Erase Mode"),
			QStringLiteral("Erase Mode"), QStringLiteral("automation.editor_mode"),
			{{QStringLiteral("mode"), QStringLiteral("erase")}}},
		{QStringLiteral("automation_editor"), QStringLiteral("Automation Editor ▸ Draw OutValues Mode"),
			QStringLiteral("Draw OutValues Mode"), QStringLiteral("automation.editor_mode"),
			{{QStringLiteral("mode"), QStringLiteral("draw_out_values")}}},
		{QStringLiteral("automation_editor"), QStringLiteral("Automation Editor ▸ Edit Tangents Mode"),
			QStringLiteral("Edit Tangents Mode"), QStringLiteral("automation.editor_mode"),
			{{QStringLiteral("mode"), QStringLiteral("edit_tangents")}}},
		// Perform: its window's own keys, as the commands its buttons run.
		{QStringLiteral("perform"), QStringLiteral("Perform ▸ Stop All"), QStringLiteral("Stop All"),
			QStringLiteral("session.stop_all"), {}},
		{QStringLiteral("perform"), QStringLiteral("Perform ▸ Start Transport"), QStringLiteral("Start Transport"),
			QStringLiteral("transport.play"), {}},
		{QStringLiteral("perform"), QStringLiteral("Perform ▸ Stop Transport"), QStringLiteral("Stop Transport"),
			QStringLiteral("transport.stop"), {}},
		{QStringLiteral("perform"), QStringLiteral("Perform ▸ Launch Scene 1"), QStringLiteral("Launch Scene 1"),
			QStringLiteral("session.launch_scene"), {{QStringLiteral("scene"), 0}}},
		{QStringLiteral("perform"), QStringLiteral("Perform ▸ Launch Scene 2"), QStringLiteral("Launch Scene 2"),
			QStringLiteral("session.launch_scene"), {{QStringLiteral("scene"), 1}}},
	};
	return table;
}

//! @a event as portable key text; empty for a bare modifier or a key Qt cannot name portably.
QString keyText(const QKeyEvent* event)
{
	const int key = event->key();
	switch (key)
	{
		case 0:
		case Qt::Key_unknown:
		case Qt::Key_Shift:
		case Qt::Key_Control:
		case Qt::Key_Alt:
		case Qt::Key_Meta:
		case Qt::Key_AltGr:
			return QString();
		default:
			break;
	}
	// Keypad and group-switch are not part of what the user bound: the same key on the keypad is
	// the same shortcut (and Qt's own matching ignores them the same way).
	const Qt::KeyboardModifiers modifiers = event->modifiers() & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	const QKeySequence sequence{QKeyCombination(modifiers, static_cast<Qt::Key>(key))};
#else
	const QKeySequence sequence{key | static_cast<int>(modifiers)};
#endif
	return sequence.toString(QKeySequence::PortableText);
}

//! The action of @a scope bound to @a keys, or nullptr.
const Action* actionOnKey(const QString& scope, const QString& keys)
{
	if (keys.isEmpty()) { return nullptr; }
	const QJsonObject chosen = overrides();
	for (const Action& entry : actionTable())
	{
		if (entry.scope != scope) { continue; }
		const QJsonValue chosenKeys = stored(chosen, entry.scope, entry.id);
		// Only a string is an override (cluster 5373: a hand-edited config must not unbind by type).
		if (chosenKeys.isString() && ShortcutMap::portable(chosenKeys.toString()) == keys) { return &entry; }
	}
	return nullptr;
}

//! The panel whose contents hold the keyboard: an editor mounted in a shell panel, or Perform's
//! window. A dialog, or a panel floated into a window of its own, is nobody's scope.
QString scopeOf(const QWidget* widget)
{
	for (const QWidget* w = widget; w != nullptr; w = w->parentWidget())
	{
		if (qobject_cast<const PerformView*>(w) != nullptr) { return QStringLiteral("perform"); }
		if (w->isWindow()) { return QString(); }  // dialogs and detached panels do not inherit their parent's scope
		if (const auto* frame = qobject_cast<const ShellPanel*>(w); frame != nullptr)
		{
			const QString id = frame->id();
			if (id == QLatin1String("arranger")) { return QStringLiteral("arranger"); }
			if (id == QLatin1String("detail")) { return QStringLiteral("note_editor"); }
			if (id == QLatin1String("automation")) { return QStringLiteral("automation_editor"); }
			return QString();
		}
	}
	return QString();
}

//! The classes that type what the user presses: a line edit, a text area, a spin box, a key
//! capture, a combo box's own line editor. Matched by class NAME through inherits() because the
//! bases are Qt's, not this header's, and the router must not link every widget class.
bool typesText(const QWidget* widget)
{
	static const char* const kTyping[] = {"QLineEdit", "QTextEdit", "QPlainTextEdit",
		"QAbstractSpinBox", "QKeySequenceEdit", "QComboBox"};
	for (const char* name : kTyping)
	{
		if (widget->inherits(name)) { return true; }
	}
	return false;
}

//! A widget that types what the user presses keeps its keys, even when it sits in a panel. The
//! walk stops at the panel (or Perform) that frames it: a panel is not a text entry, whatever it
//! contains elsewhere.
bool textEntry(const QWidget* widget)
{
	for (const QWidget* w = widget; w != nullptr && w->parentWidget() != nullptr; w = w->parentWidget())
	{
		if (typesText(w)) { return true; }
		if (qobject_cast<const ShellPanel*>(w) != nullptr || qobject_cast<const PerformView*>(w) != nullptr) { break; }
	}
	return false;
}

/*! The one router: Qt calls an application event filter before the target's own handlers, so the
 *  panel's bound key is claimed before a window QAction's shortcut and before the panel's built-in
 *  key table sees it. The command runs on the ShortcutOverride (the event Qt sends to ask "is this
 *  key a shortcut here?"), which is also what makes the panel's meaning win over a window menu
 *  action on the same key; the KeyPress that follows is consumed without running anything again,
 *  so the panel's own key table never sees it either. It runs on the GUI thread only (Qt delivers
 *  key events there) and does no I/O: the config read is ConfigManager's in-memory settings. */
class Router : public QObject
{
public:
	explicit Router(QObject* parent) : QObject(parent) {}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		Q_UNUSED(watched)
		const QEvent::Type type = event->type();
		if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress) { return false; }
		QWidget* focus = QApplication::focusWidget();
		if (focus == nullptr || textEntry(focus)) { return false; }
		const QString scope = scopeOf(focus);
		if (scope.isEmpty()) { return false; }
		const QString keys = keyText(static_cast<QKeyEvent*>(event));
		if (keys.isEmpty() || actionOnKey(scope, keys) == nullptr) { return false; }
		if (type == QEvent::ShortcutOverride) { handle(scope, static_cast<QKeyEvent*>(event)); }
		event->accept();
		return true;
	}
};

QPointer<Router> s_router;

} // namespace

const QList<Action>& actions()
{
	return actionTable();
}

QStringList scopes()
{
	return kScopes;
}

QString scopeTitle(const QString& scope)
{
	if (scope == QLatin1String("arranger")) { return QStringLiteral("Arranger"); }
	if (scope == QLatin1String("note_editor")) { return QStringLiteral("Note Editor"); }
	if (scope == QLatin1String("automation_editor")) { return QStringLiteral("Automation Editor"); }
	if (scope == QLatin1String("perform")) { return QStringLiteral("Perform"); }
	return QString();
}

bool knownScope(const QString& scope)
{
	return kScopes.contains(scope);
}

const Action* actionById(const QString& id)
{
	for (const Action& entry : actionTable())
	{
		if (entry.id == id) { return &entry; }
	}
	return nullptr;
}

const Action* action(const QString& scope, const QString& id)
{
	for (const Action& entry : actionTable())
	{
		if (entry.scope == scope && entry.id == id) { return &entry; }
	}
	return nullptr;
}

QJsonObject overrides()
{
	return uistore::read(QLatin1String(ShortcutMap::StoreKey)).value(PanelsKey).toObject();
}

QJsonValue stored(const QJsonObject& panels, const QString& scope, const QString& id)
{
	return panels.value(scope).toObject().value(id);
}

QJsonObject withStored(const QJsonObject& panels, const QString& scope, const QString& id, const QJsonValue& keys)
{
	QJsonObject out = panels;
	QJsonObject within = out.value(scope).toObject();
	if (keys.isUndefined()) { within.remove(id); } else { within.insert(id, keys); }
	if (within.isEmpty()) { out.remove(scope); } else { out.insert(scope, within); }
	return out;
}

QList<ShortcutMap::Binding> bindings()
{
	const QJsonObject chosen = overrides();
	QList<ShortcutMap::Binding> out;
	for (const Action& entry : actionTable())
	{
		const QJsonValue chosenKeys = stored(chosen, entry.scope, entry.id);
		ShortcutMap::Binding binding;
		binding.id = entry.id;
		binding.scope = entry.scope;
		binding.command = entry.command;
		binding.overridden = chosenKeys.isString();
		binding.keys = binding.overridden ? ShortcutMap::portable(chosenKeys.toString()) : QString();
		out.append(binding);
	}
	return out;
}

QString portableKey(const QKeyEvent* event)
{
	return keyText(event);
}

bool claims(const QString& scope, const QKeyEvent* event)
{
	return actionOnKey(scope, keyText(event)) != nullptr;
}

bool textEntryHasFocus()
{
	return textEntry(QApplication::focusWidget());
}

QString focusedScope()
{
	return scopeOf(QApplication::focusWidget());
}

bool handle(const QString& scope, QKeyEvent* event)
{
	const Action* chosen = actionOnKey(scope, keyText(event));
	if (chosen == nullptr) { return false; }
	ControlRegistry::instance()->invoke(chosen->command, chosen->args);
	return true;
}

void install()
{
	if (s_router.isNull() && qApp != nullptr)
	{
		s_router = new Router(qApp);
		qApp->installEventFilter(s_router);
	}
}

void uninstall()
{
	if (!s_router.isNull() && qApp != nullptr) { qApp->removeEventFilter(s_router); }
	delete s_router.data();
	s_router = nullptr;
}

} // namespace lmms::gui::panelshortcuts
