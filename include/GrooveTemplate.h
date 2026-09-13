/*
 * GrooveTemplate.h - a groove: the timing and velocity feel of a note pattern,
 *                    captured from one clip and re-applied to another
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

#ifndef LMMS_GROOVE_TEMPLATE_H
#define LMMS_GROOVE_TEMPLATE_H

#include <array>
#include <span>

#include <QString>

#include "Note.h"     // NoteVector, and volume.h's MinVolume/MaxVolume through it
#include "TimePos.h"  // DefaultTicksPerBar, tick_t
#include "LmmsTypes.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

/*! One groove step: what the groove does to a note that lands in its slot.
 *
 *  `timing` is a signed offset in TICKS, bounded by half the slot width
 *  (see GrooveTemplate::setStep). The bound is what makes a groove a *feel*
 *  rather than a rearrangement: a note is never moved so far that it belongs
 *  to a different slot, so the slot a note is read back into is the slot it
 *  was written into.
 *
 *  `velocity` is the velocity the slot is GIVEN, on the engine's own note
 *  volume (volume_t, 0..200 - see volume.h), or `NoVelocityOpinion` (-1) for a
 *  slot that says nothing about velocity at all. A groove says how loud its
 *  slots are the same way it says where they sit, and applying it REPLACES both
 *  (by the call's strength). Two decisions are load-bearing:
 *
 *    - an absolute target rather than an offset, because an offset would be
 *      added again on every repeat - a groove applied twice would keep getting
 *      louder, where an absolute target makes a second application a no-op;
 *    - the explicit "no opinion", because absolute means a slot with NO note in
 *      the source clip would otherwise say "velocity 0" and silence the notes
 *      that land there. A template with two used slots out of four must not
 *      flatten the other two.
 */
struct GrooveStep
{
	//! The velocity of a slot that says nothing about velocity.
	static constexpr int NoVelocityOpinion = -1;

	tick_t timing = 0;
	int velocity = NoVelocityOpinion;

	bool operator==(const GrooveStep& other) const
	{
		return timing == other.timing && velocity == other.velocity;
	}
	bool operator!=(const GrooveStep& other) const { return !(*this == other); }

	//! True for the step that changes nothing: no timing shift and no velocity
	//! opinion. This is the step of a slot the source clip never played.
	bool neutral() const { return timing == 0 && velocity == NoVelocityOpinion; }
};

/*! A named groove: a cycle length, a slot width, and one step per slot.
 *
 *  THE MODEL. The template is `slotCount = lengthTicks / stepTicks` steps, each
 *  holding the timing and velocity shift for one grid slot, and it CYCLES: a
 *  note anywhere on the timeline is governed by the step of the slot it falls
 *  in, modulo the cycle. So a four-slot template is a one-16th-note feel that
 *  repeats across the whole clip, which is what a groove pool is for.
 *
 *  TWO OPERATIONS, both in this header, and both pure functions over a note
 *  list (no Engine, no track, no GUI), so they are testable headlessly:
 *
 *    - extract(): read the feel OUT of a clip. Each slot's timing step is the
 *      MEAN signed deviation of the notes that fell in it from that slot's grid
 *      position, and its velocity step is the slot's MEAN velocity. A slot the
 *      clip never played gets the neutral step - no shift and NO velocity
 *      opinion - rather than a velocity of 0.
 *    - apply(): write the feel INTO a clip, with a strength, by snapping each
 *      note to its slot and giving it that slot's velocity (see the free
 *      function below - the target and the interpolation are documented there).
 *
 *  BOUNDS. Everything is bounded so a template can never be a memory or a
 *  transaction-record problem: at most MaxSteps slots, a slot width in
 *  [MinStepTicks, MaxStepTicks], a name of at most MaxNameLength characters,
 *  and a fixed-capacity step array (no step operation allocates). The pool's
 *  own bound (how many templates a project may hold) lives in GroovePool.h.
 *
 *  WHAT THIS IS NOT. Nothing here runs on the audio thread: a groove is an
 *  editing operation over a clip's note list, it is applied once, and the notes
 *  it moved are ordinary notes afterwards. Playback is unchanged and no
 *  realtime rule (docs/CONVENTIONS.md I8) is touched, because these functions
 *  are never called from a render path.
 */
class GrooveTemplate
{
public:
	//! The longest name the engine accepts (a groove name is a project-file
	//! attribute and a command argument, not a label with no limit).
	static constexpr int MaxNameLength = 64;
	//! The most slots one template may have: 64 sixteenth-notes is four 4/4
	//! bars of feel, past which a "template" is a transcription.
	static constexpr int MaxSteps = 64;
	//! The finest slot: one engine tick.
	static constexpr tick_t MinStepTicks = 1;
	//! The coarsest slot: one 4/4 bar. A template whose slot is longer than a
	//! bar is not a groove, it is a note list.
	static constexpr tick_t MaxStepTicks = DefaultTicksPerBar;

	//! An empty template: no name, no slots, valid() false.
	GrooveTemplate() = default;
	/*! A neutral template of \a lengthTicks over slots of \a stepTicks.
	 *  The values are NOT validated here - the caller proves the geometry with
	 *  isWritable() first, exactly as WarpMarkers::set proves a marker set
	 *  before a clip accepts it, so a refused template is never constructed. */
	GrooveTemplate(const QString& name, tick_t lengthTicks, tick_t stepTicks);

