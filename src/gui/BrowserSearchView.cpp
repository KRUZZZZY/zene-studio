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

#include <algorithm>

#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
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
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyleHints>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "BrowserResults.h"
#include "BrowserTagMenu.h"
#include "ControlRegistry.h"
#include "embed.h"
#include "Engine.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{

using namespace browserresults;

//! Thumbnails one gallery view draws; the rest show their kind's icon (a thumbnail decodes the file once).
constexpr int MaxThumbnails = 60;
//! How long typing pauses before the search runs (theme 3: the list follows the field as it is typed).
constexpr int LiveSearchDelayMs = 250;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The status line for a list just shown: how many, and whether there is more than it shows.
QString countText(const QJsonObject& result)
{
	const int shown = result.value(QStringLiteral("items")).toArray().size();
	const int matched = result.value(QStringLiteral("matched")).toInt(shown);
	if (matched > shown) { return BrowserSearchView::tr("Showing the first %1 of %2 results").arg(shown).arg(matched); }
	if (result.value(QStringLiteral("truncated")).toBool())
	{
		return BrowserSearchView::tr("Showing the first %1 results - the search stopped early, so narrow it to see the rest").arg(shown);
	}
	return shown > 0 ? BrowserSearchView::tr("Results: %1").arg(shown) : BrowserSearchView::tr("No results");
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
	m_search->setObjectName(QStringLiteral("browserSearch"));  // the browser panel shows it as its first control
	m_search->setProperty("browserSearchHint", true);  // ... keeping this placeholder: it teaches the syntax
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
	// The category facet (0.5.0 L11, WF-007): the catalog's own categories beside kind and
	// source. "Any category" is the empty selection and restores the whole result set.
	m_category = new QComboBox(contentParent());
	m_category->setAccessibleName(tr("Category"));
	m_indexed = new QCheckBox(tr("Whole library"), contentParent());  // WF-012
	m_indexed->setToolTip(tr("Search the library index instead of walking the folders"));
	m_indexed->setProperty("controlCommand", QStringLiteral("browser.index_query"));
	top->addWidget(m_search, 1);
	top->addWidget(m_kind);
	top->addWidget(m_root);
	top->addWidget(m_category);
	top->addWidget(m_indexed);
	addContentLayout(top);

	buildActionRow();
	m_status = new QLabel(contentParent());
	addContentWidget(m_status);

	m_collections = new QListWidget(contentParent());
	m_collections->setAccessibleName(tr("Collections"));
	m_collections->setMaximumHeight(90);
	m_collections->setContextMenuPolicy(Qt::CustomContextMenu);
	addContentWidget(m_collections);

	m_actions = new BrowserResultActions(this);  // theme 3: Enter loads, a click or Space previews
	connect(m_actions, &BrowserResultActions::failed, m_status, &QLabel::setText);

	m_results = new QTreeWidget(contentParent());
	m_results->setAccessibleName(tr("Results"));
	m_results->setHeaderLabels({tr("Name"), tr("Kind"), tr("Size (KB)"), tr("Modified"), tr("Tags")});
	m_results->setSortingEnabled(true);
	m_results->setRootIsDecorated(false);
	m_results->setContextMenuPolicy(Qt::CustomContextMenu);
	m_results->header()->setSectionResizeMode(Name, QHeaderView::Stretch);
	m_results->setProperty("browserResults", true);  // what the panel's search field steps through and loads from
	addContentWidget(m_results);

	m_gallery = new QListWidget(contentParent());
	m_gallery->setAccessibleName(tr("Results as thumbnails"));
	m_gallery->setViewMode(QListView::IconMode);
	m_gallery->setIconSize(QSize(160, 48));
	m_gallery->setResizeMode(QListView::Adjust);
	m_gallery->setMovement(QListView::Static);
	m_gallery->setContextMenuPolicy(Qt::CustomContextMenu);
	m_gallery->hide();
	m_gallery->setProperty("browserResults", true);
	addContentWidget(m_gallery);

	// Theme 3: the list follows the field as it is typed (one history entry per search, refined in place);
	// Return runs it at once. A changed query, kind or root stops the preview, as a new list does.
	m_live = new QTimer(this);
	m_live->setSingleShot(true);
	m_live->setInterval(LiveSearchDelayMs);
	connect(m_search, &QLineEdit::textEdited, this, [this] {
		m_actions->stopPreview();
		m_live->start();
	});
	connect(m_live, &QTimer::timeout, this, &BrowserSearchView::runLiveSearch);
	connect(m_search, &QLineEdit::returnPressed, this, [this] { search(m_liveEntry); });  // typed: refreshed in place
	connect(m_kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BrowserSearchView::runSearch);
	connect(m_root, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BrowserSearchView::runSearch);
	connect(m_category, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BrowserSearchView::runSearch);
	connect(m_indexed, &QCheckBox::toggled, this, &BrowserSearchView::runSearch);
	connect(m_indexed, &QCheckBox::toggled, m_root, &QWidget::setDisabled);  // the index has no roots to filter by
	connectLists();
	refreshCategories();
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
		const ControlResult started = run(QStringLiteral("browser.reindex"), {{QStringLiteral("features"), true}});
		if (started.ok) { refreshIndexStatus(); }
		else { m_status->setText(started.errorMessage); }
	});
	connect(gallery, &QPushButton::toggled, this, &BrowserSearchView::setGallery);
}

