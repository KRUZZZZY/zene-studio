/*
 * GrooveTemplate.cpp - the groove value type, its XML form, and the extract /
 *                      apply arithmetic over a note list
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

#include "GrooveTemplate.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "NoteRandom.h"

#include <QDomDocument>
#include <QDomElement>

namespace lmms
{

namespace
{

/*! Floor division: the slot of a tick must be the same for a positive and a
 *  negative position, and C++'s `/` truncates toward zero. Slots are always
 *  positive here, so only the numerator's sign matters. \a value is wide
 *  because a position plus half a slot does not fit a signed tick at the very
 *  end of the timeline. */
long long floorDiv(long long value, tick_t divisor) noexcept
{
	const long long quotient = value / divisor;
	if (value % divisor != 0 && value < 0) { return quotient - 1; }
	return quotient;
}

//! \a value narrowed back to a tick, clamped rather than wrapped: an offset
//! that has run past the end of the timeline is the last tick, never a negative.
tick_t clampedTick(long long value) noexcept
{
	constexpr long long lowest = static_cast<long long>(std::numeric_limits<tick_t>::min());
	constexpr long long highest = static_cast<long long>(std::numeric_limits<tick_t>::max());
	if (value > highest) { return static_cast<tick_t>(highest); }
	if (value < lowest) { return static_cast<tick_t>(lowest); }
	return static_cast<tick_t>(value);
}

//! The mean of \a sum over \a count, rounded to the nearest whole unit.
int roundedMean(long long sum, int count) noexcept
{
	if (count <= 0) { return 0; }
	return static_cast<int>(std::lround(static_cast<double>(sum) / static_cast<double>(count)));
}

/*! The slot \a pos falls in on the absolute timeline: the nearest multiple of
 *  the slot width, i.e. a note is assigned to the slot it is closest to. */
tick_t absoluteSlotOf(tick_t pos, tick_t stepTicks) noexcept
{
	return clampedTick(floorDiv(static_cast<long long>(pos) + stepTicks / 2, stepTicks));
}

//! \a slot taken modulo the template's cycle, never negative.
int cycleOf(tick_t slot, int slotCount) noexcept
{
	const tick_t remainder = slot % slotCount;
	return static_cast<int>(remainder < 0 ? remainder + slotCount : remainder);
}

const QString kGrooveElement = QStringLiteral("groove");
const QString kStepElement = QStringLiteral("step");

} // namespace


GrooveTemplate::GrooveTemplate(const QString& name, tick_t lengthTicks, tick_t stepTicks) :
	m_name(normalisedName(name)),
	m_lengthTicks(lengthTicks),
	m_stepTicks(stepTicks)
{
	if (stepTicks > 0 && lengthTicks > 0 && lengthTicks % stepTicks == 0
		&& lengthTicks / stepTicks <= MaxSteps)
	{
		m_slotCount = static_cast<int>(lengthTicks / stepTicks);
	}
}


bool GrooveTemplate::isWritable(const QString& name, tick_t lengthTicks, tick_t stepTicks)
{
	if (normalisedName(name).isEmpty()) { return false; }
	if (stepTicks < MinStepTicks || stepTicks > MaxStepTicks) { return false; }
	if (lengthTicks <= 0 || lengthTicks % stepTicks != 0) { return false; }
	return lengthTicks / stepTicks <= MaxSteps;
}


QString GrooveTemplate::normalisedName(const QString& name)
{
	return name.trimmed().left(MaxNameLength);
}


bool GrooveTemplate::valid() const
{
	return !m_name.isEmpty() && m_slotCount > 0 && m_stepTicks >= MinStepTicks
		&& m_stepTicks <= MaxStepTicks && m_lengthTicks == m_stepTicks * m_slotCount;
}


bool GrooveTemplate::neutral() const
{
	for (const GrooveStep& value : std::span<const GrooveStep>(m_steps.data(),
		static_cast<std::size_t>(m_slotCount)))
	{
		if (!value.neutral()) { return false; }
	}
	return true;
}


