/*
 * NoteStepInput.cpp - the note editor's Step input mode: a MIDI key or a computer key enters one step
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

/*! Step input (docs/NOTE-EDITOR.md §3a): with Step input on, one key enters one note at the clip's
 *  step cursor and the cursor moves on by the grid step, so a phrase is entered one key at a time.
 *
 *  TWO SOURCES, ONE COMMAND. A MIDI keyboard arrives as the clip track's `midiNoteOn` (the signal
 *  NotePlayHandle emits for Origin::MidiInput, which the classic piano roll's record pass listens to
 *  as well); a computer key arrives as a QKeyEvent on this widget. Both call stepEnter(key), which
 *  makes ONE `note.step_input` call with this editor's grid and note length (stepEntryLength), so the
 *  two entry paths cannot use different settings and an agent's call over the socket cannot drift
 *  from the editor's.
 *
 *  THREADS. The MIDI signal may be emitted off the GUI thread (the MIDI input thread that creates a
 *  midi-input NotePlayHandle), so its connection is explicitly QUEUED: the slot that writes notes runs
 *  on the GUI thread with every other command. Nothing here runs on the audio thread, allocates there,
 *  or emits a signal from there - the connection is made and dropped only when the mode is armed or
 *  disarmed, and the only new audio-thread-adjacent cost is the queueing of an emission that the
 *  classic piano roll already queues (docs/qwen-fixes-2026-10/cluster-new-p12-351.md).
 *
 *  STOPPING IS SAFE BY CONSTRUCTION. A step is written on the key's note-ON and nothing is held
 *  afterwards: there is no pending start for a later key-up to pair with, so stopping the transport
 *  (this mode does not read it) or disarming while a key is held writes nothing and leaves nothing
 *  behind. A clip change or the clip going away drops the connection (dropStepInput), so a key
 *  pressed afterwards reaches no editor.
 */

#include <algorithm>

#include <QJsonObject>
#include <QKeyEvent>

#include "ControlRegistry.h"
#include "ControlStepInput.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteEditor.h"
#include "NoteGrid.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{

//! One computer key and the semitone it plays, in the piano view's own keyboard layout
//! (src/gui/instrument/PianoView.cpp): the two QWERTY rows, an octave apart, sharps on the row above
//! each natural. The KEY CODE is the logical Qt key, not the platform's scan code, because this
//! mapping has to be the same on every platform the build supports (PianoView's scan-code table is
//! per-platform; a step entered here must not move when the keyboard layout does).
struct ComputerKey
{
	Qt::Key key;
	int semitones;
};

constexpr ComputerKey kStepKeys[] = {
	// The lower row: Z = C at the piano view's default octave.
	{Qt::Key_Z, 0}, {Qt::Key_S, 1}, {Qt::Key_X, 2}, {Qt::Key_D, 3}, {Qt::Key_C, 4}, {Qt::Key_V, 5},
	{Qt::Key_G, 6}, {Qt::Key_B, 7}, {Qt::Key_H, 8}, {Qt::Key_N, 9}, {Qt::Key_J, 10}, {Qt::Key_M, 11},
	{Qt::Key_Comma, 12}, {Qt::Key_L, 13}, {Qt::Key_Period, 14}, {Qt::Key_Semicolon, 15}, {Qt::Key_Slash, 16},
	// The upper row, an octave above it: Q = c.
	{Qt::Key_Q, 12}, {Qt::Key_2, 13}, {Qt::Key_W, 14}, {Qt::Key_3, 15}, {Qt::Key_E, 16}, {Qt::Key_R, 17},
	{Qt::Key_5, 18}, {Qt::Key_T, 19}, {Qt::Key_6, 20}, {Qt::Key_Y, 21}, {Qt::Key_7, 22}, {Qt::Key_U, 23},
	{Qt::Key_I, 24}, {Qt::Key_9, 25}, {Qt::Key_O, 26}, {Qt::Key_0, 27}, {Qt::Key_P, 28},
};

//! Z's key: the piano view's C at the default octave (DefaultOctave + C - one octave).
constexpr int kStepBaseKey = DefaultOctave + Key::C - KeysPerOctave;

} // namespace

