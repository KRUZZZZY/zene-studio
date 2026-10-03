/*
 * BrowserSearchView.h - the browser's search: expressions, kinds, collections, columns and the project's media
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
#ifndef LMMS_GUI_BROWSER_SEARCH_VIEW_H
#define LMMS_GUI_BROWSER_SEARCH_VIEW_H

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>

#include "SideBarWidget.h"
#include "lmms_export.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace lmms
{
struct ControlResult;
}

namespace lmms::gui
{

/*! 0.5.0 L11: one browser source over the registry's library commands.
 *  - WF-003: the search field takes browser.query's expressions (OR, -word, "phrase", name: path:
 *    tag: kind: ext:);
 *  - WF-007: a kind filter (audio, preset, project, midi) and a root filter (the library's roots);
 *  - WF-006: the saved searches - smart collections - listed beside it: pick one to run it, save the
 *    current search as one, remove one (browser.collection_*);
 *  - WF-011: the results in columns - name, kind, size, modified, tags - sorted by any of them;
 *    a row's menu edits its tags (the Tags submenu the file lists use);
 *  - WF-019: Project Media lists every file the project uses (project.missing_assets), the same
 *    columns, found or missing.
 *  - WF-012: "Whole library" searches the library index (browser.index_query) instead of walking, and
 *    Rescan rebuilds it in the background (browser.reindex), its progress on the status line;
 *  - WF-014: Back and Forward step through what the view has shown (browser.browse / history_go);
 *  - WF-018: Gallery shows the results as waveform thumbnails (browser.thumbnail) instead of columns;
 *  - WF-020 / WF-009: a row's menu finds similar sounds (browser.similar) and previews the file as it is
 *    or matched to the song's tempo and key (browser.preview).
 *  A row drags out as the file lists' rows do, so a sample drops on a track or a launcher cell. */
class LMMS_EXPORT BrowserSearchView : public SideBarWidget
{
	Q_OBJECT
public:
	explicit BrowserSearchView(QWidget* parent);

	//! Run the search field (and filters) now; what the Search button and Return do.
	void runSearch();
	//! Run the saved collection @a name.
	void runCollection(const QString& name);
	//! The indexed sounds most like the file at @a path.
	void findSimilar(const QString& path);
	//! List the project's media.
	void showProjectMedia();
	//! Step @a step entries back (negative) or forward through the view's history.
	void goHistory(int step);
	//! Thumbnails instead of columns.
	void setGallery(bool on);
	int resultCount() const;
	QTreeWidget* results() const { return m_results; }
	QListWidget* galleryView() const { return m_gallery; }

private:
	void buildActionRow();
	void connectLists();
	//! A collection picked in the list: run once per gesture (a click and an activation can both come).
	void pickCollection(const QString& name);
	QJsonObject queryArgs() const;
	//! Run a list command through browser.browse (so it joins the history) and show its items.
	void browse(const QString& command, const QJsonObject& args, const QString& label);
	//! A browse or history step's answer: its items and their count on the status line, or its error there.
	void showBrowsed(const ControlResult& shown);
	//! Put the search a history entry ran back in the field and filters (a collection or similar sounds leaves them).
	void restoreQuery(const QJsonObject& entry);
	void showItems(const QJsonArray& items);
	void fillGallery(const QJsonArray& items);
	void refreshCollections();
	void refreshIndexStatus();
	void saveCollection();
	void showRowMenu(QWidget* view, const QPoint& at, const QString& path, const QString& kind);
	void addPreviewEntries(QMenu* menu, const QString& path);

	QLineEdit* m_search = nullptr;
	QComboBox* m_kind = nullptr;
	QComboBox* m_root = nullptr;
	QCheckBox* m_indexed = nullptr;
	QLabel* m_status = nullptr;
	QListWidget* m_collections = nullptr;
	QTreeWidget* m_results = nullptr;
	QListWidget* m_gallery = nullptr;
	QJsonArray m_items;
	bool m_showingMedia = false;  //!< the list is Project Media, not a history entry
	QString m_lastPicked;  //!< pickCollection's last name and when, so one gesture runs it once
	QElapsedTimer m_lastPickedAt;
};

} // namespace lmms::gui

#endif // LMMS_GUI_BROWSER_SEARCH_VIEW_H
