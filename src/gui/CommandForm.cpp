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

#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QIntValidator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

#include "ControlVocabulary.h"
#include "ShellSelection.h"
#include "TextFloat.h"

namespace lmms::gui
{

namespace
{

QJsonObject propertiesOf(const QString& id)
{
	const ControlCommand* command = ControlRegistry::instance()->command(id);
	return command != nullptr ? command->argsSchema.value(QStringLiteral("properties")).toObject() : QJsonObject();
}

QStringList requiredOf(const QString& id)
{
	QStringList out;
	const ControlCommand* command = ControlRegistry::instance()->command(id);
	if (command == nullptr) { return out; }
	for (const QJsonValue& key : command->argsSchema.value(QStringLiteral("required")).toArray())
	{
		out.append(key.toString());
	}
	return out;
}

//! "gain_db" -> "Gain db": what a field's label reads.
QString labelFor(const QString& key)
{
	QString text = key;
	text.replace(QLatin1Char('_'), QLatin1Char(' '));
	if (!text.isEmpty()) { text[0] = text.at(0).toUpper(); }
	return text;
}

QString renderValue(const QJsonValue& value)
{
	if (value.isString()) { return value.toString(); }
	if (value.isBool()) { return value.toBool() ? QStringLiteral("yes") : QStringLiteral("no"); }
	if (value.isDouble()) { return QString::number(value.toDouble()); }
	if (value.isArray()) { return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)); }
	if (value.isObject()) { return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)); }
	return QString();
}

//! The choices an enum or boolean field offers, in the combo box's order.
QStringList choicesFor(const QJsonObject& spec)
{
	QStringList out;
	if (spec.contains(QStringLiteral("enum")))
	{
		for (const QJsonValue& value : spec.value(QStringLiteral("enum")).toArray()) { out.append(renderValue(value)); }
		return out;
	}
	return {QStringLiteral("yes"), QStringLiteral("no")};
}

bool isChoice(const QJsonObject& spec)
{
	return spec.contains(QStringLiteral("enum")) || schemaType(spec) == QStringLiteral("boolean");
}

//! Text typed into an array/object field, parsed; false when it is not that JSON type.
bool parseJson(const QString& text, const QString& type, QJsonValue* value)
{
	const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
	if (type == QStringLiteral("array") && doc.isArray()) { *value = doc.array(); return true; }
	if (type == QStringLiteral("object") && doc.isObject()) { *value = doc.object(); return true; }
	return false;
}

//! @a text as the JSON value of a field of @a type; false when it is not that type.
bool parseText(const QString& text, const QString& type, QJsonValue* value)
{
	bool ok = true;
	if (type == QStringLiteral("integer")) { *value = text.toLongLong(&ok); }
	else if (type == QStringLiteral("number")) { *value = text.toDouble(&ok); }
	else if (type == QStringLiteral("array") || type == QStringLiteral("object")) { ok = parseJson(text, type, value); }
	else { *value = text; }
	return ok;
}

//! The widget a menu belongs to: a form or message must outlive the menu (often a stack
//! object closed the moment an action runs), so it is parented past every QMenu.
QWidget* hostOf(QWidget* widget)
{
	while (qobject_cast<QMenu*>(widget) != nullptr) { widget = widget->parentWidget(); }
	return widget;
}

} // namespace

QString schemaType(const QJsonObject& spec)
{
	const QJsonValue type = spec.value(QStringLiteral("type"));
	if (!type.isArray()) { return type.toString(); }
	for (const QJsonValue& name : type.toArray())
	{
		if (name.toString() != QStringLiteral("null")) { return name.toString(); }
	}
	return QString();
}

QJsonObject selectionContext(const QString& id)
{
	const QJsonObject properties = propertiesOf(id);
	const ShellSelection* selection = ShellSelection::instance();
	const QString track = control::trackIdOf(selection->track());
	const QString clip = selection->clip() != nullptr ? control::clipIdOf(selection->clip()) : QString();
	const QString channel = selection->channel();
	QJsonObject out;
	auto offer = [&](const char* key, const QString& value) {
		const QJsonObject spec = properties.value(QLatin1String(key)).toObject();
		if (!value.isEmpty() && schemaType(spec) == QStringLiteral("string")) { out.insert(QLatin1String(key), value); }
	};
	offer("track", track);
	offer("clip", clip);
	offer("channel", channel);
	offer("target", channel.isEmpty() ? track : channel);
	return out;
}

