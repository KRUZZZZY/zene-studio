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
//! Gestures follow FL Studio's piano roll (owner decision 2026-10-03; docs/NOTE-EDITOR.md §3):
//! left draws, moves and resizes, right deletes what it crosses (or, from an empty row, slices),
//! Ctrl selects, Shift and Ctrl lock pitch and timing while dragging, Alt bypasses the snap.
//! Each gesture is previewed locally and committed as ONE command on release (note.add,
//! note.edit, note.remove_many, note.slice, note.add_many, note.select), so a stroke is one
//! undo step.
class LMMS_EXPORT NoteCanvas : public QWidget
{
	Q_OBJECT

public:
	explicit NoteCanvas(NoteEditor* editor);

	//! Recompute what painting caches from the clip: the longest note.
	void clipChanged();
	//! Re-apply the active tool's cursor without a mouse move (a toolbar or keyboard tool change).
	void refreshCursor();
	NoteRange visibleRange() const;
	//! The note under @a pos (topmost when notes overlap), or nullptr.
	Note* noteAt(const QPoint& pos) const;
	//! The note's rectangle on screen; empty when its key is folded away.
	QRect noteRect(const Note& note) const;
	//! 0.5.0 Q050-141: the note's touch target - @a noteRect widened (never moved) to the design system's
	//! minimum width, so a short note is a finger-sized target. Targets of adjacent short notes overlap;
	//! noteAt() resolves a tap on an overlap to the note whose drawn rectangle holds it, else the nearest.
	QRect touchRect(const Note& note) const;
	//! The rectangle a note of @a key over [start, end) ticks would occupy.
	QRect rectFor(int key, int start, int end) const;
	//! Whether @a pos is on the resize handle at the right end of @a note.
	bool onResizeHandle(const Note& note, const QPoint& pos) const;
	//! Whether @a pos is on the handle at the left end of @a note (Shift-drag there resizes from the start).
	bool onStartHandle(const Note& note, const QPoint& pos) const;

	//! Point @a state's grid at the song's metre as its clip sees it (0.5.0 W2): the song's time signature
	//! and, for a clip in the arrangement, the tempo map's metre changes. The grid and the ruler paint with it.
	static void followSongMeter(NoteEditorState& state);
	//! Repaint @a widget when the song's time signature changes.
	static void repaintOnMeterChange(QWidget* widget);

