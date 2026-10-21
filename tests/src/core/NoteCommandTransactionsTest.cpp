/*
 * NoteCommandTransactionsTest.cpp - the reversibility record of the note commands
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
/*! Cluster new-p11-38-port: a command that changes the project and leaves an undo step
 *  must say HOW it is reversed, so a client can undo it (and report it) without guessing.
 *  These handlers edited the project but returned no "__transaction" payload at all:
 *  - clip.quantize (ControlCommandsNoteHarmony.cpp) moves clips on their tracks;
 *  - scale.define (ControlCommandsNoteHarmony.cpp) writes the custom scales;
 *  - note.split_by_pitch (ControlCommandsNoteMoves.cpp) adds one track per key;
 *  - track.articulations_set (ControlCommandsNotePlayback.cpp) replaces a track's map;
 *  - transport.groove (ControlCommandsNotePlayback.cpp) sets the global swing;
 *  - transport.midi_options (ControlCommandsNotePlayback.cpp) writes the MIDI preferences.
 *  The record is read back from control.transactions - the same stream a client sees. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiPlayback.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The newest record the registry holds for @a command, the stream a client reads.
QJsonObject transactionFor(const QString& command)
{
	const QJsonArray transactions = run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray();
	for (int i = transactions.size() - 1; i >= 0; --i)
	{
		const QJsonObject transaction = transactions.at(i).toObject();
		if (transaction.value(QStringLiteral("command")).toString() == command) { return transaction; }
	}
	return {};
}

//! The record of @a command must name the inverse that undoes it.
void checkUndoable(const QString& command)
{
	const QJsonObject transaction = transactionFor(command);
	QVERIFY2(!transaction.isEmpty(), qPrintable(QStringLiteral("no transaction recorded for %1").arg(command)));
	QCOMPARE(transaction.value(QStringLiteral("inverse")).toObject().value(QStringLiteral("op")).toString(), QStringLiteral("control.undo"));
	QVERIFY(transaction.value(QStringLiteral("reversible")).toBool());
}

} // namespace

class NoteCommandTransactionsTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;
	QString m_customScalesSetting;
	QString m_midiChaseSetting;
	QString m_midiOffsetSetting;
	bool m_initialChase = false;
	int m_initialOffset = 0;

	Note* oneNote(int key = 60, int length = 384)
	{
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		m_track->loadInstrument(QStringLiteral("tripleoscillator"));
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		return m_clip->addNote(Note(TimePos(length), TimePos(0), key), false);
	}

	MidiClip* clipWith(std::initializer_list<std::pair<int, int>> keysAt, int length = 24)
	{
		auto* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		for (const auto& [key, at] : keysAt) { clip->addNote(Note(TimePos(length), TimePos(at), key), false); }
		return clip;
	}

	QJsonObject self(const MidiClip* clip) const { return {{QStringLiteral("clip"), control::clipIdOf(clip)}}; }

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		// These commands write user config settings; remember them to put them back afterwards.
		ConfigManager* config = ConfigManager::inst();
		m_customScalesSetting = config->value(QStringLiteral("scales"), QStringLiteral("custom"));
		m_midiChaseSetting = config->value(QStringLiteral("midi"), QStringLiteral("chase"));
		m_midiOffsetSetting = config->value(QStringLiteral("midi"), QStringLiteral("playbackoffset"));
		m_initialChase = MidiPlayback::instance().chase();
		m_initialOffset = MidiPlayback::instance().offsetMs();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		MidiPlayback::instance().setPreferences(m_initialChase, m_initialOffset);
		ConfigManager* config = ConfigManager::inst();
		const auto restore = [config](const QString& section, const QString& key, const QString& value) {
			if (value.isEmpty()) { config->deleteValue(section, key); }
			else { config->setValue(section, key, value); }
		};
		restore(QStringLiteral("scales"), QStringLiteral("custom"), m_customScalesSetting);
		restore(QStringLiteral("midi"), QStringLiteral("chase"), m_midiChaseSetting);
		restore(QStringLiteral("midi"), QStringLiteral("playbackoffset"), m_midiOffsetSetting);
		Engine::destroy();
	}

	void undoableHarmonyCommandsRecordTheirBeforeState()
	{
		auto* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(200)));
		const QString id = control::clipIdOf(clip);
		const ControlResult quantized = run(QStringLiteral("clip.quantize"), {{QStringLiteral("clips"), QJsonArray{id}}});
		QVERIFY2(quantized.ok, qPrintable(quantized.errorMessage));
		checkUndoable(QStringLiteral("clip.quantize"));
		QJsonObject transaction = transactionFor(QStringLiteral("clip.quantize"));
		QCOMPARE(transaction.value(QStringLiteral("before")).toObject().value(QStringLiteral("clips")).toArray(),
			(QJsonArray{QJsonObject{{QStringLiteral("clip"), id}, {QStringLiteral("start"), 200}}}));
		QCOMPARE(transaction.value(QStringLiteral("mechanism")).toString(),
			QStringLiteral("Track journal checkpoints: control.undo restores each clip's original start position"));

		const QString scale = QStringLiteral("UndoRecordRegression");
		QVERIFY(run(QStringLiteral("scale.define"), {{QStringLiteral("name"), scale}}).ok);
		const QJsonObject scalesBefore = run(QStringLiteral("scale.custom")).result.value(QStringLiteral("scales")).toObject();
		QVERIFY(run(QStringLiteral("scale.define"), {{QStringLiteral("name"), scale}, {QStringLiteral("degrees"), QJsonArray{0, 2, 4}}}).ok);
		checkUndoable(QStringLiteral("scale.define"));
		transaction = transactionFor(QStringLiteral("scale.define"));
		QCOMPARE(transaction.value(QStringLiteral("before")).toObject().value(QStringLiteral("scales")).toObject(), scalesBefore);
		QCOMPARE(transaction.value(QStringLiteral("mechanism")).toString(),
			QStringLiteral("Action checkpoint: control.undo writes the previous custom scales back to the config file"));
		QVERIFY(run(QStringLiteral("scale.define"), {{QStringLiteral("name"), scale}}).ok);

		MidiClip* splitClip = clipWith({{60, 0}, {64, 24}});
		const QString splitId = control::clipIdOf(splitClip);
		const QString trackId = control::trackIdOf(m_track);
		const ControlResult split = run(QStringLiteral("note.split_by_pitch"), self(splitClip));
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		checkUndoable(QStringLiteral("note.split_by_pitch"));
		transaction = transactionFor(QStringLiteral("note.split_by_pitch"));
		QCOMPARE(transaction.value(QStringLiteral("before")).toObject(), (QJsonObject{{QStringLiteral("clip"), splitId},
			{QStringLiteral("track"), trackId}, {QStringLiteral("keys"), QJsonArray{60, 64}}}));
		QCOMPARE(transaction.value(QStringLiteral("mechanism")).toString(),
			QStringLiteral("Action checkpoint: control.undo removes the per-key tracks created by this split"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
	}

	void undoablePlaybackCommandsRecordTheirBeforeState()
	{
		oneNote();
		const QString track = control::trackIdOf(m_track);
		const QJsonArray articulationsBefore = m_track->articulations().toJson();
		const QJsonArray articulations{QJsonObject{{QStringLiteral("name"), QStringLiteral("transaction articulation")}}};
		const ControlResult setArticulations = run(QStringLiteral("track.articulations_set"),
			{{QStringLiteral("track"), track}, {QStringLiteral("articulations"), articulations}});
		QVERIFY2(setArticulations.ok, qPrintable(setArticulations.errorMessage));
		checkUndoable(QStringLiteral("track.articulations_set"));
		QJsonObject transaction = transactionFor(QStringLiteral("track.articulations_set"));
		QCOMPARE(transaction.value(QStringLiteral("before")).toObject(), (QJsonObject{{QStringLiteral("track"), track},
			{QStringLiteral("articulations"), articulationsBefore}}));
		QCOMPARE(transaction.value(QStringLiteral("mechanism")).toString(),
			QStringLiteral("Action checkpoint: control.undo restores this track's previous articulation map"));

		MidiPlayback& playback = MidiPlayback::instance();
		const QJsonObject grooveBefore{{QStringLiteral("swing"), static_cast<double>(playback.swing())},
			{QStringLiteral("grid"), playback.swingGrid() == MidiPlayback::DefaultSwingGrid ? QStringLiteral("16th") : QStringLiteral("8th")}};
		const float nextSwing = playback.swing() == 0.f ? 0.25f : 0.f;
		const ControlResult setGroove = run(QStringLiteral("transport.groove"), {{QStringLiteral("swing"), nextSwing}});
		QVERIFY2(setGroove.ok, qPrintable(setGroove.errorMessage));
		checkUndoable(QStringLiteral("transport.groove"));
		transaction = transactionFor(QStringLiteral("transport.groove"));
		QCOMPARE(transaction.value(QStringLiteral("before")).toObject(), grooveBefore);
		QCOMPARE(transaction.value(QStringLiteral("mechanism")).toString(),
			QStringLiteral("Action checkpoint: control.undo restores the previous swing and grid"));

		const bool chaseBefore = playback.chase();
		const int offsetBefore = playback.offsetMs();
		const QJsonObject midiOptionsBefore{{QStringLiteral("chase"), chaseBefore}, {QStringLiteral("offset_ms"), offsetBefore}};
		const ControlResult setMidiOptions = run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), !chaseBefore}});
		QVERIFY2(setMidiOptions.ok, qPrintable(setMidiOptions.errorMessage));
		checkUndoable(QStringLiteral("transport.midi_options"));
		transaction = transactionFor(QStringLiteral("transport.midi_options"));
		QCOMPARE(transaction.value(QStringLiteral("before")).toObject(), midiOptionsBefore);
		QCOMPARE(transaction.value(QStringLiteral("mechanism")).toString(),
			QStringLiteral("Action checkpoint: control.undo restores the previous chase and MIDI offset preferences"));
	}
};

QTEST_MAIN(NoteCommandTransactionsTest)
#include "NoteCommandTransactionsTest.moc"
