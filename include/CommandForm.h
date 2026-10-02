/*
 * CommandForm.h - a registered command's arguments, as a form built from its schema
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

#ifndef LMMS_GUI_COMMAND_FORM_H
#define LMMS_GUI_COMMAND_FORM_H

#include <functional>

#include <QDialog>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "ControlRegistry.h"
#include "lmms_export.h"

class QAction;
class QFormLayout;
class QLabel;
class QLineEdit;
class QMenu;

namespace lmms::gui
{

/*! One way into a registered command from the interface. `fixed` arguments come from where it
 *  was asked (the track a menu belongs to, the selected clip) and are not editable; `defaults`
 *  pre-fill fields the user may change. `ask` false dispatches at once with `fixed` alone. */
struct CommandCall
{
	QString id;
	QJsonObject fixed;
	QJsonObject defaults;
	QString title;  //!< the form's title; the command id when empty
	bool ask = true;
	//! Show the reply (a floating message) on success - for a command whose answer is the
	//! point (a detection, an analysis), not just its effect.
	bool showResult = false;
};

/*! The shell selection's ids under the keys commands take them by: `track`, `clip`, `channel`
 *  and `target` (the selected channel, else the selected track). Only keys @a id's schema
 *  declares AS STRINGS are returned (session.* takes `track` as a column index, which an id is
 *  not), so the result can be passed as arguments as it is. */
LMMS_EXPORT QJsonObject selectionContext(const QString& id);

/*! A form for one command, built from its argsSchema (the JSON-schema subset the registry
 *  validates): a field per property, typed - an enum or a boolean is a choice, an integer or a
 *  number an edit with the schema's bounds, a string an edit, an array or object JSON text.
 *  A path argument (ScriptCommandBridge::isPathArgument) gets a file or folder picker, and a
 *  property with `"format": "keys"` captures a shortcut by being pressed.
 *  A field left empty is not sent, so an optional argument keeps the command's own default.
 *  Accepting runs the command through the registry - the same path the socket and Lua take
 *  (SPEC A11) - and a refusal keeps the form open with the reason; success closes it.
 *  One form serves every command, so no feature needs a hand-written dialog to be reachable. */
class LMMS_EXPORT CommandForm : public QDialog
{
	Q_OBJECT
public:
	explicit CommandForm(const CommandCall& call, QWidget* parent = nullptr);

	//! The editable fields' keys, in schema order (the test's and a caller's view).
	QStringList fields() const;
	//! Fill field @a key; false when there is no such field or @a value does not fit it.
	bool setField(const QString& key, const QJsonValue& value);
	//! The arguments the form would send: fixed ones and every filled field. False (with
	//! @a error) when a field holds text that is not its type or a required one is empty.
	bool collect(QJsonObject* args, QString* error) const;
	//! The last run's result (a failure until the command has run).
	const ControlResult& result() const { return m_result; }
	//! The text shown under the fields after a refusal.
	QString errorText() const;

public slots:
	//! Run the command; closes on success, stays open with the reason otherwise.
	void accept() override;

signals:
	void ran(const lmms::ControlResult& result);

private:
	struct Field
	{
		QString key;
		QJsonObject spec;
		QWidget* editor = nullptr;
		bool required = false;
	};

	void addField(QFormLayout* form, const QString& key, const QJsonObject& spec, bool required);
	//! The row widget for a field; @a editor receives the widget that holds its value.
	QWidget* makeEditor(const QString& key, const QJsonObject& spec, bool required, QWidget** editor);
	static void constrain(QLineEdit* edit, const QJsonObject& spec);
	QWidget* withBrowse(QLineEdit* edit, const QString& key);
	bool readField(const Field& field, QJsonValue* value, QString* error) const;

	CommandCall m_call;
	QList<Field> m_fields;
	QLabel* m_error = nullptr;
	ControlResult m_result;
};

/*! Run @a call: dispatched at once when it does not ask, or when nothing is left to ask (every
 *  property of the schema is fixed); otherwise through a non-modal CommandForm parented to
 *  @a parent, returned in @a form when given. A dispatched failure is shown as a floating
 *  message. Returns the dispatched result, or - when a form was shown instead - a failure of
 *  kind Busy saying the form is waiting. */
LMMS_EXPORT ControlResult runCommand(QWidget* parent, const CommandCall& call, CommandForm** form = nullptr);

/*! An action on @a menu that runs the call @a make builds when triggered (so the ids it fixes
 *  are read then, not when the menu was built). Disabled when the command is not registered. */
LMMS_EXPORT QAction* addCommandAction(QMenu* menu, const QString& label, const QString& id,
	std::function<CommandCall()> make);

/*! A submenu @a label on @a menu with one action per value the enum property @a key of command
 *  @a id allows (On and Off for a boolean property); each runs @a id at once with @a make's call plus {key: value}. Empty (and
 *  disabled) when the command or the enum is missing. */
LMMS_EXPORT QMenu* addCommandChoices(QMenu* menu, const QString& label, const QString& id, const QString& key,
	std::function<CommandCall()> make);

//! The floating message a call's outcome warrants: a failure (one the form shows itself is not
//! announced again), and a success when the call asks for its result. Nothing when headless or
//! without an interface.
LMMS_EXPORT void announce(QWidget* parent, const CommandCall& call, const ControlResult& result);

//! The JSON type a property spec names (its first non-null type for a nullable one).
LMMS_EXPORT QString schemaType(const QJsonObject& spec);

} // namespace lmms::gui

#endif // LMMS_GUI_COMMAND_FORM_H