	//! The playhead, in ticks inside the clip, or -1 when the clip is not playing.
	void setPlayhead(int tick);
	int playhead() const { return m_playhead; }
	//! Drop the gesture in flight (the clip changed or went): its release commits nothing to whichever
	//! clip is open then. The editor releases a held audition key itself.
	void cancelGesture();

protected:
	bool event(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void focusInEvent(QFocusEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;

private:
	enum class Drag
	{
		None,
		Draw,
		Move,
		Resize,
		ResizeStart,  //!< Shift-drag on a note's left edge
		Band,
		Erase,        //!< the Delete tool, or the right button over notes
		Slice,        //!< the Slice tool, or the right button from an empty row
		Mute,
		Audition,
		Paint,
		Pan           //!< the middle button scrolls both ways
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
	void paintHoverPreview(QPainter& p) const;  //!< the Draw tool's note under the idle pointer
	void paintStrokePreview(QPainter& p) const;  //!< the notes a delete or mute stroke has crossed
	bool stroked(const Note& note) const;        //!< whether the stroke in flight has crossed @a note
	void paintMovePreview(QPainter& p) const;
	void paintSlicePreview(QPainter& p) const;
	void paintGhostClip(QPainter& p, const MidiClip& clip, int offset) const;
	void paintOverlay(QPainter& p) const;

	// gestures (NoteCanvasInput.cpp)
	bool altHeld() const;  //!< Alt: the grid snap is off while it is held
	int snapTicks(int ticks) const;
	void beginPress(QMouseEvent* event);
	void pressRight(const Note* note);
	void pressDrawOrSelect(Note* note, QMouseEvent* event);
	void commitDrag(Drag drag);  //!< the one command a released gesture sends
	void commitStroke(Drag drag);  //!< ... when it was a delete, slice, mute or paint stroke
	//! The cursor the active tool shows: its own shape, or a resize/move over a note with Draw/Select.
	void updateCursor(const QPoint& pos);
	//! The key a draw gesture or the idle preview is on now (the pointer's row, scaled; the press row when off it).
	int drawKey() const;
	//! While a draw drags to another row: release the old auditioned key and sound the new one.
	void followDrawPitch();
	void pressAudition(const Note* note);
	void pressStroke(Drag drag, const Note* note);  //!< a delete or mute stroke, starting on @a note (or none)
	void markStroke(const Note* note);  //!< adds @a note (if any) to m_strokeIds, kept sorted
	int resizeDelta() const;  //!< the snapped length change of a resize drag (0 otherwise)
	int startDelta() const;   //!< the snapped start change of a resize-from-start drag (0 otherwise)
	void pressOnNote(Note* note, QMouseEvent* event);
	void pressOnEmpty(QMouseEvent* event);
	void pressBand(QMouseEvent* event);
	std::pair<int, int> drawSpan() const;  //!< the note a draw gesture makes: start, length
	void commitDraw();
	void commitMove();
	void commitResize();
	void commitResizeStart();
	void commitBand();
	void commitErase();
	void selectOnly(const std::vector<Note*>& notes, bool additive);
	Note* noteById(int id) const;
	std::vector<Note*> selectedNotes() const;
	int dragTicks() const;
	int dragKeys() const;
	int keyMovedBy(int key, int rows) const;

	// strokes, paint tool, note colour (NoteCanvasGestures.cpp)
	int scaleKey(int key) const;  //!< @a key, moved into the scale when keep-in-scale is on
	void pressPaint();
	void extendPaint(const QPoint& pos);
	//! The stroke's path from @a from to @a to: every note on it is marked, not only the one at its end.
	void strokeAlong(const QPoint& from, const QPoint& to);
	//! The paint stroke from @a from to @a to: every grid cell it crosses is painted, not just those an event lands on.
	void paintAlong(const QPoint& from, const QPoint& to);
	void commitPaint();
	void commitMute();
	int sliceTick() const;  //!< the snapped tick a slice line stands at
	void commitSlice();
	void scrubTo(const QPoint& pos);  //!< the Play tool: a drag sounds each key it reaches
	void paintPaintPreview(QPainter& p) const;
	QColor noteFill(const Note& note) const;
	QString noteLabel(const Note& note) const;

	// keys, wheel, clipboard, middle button (NoteCanvasKeys.cpp)
	void nudgeSelection(int ticks, int keys);
	bool nudgeForKey(int key, Qt::KeyboardModifiers modifiers);
	bool selectionKey(const QKeyEvent* event);
	bool clipboardKey(const QKeyEvent* event);
	void deleteSelection();
	//! note.copy / note.cut / note.paste / note.duplicate on the selection, or the whole clip when none is selected.
	void runClipboard(const QString& command);
	void wheelOnNote(const Note* note, int amount, Qt::KeyboardModifiers mods);
	void wheelView(const QWheelEvent* event, int amount);
	void pressPan();
	void panTo(const QPoint& pos);

	// keyboard editing and screen-reader access, theme 10 (NoteCanvasAccess.cpp)
	void installKeyboardAccess();  //!< the role and value a screen reader reads, and the key cursor's mark
	//! Alt+arrows, Home/End move between notes; Ctrl+Left/Right resize, Ctrl+Up/Down change velocity; Insert adds a
	//! note after the cursor, Enter opens its properties, Ctrl+Shift+L the editable note list.
	bool editKey(const QKeyEvent* event);
	static bool isEditKey(const QKeyEvent* event);  //!< the keys editKey() acts on, claimed over window shortcuts
	bool moveKey(const QKeyEvent* event);
	bool changeKey(const QKeyEvent* event);
	void moveCursor(int step, bool extend);  //!< step: a notekeys::Step
	void resizeSelection(int ticks);
	void changeVelocity(int amount);
	void insertAfterCursor();
	const Note* cursorNote() const;  //!< the note the keys act on: the last one moved to while selected, else the first selected
	void showCursorNote(const Note& note);  //!< scrolls the view to it
	QString accessibleValue() const;

	NoteEditor* m_editor;
	NoteBackgroundAudio m_audioBehind;  //!< 0.5.0 ED-081
	NoteEditorState& m_state;
	int m_longest = 0;
	int m_playhead = -1;

	Drag m_drag = Drag::None;
	Qt::KeyboardModifiers m_mods;      //!< the modifiers of the gesture's latest event (the locks, Alt)
	Qt::KeyboardModifiers m_pressMods; //!< the modifiers at the press (Ctrl selects, Ctrl+Shift adds)
	QPoint m_pressPos;
	QPoint m_currentPos;
	QPoint m_hoverPos;                   //!< the pointer's last idle position, for the Draw preview
	bool m_hovering = false;             //!< the pointer is over the note area (and a Draw preview may show)
	int m_pressTick = 0;
	int m_pressKey = -1;
	// Notes are held by Note::id(), never by pointer, across a gesture: a command arriving on
	// the socket between press and release can replace the clip's note objects (a journal
	// restore re-loads the list), and a held pointer would then be a freed one - BUGS 11.27's
	// lesson, a view must not outlive its model.
	int m_anchorId = -1;                 //!< the note a move/resize/mute stroke was started on
	std::vector<int> m_strokeIds;        //!< notes a delete or mute stroke has crossed (sorted)
	int m_muteTo = -1;                   //!< a mute stroke's state: 1 mute, 0 unmute, -1 not yet decided
	QString m_justDrawn;                 //!< the note the last click drew: a double-click that drew it is not a properties request
	ulong m_drawnAt = 0;                 //!< the timestamp of the release that drew it
	double m_wheelNotches = 0.0;         //!< Alt+wheel notches left over from a partial wheel event
	int m_auditionKey = -1;
	std::vector<std::pair<int, int>> m_painted;  //!< the paint tool's cells so far: key, start tick
	double m_panTicks = 0;               //!< the view at a middle-button press
	int m_panY = 0;
	int m_keyCursorId = -1;              //!< the note the keyboard last moved to (Note::id()), theme 10
};

} // namespace lmms::gui

#endif // LMMS_GUI_NOTE_CANVAS_H
