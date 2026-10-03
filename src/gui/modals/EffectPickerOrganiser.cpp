/*
 * EffectPickerOrganiser.cpp - favourites, folders and hidden entries in the effect picker
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
#include "EffectPickerOrganiser.h"

#include <QAbstractItemModel>
#include <QSet>

#include <QInputDialog>
#include <QJsonObject>
#include <QMenu>
#include <QStandardItemModel>

#include "CommandForm.h"
#include "PluginOrganiser.h"

namespace lmms::gui::pickerorganiser
{

namespace
{

constexpr auto Star = u"★ ";

//! plugin.organise for @a key; a refusal is shown over @a parent (runCommand), not swallowed.
void organise(QWidget* parent, const QString& key, const QJsonObject& change)
{
	QJsonObject args = change;
	args.insert(QStringLiteral("key"), key);
	runCommand(parent, CommandCall{QStringLiteral("plugin.organise"), args, {}, QString(), false});
}

} // namespace

QString keyOf(const EffectKey& key)
{
	if (key.desc == nullptr) { return {}; }
	const QString module = QString::fromUtf8(key.desc->name);
	return pluginorganiser::keyOf(module, key.desc->subPluginFeatures != nullptr ? key.attributes : QMap<QString, QString>{});
}

void markRow(QStandardItemModel& model, int row, const EffectKey& key)
{
	QStandardItem* name = model.item(row, 0);
	if (name == nullptr) { return; }
	const QString organiserKey = keyOf(key);
	const pluginorganiser::Entry entry = pluginorganiser::entry(organiserKey);
	QString text = name->data(Qt::DisplayRole).toString();
	if (text.startsWith(QString::fromUtf16(Star))) { text = text.mid(2); }
	name->setData(entry.favourite ? QString::fromUtf16(Star) + text : text, Qt::DisplayRole);
	name->setData(organiserKey, KeyRole);
	name->setData(entry.favourite, FavouriteRole);
	name->setData(entry.hidden, HiddenRole);
	name->setData(entry.folder, FolderRole);
	name->setToolTip(entry.folder.isEmpty() ? QString() : QObject::tr("Folder: %1").arg(entry.folder));
}

namespace
{

QString plainName(const QModelIndex& index)
{
	const QString text = index.data(Qt::DisplayRole).toString();
	return (text.startsWith(QString::fromUtf16(Star)) ? text.mid(2) : text).trimmed().toLower();
}

//! MX-167: whether the row's plug-in is also listed in the preferred format (and the row is not that format).
bool shadowed(const QModelIndex& nameIndex)
{
	const QString preferred = pluginorganiser::preferredFormat();
	if (preferred.isEmpty()) { return false; }
	const QAbstractItemModel* model = nameIndex.model();
	const auto typeOf = [model](int row) { return model->index(row, 1).data().toString(); };
	if (typeOf(nameIndex.row()).contains(preferred, Qt::CaseInsensitive)) { return false; }
	// The names the preferred format provides, built once per model, format and row count.
	static const QAbstractItemModel* cachedModel = nullptr;
	static QString cachedFormat;
	static int cachedRows = -1;
	static QSet<QString> names;
	if (model != cachedModel || preferred != cachedFormat || model->rowCount() != cachedRows)
	{
		names.clear();
		for (int row = 0; row < model->rowCount(); ++row)
		{
			if (typeOf(row).contains(preferred, Qt::CaseInsensitive)) { names.insert(plainName(model->index(row, 0))); }
		}
		cachedModel = model;
		cachedFormat = preferred;
		cachedRows = model->rowCount();
	}
	return names.contains(plainName(nameIndex));
}

} // namespace

bool accepts(const QModelIndex& nameIndex, const Filter& filter)
{
	if (!filter.showHidden && (nameIndex.data(HiddenRole).toBool() || shadowed(nameIndex))) { return false; }
	if (filter.favouritesOnly && !nameIndex.data(FavouriteRole).toBool()) { return false; }
	return filter.folder.isEmpty() || nameIndex.data(FolderRole).toString() == filter.folder;
}

void fillRowMenu(QMenu* menu, const QModelIndex& nameIndex, QWidget* parent, const std::function<void()>& changed)
{
	fillKeyMenu(menu, nameIndex.data(KeyRole).toString(), parent, changed);
}

void fillKeyMenu(QMenu* menu, const QString& key, QWidget* parent, const std::function<void()>& changed)
{
	if (key.isEmpty()) { return; }
	const pluginorganiser::Entry stored = pluginorganiser::entry(key);
	const bool favourite = stored.favourite;
	const bool hidden = stored.hidden;
	const QString folder = stored.folder;
	const auto act = [menu, key, parent, changed](const QString& text, const QJsonObject& change) {
		QAction* action = menu->addAction(text);
		action->setProperty("controlCommand", QStringLiteral("plugin.organise"));
		QObject::connect(action, &QAction::triggered, menu, [key, parent, change, changed] { organise(parent, key, change); changed(); });
	};
	act(favourite ? QObject::tr("Remove from Favourites") : QObject::tr("Add to Favourites"), {{QStringLiteral("favourite"), !favourite}});
	act(hidden ? QObject::tr("Show in the List") : QObject::tr("Hide"), {{QStringLiteral("hidden"), !hidden}});
	QAction* move = menu->addAction(QObject::tr("Move to Folder..."));
	move->setProperty("controlCommand", QStringLiteral("plugin.organise"));
	QObject::connect(move, &QAction::triggered, menu, [key, folder, parent, changed] {
		bool ok = false;
		const QString chosen = QInputDialog::getItem(parent, QObject::tr("Move to Folder"), QObject::tr("Folder:"),
			pluginorganiser::folders(), 0, true, &ok);
		if (ok && !chosen.trimmed().isEmpty()) { organise(parent, key, {{QStringLiteral("folder"), chosen.trimmed()}}); changed(); }
	});
	if (!folder.isEmpty()) { act(QObject::tr("Remove from Folder"), {{QStringLiteral("folder"), QString()}}); }
}

} // namespace lmms::gui::pickerorganiser
