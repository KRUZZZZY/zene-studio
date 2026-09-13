/*
 * GrooveTestSupport.h - what the groove tests share: the command driver, the
 *                       fixture builders, and the read-backs they assert on.
 *
 * ONE copy, in the repo's own convention for a shared test helper
 * (ReversibilityTestSupport.h, BrowserTestSupport.h, RackTestSupport.h): the
 * arithmetic test (GrooveTemplateTest.cpp), the surface test
 * (ControlGrooveCommandsTest.cpp) and the end-to-end socket test
 * (tests/control-groove-commands.py, which is Python and shares none of this)
 * all need the same "position and velocity of every note" reading, and two
 * copies of it would be two chances to assert on the wrong field.
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
 */

#ifndef LMMS_TESTS_GROOVE_TEST_SUPPORT_H
#define LMMS_TESTS_GROOVE_TEST_SUPPORT_H

#include <memory>

#include <QDomDocument>
#include <QDomNodeList>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "GroovePool.h"
#include "GrooveTemplate.h"
#include "Note.h"
#include "Song.h"

namespace groovetest
{

//! The command group's ids, in the order the contract lists them.
inline QStringList grooveIds()
{
	return {QStringLiteral("groove.list"), QStringLiteral("groove.extract"),
		QStringLiteral("groove.set"), QStringLiteral("groove.apply"),
		QStringLiteral("groove.quantize"), QStringLiteral("groove.remove"),
		QStringLiteral("groove.rename")};
}

//! Invoke a command through the registry, exactly as the socket does.
inline lmms::ControlResult run(const QString& id,
	const QJsonObject& args = QJsonObject())
{
	return lmms::ControlRegistry::instance()->invoke(id, args);
}

inline lmms::GroovePool& projectPool()
{
	return lmms::Engine::getSong()->groovePool();
}

/*! A fresh track and one empty clip on it, created through the product's own
 *  commands, so no fixture file is needed and the clip id comes back from the
 *  command rather than from a position a test guesses. */
inline QString makeClip(const QString& type = QStringLiteral("instrument"))
{
	const lmms::ControlResult track =
		run(QStringLiteral("track.add"), {{QStringLiteral("type"), type}});
	if (!track.ok) { return QString(); }
	const lmms::ControlResult clip = run(QStringLiteral("clip.add"),
		{{QStringLiteral("track"), track.result.value(QStringLiteral("track"))},
			{QStringLiteral("position"), 0}});
	if (!clip.ok) { return QString(); }
	return clip.result.value(QStringLiteral("clip")).toString();
}

inline bool addNote(const QString& clip, int key, int position, int velocity)
{
	return run(QStringLiteral("note.add"),
		{{QStringLiteral("clip"), clip}, {QStringLiteral("key"), key},
			{QStringLiteral("position"), position}, {QStringLiteral("length"), 12},
			{QStringLiteral("velocity"), velocity}}).ok;
}

/*! One note as a whole value, so a take can be compared in one assertion and a
 *  failure prints the take rather than two addresses. */
struct NoteAt
{
	int position = 0;
	int velocity = 0;