	/*! Whether a name/length/step triple can be a template at all:
	 *  a non-empty name of at most MaxNameLength characters, a slot width in
	 *  [MinStepTicks, MaxStepTicks], and a positive length that is a whole
	 *  number of slots, at most MaxSteps of them. */
	static bool isWritable(const QString& name, tick_t lengthTicks, tick_t stepTicks);

	//! The stored form of \a name: trimmed, and bounded to MaxNameLength.
	static QString normalisedName(const QString& name);

	const QString& name() const { return m_name; }
	//! The cycle length in ticks; 0 for an empty template.
	tick_t lengthTicks() const { return m_lengthTicks; }
	//! The slot width in ticks; 0 for an empty template.
	tick_t stepTicks() const { return m_stepTicks; }
	//! lengthTicks() / stepTicks(); 0 for an empty template.
	int slotCount() const { return m_slotCount; }
	//! True when the template has no slots at all.
	bool empty() const { return m_slotCount == 0; }
	//! True when the geometry is in range (an empty template is NOT valid).
	bool valid() const;
	//! True when every step is neutral: a template that changes nothing.
	bool neutral() const;

	//! The step of \a slot, or a neutral step outside 0..slotCount()-1.
	GrooveStep step(int slot) const;
	/*! Writes one step. False - and nothing changes - when \a slot is out of
	 *  range, when |timing| exceeds half the slot width, or when \a velocity is
	 *  neither GrooveStep::NoVelocityOpinion nor inside the engine's own volume
	 *  range (a velocity is a velocity, so a smaller one is not a negative one). */
	bool setStep(int slot, const GrooveStep& value);

	//! The step that governs a note at \a pos: the slot \a pos falls in, modulo
	//! the template's cycle.
	GrooveStep stepForPosition(tick_t pos) const noexcept;
	//! The start of the grid slot \a pos falls in, in absolute ticks (the slot's
	//! own cycle's slot, so the value is on the timeline, not inside the cycle).
	tick_t gridTickFor(tick_t pos) const noexcept;
	//! Where the groove puts a note currently at \a pos, at full strength.
	tick_t targetTickFor(tick_t pos) const noexcept;
	/*! The velocity the groove gives a note at \a pos, at full strength, or
	 *  GrooveStep::NoVelocityOpinion when the slot it falls in has none - which
	 *  is the answer for a slot the source clip never played. */
	int targetVelocityFor(tick_t pos) const noexcept;

	//! This template as a <groove> element appended to \a parent.
	void saveXml(QDomDocument& doc, QDomElement& parent) const;
	/*! Reads a <groove> element. This template is RESET first, so an element
	 *  that names no groove - or names a malformed one - leaves an empty
	 *  template rather than a half-restored one. That reset is load-bearing:
	 *  the pool is written to the project file only when it is non-empty, so
	 *  the state a restore has to be able to return to is "no groove at all". */
	bool loadXml(const QDomElement& element);

private:
	QString m_name;
	tick_t m_lengthTicks = 0;
	tick_t m_stepTicks = 0;
	int m_slotCount = 0;
	std::array<GrooveStep, MaxSteps> m_steps{};
};

/*! Reads the feel out of \a notes into \a out.
 *
 *  Every note is assigned to the grid slot it is nearest to, and the slot's
 *  step becomes the MEAN of its notes' signed deviations and the mean of their
 *  velocities (rounded to whole ticks and whole velocity units). A slot nothing
 *  landed in gets the NEUTRAL step - no timing shift and no velocity opinion -
 *  which is the honest reading: the groove says nothing about a position the
 *  clip did not play, and applying it there must change neither.
 *
 *  False - with \a out untouched - when the triple is not writable, or when
 *  \a notes is empty (there is no feel in an empty clip, and writing a
 *  neutral template for one would be an edit that records nothing).
 *
 *  \a notesRead receives how many notes were read.
 */
bool extractGroove(const NoteVector& notes, const QString& name, tick_t lengthTicks,
	tick_t stepTicks, GrooveTemplate* out, int* notesRead);

/*! Writes \a groove into \a notes, with a strength.
 *
 *  For each note, its slot is found from its CURRENT position and both of the
 *  slot's targets are approached by `strength`:
 *
 *      position:  pos + round(strength * (slotGrid + step.timing - pos))
 *      velocity:  vel + round(strength * (step.velocity            - vel))
 *                 ... and NOT AT ALL for a slot whose velocity is "no opinion"
 *
 *  At strength 1 the note lands exactly on the groove - the slot's tick AND the
 *  slot's velocity - so a second application is a no-op: the target of a note
 *  already at its target is itself, because a step's timing is bounded by half
 *  the slot width and its velocity is an absolute target rather than an offset.
 *  At strength 0 nothing moves; in between it is a partial feel, which is what a
 *  strength control is for.
 *
 *  \a strength is clamped to [0, 1]. Returns the number of notes whose
 *  position or velocity actually changed.
 */
int applyGroove(const NoteVector& notes, const GrooveTemplate& groove, float strength);

//! Clamps \a velocity to the engine's own note-volume range.
int clampNoteVelocity(int velocity) noexcept;

} // namespace lmms

#endif // LMMS_GROOVE_TEMPLATE_H