GrooveStep GrooveTemplate::step(int slot) const
{
	if (slot < 0 || slot >= m_slotCount) { return GrooveStep(); }
	return m_steps[static_cast<std::size_t>(slot)];
}


bool GrooveTemplate::setStep(int slot, const GrooveStep& value)
{
	if (slot < 0 || slot >= m_slotCount) { return false; }
	// STRICTLY inside half the slot, not merely within it. Exactly half a slot
	// is the BOUNDARY between two slots, and a note sitting on the boundary is
	// read back into the NEXT one (absoluteSlotOf rounds the half up), so a
	// shift of exactly half a slot would carry every note of that slot one slot
	// along - and a second application would carry it one more. A slot one tick
	// is the most a step may reach: a step that shifted a note into the next
	// slot would be a rearrangement, not a feel.
	// Both bounds, not std::abs: |INT_MIN| is not representable, so abs() of a loaded one is UB.
	const tick_t half = m_stepTicks / 2;
	if (half > 0 ? (value.timing <= -half || value.timing >= half) : value.timing != 0)
	{ return false; }
	const bool noOpinion = value.velocity == GrooveStep::NoVelocityOpinion;
	if (!noOpinion && (value.velocity < static_cast<int>(MinVolume)
		|| value.velocity > static_cast<int>(MaxVolume))) { return false; }
	m_steps[static_cast<std::size_t>(slot)] = value;
	return true;
}


GrooveStep GrooveTemplate::stepForPosition(tick_t pos) const noexcept
{
	if (m_slotCount <= 0) { return GrooveStep(); }
	return m_steps[static_cast<std::size_t>(cycleOf(absoluteSlotOf(pos, m_stepTicks), m_slotCount))];
}


tick_t GrooveTemplate::gridTickFor(tick_t pos) const noexcept
{
	if (m_stepTicks <= 0) { return pos; }
	return clampedTick(static_cast<long long>(absoluteSlotOf(pos, m_stepTicks)) * m_stepTicks);
}


tick_t GrooveTemplate::targetTickFor(tick_t pos) const noexcept
{
	if (m_slotCount <= 0) { return pos; }
	const long long grid = gridTickFor(pos);
	return clampedTick(grid + stepForPosition(pos).timing);
}


int GrooveTemplate::targetVelocityFor(tick_t pos) const noexcept
{
	if (m_slotCount <= 0) { return GrooveStep::NoVelocityOpinion; }
	const int wanted = stepForPosition(pos).velocity;
	if (wanted == GrooveStep::NoVelocityOpinion) { return GrooveStep::NoVelocityOpinion; }
	return clampNoteVelocity(wanted);
}


void GrooveTemplate::saveXml(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement grooveElement = doc.createElement(kGrooveElement);
	grooveElement.setAttribute(QStringLiteral("name"), m_name);
	grooveElement.setAttribute(QStringLiteral("length"), static_cast<int>(m_lengthTicks));
	grooveElement.setAttribute(QStringLiteral("step"), static_cast<int>(m_stepTicks));
	grooveElement.setAttribute(QStringLiteral("slots"), m_slotCount);
	for (int slot = 0; slot < m_slotCount; ++slot)
	{
		const GrooveStep& value = m_steps[static_cast<std::size_t>(slot)];
		// A neutral step is still written: the slot COUNT is the template's
		// shape, and a reader that skipped neutral steps could not tell a
		// four-slot template with two silent slots from a two-slot one.
		QDomElement stepElement = doc.createElement(kStepElement);
		stepElement.setAttribute(QStringLiteral("slot"), slot);
		stepElement.setAttribute(QStringLiteral("timing"), static_cast<int>(value.timing));
		stepElement.setAttribute(QStringLiteral("velocity"), value.velocity);
		grooveElement.appendChild(stepElement);
	}
	parent.appendChild(grooveElement);
}


