/*
 * ArticulationPlaybackTest.cpp - what each articulation sends ahead of its note, and a map that moves with its notes
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
/*! Q050-015: the articulation map's playback and its edits, through the real engine.
 *
 *  The subject is tests/src/plugins/MpeTestConsumer.cpp, which logs the note-ons, program changes and control
 *  changes it is handed, in order (its saveState writes them as 'events'). Playing a note through
 *  InstrumentTrack::triggerNote -> sendArticulation -> processOutEvent -> Instrument::handleMidiEvent, the log
 *  must show what the note's articulation is configured to send - a keyswitch key, a program change, a control
 *  change, each before the note's own key - and a note that asks for none must log only its key.
 *  The edit half: moving and removing entries with 'from' takes the notes with their articulation, one undo
 *  restores the map and the notes, and a bad 'from' changes nothing. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <QTemporaryDir>

#include "AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "BufferManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DummyInstrument.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentLayers.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiPlayback.h"
#include "Note.h"
#include "NoteConditions.h"
#include "NotePlayHandle.h"
#include "PluginFactory.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject entry(const QString& name, int keyswitch = -1, int program = -1, int controller = -1, int value = 0)
{
	QJsonObject out{{QStringLiteral("name"), name}};
	if (keyswitch >= 0) { out.insert(QStringLiteral("keyswitch"), keyswitch); }
	if (program >= 0) { out.insert(QStringLiteral("program"), program); }
	if (controller >= 0) { out.insert(QStringLiteral("controller"), controller); out.insert(QStringLiteral("value"), value); }
	return out;
}

constexpr auto testHostCanLoadPluginModules() -> bool
{
#ifdef Q_OS_WIN
	return false;
#else
	return true;
#endif
}

} // namespace

class ArticulationPlaybackTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_cacheDir;
	bool m_engineUp = false;
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;
	Instrument* m_consumer = nullptr;

	QString trackId() const { return control::trackIdOf(m_track); }

	//! A fresh project: one instrument track with the logging instrument, one clip, and @a keys as one-bar-spaced notes.
	void freshTrack(const QList<int>& keys, const QList<int>& articulations)
	{
		Song* song = Engine::getSong();
		song->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		m_consumer = m_track->loadInstrument(QStringLiteral("mpe_test_consumer"));
		QVERIFY2(m_consumer != nullptr && dynamic_cast<DummyInstrument*>(m_consumer) == nullptr, "the logging instrument did not load");
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		for (int i = 0; i < keys.size(); ++i)
		{
			Note* note = m_clip->addNote(Note(TimePos(48), TimePos(i * 96), keys[i]), false);
			NoteConditions conditions = note->conditions();
			conditions.articulation = articulations.value(i);
			note->setConditions(conditions);
		}
		clearLog();
	}

	static void clearLog(Instrument* instrument)
	{
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("log"));
		element.setAttribute(QStringLiteral("clearlog"), 1);
		instrument->restoreState(element);
	}

	void clearLog() { clearLog(m_consumer); }

	//! What @a instrument was handed: "k60 p3 c32=127", the frame offsets they came at, and the keys still down.
	struct Heard
	{
		QString events;
		QList<int> offsets;
		int held = 0;
	};

	static Heard heard(Instrument* instrument)
	{
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("log"));
		const QDomElement state = instrument->saveState(doc, parent);
		Heard out;
		out.events = state.attribute(QStringLiteral("events"));
		for (const QString& offset : state.attribute(QStringLiteral("offsets")).split(QLatin1Char(' '), Qt::SkipEmptyParts))
		{
			out.offsets << offset.toInt();
		}
		out.held = state.attribute(QStringLiteral("held")).toInt();
		return out;
	}

	QString log() const { return heard(m_consumer).events; }

	//! Plays the song from the start for @a periods periods; what the instrument was handed, in order.
	QString play(int periods)
	{
		clearLog();
		Song* song = Engine::getSong();
		song->setPlayPos(0, Song::PlayMode::Song);
		MidiPlayback::instance().stopped();
		song->playSong();
		for (int period = 0; period < periods; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		song->stop();
		return log();
	}

	//! Renders one period of the song playing from the start, counting the allocations it makes.
	std::uint64_t renderCounting()
	{
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		Engine::audioEngine()->renderNextPeriod();
		lmms::test::tlCountAllocations = false;
		return lmms::test::tlAllocationCount;
	}

	//! Plays from the start a period at a time until the instrument hears something; the allocations made in that
	//! period, and the periods it took (-1: nothing in @a limit periods).
	std::uint64_t firstSoundingPeriod(int limit, int* periods)
	{
		clearLog();
		Song* song = Engine::getSong();
		song->setPlayPos(0, Song::PlayMode::Song);
		MidiPlayback::instance().stopped();
		song->playSong();
		*periods = -1;
		std::uint64_t allocations = 0;
		for (int period = 0; period < limit && *periods < 0; ++period)
		{
			allocations = renderCounting();
			if (!log().isEmpty()) { *periods = period; }
		}
		song->stop();
		for (int period = 0; period < 64; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		return allocations;
	}

	QList<int> noteArticulations() const
	{
		QList<int> out;
		for (const Note* note : m_clip->notes()) { out << note->conditions().articulation; }
		return out;
	}

	QJsonArray names() const
	{
		return run(QStringLiteral("track.articulations"), {{QStringLiteral("track"), trackId()}}).result.value(QStringLiteral("articulations")).toArray();
	}

private slots:
	void initTestCase()
	{
		if (!testHostCanLoadPluginModules())
		{
			QSKIP("the logging instrument fixture is a plug-in module a Windows test host cannot load (see MpePlaybackTest)");
		}
		QVERIFY2(m_cacheDir.isValid(), "no temporary directory for the plugin scan cache");
		qputenv("LMMS_PLUGIN_DIR", QByteArrayLiteral(LMMS_MPE_CONSUMER_DIR));
		qputenv("LMMS_PLUGIN_SCAN_CACHE", QFile::encodeName(m_cacheDir.filePath(QStringLiteral("articulation-scan-cache.json"))));
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		BufferManager::init(Engine::audioEngine()->framesPerPeriod());
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		m_engineUp = true;
		QVERIFY2(PluginFactory::instance()->pluginInfo("mpe_test_consumer").descriptor != nullptr,
			qPrintable(PluginFactory::instance()->scanReport()));
	}

	void cleanupTestCase()
	{
		if (!m_engineUp) { return; }
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! Each articulation kind alone, then all three: what it sends arrives before the note's own key, and a note with
	//! no articulation sends nothing else.
	void eachSelectedArticulationSendsItsMidiBeforeTheNote()
	{
		freshTrack({60, 62, 64, 65, 67}, {1, 2, 3, 4, 0});
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), trackId()}, {QStringLiteral("articulations"),
			QJsonArray{entry(QStringLiteral("Staccato"), 24), entry(QStringLiteral("Brass"), -1, 41),
				entry(QStringLiteral("Mute"), -1, -1, 32, 127), entry(QStringLiteral("Everything"), 25, 7, 11, 90)}}}).ok);
		// Notes are a bar (96 ticks) apart; render far enough for all five.
		const QString events = play(2000);
		QCOMPARE(events, QStringLiteral("k24 k60 p41 k62 c32=127 k64 p7 c11=90 k25 k65 k67"));
	}

	//! The same keyswitch is not sent for a note that asks for another articulation, and an undone map sends nothing.
	void anUndoneMapSendsNothingMore()
	{
		freshTrack({60}, {1});
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), trackId()}, {QStringLiteral("articulations"),
			QJsonArray{entry(QStringLiteral("Legato"), -1, -1, 1, 64)}}}).ok);
		QCOMPARE(play(2000), QStringLiteral("c1=64 k60"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(play(2000), QStringLiteral("k60"));
	}

	//! Moving an entry takes the notes with it: the note that asked for "Staccato" still plays the staccato keyswitch.
	void reorderingKeepsEachNoteOnItsArticulation()
	{
		freshTrack({60, 62, 64}, {1, 2, 0});
		const QString track = trackId();
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), track}, {QStringLiteral("articulations"),
			QJsonArray{entry(QStringLiteral("Staccato"), 24), entry(QStringLiteral("Legato"), 25)}}}).ok);
		QCOMPARE(play(2000), QStringLiteral("k24 k60 k25 k62 k64"));
		// Swap the two: Legato first.
		const ControlResult swapped = run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), track},
			{QStringLiteral("articulations"), QJsonArray{entry(QStringLiteral("Legato"), 25), entry(QStringLiteral("Staccato"), 24)}},
			{QStringLiteral("from"), QJsonArray{2, 1}}});
		QVERIFY2(swapped.ok, qPrintable(swapped.errorMessage));
		QCOMPARE(swapped.result.value(QStringLiteral("notes_remapped")).toInt(), 2);
		QCOMPARE(noteArticulations(), (QList<int>{2, 1, 0}));
		QCOMPARE(play(2000), QStringLiteral("k24 k60 k25 k62 k64"));  // the sound did not change
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(noteArticulations(), (QList<int>{1, 2, 0}));
		QCOMPARE(names().at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Staccato"));
	}

	//! Removing an entry: its notes ask for none, the later entry's notes move up, one undo brings both back.
	void removingAnEntryFreesItsNotesAndOneUndoRestoresThem()
	{
		freshTrack({60, 62, 64}, {1, 2, 3});
		const QString track = trackId();
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), track}, {QStringLiteral("articulations"),
			QJsonArray{entry(QStringLiteral("A"), 20), entry(QStringLiteral("B"), 21), entry(QStringLiteral("C"), 22)}}}).ok);
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), track},
			{QStringLiteral("articulations"), QJsonArray{entry(QStringLiteral("A"), 20), entry(QStringLiteral("C"), 22)}},
			{QStringLiteral("from"), QJsonArray{1, 3}}}).ok);
		QCOMPARE(noteArticulations(), (QList<int>{1, 0, 2}));
		QCOMPARE(play(2000), QStringLiteral("k20 k60 k62 k22 k64"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(noteArticulations(), (QList<int>{1, 2, 3}));
		QCOMPARE(names().size(), 3);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(noteArticulations(), (QList<int>{1, 0, 2}));
		QCOMPARE(names().size(), 2);
	}

	//! A 'from' that does not describe the new list is refused and changes neither the map nor a note.
	void aBadOriginsListChangesNothing()
	{
		freshTrack({60}, {1});
		const QString track = trackId();
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), track}, {QStringLiteral("articulations"),
			QJsonArray{entry(QStringLiteral("A"), 20), entry(QStringLiteral("B"), 21)}}}).ok);
		const QJsonArray before = names();
		const QJsonObject two{{QStringLiteral("track"), track}, {QStringLiteral("articulations"), QJsonArray{entry(QStringLiteral("B")), entry(QStringLiteral("A"))}}};
		for (const QJsonArray& from : {QJsonArray{2}, QJsonArray{1, 1}, QJsonArray{3, 1}, QJsonArray{-1, 1}})
		{
			QJsonObject args = two;
			args.insert(QStringLiteral("from"), from);
			QCOMPARE(run(QStringLiteral("track.articulations_set"), args).errorKind, ControlErrorKind::InvalidArgs);
		}
		QCOMPARE(names(), before);
		QCOMPARE(noteArticulations(), (QList<int>{1}));
	}

	//! A note that starts in the middle of a period: its program change, control change and keyswitch come at the very
	//! frame offset the note does, and before it.
	void anArticulationComesAtTheFrameOffsetOfItsNote()
	{
		freshTrack({60, 62}, {0, 1});
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), trackId()}, {QStringLiteral("articulations"),
			QJsonArray{entry(QStringLiteral("Everything"), 25, 7, 11, 90)}}}).ok);
		clearLog();
		const Heard sounded = [this] { play(2000); return heard(m_consumer); }();
		QCOMPARE(sounded.events, QStringLiteral("k60 p7 c11=90 k25 k62"));
		QCOMPARE(sounded.offsets.size(), 5);
		QVERIFY2(sounded.offsets[4] > 0, "the second note should start inside a period, not on its first frame");
		for (int i = 1; i <= 3; ++i) { QCOMPARE(sounded.offsets[i], sounded.offsets[4]); }
		QCOMPARE(sounded.held, 0);  // the keyswitch was let go again
	}

	//! With the track's delay the note starts several periods late. Its articulation does not go out early with the
	//! song position: it arrives with the note, in the period the note starts in, and not before.
	void aDelayedNoteBringsItsArticulationWithIt()
	{
		freshTrack({60}, {1});
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), trackId()}, {QStringLiteral("articulations"),
			QJsonArray{entry(QStringLiteral("Everything"), 25, 7, 11, 90)}}}).ok);
		const int delayMs = 80;
		m_track->setDelayMs(delayMs);
		const int period = Engine::audioEngine()->framesPerPeriod();
		const int delayPeriods = delayMs * static_cast<int>(Engine::audioEngine()->outputSampleRate()) / 1000 / period;
		QVERIFY(delayPeriods >= 4);
		int first = -1;
		firstSoundingPeriod(400, &first);
		QVERIFY2(first >= delayPeriods - 1, qPrintable(QStringLiteral("something sounded in period %1, before the note's delay of ~%2").arg(first).arg(delayPeriods)));
		const Heard sounded = heard(m_consumer);
		// Rendered again to the end of the note, the instrument heard all of it in one go: the first period had the lot.
		QCOMPARE(sounded.events, QStringLiteral("p7 c11=90 k25 k60"));
		QCOMPARE(sounded.offsets.size(), 4);
		for (int i = 0; i < 3; ++i) { QCOMPARE(sounded.offsets[i], sounded.offsets[3]); }
		m_track->setDelayMs(0);
	}

	//! A layer plays the note on its own instrument, and the articulation goes to it too.
	void everyLayerThatPlaysTheNoteGetsItsArticulation()
	{
		freshTrack({60, 80}, {1, 1});
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), trackId()}, {QStringLiteral("articulations"),
			QJsonArray{entry(QStringLiteral("Brass"), 24, 5, 11, 90)}}}).ok);
		const int one = m_track->layers().addLayer(QStringLiteral("mpe_test_consumer"), nullptr);
		const int two = m_track->layers().addLayer(QStringLiteral("mpe_test_consumer"), nullptr);
		QVERIFY2(one > 0 && two > 0, "the layers did not load");
		Instrument* first = m_track->layers().layer(one)->instrument();
		Instrument* second = m_track->layers().layer(two)->instrument();
		const auto playAll = [&] {
			clearLog(m_consumer);
			clearLog(first);
			clearLog(second);
			play(2000);
		};
		playAll();
		const QString group = QStringLiteral("p5 c11=90 k24 k60 p5 c11=90 k24 k80");
		QCOMPARE(heard(m_consumer).events, group);
		QCOMPARE(heard(first).events, group);
		QCOMPARE(heard(second).events, group);
		QCOMPARE(heard(first).held, 0);
		// The track's own instrument leaves the upper keys to the layers: it hears nothing of the note it does not play.
		QVERIFY(run(QStringLiteral("rack.layer_zone_add"), {{QStringLiteral("target"), trackId()},
			{QStringLiteral("layer"), QStringLiteral("layer-0")}, {QStringLiteral("low_key"), 0}, {QStringLiteral("high_key"), 70}}).ok);
		playAll();
		QCOMPARE(heard(m_consumer).events, QStringLiteral("p5 c11=90 k24 k60"));
		QCOMPARE(heard(first).events, group);
		QCOMPARE(heard(second).events, group);
	}

	//! The articulation's own path allocates nothing, and a note that has one costs the audio thread no allocation
	//! over the same note without it.
	void anArticulationAddsNoAllocationOnTheAudioThread()
	{
		freshTrack({60}, {0});
		const ArticulationEvents all{25, 7, 11, 90};
		const TimePos time;
		// The direct call: program, controller and keyswitch events, then the key let go.
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		const bool down = m_track->sendArticulationEvents(all, 0, 60, 100, time, 0);
		m_track->releaseKeyswitch(25, 0, time, 0);
		lmms::test::tlCountAllocations = false;
		QVERIFY(down);
		QCOMPARE(static_cast<qulonglong>(lmms::test::tlAllocationCount), 0ull);
		QCOMPARE(log(), QStringLiteral("p7 c11=90 k25"));
		// The whole period the note starts in, with and without its articulation.
		int periods = -1;
		const std::uint64_t plain = firstSoundingPeriod(400, &periods);
		QVERIFY(periods >= 0);
		QVERIFY2(plain > 0, "the probe counted nothing in a period that starts a note: it is not counting");
		freshTrack({60}, {1});
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), trackId()}, {QStringLiteral("articulations"),
			QJsonArray{entry(QStringLiteral("Everything"), 25, 7, 11, 90)}}}).ok);
		const std::uint64_t withArticulation = firstSoundingPeriod(400, &periods);
		QVERIFY(periods >= 0);
		QCOMPARE(log(), QStringLiteral("p7 c11=90 k25 k60"));
		QVERIFY2(withArticulation <= plain, qPrintable(QStringLiteral("the period allocated %1 times with the articulation, %2 without")
			.arg(withArticulation).arg(plain)));
	}

	//! Through the real project file: author the map and a note's choice on the surface, project.save, clear the
	//! session, project.open - the map (order, every number) and the note's articulation come back, and play.
	void theMapSavesAndLoadsInOrderThroughTheProjectFile()
	{
		freshTrack({60}, {2});
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), trackId()}, {QStringLiteral("articulations"),
			QJsonArray{entry(QStringLiteral("One"), 20), entry(QStringLiteral("Two"), 31, 5, 9, 33), entry(QStringLiteral("Three"))}}}).ok);
		const QJsonArray saved = names();
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString project = dir.filePath(QStringLiteral("articulations.mmp"));
		const ControlResult wrote = run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}});
		QVERIFY2(wrote.ok, qPrintable(wrote.errorMessage));
		QFile file(project);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QVERIFY2(file.readAll().contains("articulations="), "the saved project carries no articulations attribute");
		Engine::getSong()->clearProject();
		QCOMPARE(Engine::getSong()->tracks().size(), 0);
		Engine::getSong()->setLoadOnLaunch(false);
		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), project}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		m_track = nullptr;
		for (Track* track : Engine::getSong()->tracks())
		{
			if (auto* instrument = dynamic_cast<InstrumentTrack*>(track)) { m_track = instrument; }
		}
		QVERIFY2(m_track != nullptr, "the reopened project has no instrument track");
		QCOMPARE(names(), saved);
		QCOMPARE(m_track->articulations().indexOf(QStringLiteral("Three")), 3);
		m_clip = dynamic_cast<MidiClip*>(m_track->getClips().front());
		QCOMPARE(noteArticulations(), (QList<int>{2}));
		// And it plays what it was saved with.
		m_consumer = m_track->instrument();
		QCOMPARE(play(2000), QStringLiteral("p5 c9=33 k31 k60"));
	}
};

QTEST_GUILESS_MAIN(ArticulationPlaybackTest)
#include "ArticulationPlaybackTest.moc"
