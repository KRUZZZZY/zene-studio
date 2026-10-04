/*
 * ArrangerRulerLanes.h - the arranger's sections, markers and tempo, each in a lane of its own
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

#ifndef LMMS_GUI_ARRANGER_RULER_LANES_H
#define LMMS_GUI_ARRANGER_RULER_LANES_H

#include <QList>
#include <QPointer>
#include <QStringList>
#include <QWidget>

#include "LmmsTypes.h"
#include "lmms_export.h"

class QJsonObject;

namespace lmms
{

class ControlRegistry;
class TempoMap;

//! arrangement.ruler_lanes: which lanes show (registered with the arranger's view commands).
LMMS_EXPORT void registerRulerLaneCommands(ControlRegistry& registry);

namespace gui
{

class SongEditor;

/*! 0.5.0 theme 9 (DESIGN-DECISIONS.md "a readable timeline"; ED-047/048/050 drew all three over the bar
 *  ruler, where they covered its numbers and each other): under the bar ruler, a lane each for the song's
 *  SECTIONS (named bands), its MARKERS (flags and names) and its TEMPO (the map's curve with its values,
 *  or the song tempo when no map is active). Each lane is named at the left, over the track heads; a
 *  label is cut to the room it has and its full text is a tooltip. A click on a section selects it as
 *  the range (section.select), on a marker moves the play position there (marker.jump). Which lanes show
 *  is a preference (arrangement.ruler_lanes, config ui/rulerlanes; all three by default), also in the
 *  lanes' context menu. Interface only: nothing here edits the project. */
class LMMS_EXPORT ArrangerRulerLanes : public QWidget
{
	Q_OBJECT
public:
	enum class Lane { Sections, Markers, Tempo };
	static constexpr int LaneCount = 3;

	explicit ArrangerRulerLanes(SongEditor* editor);

	//! The lanes by name, in Lane order: sections, markers, tempo.
	static const QStringList& laneNames();
	//! Whether @a lane shows (the preference).
	static bool laneShown(Lane lane);
	static void setLaneShown(Lane lane, bool shown);

	//! Read the preference again: the lanes shown, the height they take.
	void relayout();
	//! Where @a lane is drawn; empty while it is hidden (a test's view of it).
	QRect laneRect(Lane lane) const;
	//! The x of each marker drawn on the last paint (a test's view of it).
	QList<int> drawnMarkers() const { return m_drawn; }

protected:
	bool event(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	int xOf(tick_t tick) const;
	int contentLeft() const;
	//! The section or marker under @a pos: its command and argument, and its full name.
	bool itemAt(const QPoint& pos, QString* command, QJsonObject* args, QString* name) const;
	void paintSections(QPainter& p, const QRect& lane) const;
	void paintMarkers(QPainter& p, const QRect& lane);
	void paintTempo(QPainter& p, const QRect& lane) const;
	void paintTempoValues(QPainter& p, const QRect& lane, const TempoMap& map) const;

	QPointer<SongEditor> m_editor;
	QList<int> m_drawn;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_ARRANGER_RULER_LANES_H