namespace
{

//! A saved step's timing as the loader adopts it. Files saved before the
//! half-slot bound may carry exactly +/-half a slot, which was then the largest
//! accepted shift: it lands one tick inside the slot instead of being dropped, so
//! the saved feel survives. Anything wider was never valid and is returned as is,
//! for setStep() to refuse (the slot stays neutral).
tick_t loadedTiming(tick_t timing, tick_t stepTicks)
{
	const tick_t half = stepTicks / 2;
	if (half > 0 && (timing == half || timing == -half)) { return timing > 0 ? half - 1 : -(half - 1); }
	return timing;
}

} // namespace

bool GrooveTemplate::loadXml(const QDomElement& element)
{
	// RESET FIRST. A restore has to be able to return to "no groove at all",
	// which is the state a project that never used one saves.
	m_name.clear();
	m_lengthTicks = 0;
	m_stepTicks = 0;
	m_slotCount = 0;
	m_steps = {};

	if (element.isNull() || element.tagName() != kGrooveElement) { return false; }

	const tick_t length = element.attribute(QStringLiteral("length"), QStringLiteral("0")).toInt();
	const tick_t stepTicks = element.attribute(QStringLiteral("step"), QStringLiteral("0")).toInt();
	if (!isWritable(element.attribute(QStringLiteral("name")), length, stepTicks)) { return false; }

	m_name = normalisedName(element.attribute(QStringLiteral("name")));
	m_lengthTicks = length;
	m_stepTicks = stepTicks;
	m_slotCount = static_cast<int>(length / stepTicks);

	for (QDomElement child = element.firstChildElement(kStepElement); !child.isNull();
		child = child.nextSiblingElement(kStepElement))
	{
		// A slot that is not a NUMBER names no slot, so it is skipped whole: read
		// as 0 it would quietly overwrite slot zero while the template still
		// loaded. The "-1" default is the missing attribute, which names no slot
		// either - setStep refuses it below, leaving the slot it already holds.
		bool slotRead = false;
		const int slot = child.attribute(QStringLiteral("slot"), QStringLiteral("-1")).toInt(&slotRead);
		if (!slotRead) { continue; }
		GrooveStep value;
		value.timing = loadedTiming(
			child.attribute(QStringLiteral("timing"), QStringLiteral("0")).toInt(), m_stepTicks);
		bool velocityRead = false;
		const int velocity = child.attribute(QStringLiteral("velocity")).toInt(&velocityRead);
		// A missing or unreadable velocity says nothing about velocity; 0 would silence the slot.
		value.velocity = velocityRead ? velocity : GrooveStep::NoVelocityOpinion;
		// A step out of range or out of bounds leaves the slot it names
		// neutral (the step it already holds) rather than failing the whole
		// template: the geometry is what a reader depends on.
		setStep(slot, value);
	}
	return true;
}


int clampNoteVelocity(int velocity) noexcept
{
	return std::clamp(velocity, static_cast<int>(MinVolume), static_cast<int>(MaxVolume));
}


namespace
{

/*! Sets an optional out-parameter. One place, so the two output writes of an
 *  extraction are not two branches inside it (Gate 4 counts them). */
void setRead(int* out, int value)
{
	if (out != nullptr) { *out = value; }
}

/*! The per-slot sums of one extraction. A separate type so the accumulation
 *  loop is one function and the mean-and-write pass is another. */
struct SlotSums
{
	std::array<long long, GrooveTemplate::MaxSteps> timing{};
	std::array<long long, GrooveTemplate::MaxSteps> velocity{};
	std::array<int, GrooveTemplate::MaxSteps> counts{};
	int read = 0;

