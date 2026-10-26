/*
 * BrowserResults.h - the browser search view's result rows and what a result does (load, preview)
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
#ifndef LMMS_GUI_BROWSER_RESULTS_H
#define LMMS_GUI_BROWSER_RESULTS_H

#include <QObject>
#include <QPixmap>

#include "lmms_export.h"

class QAbstractItemView;
class QJsonObject;
class QListWidgetItem;
class QMenu;
class QModelIndex;
class QTreeWidgetItem;

namespace lmms::gui
{

namespace browserresults
{

//! The search view's columns. The first five keep their indices: every client and
//! test that already reads a result row by Name/Kind/Size/Modified/Tags is not
//! moved. Q050-121 adds the usage/state pair Project Media shows (hidden by default).
enum Column { Name, Kind, Size, Modified, Tags, Usage, State, ColumnCount };
//! Where a result row (in its Name column) or a gallery tile keeps the file it names, and the file's kind.
constexpr int PathRole = Qt::UserRole;
constexpr int KindRole = Qt::UserRole + 1;

LMMS_EXPORT QPixmap kindIcon(const QString& kind);
//! The drag key a result of @a kind carries, as the file lists' rows carry it; a file-backed result
//! (audio, preset, project, MIDI) maps to the key whose value is its file's path, which a drag also
//! publishes as a `text/uri-list` URL.
LMMS_EXPORT QString dragKey(const QString& kind);
//! A result row; the size and date sort as numbers (their data), not as text.
LMMS_EXPORT QTreeWidgetItem* rowOf(const QJsonObject& item);
//! A gallery tile: the file's waveform thumbnail when @a thumbnail, else its kind's icon.
LMMS_EXPORT QListWidgetItem* tileOf(const QJsonObject& item, bool thumbnail);
//! A left press on a result of @a kind drags its file out, as the file lists' rows do; a missing project file
//! has nothing to drop. Returns when the drag is over (a press released without moving included).
LMMS_EXPORT void dragOut(QWidget* from, const QString& kind, const QString& path, const QPixmap& icon);

} // namespace browserresults

/*! Theme 3 (0.5.0 UX), what a search result does, the file lists' keys:
 *  - Enter (or Return) loads the current result: an audio file or a preset onto a new track (browser.load;
 *    Shift+Enter puts a sample on a sample track), a project opens (project.open, after the usual "save
 *    changes?" question), a MIDI file is imported (project.import);
 *  - a click or Space previews an audio result (browser.preview, a new preview replacing the last); Space
 *    on the result already playing stops it;
 *  - the preview stops when the current result changes, and whenever stopPreview() is called - the search
 *    view calls it when its list, query or location changes, the browser panel when its tab changes or
 *    focus leaves it, and before anything is loaded. */
class LMMS_EXPORT BrowserResultActions : public QObject
{
	Q_OBJECT
public:
	explicit BrowserResultActions(QObject* parent);

	//! Act on @a view's rows: Return/Enter loads the current one, Space previews it, a new current row stops it.
	void watch(QAbstractItemView* view);
	//! Preview @a path when @a kind is audio, as it is or matched to the song (browser.preview's 'match');
	//! false (failed() says why, unless it is not audio) when nothing plays.
	bool preview(const QString& path, const QString& kind, const QString& match = QStringLiteral("none"));
	//! Load the file @a row names, as Enter does; false (failed() says why) when nothing was loaded.
	bool load(const QModelIndex& row, bool sampleTrack);
	bool loadPath(const QString& path, const QString& kind, bool sampleTrack);
	//! A result menu's "Load" entry (what Enter does), declaring the command it runs.
	void addLoadEntry(QMenu* menu, const QString& path, const QString& kind);
	//! The file previewing, empty when none.
	QString previewing() const { return m_previewing; }

public slots:
	void stopPreview();

signals:
	void failed(const QString& message);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void togglePreview(const QModelIndex& row);

	QString m_previewing;
};

} // namespace lmms::gui

#endif // LMMS_GUI_BROWSER_RESULTS_H
