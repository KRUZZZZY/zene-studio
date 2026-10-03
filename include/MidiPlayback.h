/*
 * MidiPlayback.h - global swing, note chasing, the MIDI playback offset and the loop pass
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
#ifndef LMMS_MIDI_PLAYBACK_H
#define LMMS_MIDI_PLAYBACK_H

#include <atomic>

#include "LmmsTypes.h"
#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 L07 (W3): how the arrangement's notes are played, beyond the notes themselves.
 *  - ED-142 global swing: every note on an off-beat of the swing grid (a 16th or an 8th) is played
 *    later by 'swing' of a grid step - project state, saved as <midi-playback> only when set;
 *  - ED-075 chase: when playback starts, jumps or loops into the middle of a note, the note sounds
 *    from there instead of waiting for the next one to start - a preference (config midi/chase,
 *    off by default, so playback stays as it always was);
 *  - ED-076 the MIDI playback offset: every note started later by a number of milliseconds, to line
 *    a slow instrument up - a preference (config midi/playbackoffset, 0 by default);
 *  - ED-133 the loop pass: how many times playback has come round since it started, which a note's
 *    recurrence (play on the first of every two passes, ...) is read against.
 *  The settings are atomics the audio thread reads at each note trigger; the pass and the chase
 *  state are written by the audio thread alone (Song::processNextBuffer, once a tick). */
class LMMS_EXPORT MidiPlayback : public ProjectSection
{
public:
	static constexpr float MaxSwing = 0.75f;
	static constexpr int MaxOffsetMs = 500;

	//! The one instance; registered as a ProjectSection the first time it is asked for.
	static MidiPlayback& instance();

	float swing() const { return m_swing.load(std::memory_order_relaxed); }
	//! The swing grid in ticks: a 16th (12 at 48 ticks a beat) or an 8th.
	int swingGrid() const { return m_swingGrid.load(std::memory_order_relaxed); }
	void setSwing(float swing, int grid);
	bool chase() const { return m_chase.load(std::memory_order_relaxed); }
	int offsetMs() const { return m_offsetMs.load(std::memory_order_relaxed); }
	//! Write the two preferences (and the config file).
	void setPreferences(bool chase, int offsetMs);

	// Audio thread, once a tick before the tracks play: a tick that does not follow the last one is
	// a start, a jump or a loop - notes are chased there, and a jump back counts a pass.
	void tickPlayed(tick_t tick);
	//! Playback stopped: the next tick is a start, and the pass count starts again.
	void stopped();
	bool chasingNow() const { return m_chasingNow; }
	int pass() const { return m_pass.load(std::memory_order_relaxed); }

	/*! The frames a note at song tick @a songTick waits before it starts: the swing of an off-beat
	 *  plus the playback offset. 0 when neither is set. */
	f_cnt_t delayFrames(tick_t songTick, float framesPerTick, sample_rate_t sampleRate) const;

	QString elementName() const override;
	bool shouldPersist() const override { return swing() > 0.f; }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override { setSwing(0.f, DefaultSwingGrid); }

	static constexpr int DefaultSwingGrid = 12;

private:
	MidiPlayback();

	std::atomic<float> m_swing{0.f};
	std::atomic<int> m_swingGrid{DefaultSwingGrid};
	std::atomic<bool> m_chase{false};
	std::atomic<int> m_offsetMs{0};
	// The audio thread's own; stopped() (UI thread) only raises m_restart, which the next tick takes.
	tick_t m_lastTick = -2;
	bool m_chasingNow = false;
	std::atomic<bool> m_restart{true};
	std::atomic<int> m_pass{0};
};

} // namespace lmms

#endif // LMMS_MIDI_PLAYBACK_H
