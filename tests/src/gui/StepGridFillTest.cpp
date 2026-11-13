/*
 * StepGridFillTest.cpp - note.step_fill: seeded shape and random fills in the active scale, one undo step, refusals
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
/*! 0.5.0 Q050-294.3: note.step_fill re-pitches a clip's step columns from a seed: the same seed
 *  gives the same clip and another seed another one, in every shape and kind; every key is in the
 *  active scale and in low..high; a chord keeps its interval in scale degrees; a fill is one undo
 *  step that restores the exact notes (ids too); density drops columns; a fill that changes
 *  nothing takes no step; and every refusal changes nothing. */

#include <QtTest>

#include <QApplication>
#include <QJsonObject>
#include <QJsonValue>
#include <algorithm>
#include <tuple>
#include <utility>
#include <vector>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Song.h"

using namespace lmms;

namespace
{

const QString StepFill = QStringLiteral("note.step_fill");

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

bool undo() { return run(QStringLiteral("control.undo")).ok; }

int depth() { return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(); }

//! position, key, length, velocity, id - what a fill edits, and what an undo must put back exactly.
using NoteRow = std::tuple<int, int, int, int, QString>;

std::vector<NoteRow> rowsOf(const MidiClip* clip)
{
	std::vector<NoteRow> out;
	for (const Note* note : clip->notes())
	{
		out.emplace_back(note->pos().getTicks(), note->key(), note->length().getTicks(), static_cast<int>(note->getVolume()),
			control::noteIdOf(note));
	}
	std::sort(out.begin(), out.end());
	return out;
}

std::vector<int> keysInTimeOrder(const MidiClip* clip)
{
	std::vector<int> out;
	for (const NoteRow& row : rowsOf(clip)) { out.push_back(std::get<1>(row)); }
	return out;
}

//! The pitch classes of C major.
const std::vector<int> CMajor{0, 2, 4, 5, 7, 9, 11};

bool inCMajor(int key) { return std::find(CMajor.begin(), CMajor.end(), key % 12) != CMajor.end(); }

//! The scale degree of @a key in C major: how many C-major keys lie below it.
int cMajorDegree(int key)
{
	int degree = 0;
	for (int k = 0; k < key; ++k) { degree += inCMajor(k) ? 1 : 0; }
	return degree;
}

//! Eight quarter-length columns (12 ticks apart), each holding the given keys; one entry per column.
std::vector<std::vector<int>> eightColumns(int key)
{
	return std::vector<std::vector<int>>(8, std::vector<int>{key});
}

using Arg = std::pair<QString, QJsonValue>;

QJsonObject withClip(const MidiClip* clip, std::initializer_list<Arg> more)
{
	QJsonObject out{{QStringLiteral("clip"), control::clipIdOf(clip)}};
	for (const auto& [key, value] : more) { out.insert(key, value); }
	return out;
}

} // namespace

class StepGridFillTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;

	//! A clip with one note per key in each column; column i starts at tick 12 * i and is 12 ticks long.
	MidiClip* clipOf(const std::vector<std::vector<int>>& columns)
	{
		MidiClip* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		for (std::size_t i = 0; i < columns.size(); ++i)
		{
			for (int key : columns[i])
			{
				clip->addNote(Note(TimePos(12), TimePos(static_cast<int>(i) * 12), key), false);
			}
		}
		clip->rearrangeAllNotes();
		return clip;
	}

	void freshScale()
	{
		QVERIFY(run(QStringLiteral("scale.root_set"), {{QStringLiteral("root"), QStringLiteral("C")}}).ok);
		QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("scale"), QStringLiteral("Major")}}).ok);
	}

	//! Runs a fill, reads the keys it wrote in time order, then undoes it so the next fill starts from the same clip.
	void fillKeys(MidiClip* clip, std::initializer_list<Arg> more, std::vector<int>* keys)
	{
		QVERIFY(run(StepFill, withClip(clip, more)).ok);
		*keys = keysInTimeOrder(clip);
		QVERIFY(undo());
	}

	//! A refused call: it fails with @a kind, changes no note and takes no undo step.
	void expectRefusal(MidiClip* clip, const QJsonObject& args, ControlErrorKind kind)
	{
		const std::vector<NoteRow> before = rowsOf(clip);
		const int depthBefore = depth();
		const ControlResult r = run(StepFill, args);
		QVERIFY2(!r.ok, "a refused fill must not succeed");
		QVERIFY2(r.errorKind == kind, qPrintable(r.errorMessage));
		QVERIFY(rowsOf(clip) == before);
		QCOMPARE(depth(), depthBefore);
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track != nullptr);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! (a) The same seed writes the same notes, another seed another line - in shape (all five contours)
	//! and in random.
	void repeatableBySeed()
	{
		freshScale();
		MidiClip* clip = clipOf(eightColumns(60));
		const std::vector<NoteRow> start = rowsOf(clip);
		const QStringList contours{QStringLiteral("walk"), QStringLiteral("rise"), QStringLiteral("fall"),
			QStringLiteral("arch"), QStringLiteral("valley")};
		for (const QString& contour : contours)
		{
			const Arg shape{QStringLiteral("kind"), QStringLiteral("shape")};
			const Arg name{QStringLiteral("contour"), contour};
			std::vector<int> first;
			std::vector<int> again;
			std::vector<int> other;
			fillKeys(clip, {shape, name, {QStringLiteral("seed"), 7}, {QStringLiteral("low"), 48}, {QStringLiteral("high"), 72}}, &first);
			fillKeys(clip, {shape, name, {QStringLiteral("seed"), 7}, {QStringLiteral("low"), 48}, {QStringLiteral("high"), 72}}, &again);
			fillKeys(clip, {shape, name, {QStringLiteral("seed"), 8}, {QStringLiteral("low"), 48}, {QStringLiteral("high"), 72}}, &other);
			QVERIFY2(first == again, qPrintable(contour));
			QVERIFY2(first != other, qPrintable(contour));
		}
		std::vector<int> randomFirst;
		std::vector<int> randomAgain;
		std::vector<int> randomOther;
		const Arg random{QStringLiteral("kind"), QStringLiteral("random")};
		fillKeys(clip, {random, {QStringLiteral("seed"), 7}}, &randomFirst);
		fillKeys(clip, {random, {QStringLiteral("seed"), 7}}, &randomAgain);
		fillKeys(clip, {random, {QStringLiteral("seed"), 8}}, &randomOther);
		QVERIFY(randomFirst == randomAgain);
		QVERIFY(randomFirst != randomOther);
		QVERIFY(rowsOf(clip) == start);
	}

	//! (b) Every key is a C-major key inside low..high, in both kinds.
	void keysStayInScaleAndRange()
	{
		freshScale();
		MidiClip* clip = clipOf(eightColumns(60));
		for (const QString& kind : {QStringLiteral("shape"), QStringLiteral("random")})
		{
			std::vector<int> keys;
			fillKeys(clip, {{QStringLiteral("kind"), kind}, {QStringLiteral("seed"), 3}, {QStringLiteral("low"), 48},
				{QStringLiteral("high"), 72}, {QStringLiteral("spread"), 2}}, &keys);
			QCOMPARE(static_cast<int>(keys.size()), 8);
			for (int key : keys)
			{
				QVERIFY2(inCMajor(key), qPrintable(kind));
				QVERIFY2(key >= 48 && key <= 72, qPrintable(kind));
			}
		}
	}

	//! (b) A chord keeps its interval in scale degrees: C-E-G stays a third and a fifth above its root (two
	//! and four C-major degrees), wherever it moves. One column, so the walk cannot clamp the chord at the edge.
	void chordKeepsScaleDegrees()
	{
		freshScale();
		MidiClip* clip = clipOf({{60, 64, 67}});
		std::vector<int> keys;
		fillKeys(clip, {{QStringLiteral("kind"), QStringLiteral("shape")}, {QStringLiteral("contour"), QStringLiteral("walk")},
			{QStringLiteral("seed"), 5}, {QStringLiteral("low"), 48}, {QStringLiteral("high"), 72}, {QStringLiteral("spread"), 2}}, &keys);
		QCOMPARE(static_cast<int>(keys.size()), 3);
		QCOMPARE(cMajorDegree(keys[1]) - cMajorDegree(keys[0]), 2);
		QCOMPARE(cMajorDegree(keys[2]) - cMajorDegree(keys[0]), 4);
	}

	//! (c) One control.undo restores the exact rows, ids included, and the undo depth goes up by one.
	void oneUndoRestoresExactRows()
	{
		freshScale();
		MidiClip* clip = clipOf(eightColumns(60));
		const std::vector<NoteRow> before = rowsOf(clip);
		const int depthBefore = depth();
		QVERIFY(run(StepFill, withClip(clip, {{QStringLiteral("kind"), QStringLiteral("shape")},
			{QStringLiteral("contour"), QStringLiteral("rise")}, {QStringLiteral("seed"), 9}})).ok);
		QVERIFY(rowsOf(clip) != before);
		QCOMPARE(depth(), depthBefore + 1);
		QVERIFY(undo());
		QVERIFY(rowsOf(clip) == before);
		QCOMPARE(depth(), depthBefore);
	}

	//! (d) Density 1 keeps almost no column; density 100 keeps all; a fill that changes nothing takes no step.
	void densityAndNoChangeTakeNoStep()
	{
		freshScale();
		MidiClip* clip = clipOf(eightColumns(60));
		const std::vector<NoteRow> before = rowsOf(clip);
		const int depthBefore = depth();

		QVERIFY(run(StepFill, withClip(clip, {{QStringLiteral("kind"), QStringLiteral("random")},
			{QStringLiteral("seed"), 4}, {QStringLiteral("density"), 1}})).ok);
		QVERIFY2(clip->notes().size() <= 2, "density 1 must leave at most two of eight columns");
		QVERIFY(undo());
		QVERIFY(rowsOf(clip) == before);

		QVERIFY(run(StepFill, withClip(clip, {{QStringLiteral("kind"), QStringLiteral("random")},
			{QStringLiteral("seed"), 4}, {QStringLiteral("density"), 100}})).ok);
		QCOMPARE(static_cast<int>(clip->notes().size()), 8);
		QVERIFY(undo());
		QVERIFY(rowsOf(clip) == before);
		QCOMPARE(depth(), depthBefore);

		// A valley of one column sits at the bottom of 60..72, which is key 60 already: nothing changes.
		MidiClip* still = clipOf({{60}});
		const int depthStill = depth();
		const std::vector<NoteRow> stillBefore = rowsOf(still);
		QVERIFY(run(StepFill, withClip(still, {{QStringLiteral("kind"), QStringLiteral("shape")},
			{QStringLiteral("contour"), QStringLiteral("valley")}, {QStringLiteral("spread"), 0},
			{QStringLiteral("seed"), 1}, {QStringLiteral("low"), 60}, {QStringLiteral("high"), 72}})).ok);
		QVERIFY(rowsOf(still) == stillBefore);
		QCOMPARE(depth(), depthStill);
	}

	//! (e) Every refusal fails typed, changes no note and takes no undo step.
	void refusalsChangeNothing()
	{
		freshScale();
		MidiClip* clip = clipOf(eightColumns(60));

		{
			const std::vector<NoteRow> before = rowsOf(clip);
			const int depthBefore = depth();
			const ControlResult r = run(StepFill, {{QStringLiteral("clip"), QStringLiteral("clip-999999")}});
			QVERIFY2(!r.ok, "an unknown clip must not succeed");
			QVERIFY2(r.errorKind != ControlErrorKind::None, "an unknown clip must carry an error kind");
			QVERIFY(rowsOf(clip) == before);
			QCOMPARE(depth(), depthBefore);
		}

		expectRefusal(clip, withClip(clip, {{QStringLiteral("low"), 60}, {QStringLiteral("high"), 61}}),
			ControlErrorKind::InvalidArgs);

		expectRefusal(clip, withClip(clip, {{QStringLiteral("seed"), -1}}), ControlErrorKind::InvalidArgs);
		expectRefusal(clip, withClip(clip, {{QStringLiteral("seed"), 1.5}}), ControlErrorKind::InvalidArgs);
		expectRefusal(clip, withClip(clip, {{QStringLiteral("seed"), 2147483648.0}}), ControlErrorKind::InvalidArgs);

		expectRefusal(clip, withClip(clip, {{QStringLiteral("kind"), QStringLiteral("shap")}}), ControlErrorKind::InvalidArgs);

		QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("scale"), QString()}}).ok);
		expectRefusal(clip, withClip(clip, {{QStringLiteral("kind"), QStringLiteral("random")}}), ControlErrorKind::Refused);
		freshScale();
	}
};

QTEST_MAIN(StepGridFillTest)
#include "StepGridFillTest.moc"
