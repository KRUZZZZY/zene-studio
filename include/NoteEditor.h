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
}

namespace lmms::gui
{

class NoteCanvas;
class NoteKeyboardView;
class NoteLaneView;
class NoteRuler;
class ShellButton;

//! The editor's tools (docs/NOTE-EDITOR.md §3). Keys 1..5 pick them.
enum class NoteTool
{
	Draw,
	Select,
	Slice,
	Erase,
	Audition
};

//! What every part of the editor reads: the clip, the geometry and the user's choices. One
//! instance, owned by NoteEditor and shared by reference with its parts.
struct NoteEditorState
{
	QPointer<MidiClip> clip;
	NoteGrid grid;
	NoteTool tool = NoteTool::Draw;
	int gridTicks = 12;           //!< snap: 1/16 at 192 ticks per bar
	int lastLength = 12;          //!< the length a drawn note takes
	int lastVelocity = 100;
	bool ghosts = true;           //!< draw the other clips of the track
	bool highlightScale = true;
	NoteLane lane = NoteLane::Velocity;
	QString scaleName;            //!< from scale.get_state, for the header label
};

//! The reworked piano roll (owner direction 2026-10-02, Bitwig-style). Every edit it makes is a
//! registered command - note.add / note.edit / note.remove / note.select / note.expression_set /
//! groove.quantize / scale.set / clip.resize - so the editor and an agent on the socket share
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
	//! Horizontal zoom, pixels per tick (NoteGrid clamps it).
	void setZoom(double pixelsPerTick);
	//! Fit the clip's length to the view and centre its notes vertically.
	void zoomToFit();

	//! Run a registered command as the editor's edit; the reply's result, or an empty object
	//! (and the error on stderr) when it was refused.
	QJsonObject run(const QString& command, const QJsonObject& args);

	//! Re-read the clip's notes (used keys, scale) and repaint every part.
	void refresh();
	//! The parts, for tests.
	NoteCanvas* canvas() const { return m_canvas; }
	NoteLaneView* laneView() const { return m_lanes; }

	//! window.get_state-style description of the editor, for note.editor_get_state.
	QJsonObject describe() const;

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
	void buildLaneChooser();
	void fillScaleMenu(QMenu* menu);
	void buildBody();
	void syncScrollBars();
	void readScale();
	void onClipDestroyed();
	void fitIfPending();

	NoteEditorState m_state;
	NoteRuler* m_ruler = nullptr;
	NoteKeyboardView* m_keyboard = nullptr;
	NoteCanvas* m_canvas = nullptr;
	NoteLaneView* m_lanes = nullptr;
	QScrollBar* m_hScroll = nullptr;
	QScrollBar* m_vScroll = nullptr;
	QComboBox* m_gridBox = nullptr;
	QComboBox* m_laneBox = nullptr;
	QLabel* m_clipLabel = nullptr;
	QLabel* m_scaleLabel = nullptr;
	QActionGroup* m_tools = nullptr;
	QActionGroup* m_folds = nullptr;
	int m_auditionKey = -1;
	bool m_syncing = false;
	bool m_pendingFit = false;
};

} // namespace lmms::gui

#endif // LMMS_GUI_NOTE_EDITOR_H