void BrowserSearchView::connectLists()
{
	connect(m_collections, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) { pickCollection(item->text()); });
	connect(m_collections, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { pickCollection(item->text()); });
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
			showRowMenu(m_results, at, item->data(Name, PathRole).toString(), item->text(Kind));
		}
	});
	connect(m_gallery, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
		if (QListWidgetItem* tile = m_gallery->itemAt(at))
		{
			showRowMenu(m_gallery, at, tile->data(PathRole).toString(), tile->data(KindRole).toString());
		}
	});
	connect(m_results, &QTreeWidget::itemPressed, this, [this](QTreeWidgetItem* item) {
		if (item != nullptr) { pressRow(item->text(Kind), item->data(Name, PathRole).toString(), kindIcon(item->text(Kind))); }
	});
	connect(m_gallery, &QListWidget::itemPressed, this, [this](QListWidgetItem* tile) {
		if (tile != nullptr) { pressRow(tile->data(KindRole).toString(), tile->data(PathRole).toString(), tile->icon().pixmap(48)); }
	});
	m_actions->watch(m_results);
	m_actions->watch(m_gallery);
}

void BrowserSearchView::pressRow(const QString& kind, const QString& path, const QPixmap& icon)
{
	const bool left = QGuiApplication::mouseButtons() & Qt::LeftButton;
	const QPoint from = QCursor::pos();
	dragOut(this, kind, path, icon);
	// Theme 3: a press released where it was is a click, and a click previews the row; a drag drops the file instead.
	if (left && (QCursor::pos() - from).manhattanLength() < QApplication::startDragDistance()) { m_actions->preview(path, kind); }
}

void BrowserSearchView::pickCollection(const QString& name)
{
	// A double-click, or a style that activates on one click, emits itemClicked AND itemActivated:
	// the second within the double-click interval is the same gesture, not another history entry.
	const int gesture = QGuiApplication::styleHints()->mouseDoubleClickInterval();
	if (name == m_lastPicked && m_lastPickedAt.isValid() && m_lastPickedAt.elapsed() < gesture) { return; }
	m_lastPicked = name;
	m_lastPickedAt.start();
	runCollection(name);
}

QJsonObject BrowserSearchView::queryArgs() const
{
	QJsonObject args{{QStringLiteral("text"), m_search->text()}, {QStringLiteral("limit"), 500}};
	if (m_kind->currentIndex() > 0) { args.insert(QStringLiteral("kind"), m_kind->currentText()); }
	// The category facet (0.5.0 L11, WF-007): index 0 is "Any category" and sends nothing,
	// so an unselected facet restores the full result set in both query modes.
	if (m_category->currentIndex() > 0) { args.insert(QStringLiteral("category"), m_category->currentData().toString()); }
	if (!m_indexed->isChecked() && !m_root->currentData().toString().isEmpty())
	{
		args.insert(QStringLiteral("root"), m_root->currentData().toString());
	}
	return args;
}

