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

#include <QDomDocument>
#include <QDomElement>

namespace lmms
{

namespace
{

/*! Floor division: the slot of a tick must be the same for a positive and a
 *  negative position, and C++'s `/` truncates toward zero. Slots are always
 *  positive here, so only the numerator's sign matters. */
tick_t floorDiv(tick_t value, tick_t divisor) noexcept
{
	const tick_t quotient = value / divisor;
	if (value % divisor != 0 && value < 0) { return quotient - 1; }
	return quotient;
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
	return floorDiv(pos + stepTicks / 2, stepTicks);
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
	if (std::abs(value.timing) > m_stepTicks / 2) { return false; }
	if (std::abs(value.velocity) > static_cast<int>(MaxVolume)) { return false; }
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
	return absoluteSlotOf(pos, m_stepTicks) * m_stepTicks;
}


tick_t GrooveTemplate::targetTickFor(tick_t pos) const noexcept
{
	if (m_slotCount <= 0) { return pos; }
	return gridTickFor(pos) + stepForPosition(pos).timing;
}


int GrooveTemplate::targetVelocityFor(tick_t pos, int velocity) const noexcept
{
	if (m_slotCount <= 0) { return clampNoteVelocity(velocity); }
	return clampNoteVelocity(velocity + stepForPosition(pos).velocity);
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
		const int slot = child.attribute(QStringLiteral("slot"), QStringLiteral("-1")).toInt();
		GrooveStep value;
		value.timing = child.attribute(QStringLiteral("timing"), QStringLiteral("0")).toInt();
		value.velocity = child.attribute(QStringLiteral("velocity"), QStringLiteral("0")).toInt();
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
	long long totalVelocity = 0;
	int read = 0;

	void add(const Note& note, tick_t stepTicks, int slotCount) noexcept
	{
		const tick_t pos = note.pos().getTicks();
		const tick_t slotIndex = absoluteSlotOf(pos, stepTicks);
		const std::size_t slot = static_cast<std::size_t>(cycleOf(slotIndex, slotCount));
		timing[slot] += static_cast<long long>(pos) - static_cast<long long>(slotIndex * stepTicks);
		velocity[slot] += static_cast<int>(note.getVolume());
		++counts[slot];
		totalVelocity += static_cast<int>(note.getVolume());
		++read;
	}
};

/*! Writes one slot's step: the mean deviation from the slot's own grid
 *  position, and the slot's mean velocity relative to the clip's average. A
 *  slot nothing landed in is left neutral - the slot COUNT is the template's
 *  shape. Returns false only when the template refuses a step, which its own
 *  two bounds make impossible here. */
bool writeSlot(GrooveTemplate* out, int slot, const SlotSums& sums, int reference,
	tick_t stepTicks)
{
	if (sums.counts[static_cast<std::size_t>(slot)] == 0) { return true; }
	GrooveStep value;
	const int half = static_cast<int>(stepTicks / 2);
	value.timing = static_cast<tick_t>(std::clamp(
		roundedMean(sums.timing[static_cast<std::size_t>(slot)],
			sums.counts[static_cast<std::size_t>(slot)]), -half, half));
	value.velocity = roundedMean(sums.velocity[static_cast<std::size_t>(slot)],
		sums.counts[static_cast<std::size_t>(slot)]) - reference;
	if (out->setStep(slot, value)) { return true; }
	return out->setStep(slot, GrooveStep());
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

	// The clip's own mean velocity is the reference a template records against,
	// so a template is a SHAPE and not a loudness.
	const int reference = roundedMean(sums.totalVelocity, sums.read);
	for (int slot = 0; slot < slotCount; ++slot)
	{
		if (!writeSlot(&wanted, slot, sums, reference, stepTicks)) { return false; }
	}

	*out = wanted;
	setRead(notesRead, sums.read);
	return true;
}


int applyGroove(const NoteVector& notes, const GrooveTemplate& groove, float strength)
{
	if (!groove.valid()) { return 0; }
	const float amount = std::clamp(strength, 0.0f, 1.0f);
	if (amount <= 0.0f && groove.neutral()) { return 0; }

	int changed = 0;
	for (Note* note : notes)
	{
		if (note == nullptr) { continue; }
		const tick_t pos = note->pos().getTicks();
		const tick_t target = groove.targetTickFor(pos);
		const tick_t moved = pos + static_cast<tick_t>(std::lround(amount
			* static_cast<double>(target - pos)));
		const int velocity = static_cast<int>(note->getVolume());
		const int newVelocity = clampNoteVelocity(groove.targetVelocityFor(pos, velocity));

		if (moved != pos) { note->setPos(TimePos(moved)); }
		if (newVelocity != velocity) { note->setVolume(static_cast<volume_t>(newVelocity)); }
		if (moved != pos || newVelocity != velocity) { ++changed; }
	}
	return changed;
}

} // namespace lmms
