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

#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
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

//! Thumbnails one gallery view draws; the rest show their kind's icon (a thumbnail decodes the file once).
constexpr int MaxThumbnails = 60;

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

QPixmap kindIcon(const QString& kind)
{
	if (kind == QLatin1String("preset")) { return embed::getIconPixmap("preset_file"); }
	if (kind == QLatin1String("project")) { return embed::getIconPixmap("project_file"); }
	return embed::getIconPixmap("sample_file");
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

//! A gallery tile: the file's waveform thumbnail when @a thumbnail, else its kind's icon.
QListWidgetItem* tileOf(const QJsonObject& item, bool thumbnail)
{
	const QString path = item.value(QStringLiteral("path")).toString();
	const QString kind = item.value(QStringLiteral("kind")).toString();
	auto* tile = new QListWidgetItem(item.value(QStringLiteral("name")).toString());
	const ControlResult drawn = thumbnail ? run(QStringLiteral("browser.thumbnail"), {{QStringLiteral("path"), path}}) : ControlResult();
	tile->setIcon(drawn.ok && thumbnail ? QIcon(drawn.result.value(QStringLiteral("image")).toString()) : QIcon(kindIcon(kind)));
	tile->setData(Qt::UserRole, path);
	tile->setData(Qt::UserRole + 1, kind);
	tile->setToolTip(path);
	return tile;
}

QPushButton* button(QWidget* parent, const QString& text, const char* command)
{
	auto* made = new QPushButton(text, parent);
	made->setProperty("controlCommand", QLatin1String(command));
	return made;
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
	m_indexed = new QCheckBox(tr("Whole library"), contentParent());  // WF-012
	m_indexed->setToolTip(tr("Search the library index instead of walking the folders"));
	m_indexed->setProperty("controlCommand", QStringLiteral("browser.index_query"));
	top->addWidget(m_search, 1);
	top->addWidget(m_kind);
	top->addWidget(m_root);
	top->addWidget(m_indexed);
	addContentLayout(top);

	buildActionRow();
	m_status = new QLabel(contentParent());
	addContentWidget(m_status);

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

	m_gallery = new QListWidget(contentParent());
	m_gallery->setAccessibleName(tr("Results as thumbnails"));
	m_gallery->setViewMode(QListView::IconMode);
	m_gallery->setIconSize(QSize(160, 48));
	m_gallery->setResizeMode(QListView::Adjust);
	m_gallery->setMovement(QListView::Static);
	m_gallery->setContextMenuPolicy(Qt::CustomContextMenu);
	m_gallery->hide();
	addContentWidget(m_gallery);

	connect(m_search, &QLineEdit::returnPressed, this, &BrowserSearchView::runSearch);
	connect(m_kind, &QComboBox::currentIndexChanged, this, &BrowserSearchView::runSearch);
	connect(m_root, &QComboBox::currentIndexChanged, this, &BrowserSearchView::runSearch);
	connect(m_indexed, &QCheckBox::toggled, this, &BrowserSearchView::runSearch);
	connectLists();
	refreshCollections();
	refreshIndexStatus();
}

void BrowserSearchView::buildActionRow()
{
	auto* actions = new QHBoxLayout;
	auto* back = button(contentParent(), tr("Back"), "browser.history_go");  // WF-014
	auto* forward = button(contentParent(), tr("Forward"), "browser.history_go");
	auto* save = button(contentParent(), tr("Save as Collection..."), "browser.collection_save");
	auto* media = button(contentParent(), tr("Project Media"), "project.missing_assets");
	auto* rescan = button(contentParent(), tr("Rescan Library"), "browser.reindex");
	auto* gallery = button(contentParent(), tr("Gallery"), "browser.thumbnail");  // WF-018
	gallery->setCheckable(true);
	for (QPushButton* each : {back, forward, save, media, rescan, gallery}) { actions->addWidget(each); }
	actions->addStretch(1);
	addContentLayout(actions);
	connect(back, &QPushButton::clicked, this, [this] { goHistory(-1); });
	connect(forward, &QPushButton::clicked, this, [this] { goHistory(1); });
	connect(save, &QPushButton::clicked, this, &BrowserSearchView::saveCollection);
	connect(media, &QPushButton::clicked, this, &BrowserSearchView::showProjectMedia);
	connect(rescan, &QPushButton::clicked, this, [this] {
		run(QStringLiteral("browser.reindex"), {{QStringLiteral("features"), true}});
		refreshIndexStatus();
	});
	connect(gallery, &QPushButton::toggled, this, &BrowserSearchView::setGallery);
}

void BrowserSearchView::connectLists()
{
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
	connect(m_results, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
		if (QTreeWidgetItem* item = m_results->itemAt(at))
		{
			showRowMenu(m_results, at, item->data(Name, Qt::UserRole).toString(), item->text(Kind));
		}
	});
	connect(m_gallery, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
		if (QListWidgetItem* tile = m_gallery->itemAt(at))
		{
			showRowMenu(m_gallery, at, tile->data(Qt::UserRole).toString(), tile->data(Qt::UserRole + 1).toString());
		}
	});
	connect(m_results, &QTreeWidget::itemPressed, this, [this](QTreeWidgetItem* item) {
		if (item == nullptr || !(QGuiApplication::mouseButtons() & Qt::LeftButton)) { return; }
		new StringPairDrag(dragKey(item->text(Kind)), item->data(Name, Qt::UserRole).toString(), embed::getIconPixmap("sample_file"), this);
	});
	connect(m_gallery, &QListWidget::itemPressed, this, [this](QListWidgetItem* tile) {
		if (tile == nullptr || !(QGuiApplication::mouseButtons() & Qt::LeftButton)) { return; }
		new StringPairDrag(dragKey(tile->data(Qt::UserRole + 1).toString()), tile->data(Qt::UserRole).toString(), tile->icon().pixmap(48), this);
	});
}