void BrowserSearchView::browse(const QString& command, const QJsonObject& args, const QString& label, bool live)
{
	// A live search refines the live search before it in place: one history entry, not one per pause in typing.
	const bool replace = live && m_liveEntry;
	m_live->stop();
	const ControlResult shown = run(QStringLiteral("browser.browse"), {{QStringLiteral("command"), command}, {QStringLiteral("args"), args},
		{QStringLiteral("label"), label}, {QStringLiteral("replace"), replace}});
	showBrowsed(shown);
	if (shown.ok) { m_liveEntry = live; }
}

void BrowserSearchView::showBrowsed(const ControlResult& shown)
{
	if (!shown.ok)
	{
		m_status->setText(shown.errorMessage);  // the list on show stays: a failed one replaces nothing
		return;
	}
	m_showingMedia = false;
	m_liveEntry = false;  // a list shown from the history is no longer the one being typed
	showItems(shown.result.value(QStringLiteral("items")).toArray());
	m_status->setText(countText(shown.result));
	restoreQuery(shown.result.value(QStringLiteral("entry")).toObject());
}

void BrowserSearchView::restoreQuery(const QJsonObject& entry)
{
	const QString command = entry.value(QStringLiteral("command")).toString();
	const bool indexed = command == QLatin1String("browser.index_query");
	if (!indexed && command != QLatin1String("browser.query")) { return; }  // a collection or similar sounds: no search of its own
	const QJsonObject args = entry.value(QStringLiteral("args")).toObject();
	const QSignalBlocker kind(m_kind), root(m_root), category(m_category), index(m_indexed);  // shown already: no search again
	// The same text is left alone, so a live search never moves the cursor of the field being typed in.
	if (m_search->text() != args.value(QStringLiteral("text")).toString()) { m_search->setText(args.value(QStringLiteral("text")).toString()); }
	m_kind->setCurrentIndex(std::max(0, m_kind->findText(args.value(QStringLiteral("kind")).toString())));
	m_root->setCurrentIndex(std::max(0, m_root->findData(args.value(QStringLiteral("root")).toString())));
	// A history entry can name a category the vocabulary gained (or lost) since it ran: the
	// entry's own value is re-selected by data, absent from the list becoming "Any category".
	m_category->setCurrentIndex(std::max(0, m_category->findData(args.value(QStringLiteral("category")).toString())));
	m_indexed->setChecked(indexed);
	m_root->setDisabled(indexed);
}

void BrowserSearchView::search(bool live)
{
	browse(m_indexed->isChecked() ? QStringLiteral("browser.index_query") : QStringLiteral("browser.query"), queryArgs(), m_search->text(), live);
}

void BrowserSearchView::runSearch() { search(false); }

void BrowserSearchView::runLiveSearch() { search(true); }

void BrowserSearchView::settleSearch()
{
	if (m_live->isActive()) { runLiveSearch(); }
}

void BrowserSearchView::runCollection(const QString& name)
{
	browse(QStringLiteral("browser.collection_run"), {{QStringLiteral("name"), name}, {QStringLiteral("limit"), 500}}, name);
}

void BrowserSearchView::findSimilar(const QString& path)
{
	browse(QStringLiteral("browser.similar"), {{QStringLiteral("path"), path}, {QStringLiteral("limit"), 50}},
		tr("Like %1").arg(QFileInfo(path).fileName()));
}

void BrowserSearchView::goHistory(int step)
{
	showBrowsed(run(QStringLiteral("browser.history_go"), {{QStringLiteral("step"), step}}));
}

