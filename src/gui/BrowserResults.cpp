/*
 * BrowserResults.cpp - the browser search view's result rows and what a result does (load, preview)
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

#include "BrowserResults.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QListWidgetItem>
#include <QMenu>
#include <QTreeWidgetItem>

#include "BrowserSearchView.h"
#include "ControlRegistry.h"
#include "DeprecationHelper.h"
#include "embed.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "StringPairDrag.h"

namespace lmms::gui
{

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString pathOf(const QModelIndex& row) { return row.siblingAtColumn(0).data(browserresults::PathRole).toString(); }
QString kindOf(const QModelIndex& row) { return row.siblingAtColumn(0).data(browserresults::KindRole).toString(); }

//! The command Enter runs on a result of @a kind.
QString loadCommand(const QString& kind)
{
	if (kind == QLatin1String("project")) { return QStringLiteral("project.open"); }
	if (kind == QLatin1String("midi")) { return QStringLiteral("project.import"); }
	return QStringLiteral("browser.load");
}

//! Load @a path as Enter does for a result of @a kind; an empty failure message when the user kept the open project.
ControlResult loadFile(const QString& path, const QString& kind, bool sampleTrack)
{
	const QString command = loadCommand(kind);
	const bool keep = command == QLatin1String("project.open") && getGUI() != nullptr && !getGUI()->mainWindow()->mayChangeProject(true);
	if (keep) { return ControlResult::failure(ControlErrorKind::Refused, QString()); }
	QJsonObject args{{QStringLiteral("path"), path}};
	if (sampleTrack && kind == QLatin1String("audio")) { args.insert(QStringLiteral("as"), QStringLiteral("sample_track")); }
	return run(command, args);
}

} // namespace

namespace browserresults
{

QPixmap kindIcon(const QString& kind)
{
	if (kind == QLatin1String("preset")) { return embed::getIconPixmap("preset_file"); }
	if (kind == QLatin1String("project")) { return embed::getIconPixmap("project_file"); }
	return embed::getIconPixmap("sample_file");
}

//! The drag key a row of @a kind carries, as the file lists' rows carry it.
QString dragKey(const QString& kind)
{
	if (kind == QLatin1String("preset")) { return QStringLiteral("presetfile"); }
	if (kind == QLatin1String("project")) { return QStringLiteral("projectfile"); }
	if (kind == QLatin1String("midi")) { return QStringLiteral("importedproject"); }
	return QStringLiteral("samplefile");
}

QTreeWidgetItem* rowOf(const QJsonObject& item)
{
	auto* row = new QTreeWidgetItem;
	row->setText(Name, item.value(QStringLiteral("name")).toString());
	row->setText(Kind, item.value(QStringLiteral("kind")).toString());
	const auto sizeBytes = jsonInteger(item.value(QStringLiteral("size_bytes")));
	const auto modifiedMs = jsonInteger(item.value(QStringLiteral("modified_ms")));
	// A missing file has no metadata: show a placeholder rather than a real-looking 0 KB / 1970-01-01.
	if (sizeBytes < 0) { row->setText(Size, QStringLiteral("-")); }
	else { row->setData(Size, Qt::DisplayRole, sizeBytes / 1024); }
	if (modifiedMs <= 0) { row->setText(Modified, QStringLiteral("-")); }
	else { row->setText(Modified, QDateTime::fromMSecsSinceEpoch(modifiedMs).toString(Qt::ISODate)); }
	QStringList tags;
	for (const QJsonValue& tag : item.value(QStringLiteral("tags")).toArray()) { tags << tag.toString(); }
	row->setText(Tags, tags.join(QStringLiteral(", ")));

	// Q050-121: Project Media's usage count and the explicit state that keeps a
	// missing, external or unresolved reference visibly out of the unused set.
	if (item.contains(QStringLiteral("usage_count")))
	{
		row->setData(Usage, Qt::DisplayRole, item.value(QStringLiteral("usage_count")).toInt());
	}
	if (item.contains(QStringLiteral("state"))) { row->setText(State, item.value(QStringLiteral("state")).toString()); }
	row->setData(Name, PathRole, item.value(QStringLiteral("path")).toString());
	row->setData(Name, KindRole, item.value(QStringLiteral("kind")).toString());
	row->setToolTip(Name, item.value(QStringLiteral("path")).toString());
	return row;
}

QListWidgetItem* tileOf(const QJsonObject& item, bool thumbnail)
{
	const QString path = item.value(QStringLiteral("path")).toString();
	const QString kind = item.value(QStringLiteral("kind")).toString();
	auto* tile = new QListWidgetItem(item.value(QStringLiteral("name")).toString());
	const ControlResult drawn = thumbnail ? run(QStringLiteral("browser.thumbnail"), {{QStringLiteral("path"), path}}) : ControlResult();
	tile->setIcon(drawn.ok && thumbnail ? QIcon(drawn.result.value(QStringLiteral("image")).toString()) : QIcon(kindIcon(kind)));
	tile->setData(PathRole, path);
	tile->setData(KindRole, kind);
	tile->setToolTip(path);
	return tile;
}

void dragOut(QWidget* from, const QString& kind, const QString& path, const QPixmap& icon)
{
	if (!(QGuiApplication::mouseButtons() & Qt::LeftButton) || kind == BrowserSearchView::tr("missing")) { return; }
	new StringPairDrag(dragKey(kind), path, icon, from);
}

} // namespace browserresults

BrowserResultActions::BrowserResultActions(QObject* parent) :
	QObject(parent)
{
}

void BrowserResultActions::watch(QAbstractItemView* view)
{
	view->installEventFilter(this);
	connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, &BrowserResultActions::stopPreview);
}

bool BrowserResultActions::preview(const QString& path, const QString& kind, const QString& match)
{
	if (kind != QLatin1String("audio")) { return false; }  // browser.preview plays audio files; a preset previews in the file lists
	const ControlResult played = run(QStringLiteral("browser.preview"), {{QStringLiteral("path"), path}, {QStringLiteral("match"), match}});
	if (!played.ok)
	{
		m_previewing.clear();
		emit failed(played.errorMessage);
		return false;
	}
	m_previewing = path;
	return true;
}

void BrowserResultActions::stopPreview()
{
	if (m_previewing.isEmpty()) { return; }
	m_previewing.clear();
	run(QStringLiteral("browser.preview_stop"));
}

bool BrowserResultActions::load(const QModelIndex& row, bool sampleTrack)
{
	return row.isValid() && loadPath(pathOf(row), kindOf(row), sampleTrack);
}

bool BrowserResultActions::loadPath(const QString& path, const QString& kind, bool sampleTrack)
{
	if (kind == BrowserSearchView::tr("missing")) { return false; }  // a project's missing file: nothing on disk to load, so no command
	stopPreview();  // nothing keeps playing over what was just loaded
	const ControlResult loaded = loadFile(path, kind, sampleTrack);
	if (!loaded.ok && !loaded.errorMessage.isEmpty()) { emit failed(loaded.errorMessage); }
	return loaded.ok;
}

void BrowserResultActions::addLoadEntry(QMenu* menu, const QString& path, const QString& kind)
{
	if (kind == BrowserSearchView::tr("missing")) { return; }  // a project's missing file: nothing on disk to load
	QAction* load = menu->addAction(tr("Load\tEnter"));
	load->setProperty("controlCommand", loadCommand(kind));
	connect(load, &QAction::triggered, this, [this, path, kind] { loadPath(path, kind, false); });
	menu->addSeparator();
}

void BrowserResultActions::togglePreview(const QModelIndex& row)
{
	if (!row.isValid()) { return; }
	if (pathOf(row) == m_previewing) { stopPreview(); }
	else { preview(pathOf(row), kindOf(row)); }
}

bool BrowserResultActions::eventFilter(QObject* watched, QEvent* event)
{
	auto* view = qobject_cast<QAbstractItemView*>(watched);
	if (view == nullptr || event->type() != QEvent::KeyPress) { return QObject::eventFilter(watched, event); }
	const auto* key = static_cast<QKeyEvent*>(event);
	if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
	{
		load(view->currentIndex(), key->modifiers().testFlag(Qt::ShiftModifier));
		return true;
	}
	if (key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier)
	{
		if (!key->isAutoRepeat()) { togglePreview(view->currentIndex()); }
		return true;
	}
	return QObject::eventFilter(watched, event);
}

} // namespace lmms::gui
