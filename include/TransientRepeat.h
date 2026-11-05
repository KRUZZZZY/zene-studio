/*
 * TransientRepeat.h - transient repeat / gate warp rendering (Q050-162)
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

#ifndef LMMS_TRANSIENT_REPEAT_H
#define LMMS_TRANSIENT_REPEAT_H

#include <array>
#include <atomic>
#include <cstdint>
#include <span>

#include <QString>

#include "LmmsTypes.h"
#include "WarpMarkers.h"
#include "lmms_export.h"

namespace lmms
{

class SampleFrame;

/*! The repeat interval of the two transient warp modes (Q050-162,
 *  `WarpStretchMode::Repeat` / `Gate`): either a musical division that follows
 *  the project tempo (`1/8` = an eighth note) or a free interval in
 *  milliseconds that does not.
 *
 *  A value type that packs into one 32-bit word (pack/unpack), so the clip can
 *  publish it with a single atomic store and the render thread read it with a
 *  single load - never half of an old rate and half of a new one. The default
 *  is a synced 1/16, and a clip at the default writes nothing to its file. */
struct LMMS_EXPORT TransientRepeatRate
{
	static constexpr int DefaultDivision = 16;
	static constexpr double MinIntervalMs = 5.0;
	static constexpr double MaxIntervalMs = 2000.0;

	//! Synced: the interval is 1/division of a whole note; 0 means free.
	int division = DefaultDivision;
	//! Free: the interval in microseconds (only read when division is 0).
	std::int32_t intervalMicros = 0;

	bool synced() const { return division > 0; }
	double intervalMs() const { return static_cast<double>(intervalMicros) / 1000.0; }
	bool isDefault() const { return division == DefaultDivision; }
	bool operator==(const TransientRepeatRate& other) const
	{
		return division == other.division && (synced() || intervalMicros == other.intervalMicros);
	}
	bool operator!=(const TransientRepeatRate& other) const { return !(*this == other); }

	//! The divisions offered: 1/1 .. 1/32.
	static bool validDivision(int division);
	static TransientRepeatRate syncedTo(int division);
	//! @a ms clamped to [MinIntervalMs, MaxIntervalMs].
	static TransientRepeatRate freeInterval(double ms);

	//! Synced as the positive division, free as the negated microseconds.
	std::int32_t pack() const { return synced() ? division : -intervalMicros; }
	static TransientRepeatRate unpack(std::int32_t code);

	//! The wire and file name: `1/8` for a division, `125ms` for a free interval.
	QString name() const;
	//! The division's name alone (`1/8`); empty for a free interval.
	QString divisionName() const;
	//! Parses name(); false (and @a rate untouched) for anything else.
	static bool fromName(const QString& name, TransientRepeatRate* rate);
	//! Parses divisionName(); 0 for anything else.
	static int divisionFromName(const QString& name);

	//! The interval in output frames: synced through @a outputFramesPerTick (the
	//! project tempo), free through @a outputRate alone. Never below one frame.
	double intervalFrames(double outputFramesPerTick, double outputRate) const;
};

/*! A clip's transients, as PLAY frames in ascending order (the coordinate the
 *  warp map is written in), with a fixed capacity so a copy of it is a plain
 *  value copy on the audio thread. Built on the control thread by
 *  SampleClip from clipTransientFrames(); never analysed while rendering. */
class LMMS_EXPORT TransientTable
{
public:
	static constexpr int MaxTransients = 256;

	//! Sorts, drops duplicates and keeps the first MaxTransients (authoring call).
	void set(std::span<const f_cnt_t> frames);
	int size() const { return m_count; }
	f_cnt_t operator[](int index) const { return m_frames[static_cast<std::size_t>(index)]; }

private:
	std::array<f_cnt_t, MaxTransients> m_frames{};
	int m_count = 0;
};

/*! The clip's transient table as the render thread may read it (Q050-162,
 *  round 2): three slots, one of them published, and a reader count per slot.
 *
 *  A reader PINS the published slot (increments its count), then checks the
 *  slot is still the published one; only then does it copy, and it unpins
 *  after. The single writer (the control thread) writes only a slot that is
 *  neither published nor pinned, and publishes it afterwards. So no slot is
 *  ever written while a reader copies it - a reader that pinned a slot the
 *  writer had already chosen sees it unpublished on the re-check and backs
 *  off without touching it. All the atomics are sequentially consistent: the
 *  reader's pin-then-check and the writer's publish-then-scan are a Dekker
 *  pair, which needs the single total order.
 *
 *  The reader never waits: it makes at most ReadAttempts tries (each one lost
 *  only to a publish that happened in between) and otherwise reports no slot
 *  - an empty table, i.e. the clip renders as one segment for that pass. The
 *  writer, never on the audio thread, yields until a slot is free; with three
 *  slots one is free unless readers hold both unpublished ones. */