void BrowserSearchView::showProjectMedia()
{
	const ControlResult listed = run(QStringLiteral("project.missing_assets"), {{QStringLiteral("project"), Engine::getSong()->projectFileName()}});
	if (!listed.ok)
	{
		m_status->setText(listed.errorMessage);
		return;
	}
	QJsonArray items;
	int missing = 0;
	for (const QJsonValue& value : listed.result.value(QStringLiteral("references")).toArray())
	{
		const QJsonObject ref = value.toObject();
		const bool gone = ref.value(QStringLiteral("missing")).toBool();
		missing += gone ? 1 : 0;
		// 'path' is where the reference resolves (against the project file's folder); 'resolved' is only a flag.
		const QString resolved = ref.value(QStringLiteral("path")).toString();
		const QString path = resolved.isEmpty() ? ref.value(QStringLiteral("raw")).toString() : resolved;
		items.append(QJsonObject{{QStringLiteral("name"), QFileInfo(path).fileName()}, {QStringLiteral("path"), path},
			{QStringLiteral("kind"), gone ? tr("missing") : QStringLiteral("audio")},
			{QStringLiteral("size_bytes"), gone ? qint64(-1) : QFileInfo(path).size()},
			{QStringLiteral("modified_ms"), gone ? qint64(-1) : QFileInfo(path).lastModified().toMSecsSinceEpoch()},
			{QStringLiteral("tags"), QJsonArray{ref.value(QStringLiteral("tag")).toString()}}});
	}
	m_showingMedia = true;
	showItems(items);
	m_status->setText(tr("Project Media: %1 files, %2 missing").arg(items.size()).arg(missing));
}

void BrowserSearchView::showItems(const QJsonArray& items)
{
	m_items = items;
	m_actions->stopPreview();  // theme 3: a new list (query, location, history) stops the preview
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

void BrowserSearchView::refreshCategories()
{
	// The vocabulary is what the catalog carries now (browser.categories reads the store):
	// a category a file was given since the last look appears here, and one no file carries
	// any more disappears. The selection follows by value, so a surviving choice is kept.
	const QString selected = m_category->currentData().toString();
	const QSignalBlocker block(m_category);  // repopulating must not re-run the search it feeds
	m_category->clear();
	m_category->addItem(tr("Any category"), QString());
	for (const QJsonValue& value : run(QStringLiteral("browser.categories")).result.value(QStringLiteral("categories")).toArray())
	{
		const QString category = value.toObject().value(QStringLiteral("category")).toString();
		m_category->addItem(category, category);
	}
	m_category->setCurrentIndex(std::max(0, m_category->findData(selected)));
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
	const ControlResult saved = run(QStringLiteral("browser.collection_save"), args);
	if (!saved.ok) { m_status->setText(saved.errorMessage); }
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
		connect(action, &QAction::triggered, this, [this, path, match = QLatin1String(match)] {
			m_actions->preview(path, QStringLiteral("audio"), match);  // an error shows on the status line
		});
	}
	QAction* stop = preview->addAction(tr("Stop"));
	stop->setProperty("controlCommand", QStringLiteral("browser.preview_stop"));
	connect(stop, &QAction::triggered, this, [this] {
		m_actions->stopPreview();
		run(QStringLiteral("browser.preview_stop"));  // whoever started it
	});
}

void BrowserSearchView::showRowMenu(QWidget* view, const QPoint& at, const QString& path, const QString& kind)
{
	QMenu menu(this);
	m_actions->addLoadEntry(&menu, path, kind);  // theme 3: what Enter does, named where the other actions are
	if (kind == QLatin1String("audio"))
	{
		addPreviewEntries(&menu, path);
		QAction* similar = menu.addAction(tr("Find Similar Sounds"));  // WF-020
		similar->setProperty("controlCommand", QStringLiteral("browser.similar"));
		connect(similar, &QAction::triggered, this, [this, path] { findSimilar(path); });
		menu.addSeparator();
	}
	addBrowserTagMenu(&menu, path);
	addBrowserCategoryMenu(&menu, path);
	const QAction* chosen = menu.exec(static_cast<QAbstractScrollArea*>(view)->viewport()->mapToGlobal(at));
	// A tag or category edit: the list is shown again as it stands (the current history entry,
	// not a new one), and the category filter's vocabulary follows the edit.
	const QString command = chosen != nullptr ? chosen->data().toString() : QString();
	if (command.startsWith(QLatin1String("browser.tag")) || command.startsWith(QLatin1String("browser.category")))
	{
		refreshCategories();
		m_showingMedia ? showProjectMedia() : goHistory(0);
	}
}

} // namespace lmms::gui
