/*
 * CommandFormFields.cpp - the editor a command form gives each argument
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

#include "CommandFormFields.h"

#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPair>
#include <QRegularExpression>
#include <QToolButton>

#include <cmath>

#include "Clip.h"
#include "CommandForm.h"
#include "CommandMetadata.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "ShellSelection.h"
#include "Song.h"
#include "Track.h"
#include "DesignTokens.h"

namespace lmms::gui::formfield
{

namespace
{

//! The editor kinds; a value widget carries its kind as the KindProperty property.
enum class Kind
{
	Text,
	Choice,
	Pick,
	Keys,
	List,
	Json,
};
const char* const KindProperty = "zeneFieldKind";

using Reference = QPair<QString, QString>;  // (id, name)

QString tr(const char* text)
{
	return QCoreApplication::translate("CommandForm", text);
}

Kind kindOf(const QWidget* editor)
{
	return static_cast<Kind>(editor->property(KindProperty).toInt());
}

template<typename T>
T* withKind(T* editor, Kind kind)
{
	editor->setProperty(KindProperty, static_cast<int>(kind));
	return editor;
}

QString itemType(const QJsonObject& spec)
{
	return schemaType(spec.value(QStringLiteral("items")).toObject());
}

bool isChoice(const QJsonObject& spec)
{
	return spec.contains(QStringLiteral("enum")) || schemaType(spec) == QStringLiteral("boolean");
}

//! An array of plain values (strings or numbers) - typed as "a, b, c", not as JSON.
bool isPlainList(const QJsonObject& spec)
{
	const QString items = itemType(spec);
	return schemaType(spec) == QStringLiteral("array")
		&& (items == QStringLiteral("string") || items == QStringLiteral("integer") || items == QStringLiteral("number"));
}

bool isRaw(const QJsonObject& spec)
{
	const QString type = schemaType(spec);
	return type == QStringLiteral("array") || type == QStringLiteral("object");
}

QString compactJson(const QJsonValue& value)
{
	if (value.isArray()) { return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)); }
	if (value.isObject()) { return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)); }
	return valueText(value);
}

QList<Reference> projectTracks()
{
	QList<Reference> out;
	const Song* song = Engine::getSong();
	if (song == nullptr) { return out; }
	for (const Track* track : song->tracks()) { out.append({control::trackIdOf(track), track->name()}); }
	return out;
}

QList<Reference> mixerChannels()
{
	QList<Reference> out;
	Mixer* mixer = Engine::mixer();
	if (mixer == nullptr) { return out; }
	for (int i = 0; i < mixer->numChannels(); ++i)
	{
		out.append({control::channelIdOf(mixer->mixerChannel(i)), mixer->mixerChannel(i)->m_name});
	}
	return out;
}

//! What a reference argument can name: "track" the project's tracks, "channel" the mixer's, "target" either.
QList<Reference> referencesFor(const QString& key, const QJsonObject& spec)
{
	if (schemaType(spec) != QStringLiteral("string") || spec.contains(QStringLiteral("enum"))) { return {}; }
	const QString last = key.section(QLatin1Char('_'), -1);
	QList<Reference> out;
	if (last == QStringLiteral("track") || last == QStringLiteral("target")) { out.append(projectTracks()); }
	if (last == QStringLiteral("channel") || last == QStringLiteral("target")) { out.append(mixerChannels()); }
	return out;
}

QString nameOf(const QString& id)
{
	for (const Reference& known : projectTracks() + mixerChannels())
	{
		if (known.first == id) { return known.second; }
	}
	const Clip* clip = ShellSelection::instance()->clip();
	return clip != nullptr && control::clipIdOf(clip) == id ? clip->name() : QString();
}

QWidget* choiceEditor(QWidget* parent, const QJsonObject& spec, bool required)
{
	auto* box = withKind(new QComboBox(parent), Kind::Choice);
	// The blank entry says what it does: the command's own default, never an unexplained empty line.
	if (!required) { box->addItem(tr("(Default)")); }
	const bool flag = schemaType(spec) == QStringLiteral("boolean");
	const QJsonArray values = flag ? QJsonArray{true, false} : spec.value(QStringLiteral("enum")).toArray();
	for (const QJsonValue& value : values)
	{
		box->addItem(choiceLabel(value), QVariant::fromValue(value));
		box->setItemData(box->count() - 1, valueText(value), Qt::ToolTipRole);  // the value a script would send
	}
	return box;
}

QWidget* pickEditor(QWidget* parent, const QList<Reference>& references)
{
	auto* box = withKind(new QComboBox(parent), Kind::Pick);
	box->setEditable(true);
	box->setInsertPolicy(QComboBox::NoInsert);
	for (const Reference& reference : references)
	{
		box->addItem(reference.second.isEmpty() ? reference.first : QStringLiteral("%1  (%2)").arg(reference.second, reference.first),
			reference.first);
	}
	box->setCurrentIndex(-1);
	box->lineEdit()->setPlaceholderText(tr("Choose from the list, or type an ID"));
	return box;
}

//! The text field's validator: the schema's type and bounds, read in the C locale as parseScalar reads it.
void constrain(QLineEdit* edit, const QJsonObject& spec)
{
	const QString type = schemaType(spec);
	const QJsonValue low = spec.value(QStringLiteral("minimum"));
	const QJsonValue high = spec.value(QStringLiteral("maximum"));
	// A "1,5" a German locale's validator would pass is not a number to QString::toDouble, and its "1.5" would be refused.
	QLocale c = QLocale::c();
	c.setNumberOptions(QLocale::RejectGroupSeparator);
	if (type == QStringLiteral("integer"))
	{
		auto* validator = new QIntValidator(low.isDouble() ? low.toInt() : INT_MIN, high.isDouble() ? high.toInt() : INT_MAX, edit);
		validator->setLocale(c);
		edit->setValidator(validator);
	}
	else if (type == QStringLiteral("number"))
	{
		auto* validator = new QDoubleValidator(edit);
		validator->setLocale(c);
		if (low.isDouble()) { validator->setBottom(low.toDouble()); }
		if (high.isDouble()) { validator->setTop(high.toDouble()); }
		edit->setValidator(validator);
	}
}

QLineEdit* textEditor(QWidget* parent, const QJsonObject& spec, Kind kind)
{
	auto* edit = withKind(new QLineEdit(parent), kind);
	if (kind == Kind::Text) { constrain(edit, spec); }
	if (kind == Kind::List) { edit->setPlaceholderText(tr("Values separated by commas")); }
	if (kind == Kind::Json)
	{
		edit->setPlaceholderText(schemaType(spec) == QStringLiteral("array") ? QStringLiteral("[ ... ]") : QStringLiteral("{ ... }"));
	}
	return edit;
}

QString pickerText(PathRole role, bool colour)
{
	if (colour) { return tr("Choose Colour…"); }
	switch (role)
	{
	case PathRole::OpenFile: return tr("Open…");
	case PathRole::SaveFile: return tr("Save As…");
	case PathRole::OpenFolder:
	case PathRole::SaveFolder: return tr("Choose Folder…");
	case PathRole::None: break;
	}
	return QString();
}

//! Ask for a path of @a role (or a colour) over @a parent, starting from @a current; empty when dismissed.
QString choose(QWidget* parent, PathRole role, bool colour, const QString& current)
{
	const QString title = parent->window()->windowTitle();
	if (colour)
	{
		const QColor picked = QColorDialog::getColor(QColor(current), parent, title);
		return picked.isValid() ? picked.name() : QString();
	}
	switch (role)
	{
	case PathRole::OpenFile: return QFileDialog::getOpenFileName(parent, title, current);
	case PathRole::SaveFile: return QFileDialog::getSaveFileName(parent, title, current);
	case PathRole::OpenFolder:
	case PathRole::SaveFolder: return QFileDialog::getExistingDirectory(parent, title, current);
	case PathRole::None: break;
	}
	return QString();
}

//! A text field with what belongs beside it: its unit, and the picker of a path or a colour.
QWidget* withExtras(QWidget* parent, QLineEdit* edit, const QString& id, const QString& key, const QJsonObject& spec)
{
	static const QRegularExpression colourKey(QStringLiteral("(^|_)colou?r$"));
	const bool colour = colourKey.match(key).hasMatch();
	const PathRole role = pathRole(id, key, spec);
	const QString unit = fieldUnit(key, spec);
	if (!colour && role == PathRole::None && unit.isEmpty()) { return edit; }
	auto* row = new QWidget(parent);
	auto* box = new QHBoxLayout(row);
	box->setContentsMargins(0, 0, 0, 0);
	box->addWidget(edit, 1);
	if (!unit.isEmpty()) { box->addWidget(captionLabel(unit, row)); }
	if (colour || role != PathRole::None)
	{
		auto* browse = new QToolButton(row);
		browse->setText(pickerText(role, colour));
		browse->setAccessibleName(tr("Choose %1").arg(fieldLabel(key, spec).toLower()));
		browse->setToolTip(colour ? browse->accessibleName() : pathRoleText(role));
		QObject::connect(browse, &QToolButton::clicked, edit, [edit, role, colour] {
			const QString chosen = choose(edit, role, colour, edit->text());
			if (!chosen.isEmpty()) { edit->setText(chosen); }
		});
		box->addWidget(browse);
	}
	return row;
}

bool parseScalar(const QString& text, const QString& type, QJsonValue* value)
{
	bool ok = true;
	if (type == QStringLiteral("integer")) { *value = text.toLongLong(&ok); }
	else if (type == QStringLiteral("number")) { *value = text.toDouble(&ok); }
	else { *value = text; }
	return ok;
}

bool parseJson(const QString& text, const QString& type, QJsonValue* value)
{
	const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
	if (type == QStringLiteral("array") && doc.isArray()) { *value = doc.array(); return true; }
	if (type == QStringLiteral("object") && doc.isObject()) { *value = doc.object(); return true; }
	return false;
}

bool inRange(const QJsonValue& value, const QJsonObject& spec)
{
	const QJsonValue low = spec.value(QStringLiteral("minimum"));
	const QJsonValue high = spec.value(QStringLiteral("maximum"));
	if (!value.isDouble()) { return true; }
	return !(low.isDouble() && value.toDouble() < low.toDouble()) && !(high.isDouble() && value.toDouble() > high.toDouble());
}

bool itemMatchesType(const QJsonValue& value, const QString& type)
{
	if (type == QStringLiteral("string")) { return value.isString(); }
	if (type == QStringLiteral("number")) { return value.isDouble(); }
	return type == QStringLiteral("integer") && value.isDouble() && std::floor(value.toDouble()) == value.toDouble();
}

bool listMatchesSchema(const QJsonArray& items, const QJsonObject& spec)
{
	const QJsonValue minimum = spec.value(QStringLiteral("minItems"));
	const QJsonValue maximum = spec.value(QStringLiteral("maxItems"));
	if (minimum.isDouble() && items.size() < minimum.toDouble()) { return false; }
	if (maximum.isDouble() && items.size() > maximum.toDouble()) { return false; }
	const QJsonObject itemSpec = spec.value(QStringLiteral("items")).toObject();
	for (const QJsonValue& item : items)
	{
		if (!itemMatchesType(item, itemType(spec)) || !inRange(item, itemSpec)) { return false; }
	}
	return true;
}

//! "a, b, c" as a list of the items' type; a list typed as JSON ("[...]") is read as JSON.
//! An empty item is refused, with the reason in @a error: malformed input is not a shorter list.
bool parseList(const QString& text, const QJsonObject& spec, QJsonValue* value, QString* error)
{
	QJsonArray items;
	if (text.startsWith(QLatin1Char('[')))
	{
		if (!parseJson(text, QStringLiteral("array"), value)) { return false; }
		items = value->toArray();
	}
	else
	{
		for (const QString& part : text.split(QLatin1Char(',')))
		{
			const QString entry = part.trimmed();
			if (entry.isEmpty())
			{
				*error = tr("\"%1\" has an empty item").arg(text);
				return false;
			}
			QJsonValue item;
			if (!parseScalar(entry, itemType(spec), &item)) { return false; }
			items.append(item);
		}
	}
	if (!listMatchesSchema(items, spec)) { return false; }
	*value = items;
	return true;
}

QString typeError(const QString& text, const QString& type)
{
	if (type == QStringLiteral("integer")) { return tr("\"%1\" is not a whole number").arg(text); }
	if (type == QStringLiteral("number")) { return tr("\"%1\" is not a number").arg(text); }
	return tr("\"%1\" is not a valid %2").arg(text, type);
}

//! A typed field's text as its value, refused when it is not of the type or outside the bounds.
bool readTyped(const QString& text, const QJsonObject& spec, QJsonValue* value, QString* error)
{
	const QString type = schemaType(spec);
	if (!parseScalar(text, type, value)) { *error = typeError(text, type); return false; }
	if (inRange(*value, spec)) { return true; }
	*error = tr("%1 is outside the range %2").arg(text, fieldRange(spec));  // the label before it carries the unit
	return false;
}

QString editorText(const QWidget* editor, const QJsonObject& spec)
{
	if (const auto* box = qobject_cast<const QComboBox*>(editor)) { return box->currentText().trimmed(); }
	if (const auto* keys = qobject_cast<const QKeySequenceEdit*>(editor))
	{
		return keys->keySequence().toString(QKeySequence::PortableText);
	}
	const QString text = static_cast<const QLineEdit*>(editor)->text();
	return schemaType(spec) == QStringLiteral("string") ? text : text.trimmed();
}

//! A picked reference: the chosen entry's id, or the text as typed (an id the list does not hold).
QJsonValue pickedValue(const QComboBox* box, const QString& text)
{
	const int at = box->currentIndex();
	return at >= 0 && box->itemText(at) == box->currentText() ? QJsonValue(box->itemData(at).toString()) : QJsonValue(text);
}

bool readText(const QWidget* editor, const QString& text, const QJsonObject& spec, QJsonValue* value, QString* error)
{
	switch (kindOf(editor))
	{
	case Kind::Pick: *value = pickedValue(static_cast<const QComboBox*>(editor), text); return true;
	case Kind::Keys: *value = text; return true;
	case Kind::List:
	{
		QString reason;
		if (parseList(text, spec, value, &reason)) { return true; }
		*error = reason.isEmpty() ? tr("\"%1\" is not a list of values separated by commas").arg(text) : reason;
		return false;
	}
	case Kind::Json:
		if (parseJson(text, schemaType(spec), value)) { return true; }
		*error = typeError(text, schemaType(spec));
		return false;
	default: break;
	}
	return readTyped(text, spec, value, error);
}

bool writeChoice(QComboBox* box, const QJsonValue& value)
{
	for (int i = 0; i < box->count(); ++i)
	{
		const QVariant data = box->itemData(i);
		const bool blank = !data.isValid();
		if (blank ? value.isUndefined() || value.isNull() : data.toJsonValue() == value)
		{
			box->setCurrentIndex(i);
			return true;
		}
	}
	return false;
}

void writePick(QComboBox* box, const QJsonValue& value)
{
	const int at = box->findData(value.toString());
	if (at >= 0) { box->setCurrentIndex(at); }
	else { box->setEditText(valueText(value)); }
}

Editor single(QWidget* editor)
{
	return {editor, editor};
}

} // namespace

QLabel* captionLabel(const QString& text, QWidget* parent)
{
	auto* label = new QLabel(text, parent);
	label->setWordWrap(true);
	label->setFont(tokens::font(tokens::TextRole::Caption));
	// A widget stylesheet, not the palette: the application stylesheet sets every QLabel's colour.
	label->setStyleSheet(QStringLiteral("color: %1;").arg(tokens::palette().textMuted.name()));
	return label;
}

Editor makeEditor(QWidget* parent, const QString& id, const QString& key, const QJsonObject& spec, bool required)
{
	if (isChoice(spec)) { return single(choiceEditor(parent, spec, required)); }
	if (spec.value(QStringLiteral("format")).toString() == QStringLiteral("keys"))
	{
		return single(withKind(new QKeySequenceEdit(parent), Kind::Keys));
	}
	if (const QList<Reference> references = referencesFor(key, spec); !references.isEmpty())
	{
		return single(pickEditor(parent, references));
	}
	if (isPlainList(spec)) { return single(textEditor(parent, spec, Kind::List)); }
	if (isRaw(spec)) { return single(textEditor(parent, spec, Kind::Json)); }
	QLineEdit* edit = textEditor(parent, spec, Kind::Text);
	return {withExtras(parent, edit, id, key, spec), edit};
}

bool isRawField(const QJsonObject& spec)
{
	return !isChoice(spec) && !isPlainList(spec) && isRaw(spec);
}

bool readValue(const QWidget* editor, const QJsonObject& spec, QJsonValue* value, QString* error)
{
	*value = QJsonValue(QJsonValue::Undefined);
	if (kindOf(editor) == Kind::Choice)
	{
		const QVariant data = static_cast<const QComboBox*>(editor)->currentData();
		if (data.isValid()) { *value = data.toJsonValue(); }
		return true;
	}
	const QString text = editorText(editor, spec);
	return text.isEmpty() || readText(editor, text, spec, value, error);
}

//! The JSON types a schema type can have, so a value is checked without guessing its type again.
bool isOfType(const QJsonValue& value, const QString& type)
{
	if (type == QStringLiteral("integer") || type == QStringLiteral("number")) { return value.isDouble(); }
	if (type == QStringLiteral("boolean")) { return value.isBool(); }
	if (type == QStringLiteral("array")) { return value.isArray(); }
	if (type == QStringLiteral("object")) { return value.isObject(); }
	if (type == QStringLiteral("string")) { return value.isString(); }
	return true;  // a type the form does not constrain (a raw blob, a path)
}

//! Whether @a value is what @a editor (spec @a spec) can be read back as: a choice needs its value
//! offered, any other editor holds its field's own type - a plain list and raw JSON are typed
//! "array"/"object" by the schema, so the same check covers them. Bounds are collect()'s business
//! (a field may hold an out-of-range number, so the user can correct it).
bool fitsEditor(QWidget* editor, const QJsonObject& spec, const QJsonValue& value)
{
	if (value.isUndefined()) { return true; }  // a blank field holds Undefined, which readValue answers
	if (kindOf(editor) == Kind::Choice) { return true; }  // writeChoice answers whether the value is offered
	return isOfType(value, schemaType(spec));
}

bool writeValue(QWidget* editor, const QJsonObject& spec, const QJsonValue& value)
{
	if (!fitsEditor(editor, spec, value)) { return false; }
	switch (kindOf(editor))
	{
	case Kind::Choice: return writeChoice(static_cast<QComboBox*>(editor), value);
	case Kind::Pick: writePick(static_cast<QComboBox*>(editor), value); return true;
	case Kind::Keys:
		static_cast<QKeySequenceEdit*>(editor)->setKeySequence(QKeySequence::fromString(valueText(value), QKeySequence::PortableText));
		return true;
	case Kind::Json: static_cast<QLineEdit*>(editor)->setText(compactJson(value)); return true;
	case Kind::List:
	case Kind::Text: static_cast<QLineEdit*>(editor)->setText(valueText(value)); return true;
	}
	return false;
}

QString fixedText(const QString& key, const QJsonValue& value)
{
	Q_UNUSED(key)
	const QString name = value.isString() ? nameOf(value.toString()) : QString();
	return name.isEmpty() ? valueText(value) : QStringLiteral("%1  (%2)").arg(name, value.toString());
}

} // namespace lmms::gui::formfield