CommandForm::CommandForm(const CommandCall& call, QWidget* parent) :
	QDialog(parent),
	m_call(call),
	m_result(ControlResult::failure(ControlErrorKind::Busy, tr("the command has not run yet")))
{
	const ControlCommand* command = ControlRegistry::instance()->command(call.id);
	setWindowTitle(call.title.isEmpty() ? call.id : call.title);
	setAccessibleName(windowTitle());
	setObjectName(QStringLiteral("commandForm"));

	auto* layout = new QVBoxLayout(this);
	auto* about = new QLabel(command != nullptr ? command->description.section(QStringLiteral(". "), 0, 0) : QString(), this);
	about->setWordWrap(true);
	about->setObjectName(QStringLiteral("commandFormAbout"));
	layout->addWidget(about);

	auto* form = new QFormLayout;
	for (auto it = call.fixed.begin(); it != call.fixed.end(); ++it)
	{
		auto* value = new QLabel(renderValue(it.value()), this);
		value->setObjectName(QStringLiteral("fixed:") + it.key());
		form->addRow(labelFor(it.key()), value);
	}
	const QStringList required = requiredOf(call.id);
	const QJsonObject properties = propertiesOf(call.id);
	for (auto it = properties.begin(); it != properties.end(); ++it)
	{
		if (call.fixed.contains(it.key())) { continue; }
		addField(form, it.key(), it.value().toObject(), required.contains(it.key()));
	}
	layout->addLayout(form);

	m_error = new QLabel(this);
	m_error->setObjectName(QStringLiteral("commandFormError"));
	m_error->setWordWrap(true);
	m_error->hide();
	layout->addWidget(m_error);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Run"));
	connect(buttons, &QDialogButtonBox::accepted, this, &CommandForm::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &CommandForm::reject);
	layout->addWidget(buttons);

	for (auto it = call.defaults.begin(); it != call.defaults.end(); ++it) { setField(it.key(), it.value()); }
}

void CommandForm::addField(QFormLayout* form, const QString& key, const QJsonObject& spec, bool required)
{
	Field field;
	field.key = key;
	field.spec = spec;
	field.required = required;
	field.editor = makeEditor(spec, required);
	field.editor->setObjectName(QStringLiteral("field:") + key);
	field.editor->setAccessibleName(labelFor(key));
	form->addRow(required ? labelFor(key) + QStringLiteral(" *") : labelFor(key), field.editor);
	m_fields.append(field);
}

QWidget* CommandForm::makeEditor(const QJsonObject& spec, bool required)
{
	if (isChoice(spec))
	{
		auto* box = new QComboBox(this);
		if (!required) { box->addItem(QString()); }
		box->addItems(choicesFor(spec));
		return box;
	}
	auto* edit = new QLineEdit(this);
	const QString type = schemaType(spec);
	const QJsonValue low = spec.value(QStringLiteral("minimum"));
	const QJsonValue high = spec.value(QStringLiteral("maximum"));
	if (type == QStringLiteral("integer"))
	{
		edit->setValidator(new QIntValidator(low.isDouble() ? low.toInt() : INT_MIN,
			high.isDouble() ? high.toInt() : INT_MAX, edit));
	}
	else if (type == QStringLiteral("number"))
	{
		auto* validator = new QDoubleValidator(edit);
		if (low.isDouble()) { validator->setBottom(low.toDouble()); }
		if (high.isDouble()) { validator->setTop(high.toDouble()); }
		edit->setValidator(validator);
	}
	else if (type == QStringLiteral("array") || type == QStringLiteral("object"))
	{
		edit->setPlaceholderText(type == QStringLiteral("array") ? QStringLiteral("[ ... ]") : QStringLiteral("{ ... }"));
	}
	return edit;
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
		if (field.key != key) { continue; }
		const QString text = renderValue(value);
		if (auto* box = qobject_cast<QComboBox*>(field.editor))
		{
			const int index = box->findText(text);
			if (index >= 0) { box->setCurrentIndex(index); }
			return index >= 0;
		}
		static_cast<QLineEdit*>(field.editor)->setText(text);
		return true;
	}
	return false;
}

