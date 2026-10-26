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

#include <functional>

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
class QPushButton;
class QListWidget;
class QTimer;
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
 *  - WF-007: a kind filter (audio, preset, project, midi), a root filter (the library's roots) and
 *    - 0.5.0 L11 (WF-007) completed - a category filter beside them, populated from the catalog's
 *    own categories (browser.categories), "Any category" restoring the whole result set;
 *  - R-workflow-4 (0.5.0 L11): "Search file metadata" - browser.query's 'probe' - matches what the
 *    audio files themselves say: the embedded title/artist/album/comment/genre and the sample rate,
 *    channels, frames and duration, opening at most probe_limit (200) files per search;
 *  - WF-006: the saved searches - smart collections - listed beside it: pick one to run it, save the
 *    current search as one, remove one (browser.collection_*);
 *  - WF-011: the results in columns - name, kind, size, modified, tags - sorted by any of them;
 *    a row's menu edits its tags (the Tags submenu the file lists use);
 *  - WF-019: Project Media lists every file the project uses (project.missing_assets), the same
 *    columns, found or missing.
 *  - WF-012: "Whole library" searches the library index (browser.index_query) instead of walking, and
 *    Rescan rebuilds it in the background (browser.reindex), its progress on a line of its own, so a
 *    scan never hides the search's count;
 *  - WF-014: Back and Forward step through what the view has shown (browser.browse / history_go);
 *  - WF-018: Gallery shows the results as waveform thumbnails (browser.thumbnail) instead of columns;
 *  - WF-020 / WF-009: a row's menu finds similar sounds (browser.similar) and previews the file as it is
 *    or matched to the song's tempo and key (browser.preview);
 *  - theme 3 (0.5.0 UX): the list follows the search field as it is typed (one history entry, refined in
 *    place); Enter loads the current result, a click or Space previews it (BrowserResultActions), and the
 *    preview stops when the result, the list, the query or the kind or root changes.
 *  A row drags out as the file lists' rows do, so a sample drops on a track or a launcher cell. */
class LMMS_EXPORT BrowserSearchView : public SideBarWidget
{
	Q_OBJECT
public:
	explicit BrowserSearchView(QWidget* parent);

	//! Run the search field (and filters) now, as a new history entry; what a changed kind, root or index does.
	void runSearch();
	//! Run a search typing has left pending now (what the panel's Enter and arrow keys do first).
	Q_INVOKABLE void settleSearch();
	//! Run the saved collection @a name.
	void runCollection(const QString& name);
	//! Remove the saved collection @a name (the list's menu); a refusal shows on the status line.
	void removeCollection(const QString& name);
	//! The indexed sounds most like the file at @a path.
	void findSimilar(const QString& path);
	//! List the project's media with its usage counts, unused local assets and
	//! distinct missing/external states (project.media_usage).
	void showProjectMedia();
	//! In Project Media, show the non-used rows: unused candidates beside the
	//! missing and external references that are not candidates.
	void setShowUnused(bool on);
	//! Remove the selected still-unused local media files after a preview and a
	//! confirmation (project.media_cleanup). Cancel changes nothing.
	void cleanupSelectedMedia();
	//! Step @a step entries back (negative) or forward through the view's history.
	void goHistory(int step);
	//! Thumbnails instead of columns.
	void setGallery(bool on);
	int resultCount() const;
	//! The status line's text: the last search's own feedback (its count, or why it failed).
	QString statusText() const;
	//! The library scan's line: its progress, kept off the status line (a scan never hides a count).
	QString scanStatusText() const;
	//! The index scan's state on the scan line; the browser polls it while a scan runs.
	void refreshIndexStatus();
	QTreeWidget* results() const { return m_results; }
	QListWidget* galleryView() const { return m_gallery; }
	//! The category facet (Any category + the catalog's own), for tests and the panel.
	QComboBox* categoryFilter() const { return m_category; }
	class BrowserResultActions* actions() const { return m_actions; }

private:
	void buildActionRow();
	void connectLists();
	//! A collection picked in the list: run once per gesture (a click and an activation can both come).
	void pickCollection(const QString& name);
	QJsonObject queryArgs() const;
	//! The search the field and filters describe; @a live when typing ran it (it refines the last live one in place).
	void search(bool live);
	void runLiveSearch();
	//! A press on a result: a drag drops its file, a click previews it.
	void pressRow(const QString& kind, const QString& path, const QPixmap& icon);
	//! Run a list command through browser.browse (so it joins the history) and show its items.
	void browse(const QString& command, const QJsonObject& args, const QString& label, bool live = false);
	//! A browse or history step's answer: its items and their count on the status line, or its error there.
	void showBrowsed(const ControlResult& shown);
	//! Put the search a history entry ran back in the field and filters (a collection or similar sounds leaves them).
	void restoreQuery(const QJsonObject& entry);
	void showItems(const QJsonArray& items);
	void fillGallery(const QJsonArray& items);
	void refreshCollections();
	//! Re-read the category vocabulary (browser.categories) into the filter, keeping the selection.
	void refreshCategories();
	void saveCollection();
	void showRowMenu(QWidget* view, const QPoint& at, const QString& path, const QString& kind);
	void addPreviewEntries(QMenu* menu, const QString& path);
	//! The rows Project Media shows: every file, or - with the Unused switch on -
	//! the unused candidates and the missing/external references kept distinct.
	QJsonArray mediaItems() const;
	void showMediaColumns(bool on);
	QStringList selectedUnusedPaths() const;

	QPushButton* m_unused = nullptr;
	QPushButton* m_cleanup = nullptr;

	QLineEdit* m_search = nullptr;
	QComboBox* m_kind = nullptr;
	QComboBox* m_root = nullptr;
	QComboBox* m_category = nullptr;  //!< the category facet (Any category + the catalog's own)
	QCheckBox* m_indexed = nullptr;
	QCheckBox* m_probe = nullptr;
	QLabel* m_status = nullptr;
	QLabel* m_scanStatus = nullptr;  //!< the library scan's line, apart from the search's own status
	QListWidget* m_collections = nullptr;
	QTreeWidget* m_results = nullptr;
	QListWidget* m_gallery = nullptr;
	class BrowserResultActions* m_actions = nullptr;
	QTimer* m_live = nullptr;  //!< the pause in typing before a live search runs
	bool m_liveEntry = false;  //!< the current history entry is a live search (the next one replaces it)
	QJsonArray m_items;
	QJsonArray m_mediaFiles;  //!< the project.media_usage files, for the Unused switch and refresh
	bool m_showingMedia = false;  //!< the list is Project Media, not a history entry
	int m_mediaUnusedCount = 0;
	int m_mediaMissingCount = 0;
	int m_mediaExternalCount = 0;
	QString m_lastPicked;  //!< pickCollection's last name and when, so one gesture runs it once
	QElapsedTimer m_lastPickedAt;
};

/*! Q050-121: tests replace the cleanup confirmation (default: a question box
 *  naming every path). Passing an empty function restores the default. */
using BrowserMediaCleanupConfirm = std::function<bool(const QStringList& paths)>;
LMMS_EXPORT void setBrowserMediaCleanupConfirm(BrowserMediaCleanupConfirm confirm);

} // namespace lmms::gui

#endif // LMMS_GUI_BROWSER_SEARCH_VIEW_H