	bool operator==(const NoteAt& other) const
	{
		return position == other.position && velocity == other.velocity;
	}
};

inline QString describe(const QVector<NoteAt>& notes)
{
	QStringList parts;
	for (const NoteAt& note : notes)
	{
		parts << QStringLiteral("%1@%2").arg(note.position).arg(note.velocity);
	}
	return QStringLiteral("[") + parts.join(QStringLiteral(" ")) + QStringLiteral("]");
}

//! The position and velocity of every note of a clip, in the clip's own order.
inline QVector<NoteAt> takeOf(const QString& clip)
{
	QVector<NoteAt> out;
	const lmms::ControlResult state =
		run(QStringLiteral("roll.get_state"), {{QStringLiteral("clip"), clip}});
	for (const QJsonValue& value : state.result.value(QStringLiteral("notes")).toArray())
	{
		const QJsonObject note = value.toObject();
		out.append(NoteAt{note.value(QStringLiteral("position")).toInt(),
			note.value(QStringLiteral("velocity")).toInt()});
	}
	return out;
}

//! The same, for a note list held in memory (the arithmetic tests).
inline QVector<NoteAt> takeOf(const lmms::NoteVector& notes)
{
	QVector<NoteAt> out;
	for (const lmms::Note* note : notes)
	{
		out.append(NoteAt{static_cast<int>(note->pos().getTicks()),
			static_cast<int>(note->getVolume())});
	}
	return out;
}

/*! One groove's steps as the wire reports them: the timing offset in ticks and
 *  the velocity offset, so the extraction rule and the stored template are
 *  both asserted rather than assumed. */
inline QVector<NoteAt> stepsOf(const QString& name)
{
	QVector<NoteAt> out;
	const lmms::ControlResult listed =
		run(QStringLiteral("groove.list"), {{QStringLiteral("name"), name}});
	for (const QJsonValue& value : listed.result.value(QStringLiteral("groove"))
		.toObject().value(QStringLiteral("steps")).toArray())
	{
		const QJsonObject step = value.toObject();
		out.append(NoteAt{step.value(QStringLiteral("timing")).toInt(),
			step.value(QStringLiteral("velocity")).toInt()});
	}
	return out;
}

inline QStringList namesInPool()
{
	QStringList out;
	for (const QJsonValue& value : run(QStringLiteral("groove.list"))
		.result.value(QStringLiteral("templates")).toArray())
	{
		out << value.toObject().value(QStringLiteral("name")).toString();
	}
	return out;
}

/*! The contract table's row for \a id, or nullptr - the SPEC A16 classification
 *  both groove tests hold the group to. */
inline const lmms::control::ReversibilityEntry* contractRow(const QString& id)
{
	return lmms::control::ReversibilityTable::instance().lookup(id);
}

/*! The QUANTISE fixture: four notes at 5 / 17 / 29 / 41 with velocities
 *  90 / 130 / 110 / 70, quantised onto the grid first, on a clip of its own.
 *
 *  It exists because a humanised take has to be reproduced from an IDENTICAL
 *  starting state, velocities included: a re-quantise puts the POSITIONS back on
 *  the grid but nothing puts a jittered velocity back, so "the same call on the
 *  same notes" is asserted from a second clip built this way rather than from a
 *  reset. */
inline QString onGridClip()
{
	const QString clip = makeClip(QStringLiteral("instrument"));
	if (clip.isEmpty()) { return clip; }
	if (!addNote(clip, 60, 5, 90) || !addNote(clip, 62, 17, 130)
		|| !addNote(clip, 64, 29, 110) || !addNote(clip, 65, 41, 70))
	{
		return QString();
	}
	const lmms::ControlResult quantised = run(QStringLiteral("groove.quantize"),
		{{QStringLiteral("clip"), clip}, {QStringLiteral("grid"), 12},
			{QStringLiteral("strength"), 1.0}});
	return quantised.ok ? clip : QString();
}

/*! Writes \a path's project text to \a outPath with its <groove-pool> element
 *  removed: the negative control for the reset-on-absence rule, because the
 *  element is written only when the pool is non-empty and therefore has to be
 *  able to restore "no pool at all". False when the file, the element or the
 *  rewrite is not what this control needs. */
inline bool rewriteWithoutGroovePool(const QString& path, const QString& outPath)
{
	QFile in(path);
	if (!in.open(QIODevice::ReadOnly)) { return false; }
	const QString text = QString::fromUtf8(in.readAll());
	in.close();
	QDomDocument document;
	if (!document.setContent(text)) { return false; }
	const QDomNodeList pools = document.elementsByTagName(QStringLiteral("groove-pool"));
	if (pools.size() != 1) { return false; }
	pools.at(0).parentNode().removeChild(pools.at(0));
	QFile out(outPath);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	out.write(document.toString().toUtf8());
	out.close();
	return true;
}

/*! The "feel" fixture as ARGUMENTS: four 1/16 slots at 12 ticks, taken from a
 *  clip whose notes sit at 9 / 26 / 34 / 51 with velocities 120 / 80 / 100 / 100.
 *  A slot's timing step is its notes' mean deviation (3 / -3 / +2 / -2) and its
 *  velocity step is their mean velocity, so the slots read 100 / 120 / 80 / 100. */
inline QJsonArray feelSteps()
{
	return QJsonArray{
		QJsonObject{{QStringLiteral("slot"), 0}, {QStringLiteral("timing"), 3},
			{QStringLiteral("velocity"), 100}},
		QJsonObject{{QStringLiteral("slot"), 1}, {QStringLiteral("timing"), -3},
			{QStringLiteral("velocity"), 120}},
		QJsonObject{{QStringLiteral("slot"), 2}, {QStringLiteral("timing"), 2},
			{QStringLiteral("velocity"), 80}},
		QJsonObject{{QStringLiteral("slot"), 3}, {QStringLiteral("timing"), -2},
			{QStringLiteral("velocity"), 100}},
	};
}

//! The fixture the arithmetic and the surface both start from: notes at
//! 9 / 26 / 34 / 51 ticks with velocities 120 / 80 / 100 / 100.
inline void addFeelNotes(const QString& clip)
{
	addNote(clip, 60, 9, 120);
	addNote(clip, 62, 26, 80);
	addNote(clip, 64, 34, 100);
	addNote(clip, 65, 51, 100);
}

/*! Owns the notes the arithmetic tests manipulate and keeps the view in sync
 *  (the helper tests/src/core/NoteTransformTest.cpp uses for the same reason). */
class NoteTable
{
public:
	lmms::Note* add(int key, int pos, int length, int volume)
	{
		m_owned.emplace_back(new lmms::Note(lmms::TimePos(length), lmms::TimePos(pos), key,
			static_cast<lmms::volume_t>(volume), lmms::DefaultPanning));
		m_view.push_back(m_owned.back().get());
		return m_owned.back().get();
	}

	const lmms::NoteVector& vector() const { return m_view; }

private:
	std::vector<std::unique_ptr<lmms::Note>> m_owned;
	lmms::NoteVector m_view;
};

//! The same four slots as NoteTable notes, for the engine half.
inline void fillFeelClip(NoteTable* notes)
{
	notes->add(60, 9, 12, 120);
	notes->add(62, 26, 12, 80);
	notes->add(64, 34, 12, 100);
	notes->add(65, 51, 12, 100);
}

} // namespace groovetest

/*! A GrooveStep prints as its two numbers when a QCOMPARE fails: without this
 *  QtTest falls back to a formatter that prints the object's address, which
 *  turns a one-line failure into a bisect. */
namespace QTest
{
template <>
inline char* toString(const lmms::GrooveStep& step)
{
	return qstrdup(qPrintable(QStringLiteral("GrooveStep{timing=%1, velocity=%2}")
		.arg(step.timing).arg(step.velocity)));
}
} // namespace QTest

#endif // LMMS_TESTS_GROOVE_TEST_SUPPORT_H
