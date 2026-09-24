/*
 * SessionTuning.cpp - the ONE session-wide tuning table every instrument
 *                      reads at render (board card #712)
 *
 * See include/SessionTuning.h for the contract (double-buffer flip, realtime
 * rule, bounds, and what is deliberately NOT re-implemented: Interval/Scale/
 * Keymap stay the Microtuner's - this file constructs a table FROM them).
 * The .scl/.kbm parsers are SessionTuningScala.cpp and the MTS-ESP
 * publication half is SessionTuningMtsEsp.cpp - both split for the
 * file-length ratchet, both members of this class.
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
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "SessionTuning.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <QJsonArray>
#include <QString>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Keymap.h"
#include "Scale.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

namespace
{

//! The reference-note arithmetic of one table entry, in the pieces
//! Microtuner::keyToFreq() computes inline: the base key's degree/octave
//! (the reference, since the session table always uses the keymap's own -
//! the per-track base-note override is a Microtuner feature and stays one).
struct Reference
{
	int keymapOctave = 0;
	int scaleDegree = 0;
	double octaveRatio = 2.0;
	float middleFreq = 440.f;
};

bool referenceOf(const Keymap& keymap, const std::vector<Interval>& intervals,
	int octaveDegree, Reference* out, QString* error)
{
	const int baseNote = keymap.getBaseKey();
	const int baseDegree = octaveDegree == 0 ? 0 : keymap.getDegree(baseNote);
	if (octaveDegree > 0 && baseDegree == -1)
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("the base key %1 is not mapped by the keymap, so the "
				"table has no reference frequency (the dialog warns the same way)").arg(baseNote);
		}
		return false;
	}
	int baseRem = octaveDegree == 0 ? 0 : baseDegree % octaveDegree;
	if (baseRem < 0) { baseRem += octaveDegree; }
	out->octaveRatio = octaveDegree == 0 ? 2.0 : intervals[octaveDegree].getRatio();
	// Same expression, same types, same order as Microtuner::keyToFreq() -
	// the parity test (SessionTuningTest) holds the two to the same float.
	out->middleFreq = (keymap.getBaseFreq()
			/ std::pow(out->octaveRatio,
				(octaveDegree == 0 ? 0 : baseDegree / octaveDegree) + keymap.getOctave(baseNote)))
		/ intervals[baseRem].getRatio();
	out->scaleDegree = baseRem;
	out->keymapOctave = keymap.getOctave(baseNote);
	return true;
}

//! The cap's content rule for ONE snapshot entry: 0 is the unmapped marker,
//! everything else must be a finite non-negative number. Decided here so
//! setState() is a loop and a commit, not a boolean expression tower.
bool entryIsFrequency(const QJsonValue& value)
{
	if (!value.isDouble()) { return false; }
	const double frequency = value.toDouble(-1.0);
	return std::isfinite(frequency) && frequency >= 0.0;
}

} // namespace

SessionTuning::SessionTuning()
	: m_keymap(std::make_shared<Keymap>())
{
	// Default content is 12-TET and the table starts INACTIVE, so the first
	// render after startup takes exactly the branches it took before #712.
	for (Table& table : m_tables) { table = twelveTetTable(); }
}

SessionTuning* SessionTuning::instance()
{
	// Deliberately never destroyed (the MidiClock::instance() reasoning): the
	// audio thread may be inside noteToFreq() when static destruction runs.
	static SessionTuning* const instance = new SessionTuning();
	return instance;
}

SessionTuning::Table SessionTuning::twelveTetTable()
{
	Table table;
	table.active = false;
	for (int key = 0; key < TableSize; ++key)
	{
		table.frequencies[key] = static_cast<double>(DefaultBaseFreq)
			* std::pow(2.0, (key - DefaultBaseKey) / 12.0);
	}
	return table;
}

bool SessionTuning::buildTable(const Scale* scale, const Keymap& keymap,
	Table* out, QString* error)
{
	*out = Table{};
	if (scale == nullptr)
	{
		// No scale yet: 12-TET content over the (inactive or active) table.
		*out = twelveTetTable();
		return true;
	}

	const std::vector<Interval>& intervals = scale->getIntervals();
	const int octaveDegree = static_cast<int>(intervals.size()) - 1;
	if (octaveDegree < 0)
	{
		if (error != nullptr) { *error = QStringLiteral("the scale has no intervals"); }
		return false;
	}
	// The reference note must be mapped, or no key has a frequency - refused
	// (the dialog only warns) because an un-referenceable session table would
	// be SILENT, not merely unusable. Validated before anything is written.
	Reference reference;
	if (!referenceOf(keymap, intervals, octaveDegree, &reference, error)) { return false; }

	for (int key = 0; key < TableSize; ++key)
	{
		if (octaveDegree == 0)
		{
			// A single 1/1 interval: constant base frequency, exactly like
			// Microtuner::keyToFreq()'s octaveDegree == 0 branch.
			out->frequencies[key] = keymap.getBaseFreq();
			continue;
		}
		const int keymapDegree = keymap.getDegree(key);
		if (keymapDegree == -1)
		{
			out->frequencies[key] = 0.0; // unmapped: the note stays silent
			continue;
		}
		const int scaleOctave = keymapDegree / octaveDegree;
		int rem = keymapDegree % octaveDegree;
		if (rem < 0) { rem += octaveDegree; }
		out->frequencies[key] = reference.middleFreq * intervals[rem].getRatio()
			* std::pow(reference.octaveRatio, keymap.getOctave(key) + scaleOctave);
	}
	// (active is not buildTable's to decide - commit() sets it.)
	return true;
}

void SessionTuning::commit(const Table& next, bool nextActive, const QString& source)
{
	// Claim a non-published, reader-free slot before writing it. With three
	// slots, one can remain published while another is held by a reader and
	// the third is available for this commit. Concurrent writers retry if a
	// competing commit publishes first, preserving publication order.
	for (;;)
	{
		const int front = m_front.load(std::memory_order_acquire);
		int back = (front + 1) % TableCount;
		bool claimed = false;
		for (int attempt = 0; attempt < TableCount - 1; ++attempt)
		{
			unsigned expected = 0u;
			if (back != front && m_slotStates[back].compare_exchange_weak(
				expected, WriterBit, std::memory_order_acquire,
				std::memory_order_relaxed))
			{
				claimed = true;
				break;
			}
			back = (back + 1) % TableCount;
		}
		if (!claimed) { continue; }

		if (m_front.load(std::memory_order_acquire) != front)
		{
			m_slotStates[back].fetch_and(~WriterBit, std::memory_order_release);
			continue;
		}
		Table& target = m_tables[back];
		target = next;
		target.active = nextActive;
		int expectedFront = front;
		if (m_front.compare_exchange_strong(expectedFront, back,
			std::memory_order_release, std::memory_order_acquire))
		{
			m_source = source.left(MaxSourceLength);
			m_slotStates[back].fetch_and(~WriterBit, std::memory_order_release);
			break;
		}
		m_slotStates[back].fetch_and(~WriterBit, std::memory_order_release);
	}

	retuneSoundingNotes();
	publishToMtsEsp();
}

void SessionTuning::retuneSoundingNotes()
{
	Song* song = Engine::getSong();
	if (song == nullptr) { return; }
	// The engine's own change lock, exactly as InstrumentTrack::updateBaseNote()
	// takes it: the control thread walks m_processHandles while the render
	// thread may still be inserting into it.
	Engine::audioEngine()->requestChangeInModel();
	for (Track* track : song->tracks())
	{
		if (auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(track))
		{
			// Each marked handle recomputes from THIS table on its next play()
			// (NotePlayHandle::play sees m_frequencyNeedsUpdate), so a table
			// change retunes notes already sounding - the card's whole point.
			instrumentTrack->retunePlayingNotes();
		}
	}
	Engine::audioEngine()->doneChangeInModel();
}

bool SessionTuning::setState(const QJsonObject& state, QString* error)
{
	const QJsonArray frequencies = state.value(QStringLiteral("frequencies")).toArray();
	if (frequencies.size() != TableSize)
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("the snapshot carries %1 frequencies; the table is "
				"%2 (the stated cap) - nothing was written")
				.arg(frequencies.size()).arg(TableSize);
		}
		return false;
	}
	Table next;
	for (int key = 0; key < TableSize; ++key)
	{
		const QJsonValue value = frequencies.at(key);
		// 0 is a legal entry: it is the keymap's "unmapped" marker. Anything
		// negative, non-numeric or non-finite is refused WHOLESALE - the cap
		// contract says a snapshot either replays exactly or writes nothing.
		if (!entryIsFrequency(value))
		{
			if (error != nullptr)
			{
				*error = QStringLiteral("frequency %1 is not a finite number >= 0 - "
					"nothing was written").arg(key);
			}
			return false;
		}
		next.frequencies[key] = value.toDouble();
	}
	const QString source = state.contains(QStringLiteral("source"))
		? state.value(QStringLiteral("source")).toString()
		: m_source;
	if (source.size() > MaxSourceLength)
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("the source string is %1 characters; the cap is %2 - "
				"nothing was written").arg(source.size()).arg(MaxSourceLength);
		}
		return false;
	}
	const bool active = state.contains(QStringLiteral("active"))
		? state.value(QStringLiteral("active")).toBool(true)
		: true;
	commit(next, active, source);
	return true;
}

bool SessionTuning::setNoteFrequency(int key, double frequency, const QString& source,
	QString* error)
{
	if (key < 0 || key >= TableSize)
	{
		if (error != nullptr) { *error = QStringLiteral("note %1 is outside 0..%2").arg(key).arg(TableSize - 1); }
		return false;
	}
	if (!std::isfinite(frequency) || frequency <= 0.0)
	{
		if (error != nullptr) { *error = QStringLiteral("frequency %1 must be a finite positive Hz value").arg(frequency); }
		return false;
	}
	Table next = m_tables[m_front.load(std::memory_order_relaxed)];
	next.frequencies[key] = frequency;
	commit(next, true, source.isEmpty() ? QStringLiteral("manual (mts.set_note)") : source);
	return true;
}

void SessionTuning::reset()
{
	commit(twelveTetTable(), false, QStringLiteral("12-TET (session table inactive)"));
}

void SessionTuning::feedScale(std::shared_ptr<const Scale> scale, const QString& source)
{
	Table next;
	QString buildError;
	if (!buildTable(scale.get(), *m_keymap, &next, &buildError))
	{
		// The dialog already validated what it applied; a table that cannot be
		// built (base key unmapped) keeps the one it had rather than going
		// silent behind the user's back.
		qWarning("SessionTuning: refusing the fed scale: %s", qPrintable(buildError));
		return;
	}
	m_scale = std::move(scale);
	commit(next, isActive(), source); // content only - active unchanged
}

void SessionTuning::feedKeymap(std::shared_ptr<const Keymap> keymap, const QString& source)
{
	Table next;
	QString buildError;
	if (!buildTable(m_scale.get(), *keymap, &next, &buildError))
	{
		qWarning("SessionTuning: refusing the fed keymap: %s", qPrintable(buildError));
		return;
	}
	m_keymap = std::move(keymap);
	commit(next, isActive(), source);
}

QJsonObject SessionTuning::stateJson() const
{
	const Table& table = m_tables[m_front.load(std::memory_order_acquire)];
	QJsonArray frequencies;
	for (int key = 0; key < TableSize; ++key)
	{
		frequencies.append(table.frequencies[key]);
	}
	QJsonObject state;
	state.insert(QStringLiteral("active"), table.active);
	state.insert(QStringLiteral("source"), m_source);
	state.insert(QStringLiteral("frequencies"), frequencies);
	return state;
}

} // namespace lmms