bool CommandForm::readField(const Field& field, QJsonValue* value, QString* error) const
{
	const auto* box = qobject_cast<const QComboBox*>(field.editor);
	const QString text = box != nullptr ? box->currentText() : static_cast<const QLineEdit*>(field.editor)->text().trimmed();
	if (text.isEmpty())
	{
		if (field.required) { *error = tr("%1 is required").arg(labelFor(field.key)); }
		return !field.required;
	}
	const QString type = schemaType(field.spec);
	if (box != nullptr && type == QStringLiteral("boolean")) { *value = text == QStringLiteral("yes"); return true; }
	if (parseText(text, type, value)) { return true; }
	*error = tr("%1: \"%2\" is not a valid %3").arg(labelFor(field.key), text, type);
	return false;
}

bool CommandForm::collect(QJsonObject* args, QString* error) const
{
	QJsonObject out = m_call.fixed;
	for (const Field& field : m_fields)
	{
		QJsonValue value;
		if (!readField(field, &value, error)) { return false; }
		if (!value.isUndefined()) { out.insert(field.key, value); }
	}
	*args = out;
	return true;
}

QString CommandForm::errorText() const
{
	return m_error->text();
}

void CommandForm::accept()
{
	QJsonObject args;
	QString error;
	if (!collect(&args, &error))
	{
		m_error->setText(error);
		m_error->show();
		return;
	}
	m_result = ControlRegistry::instance()->invoke(m_call.id, args);
	emit ran(m_result);
	if (!m_result.ok)
	{
		m_error->setText(m_result.errorMessage);
		m_error->show();
		return;
	}
	m_error->clear();
	m_error->hide();
	announce(parentWidget(), m_call, m_result);
	QDialog::accept();
}

void announce(QWidget* parent, const CommandCall& call, const ControlResult& result)
{
	if (ControlRegistry::instance()->isHeadless() || (result.ok && !call.showResult)) { return; }
	constexpr int MaxChars = 400;
	QString text = result.ok ? QString::fromUtf8(QJsonDocument(result.result).toJson(QJsonDocument::Indented))
		: result.errorMessage;
	if (text.size() > MaxChars) { text = text.left(MaxChars - 1) + QStringLiteral("…"); }
	TextFloat::displayMessage(call.title.isEmpty() ? call.id : call.title, text, QPixmap(), result.ok ? 8000 : 4000, parent);
}

ControlResult runCommand(QWidget* parent, const CommandCall& call, CommandForm** form)
{
	if (form != nullptr) { *form = nullptr; }
	bool nothingToAsk = true;
	const QJsonObject properties = propertiesOf(call.id);
	for (auto it = properties.begin(); it != properties.end() && nothingToAsk; ++it)
	{
		nothingToAsk = call.fixed.contains(it.key());
	}
	if (!call.ask || nothingToAsk)
	{
		const ControlResult result = ControlRegistry::instance()->invoke(call.id, call.fixed);
		announce(parent, call, result);
		return result;
	}
	auto* shown = new CommandForm(call, parent);
	shown->setAttribute(Qt::WA_DeleteOnClose);
	shown->setModal(false);
	shown->show();
	if (form != nullptr) { *form = shown; }
	return ControlResult::failure(ControlErrorKind::Busy, QObject::tr("waiting for the form"));
}

QAction* addCommandAction(QMenu* menu, const QString& label, const QString& id, std::function<CommandCall()> make)
{
	QAction* action = menu->addAction(label);
	action->setProperty("controlCommand", id);
	action->setEnabled(ControlRegistry::instance()->hasCommand(id));
	QObject::connect(action, &QAction::triggered, menu, [menu, make = std::move(make)] {
		runCommand(hostOf(menu), make());
	});
	return action;
}

QMenu* addCommandChoices(QMenu* menu, const QString& label, const QString& id, const QString& key,
	std::function<CommandCall()> make)
{
	QMenu* sub = menu->addMenu(label);
	const QJsonObject spec = propertiesOf(id).value(key).toObject();
	const bool flag = schemaType(spec) == QStringLiteral("boolean");
	const QJsonArray values = flag ? QJsonArray{true, false} : spec.value(QStringLiteral("enum")).toArray();
	sub->setEnabled(!values.isEmpty());
	for (const QJsonValue& value : values)
	{
		const QString text = flag ? (value.toBool() ? QObject::tr("On") : QObject::tr("Off")) : renderValue(value);
		QAction* action = sub->addAction(text);
		action->setProperty("controlCommand", id);
		QObject::connect(action, &QAction::triggered, sub, [sub, key, value, make] {
			CommandCall call = make();
			call.fixed.insert(key, value);
			call.ask = false;
			runCommand(hostOf(sub), call);
		});
	}
	return sub;
}

} // namespace lmms::gui
