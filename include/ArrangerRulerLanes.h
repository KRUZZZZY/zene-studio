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
 *  lanes' context menu. Interface only: nothing here edits the project.
 *
 *  0.5.0 E23: a fourth lane, the CHORD TRACK - each chord as a band from where it starts for as long as it
 *  sounds (ChordTrack::effectiveLength), named by its root and chord ("C Major"), its scale in the tooltip.
 *  It takes room only while the chord track holds a chord (the preference still says whether it may show),
 *  and a click on a chord moves the play position there (transport.seek). The lane follows the chord track
 *  by its revision, so a chord set, removed, undone or loaded shows without a signal from the model.
 *
 *  0.5.0 theme 9 (wave 3): no label collides. A label too long for its room is elided, one with no room is left out,
 *  and the tooltip names what the lane could not: every marker of a crowded cluster together, and in the tempo lane
 *  the tempo change under the pointer, else the tempo there. A double-click on a section or a marker zooms the arranger
 *  to it (arrangement.zoom start/end: the section, or the marker up to the next one) - Bitwig's Alt-click on a cue. */
class LMMS_EXPORT ArrangerRulerLanes : public QWidget
{
	Q_OBJECT
public:
	enum class Lane { Sections, Markers, Tempo, Chords };
	static constexpr int LaneCount = 4;

	explicit ArrangerRulerLanes(SongEditor* editor);

	//! The lanes by name, in Lane order: sections, markers, tempo.
	static const QStringList& laneNames();
	//! Whether @a lane may show (the preference).
	static bool laneShown(Lane lane);
	//! Whether @a lane is drawn: the preference, and for the chord lane a chord track that holds a chord.
	static bool laneDrawn(Lane lane);
	static void setLaneShown(Lane lane, bool shown);

	//! Read the preference again: the lanes shown, the height they take (a thin strip while none is drawn).
	void relayout();
	//! Where @a lane is drawn; empty while it is hidden (a test's view of it).
	QRect laneRect(Lane lane) const;
	//! The x of each marker drawn on the last paint (a test's view of it).
	QList<int> drawnMarkers() const { return m_drawn; }
	//! 0.5.0 theme 9 (wave 3): the tempo lane's tooltip at @a pos - the tempo change there, else the tempo there
	//! (ArrangerRulerTempo.cpp, with the lane's painting); empty off the lane.
	QString tempoTip(const QPoint& pos) const;
	//! Every marker (ProjectMarkers order) whose flag stands within a label's reach of the one at @a index, by name:
	//! the markers a crowded lane could not label, which its tooltip names together.
	QStringList markerCluster(int index) const;

protected:
	bool event(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	//! 0.5.0 theme 9 (wave 3): a double-click on a section or a marker zooms the arranger to it.
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	int xOf(tick_t tick) const;
	//! The song tick at x = @a x (the inverse of xOf).
	tick_t tickAt(int x) const;
	int contentLeft() const;
	//! The section or marker under @a pos: its command and argument, and its full name.
	bool itemAt(const QPoint& pos, QString* command, QJsonObject* args, QString* name) const;
	void paintSections(QPainter& p, const QRect& lane) const;
	void paintMarkers(QPainter& p, const QRect& lane);
	void paintTempo(QPainter& p, const QRect& lane) const;
	void paintTempoValues(QPainter& p, const QRect& lane, const TempoMap& map) const;
	void paintChords(QPainter& p, const QRect& lane) const;
	//! The chord under @a pos, as itemAt reports it.
	bool chordAt(const QPoint& pos, QString* command, QJsonObject* args, QString* name) const;
	//! Polled: a changed chord track repaints the lane, and re-lays it out when it gains or loses its room.
	void followChordTrack();

	QPointer<SongEditor> m_editor;
	QList<int> m_drawn;
	unsigned m_chordRevision = 0;
	bool m_chordsDrawn = false;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_ARRANGER_RULER_LANES_H