	void add(const Note& note, tick_t stepTicks, int slotCount) noexcept
	{
		const tick_t pos = note.pos().getTicks();
		const tick_t slotIndex = absoluteSlotOf(pos, stepTicks);
		const std::size_t slot = static_cast<std::size_t>(cycleOf(slotIndex, slotCount));
		timing[slot] += static_cast<long long>(pos)
			- (static_cast<long long>(slotIndex) * stepTicks);
		velocity[slot] += static_cast<int>(note.getVolume());
		++counts[slot];
		++read;
	}
};

/*! Writes one slot's step: the mean deviation from the slot's own grid
 *  position and the slot's mean velocity. A slot nothing landed in is left
 *  NEUTRAL - no shift and no velocity opinion - because the slot count is the
 *  template's shape and an unplayed slot must not silence the notes that land
 *  there in the clip the groove is applied to. Returns false only when the
 *  template refuses a step, which its own two bounds make impossible here. */
bool writeSlot(GrooveTemplate* out, int slot, const SlotSums& sums, tick_t stepTicks)
{
	if (sums.counts[static_cast<std::size_t>(slot)] == 0) { return true; }
	GrooveStep value;
	// The same STRICT half a slot setStep accepts (see it): the clamp stops one
	// tick short of the boundary, because a note reading its own slot is what
	// makes the groove a fixed point. Clamping to the boundary instead would
	// produce a step the template refuses, and a refused step falls back to the
	// neutral one - so an extracted groove whose notes all sit exactly on the
	// boundary would capture NO feel at all.
	const int half = static_cast<int>(stepTicks / 2);
	const int reach = half > 0 ? half - 1 : 0;
	value.timing = static_cast<tick_t>(std::clamp(
		roundedMean(sums.timing[static_cast<std::size_t>(slot)],
			sums.counts[static_cast<std::size_t>(slot)]), -reach, reach));
	value.velocity = roundedMean(sums.velocity[static_cast<std::size_t>(slot)],
		sums.counts[static_cast<std::size_t>(slot)]);
	if (out->setStep(slot, value)) { return true; }
	return out->setStep(slot, GrooveStep());
}

/*! The velocity \a amount of the way from \a velocity towards \a velocityTarget,
 *  clamped to the engine's own bounds. A slot that carries NO velocity opinion
 *  leaves the note exactly as it is: a slot the source clip never played must not
 *  silence the note that lands there. Split out of applyGroove() so that function
 *  stays inside the CCN target. */
int groovedVelocity(int velocity, int velocityTarget, float amount) noexcept
{
	if (velocityTarget == GrooveStep::NoVelocityOpinion) { return velocity; }
	return clampNoteVelocity(velocity + static_cast<int>(std::lround(
		amount * static_cast<double>(velocityTarget - velocity))));
}

} // namespace


bool extractGroove(const NoteVector& notes, const QString& name, tick_t lengthTicks,
	tick_t stepTicks, GrooveTemplate* out, int* notesRead)
{
	setRead(notesRead, 0);
	if (out == nullptr) { return false; }
	if (!GrooveTemplate::isWritable(name, lengthTicks, stepTicks)) { return false; }
	if (notes.empty()) { return false; }

	GrooveTemplate wanted(name, lengthTicks, stepTicks);
	const int slotCount = wanted.slotCount();
	SlotSums sums;
	for (const Note* note : notes)
	{
		if (note == nullptr) { continue; }
		sums.add(*note, stepTicks, slotCount);
	}
	if (sums.read == 0) { return false; }

	for (int slot = 0; slot < slotCount; ++slot)
	{
		if (!writeSlot(&wanted, slot, sums, stepTicks)) { return false; }
	}

	*out = wanted;
	setRead(notesRead, sums.read);
	return true;
}


namespace
{
/*! Salts for the random amount's draws. A DISTINCT salt per axis matters: the
 *  draw is a pure function of the seed and the note's identity, so without one
 *  a groove's random offset and a quantise's humanise would be the SAME number
 *  for the same note - two operations would then add the identical jitter to
 *  one note instead of two independent ones. */
constexpr std::uint32_t GrooveRandomTickSalt = 0x6F1D2C3B;
constexpr std::uint32_t GrooveRandomVelocitySalt = 0x1A5E7C90;
} // namespace


