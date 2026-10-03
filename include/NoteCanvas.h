/*
 * NoteCanvas.h - the note editor's note area: rows, grid, notes, ghosts, and the tools
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

#ifndef LMMS_GUI_NOTE_CANVAS_H
#define LMMS_GUI_NOTE_CANVAS_H

#include <utility>
#include <vector>

#include <QPoint>
#include <QRect>
#include <QWidget>

#include "NoteBackgroundAudio.h"
#include "lmms_export.h"

namespace lmms
{
class MidiClip;
class Note;
}

namespace lmms::gui
{

class NoteEditor;
struct NoteEditorState;

//! The visible notes of the clip: [first, last) indices into its sorted note list. A note
//! starting before the view can still reach into it, so the search starts one longest-note
//! length to the left (NoteCanvas::visibleRange).
using NoteRange = std::pair<int, int>;

//! The note area. Painting reads the clip and the shared NoteEditorState; it draws only the
//! rows and notes on screen, found by binary search over the position-sorted note list, so a
//! frame costs the visible notes, not the clip's (PianoRollBenchmarkTest measures it).
//! Gestures are previewed locally and committed as ONE command on release (note.add,
//! note.edit, note.remove, note.select), so a drag is one undo step.
class LMMS_EXPORT NoteCanvas : public QWidget
{
	Q_OBJECT

public:
	explicit NoteCanvas(NoteEditor* editor);

	//! Recompute what painting caches from the clip: the longest note.
	void clipChanged();
	NoteRange visibleRange() const;
	//! The note under @a pos (topmost when notes overlap), or nullptr.
	Note* noteAt(const QPoint& pos) const;
	//! The note's rectangle on screen; empty when its key is folded away.
	QRect noteRect(const Note& note) const;
	//! The rectangle a note of @a key over [start, end) ticks would occupy.
	QRect rectFor(int key, int start, int end) const;
	//! Whether @a pos is on the resize handle at the right end of @a note.
	bool onResizeHandle(const Note& note, const QPoint& pos) const;

	//! The playhead, in ticks inside the clip, or -1 when the clip is not playing.
	void setPlayhead(int tick);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	enum class Drag
	{
		None,
		Draw,
		Move,
		Resize,
		Band,
		Erase,
		Audition,
		Paint,
		Velocity
	};

	// painting (NoteCanvas.cpp)
	void paintRows(QPainter& p, const QRect& area) const;
	void paintGrid(QPainter& p, const QRect& area) const;
	void paintClipBounds(QPainter& p) const;
	void paintGhosts(QPainter& p) const;
	void paintNotes(QPainter& p) const;
	void paintNote(QPainter& p, const Note& note, const QRect& r) const;
	void paintDragPreview(QPainter& p) const;
	void paintDrawPreview(QPainter& p) const;
	void paintErasePreview(QPainter& p) const;
	void paintMovePreview(QPainter& p) const;
	void paintGhostClip(QPainter& p, const MidiClip& clip, int offset) const;
	void paintOverlay(QPainter& p) const;

	// gestures (NoteCanvasInput.cpp)
	void beginPress(QMouseEvent* event);
	void pressDrawOrSelect(Note* note, QMouseEvent* event);
	void commitDrag(Drag drag, bool shift);  //!< the one command a released gesture sends
	void pressAudition(const Note* note);
	void pressErase(const Note* note);
	bool nudgeForKey(int key, bool shift);
	int resizeDelta() const;  //!< the snapped length change of a resize drag (0 otherwise)
	void pressOnNote(Note* note, QMouseEvent* event);
	void pressOnEmpty(QMouseEvent* event);
	std::pair<int, int> drawSpan() const;  //!< the note a draw gesture makes: start, length
	void commitDraw();
	void commitMove();
	void commitResize();
	void commitBand(bool additive);
	void commitErase();
	void sliceAt(Note* note, const QPoint& pos);
	void selectOnly(const std::vector<Note*>& notes, bool additive);
	Note* noteById(int id) const;
	std::vector<Note*> selectedNotes() const;
	void nudgeSelection(int ticks, int keys);
	void deleteSelection();
	int dragTicks() const;
	int dragKeys() const;
	int keyMovedBy(int key, int rows) const;

	// paint tool, velocity drag, note colour (NoteCanvasGestures.cpp)
	int scaleKey(int key) const;  //!< @a key, moved into the scale when keep-in-scale is on
	void pressPaint();
	void pressMute(const Note* note);
	void extendPaint(const QPoint& pos);
	void commitPaint();
	void pressVelocity(Note* note);
	int velocityDelta() const;
	void commitVelocity();
	void paintPaintPreview(QPainter& p) const;
	void paintVelocityPreview(QPainter& p) const;
	QColor noteFill(const Note& note) const;
	QString noteLabel(const Note& note) const;

	NoteEditor* m_editor;
	NoteBackgroundAudio m_audioBehind;  //!< 0.5.0 ED-081
	NoteEditorState& m_state;
	int m_longest = 0;
	int m_playhead = -1;

	Drag m_drag = Drag::None;
	QPoint m_pressPos;
	QPoint m_currentPos;
	int m_pressTick = 0;
	int m_pressKey = -1;
	// Notes are held by Note::id(), never by pointer, across a gesture: a command arriving on
	// the socket between press and release can replace the clip's note objects (a journal
	// restore re-loads the list), and a held pointer would then be a freed one - BUGS 11.27's
	// lesson, a view must not outlive its model.
	int m_anchorId = -1;                 //!< the note a move/resize was started on
	std::vector<int> m_erasedIds;        //!< notes the eraser has crossed, removed on release
	int m_auditionKey = -1;
	std::vector<std::pair<int, int>> m_painted;  //!< the paint tool's cells so far: key, start tick
};

} // namespace lmms::gui

#endif // LMMS_GUI_NOTE_CANVAS_H
