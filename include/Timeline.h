/*
 * Timeline.h
 *
 * Copyright (c) 2023 Dominic Clark
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
 */

#ifndef LMMS_TIMELINE_H
#define LMMS_TIMELINE_H

#include <QObject>

#include "AudioEngine.h"
#include "Engine.h"
#include "JournallingObject.h"
#include "TimePos.h"

namespace lmms {

class Timeline : public QObject, public JournallingObject
{
	Q_OBJECT

public:
	enum class StopBehaviour
	{
		BackToZero,
		BackToStart,
		KeepPosition
	};

	auto pos() const -> const TimePos& { return m_pos; }

	auto ticks() const -> tick_t { return m_pos.getTicks(); }

	//! Forcefully sets the current ticks, resets the frame offset, and sets the elapsed seconds based on the global position (ignoring potential mid-song tempo changes)
	//! Unless `jumped` is passed as false, this function will emit the `positionJumped` signal to allow other widgets to update accordingly.
	void setTicks(tick_t ticks, bool jumped = true)
	{
		m_pos.setTicks(ticks);
		m_frameOffset = 0;
		m_elapsedSeconds = ticks * Engine::framesPerTick() / Engine::audioEngine()->outputSampleRate();
		if (jumped) { emit positionJumped(); }
		emit positionChanged();
	}

	//! Advances the current timeline position by a certain number of ticks, in addition to updating the elapsed time based on the current tempo.
	void incrementTicks(tick_t increment)
	{
		m_pos.setTicks(ticks() + increment);
		m_elapsedSeconds += increment * Engine::framesPerTick() / Engine::audioEngine()->outputSampleRate();
		emit positionChanged();
	}

	auto frameOffset() const -> float { return m_frameOffset; }
	void setFrameOffset(const float frame) { m_frameOffset = frame; }

	auto loopBegin() const -> TimePos { return m_loopBegin; }
	auto loopEnd() const -> TimePos { return m_loopEnd; }
	auto loopEnabled() const -> bool { return m_loopEnabled; }

	void setLoopBegin(TimePos begin);
	void setLoopEnd(TimePos end);
	void setLoopPoints(TimePos begin, TimePos end);
	void setLoopEnabled(bool enabled);

	// ---- punch in/out (0.3.0) ---------------------------------------------
	/*! A punch region is a tick range on the transport. While it is ARMED,
	 *  capture is gated to it: only material whose transport position falls in
	 *  [punchBegin, punchEnd) is captured, and a take armed outside the region
	 *  captures nothing. This is the ENGINE half of the feature
	 *  (docs/CLIP-CAPTURE-DESIGN.md slice B, which names the timeline as the
	 *  natural host for a punch range): the range is project state - it is
	 *  written with the timeline and survives a save/load - and
	 *  punchCapturesAt() is the one predicate the capture path consults.
	 *
	 *  The audio-side gate itself is NOT wired in 0.3.0: no command reaches
	 *  the recorder's input path, ALSA has no capture path in this build and
	 *  nothing exercises it. The range, its arm flag and the predicate are
	 *  real and drivable (transport.punch_set / punch_clear / punch_get_state);
	 *  docs/KNOWN-LIMITATIONS.md carries the one-line statement. */
	auto punchBegin() const -> tick_t { return m_punchBegin; }
	auto punchEnd() const -> tick_t { return m_punchEnd; }
	auto punchEnabled() const -> bool { return m_punchEnabled; }
	//! True while an armed region covers at least one tick: the gate is live.
	bool punchArmed() const { return m_punchEnabled && m_punchEnd > m_punchBegin; }
	//! THE GATE. True when \a ticks is inside an ARMED region; false otherwise
	//! (unarmed, empty, or outside). Deliberately a pure function of the range
	//! so it can be asserted without an audio device.
	bool punchCapturesAt(tick_t ticks) const;
	//! True when the region is worth writing: armed, or holding a non-empty
	//! range. False for a timeline that has never punched, which is what keeps
	//! such a project's saved bytes identical to what it has always had.
	bool shouldPersistPunch() const
	{
		return m_punchEnabled || m_punchBegin > 0 || m_punchEnd > 0;
	}
	//! Sets the range (normalised, as setLoopPoints is), keeping the arm flag.
	void setPunchRange(tick_t begin, tick_t end);
	void setPunchEnabled(bool enabled);
	//! Disarm and forget the region: the state a project that never punched has.
	void clearPunch();

	auto playStartPosition() const -> TimePos { return m_playStartPosition; }
	auto stopBehaviour() const -> StopBehaviour { return m_stopBehaviour; }

	void setPlayStartPosition(TimePos position) { m_playStartPosition = position; }
	void setStopBehaviour(StopBehaviour behaviour);

	auto getElapsedSeconds() const -> double { return m_elapsedSeconds + frameOffset() / Engine::audioEngine()->outputSampleRate(); }

	auto nodeName() const -> QString override { return "timeline"; }

signals:
	void loopEnabledChanged(bool enabled);
	void punchChanged();
	void stopBehaviourChanged(lmms::Timeline::StopBehaviour behaviour);
	void positionChanged();
	void positionJumped();

protected:
	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;

private:
	TimePos m_loopBegin = TimePos{0};
	TimePos m_loopEnd = TimePos{DefaultTicksPerBar};
	bool m_loopEnabled = false;
	// Punch in/out. Default: no region (begin == end == 0) and disarmed, so a
	// timeline that has never punched writes no punch attributes at all.
	tick_t m_punchBegin = 0;
	tick_t m_punchEnd = 0;
	bool m_punchEnabled = false;
	TimePos m_pos = TimePos{0};

	float m_frameOffset = 0;

	double m_elapsedSeconds = 0;

	StopBehaviour m_stopBehaviour = StopBehaviour::BackToStart;
	TimePos m_playStartPosition = TimePos{-1};
};

} // namespace lmms

#endif // LMMS_TIMELINE_H