//! applyGroove's random timing: @a moved offset by a seeded draw in +-@a randomTicks,
//! never before tick 0 (a position the engine cannot represent is clamped rather than
//! produced, exactly as quantizeNotes does). @a randomTicks 0 leaves @a moved alone.
//! @a slotTick - NOT the note's own position - is what the draw is made from, and
//! that is what makes the call a FIXED POINT: a note already sitting where the
//! groove put it reads the same slot, rolls the same jitter and stays where it
//! is. It is humanise's rule for the same reason (NoteTransform.cpp, jitterFor).
static tick_t jitteredTick(const Note& note, tick_t slotTick, tick_t moved, tick_t randomTicks,
	std::uint32_t seed)
{
	if (randomTicks <= 0) { return moved; }
	const double unit = static_cast<double>(NoteRandom::rollUnit(seed,
		note.key(), slotTick, note.length().getTicks(), GrooveRandomTickSalt));
	const tick_t jittered = clampedTick(moved + static_cast<tick_t>(std::lround(
		(unit * 2.0 - 1.0) * static_cast<double>(randomTicks))));
	return jittered < 0 ? 0 : jittered;
}

//! applyGroove's random velocity: a seeded draw in +-@a randomVelocity, clamped to the
//! engine's note-volume range. A separate salt, so it is an independent draw.
static int jitteredVolume(const Note& note, tick_t pos, int velocity, int randomVelocity,
	std::uint32_t seed)
{
	if (randomVelocity <= 0) { return velocity; }
	const double unit = static_cast<double>(NoteRandom::rollUnit(seed,
		note.key(), pos, note.length().getTicks(), GrooveRandomVelocitySalt));
	return std::clamp(velocity + static_cast<int>(std::lround(
		(unit * 2.0 - 1.0) * static_cast<double>(randomVelocity))), 0, 200);
}

//! The groove strength as an amount, with a NaN read as 0. std::clamp leaves a
//! NaN as a NaN (every NaN comparison is false), so a NaN amount would slip past
//! applyGroove's early return and reach lround and the tick conversion. Reading
//! it as 0 is the same idiom the quantise's humanise uses for its own strength.
static float grooveAmount(float strength)
{
	return std::isnan(strength) ? 0.0f : std::clamp(strength, 0.0f, 1.0f);
}

int applyGroove(const NoteVector& notes, const GrooveTemplate& groove, float strength,
	tick_t randomTicks, int randomVelocity, std::uint32_t seed)
{
	if (!groove.valid()) { return 0; }
	const float amount = grooveAmount(strength);
	if (amount <= 0.0f && groove.neutral()) { return 0; }

	int changed = 0;
	for (Note* note : notes)
	{
		if (note == nullptr) { continue; }
		const tick_t pos = note->pos().getTicks();
		const tick_t target = groove.targetTickFor(pos);
		// The travel is a wide difference of two ticks that can each be the very
		// last one: in tick_t arithmetic that subtraction overflows.
		const tick_t moved = clampedTick(pos + static_cast<tick_t>(std::lround(amount
			* static_cast<double>(static_cast<long long>(target) - pos))));
		const int velocity = static_cast<int>(note->getVolume());
		const int newVelocity = groovedVelocity(velocity, groove.targetVelocityFor(pos), amount);

		// The RANDOM amount (the groove pool's own control, alongside its
		// timing and velocity amounts). Drawn from the note's identity and the
		// caller's seed, never from hidden state, so the same call on the same
		// notes reproduces the same take - the rule quantise's humanise follows.
		// The timing draw reads the note's grid SLOT rather than its position, so
		// repeating the call at full strength is a no-op instead of a re-roll:
		// the position has moved, the slot it sits in has not.
		const tick_t jittered = jitteredTick(*note, groove.gridTickFor(pos), moved, randomTicks,
			seed);
		// The velocity draw stays on the position: the target is an absolute
		// velocity, so a repeat rolls the jitter again - the documented contract
		// of the surface (groove.quantize's humanise, ControlGrooveCommandsTest).
		const int jitteredVelocity = jitteredVolume(*note, pos, newVelocity, randomVelocity, seed);

		if (jittered != pos) { note->setPos(TimePos(jittered)); }
		if (jitteredVelocity != velocity) { note->setVolume(static_cast<volume_t>(jitteredVelocity)); }
		if (jittered != pos || jitteredVelocity != velocity) { ++changed; }
	}
	return changed;
}

} // namespace lmms