int NoteEditor::stepEntryLength() const
{
	// The length a click draws (ED-073): New Note Length when one is set, else the last drawn one.
	const int wanted = m_state.newLength > 0 ? m_state.newLength : m_state.lastLength;
	return std::max(1, wanted);
}

void NoteEditor::setStepCursor(int ticks)
{
	if (m_state.clip == nullptr) { return; }
	control::setStepInputCursor(clipId(), ticks);
	emit stateChanged();
}

void NoteEditor::followStepInput()
{
	dropStepInput();
	if (!m_state.stepInput || m_state.clip == nullptr) { return; }
	InstrumentTrack* track = m_state.clip->instrumentTrack();
	if (track == nullptr) { return; }
	// Queued on purpose: the emitter can be the MIDI input thread, and the note is written here, on
	// the GUI thread, under the same rule every other command follows.
	m_stepConnection = connect(track, &InstrumentTrack::midiNoteOn, this, &NoteEditor::onMidiNoteOn,
		Qt::QueuedConnection);
}

void NoteEditor::dropStepInput()
{
	if (m_stepConnection) { disconnect(m_stepConnection); m_stepConnection = QMetaObject::Connection(); }
}

void NoteEditor::setStepInput(bool on)
{
	if (on == m_state.stepInput)
	{
		emit stateChanged();  // the toggle follows the state even when nothing changed
		return;
	}
	m_state.stepInput = on;
	if (on)
	{
		// A phrase starts where the user is looking: the playhead inside the clip, on the grid. A
		// position outside the clip leaves the clip's own cursor alone.
		const Song* song = Engine::getSong();
		if (m_state.clip != nullptr && song != nullptr)
		{
			const int at = static_cast<int>(song->getPlayPos(Song::PlayMode::Song).getTicks()
				- m_state.clip->startPosition().getTicks());
			if (at >= 0 && at <= m_state.clip->length().getTicks())
			{
				control::setStepInputCursor(clipId(), NoteGrid::snapDown(at, std::max(1, m_state.gridTicks)));
			}
		}
		followStepInput();
	}
	else { dropStepInput(); }
	emit stateChanged();
}

void NoteEditor::onMidiNoteOn(const lmms::Note& note)
{
	stepEnter(note.key());
}

void NoteEditor::stepEnter(int key)
{
	if (!m_state.stepInput || m_state.clip == nullptr || key < 0 || key >= NumKeys) { return; }
	const QJsonObject placed = run(QStringLiteral("note.step_input"), {
		{QStringLiteral("clip"), clipId()},
		{QStringLiteral("key"), key},
		{QStringLiteral("length"), stepEntryLength()},
		{QStringLiteral("step"), std::max(1, m_state.gridTicks)},
		{QStringLiteral("velocity"), m_state.lastVelocity}});
	if (placed.isEmpty()) { return; }  // refused, and run() has already shown why
	revealStepCursor();
	emit stateChanged();
}

void NoteEditor::revealStepCursor()
{
	// Keep the cursor on screen: entering a phrase past the right edge would otherwise write notes
	// the user cannot see. A quarter of a bar of room follows the cursor, so the next cell shows.
	const NoteGrid& g = m_state.grid;
	if (g.pixelsPerTick() <= 0.0) { return; }
	const double x = g.xOfTick(control::stepInputCursor(clipId()));
	if (x >= 0.0 && x < g.viewportWidth() - 8) { return; }
	m_state.grid.setScrollTicks(std::max(0.0, control::stepInputCursor(clipId()) - DefaultTicksPerBar / 4.0));
}

bool NoteEditor::stepInputKey(const QKeyEvent* event)
{
	if (!m_state.stepInput || m_state.clip == nullptr) { return false; }
	// A plain key only: Shift/Ctrl/Alt are the editor's gestures, and a held key is one step, not a
	// drum roll (auto-repeat is ignored).
	if (event->isAutoRepeat() || event->modifiers() != Qt::NoModifier) { return false; }
	const auto* found = std::find_if(std::begin(kStepKeys), std::end(kStepKeys),
		[event](const ComputerKey& c) { return c.key == event->key(); });
	if (found == std::end(kStepKeys)) { return false; }
	const int key = kStepBaseKey + found->semitones;
	if (key < 0 || key >= NumKeys) { return false; }
	stepEnter(key);
	return true;
}

} // namespace lmms::gui