QJsonObject BrowserSearchView::queryArgs() const
{
	QJsonObject args{{QStringLiteral("text"), m_search->text()}, {QStringLiteral("limit"), 500}};
	if (m_kind->currentIndex() > 0) { args.insert(QStringLiteral("kind"), m_kind->currentText()); }
	if (!m_indexed->isChecked() && !m_root->currentData().toString().isEmpty())
	{
		args.insert(QStringLiteral("root"), m_root->currentData().toString());
	}
	return args;
}

void BrowserSearchView::browse(const QString& command, const QJsonObject& args, const QString& label)
{
	const ControlResult shown = run(QStringLiteral("browser.browse"), {{QStringLiteral("command"), command}, {QStringLiteral("args"), args},
		{QStringLiteral("label"), label}});
	m_showingMedia = false;
	showItems(shown.result.value(QStringLiteral("items")).toArray());
}

void BrowserSearchView::runSearch()
{
	browse(m_indexed->isChecked() ? QStringLiteral("browser.index_query") : QStringLiteral("browser.query"), queryArgs(), m_search->text());
}

void BrowserSearchView::runCollection(const QString& name)
{
	browse(QStringLiteral("browser.collection_run"), {{QStringLiteral("name"), name}, {QStringLiteral("limit"), 500}}, name);
}

void BrowserSearchView::findSimilar(const QString& path)
{
	const ControlResult found = run(QStringLiteral("browser.browse"), {{QStringLiteral("command"), QStringLiteral("browser.similar")},
		{QStringLiteral("args"), QJsonObject{{QStringLiteral("path"), path}, {QStringLiteral("limit"), 50}}},
		{QStringLiteral("label"), tr("Like %1").arg(QFileInfo(path).fileName())}});
	if (!found.ok)
	{
		m_status->setText(found.errorMessage);
		return;
	}
	m_showingMedia = false;
	showItems(found.result.value(QStringLiteral("items")).toArray());
}

void BrowserSearchView::goHistory(int step)
{
	const ControlResult shown = run(QStringLiteral("browser.history_go"), {{QStringLiteral("step"), step}});
	if (!shown.ok) { return; }
	m_showingMedia = false;
	showItems(shown.result.value(QStringLiteral("items")).toArray());
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
	m_showingMedia = true;
	showItems(items);
}

