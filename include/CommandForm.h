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
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

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
	QString title;  //!< the form's title; the command's name (commandName) when empty
	bool ask = true;
	//! Show the reply on success - for a command whose answer is the point (a detection, an
	//! analysis), not just its effect: a form keeps it on screen, a direct run floats it.
	bool showResult = false;
};

/*! The shell selection's ids under the keys commands take them by: `track`, `clip`, `channel`
 *  and `target` (the selected channel, else the selected track). Only keys @a id's schema
 *  declares AS STRINGS are returned (session.* takes `track` as a column index, which an id is
 *  not), so the result can be passed as arguments as it is. */
LMMS_EXPORT QJsonObject selectionContext(const QString& id);

/*! A form for one command, built from its argsSchema (the JSON-schema subset the registry
 *  validates). 0.5.0 themes 4 and 5: it is titled by the command's name with its id secondary at
 *  the foot; a command that cannot be undone says so above everything; the whole description is
 *  shown; each argument has a label in words with its unit, a semantic editor
 *  (formfield::makeEditor: choices, track/channel pickers, Open / Save As / Choose Folder for an
 *  input or output path) and a guidance line with its range, its default and what a blank means;
 *  required fields come first and are marked; raw JSON is only under Advanced; and the fixed
 *  arguments (where it was asked from) are shown by name. The body scrolls, so a long form keeps
 *  its buttons on screen.
 *  A field left empty is not sent, so an optional argument keeps the command's own default.
 *  Run invokes the command through the registry - the same path the socket and Lua take
 *  (SPEC A11) - and a refusal keeps the form open with the reason. On success a command whose
 *  answer is the point (a read, or a call that asks for its result) keeps the form open with that
 *  answer shown; any other closes it and floats a brief confirmation.
 *  One form serves every command, so no feature needs a hand-written dialog to be reachable. */
class LMMS_EXPORT CommandForm : public QDialog
{
	Q_OBJECT
public:
	explicit CommandForm(const CommandCall& call, QWidget* parent = nullptr);

	//! The editable fields' keys: the required ones first, then the rest in schema order.
	QStringList fields() const;
	//! Fill field @a key; false when there is no such field or @a value does not fit it.
	bool setField(const QString& key, const QJsonValue& value);
	//! The arguments the form would send: fixed ones and every filled field. False (with
	//! @a error, and the field's editor in @a invalid when given) when a field holds text that is
	//! not its type or is out of its bounds, or a required one is empty.
	bool collect(QJsonObject* args, QString* error, QWidget** invalid = nullptr) const;
	//! The last run's result (a failure until the command has run).
	const ControlResult& result() const { return m_result; }
	//! The text shown under the fields after a refusal.
	QString errorText() const;
	//! The answer shown after a successful run that keeps the form open; empty otherwise.
	QString resultText() const;
	//! The "cannot be undone" line above the fields; empty for a command that can be undone.
	QString warningText() const;

public slots:
	//! Run the command; stays open with the reason on a refusal, and with the answer for a
	//! command whose answer is the point; closes otherwise.
	void accept() override;
	//! Close; after a successful run it counts as done (accepted), not as a cancel.
	void reject() override;

signals:
	void ran(const lmms::ControlResult& result);

private:
	struct Field
	{
		QString key;
		QJsonObject spec;
		QWidget* editor = nullptr;
		QString help;
		bool required = false;
	};

	void addHeader(QVBoxLayout* layout);
	void addBody(QVBoxLayout* layout);
	void addFooter(QVBoxLayout* layout);
	//! The Advanced section (raw-JSON fields), under @a body; its form layout.
	QFormLayout* addAdvanced(QVBoxLayout* body, bool open);
	void addField(QFormLayout* form, const QString& key, const QJsonObject& spec, bool required);
	void applyDefaults();
	bool readField(const Field& field, QJsonValue* value, QString* error) const;
	void showError(const QString& error, QWidget* invalid);
	void dropAnswer();
	void showAnswer();
	//! A read, or a call that asks for its result: success keeps the form open with the answer.
	bool keepsOpen() const;

	CommandCall m_call;
	QList<Field> m_fields;
	QLabel* m_warning = nullptr;
	QLabel* m_error = nullptr;
	QPlainTextEdit* m_answer = nullptr;
	QPushButton* m_run = nullptr;
	QPushButton* m_close = nullptr;
	QPushButton* m_copy = nullptr;
	ControlResult m_result;
	bool m_succeeded = false;
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
//! announced again), and a success when the call asks for its result - as "Label: value" lines
//! (resultSummary), not raw JSON. Nothing when headless or without an interface.
LMMS_EXPORT void announce(QWidget* parent, const CommandCall& call, const ControlResult& result);

//! The JSON type a property spec names (its first non-null type for a nullable one).
LMMS_EXPORT QString schemaType(const QJsonObject& spec);

} // namespace lmms::gui

#endif // LMMS_GUI_COMMAND_FORM_H
