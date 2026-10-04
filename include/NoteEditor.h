/*
 * NoteEditor.h - the 0.5.0 note editor: the reworked piano roll in the shell's detail panel
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

#ifndef LMMS_GUI_NOTE_EDITOR_H
#define LMMS_GUI_NOTE_EDITOR_H

#include <QJsonObject>
#include <QPointer>
#include <QWidget>

#include "MidiClip.h"
#include "NoteGrid.h"
#include "NoteLanes.h"
#include "lmms_export.h"

class QActionGroup;
class QComboBox;
class QHBoxLayout;
class QMenu;
class QLabel;
class QScrollBar;

namespace lmms
{
class Note;
class Piano;
}

namespace lmms::gui
{

class ControllerLaneView;
class NoteCanvas;
class NoteKeyboardView;
class NoteLaneView;
class NoteRuler;
class ShellButton;

//! The editor's tools (docs/NOTE-EDITOR.md §3): FL Studio's piano-roll tools and letters - Draw P, Select E,
//! Slice C, Delete D (Erase), Play Y (Audition), Paint B, Mute T. Keys 1..7 pick them too, in this order.
enum class NoteTool
{
	Draw,
	Select,
	Slice,
	Erase,
	Audition,
	Paint, //!< drag to lay a note on every grid cell crossed
	Mute   //!< click a note (or the selection under it) to mute or unmute it
};

//! What a note's colour shows.
enum class NoteColor
{
	Velocity,
	Pitch,  //!< a hue per pitch class
	Track   //!< the clip's (or its track's) colour
};

//! What every part of the editor reads: the clip, the geometry and the user's choices. One
//! instance, owned by NoteEditor and shared by reference with its parts.
struct NoteEditorState
{
	QPointer<MidiClip> clip;
	NoteGrid grid;
	NoteTool tool = NoteTool::Draw;
	int gridTicks = 12;           //!< snap: 1/16 at 192 ticks per bar
	int lastLength = 12;          //!< the length a drawn note takes: the last note drawn, touched or resized (FL)
	int lastVelocity = 100;
	int newLength = 0;            //!< a clicked note's length in ticks; 0: the last drawn or resized (ED-073)
	QString audioBehind;          //!< the audio clip drawn behind the notes: "", "auto" or a clip id (ED-081)
	bool ghosts = true;           //!< draw the other clips of the track
	bool highlightScale = true;
	NoteLane lane = NoteLane::Velocity;
	QString scaleName;            //!< from scale.get_state, for the header label
	NoteColor colorMode = NoteColor::Velocity;
	bool keepInScale = false;     //!< drawn and moved notes land on the nearest key of the scale
	QString controllerLane;       //!< the controller lane shown instead of the note lane ("cc:1", ...), or empty
};

//! The reworked piano roll (owner direction 2026-10-02; its gestures FL Studio's, 2026-10-03). Every edit it
//! makes is a registered command - note.add / note.edit / note.remove_many / note.slice / note.copy, cut, paste,
//! duplicate / note.select / note.expression_set / groove.quantize / scale.set / clip.resize - so the editor
//! and an agent on the socket share
//! one implementation and one undo history (SPEC A11). It reads the clip directly, on the GUI
//! thread, and never touches the audio thread: auditioning goes through the instrument track's
//! piano model, the same thread-safe path a MIDI keyboard uses.
class LMMS_EXPORT NoteEditor : public QWidget
{
	Q_OBJECT

public:
	explicit NoteEditor(QWidget* parent = nullptr);
	~NoteEditor() override;

	void setClip(MidiClip* clip);
	MidiClip* clip() const { return m_state.clip.data(); }
	QString clipId() const;

	NoteEditorState& state() { return m_state; }
	const NoteEditorState& state() const { return m_state; }

	void setTool(NoteTool tool);
	void setGridTicks(int ticks);
	void setFold(NoteFold fold);
	void setLane(NoteLane lane);
	void setGhosts(bool on);
	//! 0.5.0 L07: a clicked note's length (0: the last one) and velocity (ED-073); the audio behind (ED-081).
	void setNewNote(int length, int velocity);
	void setAudioBehind(const QString& choice);
	void setColorMode(NoteColor mode);
	void setKeepInScale(bool on);
	//! Show the clip's controller lane @a id (cc:<n>, pitchbend, aftertouch) under the notes; empty
	//! goes back to the note lane.
	void setControllerLane(const QString& id);
	//! The clip track's name for @a key (key_names.*), or empty.
	QString keyLabel(int key) const;
	//! Horizontal zoom, pixels per tick (NoteGrid clamps it).
	void setZoom(double pixelsPerTick);
	//! Fit the clip's length to the view and centre its notes vertically.
	void zoomToFit();
	//! FL's Z: fit the selected notes to the view (with a little room), or the whole clip when none is selected.
	void zoomToSelection();
	//! Whether @a event is one of the editor's own keys - a tool letter (P B D T C E Y), Z, or 1..7 - with
	//! no modifier; the note area claims these over a window shortcut while it has the focus.
	static bool handlesKey(const QKeyEvent* event);

	//! Run a registered command as the editor's edit; the reply's result, or an empty object
	//! (with the refusal shown as a floating message and on stderr) when it was refused.
	QJsonObject run(const QString& command, const QJsonObject& args);

	//! Re-read the clip's notes (used keys, scale) and repaint every part.
	void refresh();
	//! The parts, for tests.
	NoteCanvas* canvas() const { return m_canvas; }
	NoteLaneView* laneView() const { return m_lanes; }

	//! window.get_state-style description of the editor, for note.editor_get_state.
	QJsonObject describe() const;

	//! Show the song position inside the clip on the note area (and follow it while playing);
	//! @a always shows it even when stopped (after a seek).
	void updatePlayhead(bool always = false);

	//! Audition: press and release a key through the clip's instrument (GUI thread only).
	void auditionPress(int key, int velocity);
	void auditionRelease(int key);

signals:
	void clipChanged();
	void stateChanged();

protected:
	void keyPressEvent(QKeyEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	QWidget* buildToolbar();
	void addToolButtons(QWidget* bar, QHBoxLayout* row);
	void addGridControls(QWidget* bar, QHBoxLayout* row);
	void addFoldControls(QWidget* bar, QHBoxLayout* row);
	void addScaleButton(QWidget* bar, QHBoxLayout* row);
	void addViewButtons(QWidget* bar, QHBoxLayout* row);
	void addNotesMenu(QWidget* bar, QHBoxLayout* row);
	void fillNotesMenu(QMenu* menu);
	void fillVelocityMenu(QMenu* menu, const QJsonObject& scoped);
	void fillTimingMenu(QMenu* menu, const QJsonObject& scoped);
	void fillMuteMenu(QMenu* menu);
	//! The New Note Length choices (window.note_editor_set new_length): the Notes menu's, and the grid box's
	//! right-click menu (FL: right-clicking the snap sets the default length).
	void fillNewLengthMenu(QMenu* menu);
	void fillScriptsMenu(QMenu* menu, const QString& clip);
	void fillPitchCurveMenu(QMenu* menu);
	//! "selection" when the clip has selected notes, else "clip" - the scope the Notes menu acts on.
	QString noteScope() const;
	void buildLaneChooser();
	void fillScaleMenu(QMenu* menu);
	void buildBody();
	void syncScrollBars();
	void readScale();
	//! readScale() when the scale context changed since it last ran (a change made elsewhere).
	void followScale();
	void onClipDestroyed();
	//! The clip changed or went: every gesture in flight is dropped (none commits to another clip)
	//! and a held audition key is released on the instrument it sounds on.
	void cancelGestures();
	void fitIfPending();

	NoteEditorState m_state;
	NoteRuler* m_ruler = nullptr;
	NoteKeyboardView* m_keyboard = nullptr;
	NoteCanvas* m_canvas = nullptr;
	NoteLaneView* m_lanes = nullptr;
	ControllerLaneView* m_ccLane = nullptr;
	QScrollBar* m_hScroll = nullptr;
	QScrollBar* m_vScroll = nullptr;
	QComboBox* m_gridBox = nullptr;
	QComboBox* m_laneBox = nullptr;
	QLabel* m_clipLabel = nullptr;
	QLabel* m_scaleLabel = nullptr;
	QActionGroup* m_tools = nullptr;
	QActionGroup* m_folds = nullptr;
	int m_auditionKey = -1;
	QPointer<Piano> m_auditionPiano;  //!< the instrument the held audition key sounds on, whatever clip is open now
	int m_scaleRoot = -1;     //!< the scale context readScale() last read
	QString m_scaleSeen;
	bool m_syncing = false;
	bool m_pendingFit = false;
	class QTimer* m_playheadTimer = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_NOTE_EDITOR_H