void BrowserSearchView::showItems(const QJsonArray& items)
{
	m_items = items;
	m_results->setSortingEnabled(false);
	m_results->clear();
	for (const QJsonValue& item : items) { m_results->addTopLevelItem(rowOf(item.toObject())); }
	m_results->setSortingEnabled(true);
	if (!m_gallery->isHidden()) { fillGallery(items); }  // the gallery mode, shown or not
}

void BrowserSearchView::fillGallery(const QJsonArray& items)
{
	m_gallery->clear();
	int thumbnails = 0;
	for (const QJsonValue& value : items)
	{
		const bool audio = value.toObject().value(QStringLiteral("kind")).toString() == QLatin1String("audio");
		m_gallery->addItem(tileOf(value.toObject(), audio && thumbnails < MaxThumbnails));
		thumbnails += audio ? 1 : 0;
	}
}

void BrowserSearchView::setGallery(bool on)
{
	m_results->setVisible(!on);
	m_gallery->setVisible(on);
	if (on) { fillGallery(m_items); }
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

void BrowserSearchView::refreshIndexStatus()
{
	const QJsonObject status = run(QStringLiteral("browser.index_status")).result;
	const int entries = status.value(QStringLiteral("entries")).toInt();
	if (status.value(QStringLiteral("running")).toBool())
	{
		m_status->setText(tr("Scanning the library: %1 files, %2 measured").arg(status.value(QStringLiteral("scanned")).toInt())
			.arg(status.value(QStringLiteral("analysed")).toInt()));
		QTimer::singleShot(1000, this, &BrowserSearchView::refreshIndexStatus);
		return;
	}
	const QDateTime built = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(status.value(QStringLiteral("built_ms")).toDouble()));
	m_status->setText(entries > 0 ? tr("Library index: %1 files, built %2").arg(entries).arg(built.toString(Qt::ISODate))
		: tr("No library index yet - Rescan Library builds it"));
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

void BrowserSearchView::addPreviewEntries(QMenu* menu, const QString& path)
{
	QMenu* preview = menu->addMenu(tr("Preview"));  // WF-009
	const std::pair<QString, const char*> modes[] = {{tr("As It Is"), "none"}, {tr("At the Song's Tempo"), "tempo"},
		{tr("In the Song's Key"), "key"}, {tr("Matched to Tempo and Key"), "both"}};
	for (const auto& [label, match] : modes)
	{
		QAction* action = preview->addAction(label);
		action->setProperty("controlCommand", QStringLiteral("browser.preview"));
		connect(action, &QAction::triggered, this, [path, match = QLatin1String(match)] {
			run(QStringLiteral("browser.preview"), {{QStringLiteral("path"), path}, {QStringLiteral("match"), match}});
		});
	}
	QAction* stop = preview->addAction(tr("Stop"));
	stop->setProperty("controlCommand", QStringLiteral("browser.preview_stop"));
	connect(stop, &QAction::triggered, this, [] { run(QStringLiteral("browser.preview_stop")); });
}

void BrowserSearchView::showRowMenu(QWidget* view, const QPoint& at, const QString& path, const QString& kind)
{
	QMenu menu(this);
	if (kind == QLatin1String("audio"))
	{
		addPreviewEntries(&menu, path);
		QAction* similar = menu.addAction(tr("Find Similar Sounds"));  // WF-020
		similar->setProperty("controlCommand", QStringLiteral("browser.similar"));
		connect(similar, &QAction::triggered, this, [this, path] { findSimilar(path); });
		menu.addSeparator();
	}
	addBrowserTagMenu(&menu, path);
	const QAction* chosen = menu.exec(static_cast<QAbstractScrollArea*>(view)->viewport()->mapToGlobal(at));
	// A tag edit: the list is shown again as it stands (the current history entry, not a new one).
	if (chosen != nullptr && chosen->data().toString().startsWith(QLatin1String("browser.tag"))) { m_showingMedia ? showProjectMedia() : goHistory(0); }
}

} // namespace lmms::gui
