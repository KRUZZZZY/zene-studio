/*
 * ArrangeToolsTest.cpp - the clip list, sections, remove silence, tab to transient, note rotation
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
/*! 0.5.0 L04 (W3): clip.list finds clips by track and name (ED-036); sections are added, changed,
 *  removed and undone, saved only when there is one, and selected as the range (ED-047); a quiet middle
 *  stretch of a sample clip is cut out, one undo step (ED-040); tab to transient refuses where there
 *  is no audio (ED-025); notes rotate inside their clip (ED-028). */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include <vector>

#include "ArrangementSections.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A sample clip of three equal parts: loud, silent, loud.
SampleClip* loudQuietLoud(Track* track)
{
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	std::vector<SampleFrame> data(static_cast<std::size_t>(rate * 3), SampleFrame(0.5f, 0.5f));
	for (int f = rate; f < 2 * rate; ++f) { data[static_cast<std::size_t>(f)] = SampleFrame(0.f, 0.f); }
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	clip->movePosition(TimePos(0));
	return clip;
}

} // namespace

class ArrangeToolsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theClipListFindsByName()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		Clip* verse = track->createClip(TimePos(0));
		verse->setName(QStringLiteral("Verse riff"));
		track->createClip(TimePos(192))->setName(QStringLiteral("Chorus"));
		const QJsonArray found = run(QStringLiteral("clip.list"), {{QStringLiteral("name"), QStringLiteral("riff")}}).result.value(QStringLiteral("clips")).toArray();
		QCOMPARE(found.size(), 1);
		QCOMPARE(found.first().toObject().value(QStringLiteral("clip")).toString(), control::clipIdOf(verse));
		QCOMPARE(run(QStringLiteral("clip.list"), {{QStringLiteral("track"), control::trackIdOf(track)}}).result.value(QStringLiteral("count")).toInt(), 2);
	}

	void sectionsAreSpansWithUndo()
	{
		QVERIFY(!ArrangementSections::instance().shouldPersist());
		const ControlResult added = run(QStringLiteral("section.add"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 768},
			{QStringLiteral("name"), QStringLiteral("Verse")}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString id = added.result.value(QStringLiteral("section")).toString();
		QVERIFY(run(QStringLiteral("section.set"), {{QStringLiteral("section"), id}, {QStringLiteral("name"), QStringLiteral("Verse 1")}}).ok);
		QCOMPARE(ArrangementSections::instance().at(100)->name, QStringLiteral("Verse 1"));
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("song"));
		ArrangementSections::instance().save(doc, root);
		QCOMPARE(root.firstChildElement().firstChildElement().attribute(QStringLiteral("name")), QStringLiteral("Verse 1"));
		QVERIFY(run(QStringLiteral("section.select"), {{QStringLiteral("section"), id}}).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the rename
		QCOMPARE(ArrangementSections::instance().at(100)->name, QStringLiteral("Verse"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the add
		QVERIFY(ArrangementSections::instance().sections().isEmpty());
		QCOMPARE(run(QStringLiteral("section.add"), {{QStringLiteral("start"), 100}, {QStringLiteral("end"), 50}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void aQuietStretchIsCutOut()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		SampleClip* clip = loudQuietLoud(track);
		const ControlResult cut = run(QStringLiteral("clip.remove_silence"), {{QStringLiteral("clip"), control::clipIdOf(clip)}});
		QVERIFY2(cut.ok, qPrintable(cut.errorMessage));
		QCOMPARE(cut.result.value(QStringLiteral("removed")).toInt(), 1);
		QCOMPARE(track->getClips().size(), std::size_t{2});  // the loud parts either side
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(track->getClips().size(), std::size_t{1});
	}

	void noTransientWithoutAudio()
	{
		Engine::getSong()->setPlayPos(5000, Song::PlayMode::Song);
		QCOMPARE(run(QStringLiteral("arrangement.next_transient")).errorKind, ControlErrorKind::NotFound);
	}

	void notesRotateInsideTheClip()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(24), TimePos(0), 60), false);
		clip->addNote(Note(TimePos(24), TimePos(144), 64), false);
		clip->changeLength(TimePos(192));
		QVERIFY(run(QStringLiteral("note.rotate"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("amount"), 96}}).ok);
		QCOMPARE(clip->notes()[0]->pos().getTicks(), tick_t{48});   // 144 + 96 wrapped
		QCOMPARE(clip->notes()[0]->key(), 64);
		QCOMPARE(clip->notes()[1]->pos().getTicks(), tick_t{96});
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clip->notes()[0]->key(), 60);
	}
};

QTEST_MAIN(ArrangeToolsTest)
#include "ArrangeToolsTest.moc"