class LMMS_EXPORT PublishedTransients
{
public:
	static constexpr int Slots = 3;
	static constexpr int ReadAttempts = 4;

	//! Control thread (the one writer): build a table from @a frames and publish it.
	void publish(std::span<const f_cnt_t> frames);
	//! Any thread, lock-free and non-waiting: the slot now pinned for reading, or -1.
	int pin() const;
	//! The table of a slot pin() returned; valid until unpin().
	const TransientTable& pinned(int slot) const { return m_tables[static_cast<std::size_t>(slot)]; }
	void unpin(int slot) const;
	//! pin(), copy, unpin(): the latest table, or an empty one if every attempt raced a publish.
	TransientTable read() const;
	//! Test seam: the slot currently published.
	int publishedSlot() const { return m_published.load(); }

private:
	std::array<TransientTable, Slots> m_tables{};
	mutable std::array<std::atomic<int>, Slots> m_readers{};
	std::atomic<int> m_published{0};
};

/*! Where a transient render reads its audio: the sample buffer, the clip
 *  window inside it (in PLAY frames, the coordinate the segments use) and the
 *  two Sample properties every other render path applies - a reversed source
 *  reads play frame p from buffer frame size - 1 - p, and every frame is
 *  scaled by the amplification (Sample::render). */
struct TransientSource
{
	const SampleFrame* buffer = nullptr;
	f_cnt_t bufferFrames = 0;
	f_cnt_t windowIn = 0;
	f_cnt_t windowFrames = 0;
	bool reversed = false;
	float amplification = 1.0f;

	//! The frame at window-relative play position @a index (< windowFrames).
	const SampleFrame& at(f_cnt_t index) const;
};

/*! The render half: one play handle's transient segments and the cursor that
 *  walks them. `prepare` and `addSegment` run once when the handle is built,
 *  `render` once per period - all three allocation-free and lock-free, on fixed
 *  arrays.
 *
 *  A segment starts at a transient (or at the head of the clip's window) and
 *  owns the output span until the next segment's start - its SLOT, which the
 *  clip's warp map sets. Inside its slot a segment's audio plays at its own
 *  natural rate from the transient (so the pitch never moves), and:
 *
 *  - **Repeat** restarts it from the transient at every interval;
 *  - **Gate** plays it once and silences everything from the first interval on.
 *
 *  Either way the audio of one segment never reaches into the next segment's
 *  source, and the slot ends where the next one starts - so the render lasts
 *  exactly the warped span, with a partial last interval at the clip end. A
 *  cut inside a slot gets a short linear ramp so it does not click. */
class LMMS_EXPORT TransientRepeatRenderer
{
public:
	static constexpr int MaxSegments = TransientTable::MaxTransients + 1;
	static constexpr double RampFrames = 32.0;

	//! @a intervalFrames the repeat interval, @a sourcePerOutput the natural
	//! rate (source frames per output frame), @a windowFrames the window length.
	void prepare(WarpStretchMode mode, double intervalFrames, double sourcePerOutput, f_cnt_t windowFrames);
	/*! Appends a segment starting at window-relative source frame @a source and
	 *  output frame @a output (relative to this pass's first frame; the first
	 *  segment may start before it). False - and nothing added - when it would
	 *  not strictly follow the previous one in both coordinates, or past the
	 *  capacity. */
	bool addSegment(double source, double output);
	int segmentCount() const { return m_count; }

	//! Renders @a frames output frames from @a source (the clip's window, read
	//! the way Sample::render reads it) and advances the cursor.
	void render(const TransientSource& source, SampleFrame* dst, f_cnt_t frames);
	//! The window-relative source frame the last rendered frame read.
	double sourcePosition() const { return m_sourcePosition; }

private:
	//! The gain and source frame of output frame @a position; gain 0 is silence.
	float frameAt(std::int64_t position, double* source);

	WarpStretchMode m_mode = WarpStretchMode::Repeat;
	double m_interval = 1.0;
	double m_sourcePerOutput = 1.0;
	f_cnt_t m_windowFrames = 0;
	std::array<double, MaxSegments> m_source{};
	std::array<double, MaxSegments> m_output{};
	int m_count = 0;
	int m_segment = 0;
	std::int64_t m_position = 0;
	double m_sourcePosition = 0.0;
};

/*! The exact (fractional) clip-relative tick at which play frame @a frame
 *  sounds under @a warp - WarpMarkers::timelineOffsetAt without the integer
 *  truncation, which at ~460 frames per tick would put a segment boundary up
 *  to 10 ms off. With no markers the clip's own linear rate @a framesPerTick
 *  from @a clipIn. Pure and allocation-free (audio thread). */
LMMS_EXPORT double transientTicksAt(const WarpMarkers& warp, f_cnt_t frame, f_cnt_t clipIn, double framesPerTick);

} // namespace lmms

#endif // LMMS_TRANSIENT_REPEAT_H
