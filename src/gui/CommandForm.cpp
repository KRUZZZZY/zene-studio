/*
 * CommandForm.cpp - a registered command's arguments, as a form built from its schema
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

#include "CommandForm.h"

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "CommandFormFields.h"
#include "CommandMetadata.h"
#include "DesignTokens.h"

namespace lmms::gui
{

namespace
{

//! The schema's keys in the order a form asks for them: the required ones first (the identity a
//! dangerous option is about comes before the option), then the rest in schema order.
QStringList fieldOrder(const QJsonObject& properties, const QStringList& required)
{
	QStringList first;
	QStringList rest;
	for (auto it = properties.begin(); it != properties.end(); ++it)
	{
		(required.contains(it.key()) ? first : rest).append(it.key());
	}
	return first + rest;
}

bool anyRawRequired(const QJsonObject& properties, const QStringList& required, const QJsonObject& fixed)
{
	for (const QString& key : required)
	{
		if (formfield::isRawField(properties.value(key).toObject()) && !fixed.contains(key)) { return true; }
	}
	return false;
}

} // namespace

CommandForm::CommandForm(const CommandCall& call, QWidget* parent) :
	QDialog(parent),
	m_call(call),
	m_result(ControlResult::failure(ControlErrorKind::Busy, tr("the command has not run yet")))
{
	setWindowTitle(call.title.isEmpty() ? commandName(call.id) : call.title);
	setAccessibleName(windowTitle());
	setObjectName(QStringLiteral("commandForm"));
	auto* layout = new QVBoxLayout(this);
	addHeader(layout);
	addBody(layout);
	addFooter(layout);
	applyDefaults();
}

void CommandForm::addHeader(QVBoxLayout* layout)
{
	// Theme 4: a command that cannot be undone says so before anything else is read.
	m_warning = new QLabel(commandUndoWarning(m_call.id), this);
	m_warning->setObjectName(QStringLiteral("commandFormWarning"));
	m_warning->setWordWrap(true);
	m_warning->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(tokens::palette().warning.name()));
	m_warning->setVisible(!m_warning->text().isEmpty());
	layout->addWidget(m_warning);
	// The whole description (UX 80), not its first sentence: prerequisites and side effects come later in it.
	auto* about = new QLabel(commandDescription(m_call.id), this);
	about->setWordWrap(true);
	about->setObjectName(QStringLiteral("commandFormAbout"));
	layout->addWidget(about);
	const QStringList required = commandRequired(m_call.id);
	if (std::any_of(required.begin(), required.end(), [this](const QString& key) { return !m_call.fixed.contains(key); }))
	{
		layout->addWidget(formfield::captionLabel(tr("Fields marked * are required."), this));
	}
}

void CommandForm::addBody(QVBoxLayout* layout)
{
	// The body scrolls (UX 86): a command with many arguments keeps Run and Cancel on screen.
	auto* scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* body = new QWidget(scroll);
	auto* column = new QVBoxLayout(body);
	column->setContentsMargins(0, 0, 0, 0);
	auto* form = new QFormLayout;
	column->addLayout(form);
	for (auto it = m_call.fixed.begin(); it != m_call.fixed.end(); ++it)
	{
		// Where it was asked from, by name (UX 85): the track or channel it will act on, its id beside.
		auto* value = new QLabel(formfield::fixedText(it.key(), it.value()), body);
		value->setObjectName(QStringLiteral("fixed:") + it.key());
		value->setTextInteractionFlags(Qt::TextSelectableByMouse);
		form->addRow(fieldLabel(it.key()), value);
	}
	const QStringList required = commandRequired(m_call.id);
	const QJsonObject properties = commandProperties(m_call.id);
	QFormLayout* advanced = nullptr;
	for (const QString& key : fieldOrder(properties, required))
	{
		if (m_call.fixed.contains(key)) { continue; }
		const QJsonObject spec = properties.value(key).toObject();
		const bool raw = formfield::isRawField(spec);
		if (raw && advanced == nullptr) { advanced = addAdvanced(column, anyRawRequired(properties, required, m_call.fixed)); }
		addField(raw ? advanced : form, key, spec, required.contains(key));
	}
	column->addStretch();
	scroll->setWidget(body);
	layout->addWidget(scroll, 1);
}

QFormLayout* CommandForm::addAdvanced(QVBoxLayout* body, bool open)
{
	// Theme 5: raw JSON is for a script writer, so it waits behind a disclosure - open only when a
	// required argument has no other editor.
	auto* toggle = new QToolButton(this);
	toggle->setText(tr("Advanced: raw values"));
	toggle->setCheckable(true);
	toggle->setChecked(open);
	toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
	auto* section = new QWidget(this);
	section->setObjectName(QStringLiteral("commandFormAdvanced"));
	section->setVisible(open);
	auto* form = new QFormLayout(section);
	form->setContentsMargins(0, 0, 0, 0);
	form->addRow(formfield::captionLabel(tr("These arguments are typed as JSON, as a script sends them."), section));
	connect(toggle, &QToolButton::toggled, section, [toggle, section](bool on) {
		section->setVisible(on);
		toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
	});
	body->addWidget(toggle);
	body->addWidget(section);
	return form;
}

void CommandForm::addField(QFormLayout* form, const QString& key, const QJsonObject& spec, bool required)
{
	QWidget* host = form->parentWidget() != nullptr ? form->parentWidget() : this;
	const formfield::Editor editor = formfield::makeEditor(host, m_call.id, key, spec, required);
	Field field{key, spec, editor.value, fieldHelp(m_call.id, key, spec, required), required};
	const QString label = fieldLabel(key, spec);
	field.editor->setObjectName(QStringLiteral("field:") + key);
	field.editor->setAccessibleName(required ? tr("%1 (required)").arg(label) : label);
	field.editor->setAccessibleDescription(field.help);
	field.editor->setToolTip(field.help);
	form->addRow(required ? label + QStringLiteral(" *") : label, editor.row);
	QLabel* help = formfield::captionLabel(field.help, host);
	help->setObjectName(QStringLiteral("help:") + key);
	form->addRow(QString(), help);
	m_fields.append(field);
}

void CommandForm::addFooter(QVBoxLayout* layout)
{
	m_error = new QLabel(this);
	m_error->setObjectName(QStringLiteral("commandFormError"));
	m_error->setWordWrap(true);
	m_error->hide();
	layout->addWidget(m_error);
	m_answer = new QPlainTextEdit(this);
	m_answer->setObjectName(QStringLiteral("commandFormResult"));
	m_answer->setReadOnly(true);
	m_answer->setAccessibleName(tr("Result"));
	m_answer->hide();
	layout->addWidget(m_answer, 1);
	// The id stays visible but secondary (theme 4): what a script, the socket or MCP would call.
	QLabel* id = formfield::captionLabel(tr("Command ID: %1").arg(m_call.id), this);
	id->setObjectName(QStringLiteral("commandFormId"));
	id->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(id);

	auto* buttons = new QDialogButtonBox(this);
	m_run = buttons->addButton(tr("Run"), QDialogButtonBox::AcceptRole);
	m_run->setDefault(true);
	m_copy = buttons->addButton(tr("Copy Result"), QDialogButtonBox::ActionRole);
	m_copy->hide();
	m_close = buttons->addButton(QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &CommandForm::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &CommandForm::reject);
	connect(m_copy, &QPushButton::clicked, this, [this] {
		QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(m_result.result).toJson(QJsonDocument::Indented)));
	});
	layout->addWidget(buttons);
}

void CommandForm::applyDefaults()
{
	// The call's defaults first (the selection, the caller's choice), else the schema's own default.
	for (const Field& field : m_fields)
	{
		const QJsonValue given = m_call.defaults.value(field.key);
		const QJsonValue value = given.isUndefined() ? field.spec.value(QStringLiteral("default")) : given;
		if (!value.isUndefined() && !setField(field.key, value))
		{
			qWarning("%s: default '%s' fits no field", qPrintable(m_call.id), qPrintable(field.key));
		}
	}
	for (auto it = m_call.defaults.begin(); it != m_call.defaults.end(); ++it)
	{
		if (!fields().contains(it.key())) { qWarning("%s: default '%s' fits no field", qPrintable(m_call.id), qPrintable(it.key())); }
	}
}

QStringList CommandForm::fields() const
{
	QStringList out;
	for (const Field& field : m_fields) { out.append(field.key); }
	return out;
}

bool CommandForm::setField(const QString& key, const QJsonValue& value)
{
	for (const Field& field : m_fields)
	{
		if (field.key == key) { return formfield::writeValue(field.editor, value); }
	}
	return false;
}

bool CommandForm::readField(const Field& field, QJsonValue* value, QString* error) const
{
	QString reason;
	const QString label = fieldLabel(field.key, field.spec);
	if (!formfield::readValue(field.editor, field.spec, value, &reason))
	{
		*error = tr("%1: %2").arg(label, reason);
		return false;
	}
	if (value->isUndefined() && field.required)
	{
		*error = tr("%1 is required").arg(label);
		return false;
	}
	return true;
}

bool CommandForm::collect(QJsonObject* args, QString* error, QWidget** invalid) const
{
	QJsonObject out = m_call.fixed;
	for (const Field& field : m_fields)
	{
		// Undefined until a field is read: a default QJsonValue is Null, which sent every empty optional field as null.
		QJsonValue value(QJsonValue::Undefined);
		if (!readField(field, &value, error)) { if (invalid != nullptr) { *invalid = field.editor; } return false; }
		if (!value.isUndefined()) { out.insert(field.key, value); }
	}
	*args = out;
	return true;
}

QString CommandForm::errorText() const
{
	return m_error->text();
}

QString CommandForm::resultText() const
{
	return m_answer->isHidden() ? QString() : m_answer->toPlainText();
}

QString CommandForm::warningText() const
{
	return m_warning->isHidden() ? QString() : m_warning->text();
}

bool CommandForm::keepsOpen() const
{
	const ControlCommand* command = ControlRegistry::instance()->command(m_call.id);
	return m_call.showResult || (command != nullptr && !command->mutating);
}

void CommandForm::showError(const QString& error, QWidget* invalid)
{
	m_error->setText(error);
	m_error->show();
	if (invalid == nullptr) { return; }
	invalid->setAccessibleDescription(error);
	invalid->setFocus(Qt::OtherFocusReason);
}

void CommandForm::showAnswer()
{
	// The answer stays on screen to be read and copied (UX 97), and Close no longer reads as Cancel.
	m_succeeded = true;
	m_answer->setPlainText(resultSummary(m_result.result));
	m_answer->show();
	m_copy->show();
	m_run->setText(tr("Run Again"));
	m_close->setText(tr("Close"));
}

void CommandForm::accept()
{
	QJsonObject args;
	QString error;
	QWidget* invalid = nullptr;
	for (const Field& field : m_fields) { field.editor->setAccessibleDescription(field.help); }
	if (!collect(&args, &error, &invalid))
	{
		showError(error, invalid);
		return;
	}
	m_result = ControlRegistry::instance()->invoke(m_call.id, args);
	emit ran(m_result);
	if (!m_result.ok)
	{
		showError(m_result.errorMessage, nullptr);
		return;
	}
	m_error->clear();
	m_error->hide();
	if (keepsOpen())
	{
		showAnswer();
		return;
	}
	// A change closes the form with a brief confirmation of what it did (UX 96), never silently.
	CommandCall confirmed = m_call;
	confirmed.showResult = true;
	announce(parentWidget(), confirmed, m_result);
	QDialog::accept();
}

void CommandForm::reject()
{
	if (m_succeeded)
	{
		done(QDialog::Accepted);
		return;
	}
	QDialog::reject();
}

} // namespace lmms::gui
