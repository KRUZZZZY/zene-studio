/*
 * BrowserSearchView.cpp - the browser's search: expressions, kinds, collections, columns and the project's media
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
#include "BrowserSearchView.h"

#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "BrowserTagMenu.h"
#include "ControlRegistry.h"
#include "embed.h"
#include "Engine.h"
#include "Song.h"
#include "StringPairDrag.h"

namespace lmms::gui
{

namespace
{

enum Column { Name, Kind, Size, Modified, Tags };

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The drag key a row of @a kind carries, as the file lists' rows carry it.
QString dragKey(const QString& kind)
{
	if (kind == QLatin1String("preset")) { return QStringLiteral("presetfile"); }
	if (kind == QLatin1String("project")) { return QStringLiteral("projectfile"); }
	if (kind == QLatin1String("midi")) { return QStringLiteral("importedproject"); }
	return QStringLiteral("samplefile");
}

//! A result row; the size and date sort as numbers (their data), not as text.
QTreeWidgetItem* rowOf(const QJsonObject& item)
{
	auto* row = new QTreeWidgetItem;
	row->setText(Name, item.value(QStringLiteral("name")).toString());
	row->setText(Kind, item.value(QStringLiteral("kind")).toString());
	row->setData(Size, Qt::DisplayRole, item.value(QStringLiteral("size_bytes")).toInteger() / 1024);
	row->setText(Modified, QDateTime::fromMSecsSinceEpoch(item.value(QStringLiteral("modified_ms")).toInteger()).toString(Qt::ISODate));
	QStringList tags;
	for (const QJsonValue& tag : item.value(QStringLiteral("tags")).toArray()) { tags << tag.toString(); }
	row->setText(Tags, tags.join(QStringLiteral(", ")));
	row->setData(Name, Qt::UserRole, item.value(QStringLiteral("path")).toString());
	row->setToolTip(Name, item.value(QStringLiteral("path")).toString());
	return row;
}

} // namespace

BrowserSearchView::BrowserSearchView(QWidget* parent) :
	SideBarWidget(tr("Search"), embed::getIconPixmap("zoom"), parent)
{
	auto* top = new QHBoxLayout;
	m_search = new QLineEdit(contentParent());
	m_search->setPlaceholderText(tr("Search: kick OR snare, -loop, tag:drums, kind:audio, \"bass drum\""));
	m_search->setAccessibleName(tr("Search the library"));
	m_kind = new QComboBox(contentParent());
	m_kind->setAccessibleName(tr("Kind"));
	for (const QString& kind : {tr("Any kind"), QStringLiteral("audio"), QStringLiteral("preset"), QStringLiteral("project"), QStringLiteral("midi")})
	{
		m_kind->addItem(kind);
	}
	m_root = new QComboBox(contentParent());
	m_root->setAccessibleName(tr("Where to look"));
	m_root->addItem(tr("Everywhere"), QString());
	for (const QJsonValue& root : run(QStringLiteral("browser.roots")).result.value(QStringLiteral("roots")).toArray())
	{
		m_root->addItem(root.toObject().value(QStringLiteral("label")).toString(root.toObject().value(QStringLiteral("id")).toString()),
			root.toObject().value(QStringLiteral("id")).toString());
	}
	top->addWidget(m_search, 1);
	top->addWidget(m_kind);
	top->addWidget(m_root);
	addContentLayout(top);

	auto* actions = new QHBoxLayout;
	auto* save = new QPushButton(tr("Save as Collection..."), contentParent());
	save->setProperty("controlCommand", QStringLiteral("browser.collection_save"));
	auto* media = new QPushButton(tr("Project Media"), contentParent());
	media->setProperty("controlCommand", QStringLiteral("project.missing_assets"));
	actions->addWidget(save);
	actions->addWidget(media);
	actions->addStretch(1);
	addContentLayout(actions);

	m_collections = new QListWidget(contentParent());
	m_collections->setAccessibleName(tr("Smart collections"));
	m_collections->setMaximumHeight(90);
	m_collections->setContextMenuPolicy(Qt::CustomContextMenu);
	addContentWidget(m_collections);

	m_results = new QTreeWidget(contentParent());
	m_results->setHeaderLabels({tr("Name"), tr("Kind"), tr("Size (KB)"), tr("Modified"), tr("Tags")});
	m_results->setSortingEnabled(true);
	m_results->setRootIsDecorated(false);
	m_results->setContextMenuPolicy(Qt::CustomContextMenu);
	m_results->header()->setSectionResizeMode(Name, QHeaderView::Stretch);
	addContentWidget(m_results);

	connect(m_search, &QLineEdit::returnPressed, this, &BrowserSearchView::runSearch);
	connect(m_kind, &QComboBox::currentIndexChanged, this, &BrowserSearchView::runSearch);
	connect(m_root, &QComboBox::currentIndexChanged, this, &BrowserSearchView::runSearch);
	connect(save, &QPushButton::clicked, this, &BrowserSearchView::saveCollection);
	connect(media, &QPushButton::clicked, this, &BrowserSearchView::showProjectMedia);
	connect(m_collections, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) { runCollection(item->text()); });
	connect(m_collections, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { runCollection(item->text()); });
	connect(m_collections, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
		QListWidgetItem* item = m_collections->itemAt(at);
		if (item == nullptr) { return; }
		QMenu menu(this);
		QAction* remove = menu.addAction(tr("Remove Collection"));
		remove->setProperty("controlCommand", QStringLiteral("browser.collection_remove"));
		if (menu.exec(m_collections->viewport()->mapToGlobal(at)) == remove)
		{
			run(QStringLiteral("browser.collection_remove"), {{QStringLiteral("name"), item->text()}});
			refreshCollections();
		}
	});
	connect(m_results, &QWidget::customContextMenuRequested, this, &BrowserSearchView::showRowMenu);
	connect(m_results, &QTreeWidget::itemPressed, this, [this](QTreeWidgetItem* item) {
		if (item == nullptr || !(QGuiApplication::mouseButtons() & Qt::LeftButton)) { return; }
		new StringPairDrag(dragKey(item->text(Kind)), item->data(Name, Qt::UserRole).toString(), embed::getIconPixmap("sample_file"), this);
	});
	refreshCollections();
}

QJsonObject BrowserSearchView::queryArgs() const
{
	QJsonObject args{{QStringLiteral("text"), m_search->text()}, {QStringLiteral("limit"), 500}};
	if (m_kind->currentIndex() > 0) { args.insert(QStringLiteral("kind"), m_kind->currentText()); }
	if (!m_root->currentData().toString().isEmpty()) { args.insert(QStringLiteral("root"), m_root->currentData().toString()); }
	return args;
}

void BrowserSearchView::runSearch()
{
	showItems(run(QStringLiteral("browser.query"), queryArgs()).result.value(QStringLiteral("items")).toArray());
}

void BrowserSearchView::runCollection(const QString& name)
{
	showItems(run(QStringLiteral("browser.collection_run"), {{QStringLiteral("name"), name}, {QStringLiteral("limit"), 500}})
		.result.value(QStringLiteral("items")).toArray());
}

void BrowserSearchView::showProjectMedia()
{
	const ControlResult listed = run(QStringLiteral("project.missing_assets"), {{QStringLiteral("project"), Engine::getSong()->projectFileName()}});
	QJsonArray items;
	for (const QJsonValue& value : listed.result.value(QStringLiteral("references")).toArray())
	{
		const QJsonObject ref = value.toObject();
		const QString path = ref.value(QStringLiteral("resolved")).toString(ref.value(QStringLiteral("raw")).toString());
		items.append(QJsonObject{{QStringLiteral("name"), QFileInfo(path).fileName()}, {QStringLiteral("path"), path},
			{QStringLiteral("kind"), ref.value(QStringLiteral("missing")).toBool() ? tr("missing") : QStringLiteral("audio")},
			{QStringLiteral("size_bytes"), QFileInfo(path).size()}, {QStringLiteral("modified_ms"), QFileInfo(path).lastModified().toMSecsSinceEpoch()},
			{QStringLiteral("tags"), QJsonArray{ref.value(QStringLiteral("tag")).toString()}}});
	}
	showItems(items);
}

void BrowserSearchView::showItems(const QJsonArray& items)
{
	m_results->setSortingEnabled(false);
	m_results->clear();
	for (const QJsonValue& item : items) { m_results->addTopLevelItem(rowOf(item.toObject())); }
	m_results->setSortingEnabled(true);
}

int BrowserSearchView::resultCount() const { return m_results->topLevelItemCount(); }

void BrowserSearchView::refreshCollections()
{
	m_collections->clear();
	for (const QJsonValue& value : run(QStringLiteral("browser.collections")).result.value(QStringLiteral("collections")).toArray())
	{
		m_collections->addItem(value.toObject().value(QStringLiteral("name")).toString());
	}
	m_collections->setVisible(m_collections->count() > 0);
}

void BrowserSearchView::saveCollection()
{
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("Save as Collection"), tr("Name:"), QLineEdit::Normal, m_search->text(), &ok);
	if (!ok || name.trimmed().isEmpty()) { return; }
	QJsonObject args = queryArgs();
	args.remove(QStringLiteral("limit"));
	args.insert(QStringLiteral("name"), name.trimmed());
	run(QStringLiteral("browser.collection_save"), args);
	refreshCollections();
}

void BrowserSearchView::showRowMenu(const QPoint& at)
{
	QTreeWidgetItem* item = m_results->itemAt(at);
	if (item == nullptr) { return; }
	QMenu menu(this);
	addBrowserTagMenu(&menu, item->data(Name, Qt::UserRole).toString());
	menu.exec(m_results->viewport()->mapToGlobal(at));
	runSearch();
}

} // namespace lmms::gui
