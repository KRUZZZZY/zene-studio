/*
 * MidiInputTransformTest.cpp - a track's MIDI input transform: the event rules, the commands, the file, live input
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

/*! ED-145 / ED-113 (0.5.0 L07): MidiInputTransform's rules on single events (channel, key range,
 *  transpose, scale snap, velocity; a note-off following its note-on across a change),
 *  track.input_transform_set / get (validation, one undo step, scale_snap from the scale context),
 *  the attribute in the track's element (only when set; a preset never carries it), and live input
 *  through InstrumentTrack::processInEvent starting the transformed note. */

#include <QtTest>

#include <QDomDocument>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "MidiInputTransform.h"
#include "NotePlayHandle.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

MidiEvent noteOn(int key, int velocity, int channel = 0)
{
	return MidiEvent(MidiNoteOn, static_cast<int8_t>(channel), static_cast<int16_t>(key), static_cast<int16_t>(velocity));
}

MidiEvent noteOff(int key, int channel = 0)
{
	return MidiEvent(MidiNoteOff, static_cast<int8_t>(channel), static_cast<int16_t>(key), 0);
}

bool set(MidiInputTransform& transform, const QJsonObject& fields)
{
	QString why;
	return transform.fromJson(fields, &why);
}

} // namespace

class MidiInputTransformTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void theIdentityChangesNothing()
	{
		MidiInputTransform transform;
		QVERIFY(!transform.active());
		MidiEvent event = noteOn(60, 90);
		QVERIFY(transform.apply(&event));
		QCOMPARE(event.key(), 60);
		QCOMPARE(static_cast<int>(event.velocity()), 90);
	}

	void transposeVelocityAndTheKeyRange()
	{
		MidiInputTransform transform;
		QVERIFY(set(transform, {{QStringLiteral("transpose"), 12}, {QStringLiteral("velocity_scale"), 50},
			{QStringLiteral("velocity_offset"), 10}, {QStringLiteral("key_low"), 48}, {QStringLiteral("key_high"), 72}}));
		QVERIFY(transform.active());
		MidiEvent event = noteOn(60, 100);
		QVERIFY(transform.apply(&event));
		QCOMPARE(event.key(), 72);
		QCOMPARE(static_cast<int>(event.velocity()), 60);  // 100 x 50 % + 10
		MidiEvent outside = noteOn(40, 100);
		QVERIFY2(!transform.apply(&outside), "a note below key_low is dropped");
		MidiEvent outsideOff = noteOff(40);
		QVERIFY2(!transform.apply(&outsideOff), "and so is its note-off");
		QVERIFY(set(transform, {{QStringLiteral("fixed_velocity"), 127}}));
		MidiEvent fixed = noteOn(50, 3);
		QVERIFY(transform.apply(&fixed));
		QCOMPARE(static_cast<int>(fixed.velocity()), 127);
		QVERIFY(!set(transform, {{QStringLiteral("transpose"), 49}}));  // out of range: refused
		QVERIFY(set(transform, {}));
	}

	void aNoteOffFollowsItsNoteOnAcrossAChange()
	{
		MidiInputTransform transform;
		QVERIFY(set(transform, {{QStringLiteral("transpose"), 5}}));
		MidiEvent on = noteOn(60, 100);
		QVERIFY(transform.apply(&on));
		QCOMPARE(on.key(), 65);
		QVERIFY(set(transform, {{QStringLiteral("transpose"), -7}}));  // changed while the key is held
		MidiEvent off = noteOff(60);
		QVERIFY(transform.apply(&off));
		QCOMPARE(off.key(), 65);  // the note that sounds is the one released
		MidiEvent zeroVelocity = noteOn(62, 100);
		QVERIFY(transform.apply(&zeroVelocity));
		MidiEvent release = noteOn(62, 0);  // a note-on of velocity 0 is a note-off
		QVERIFY(transform.apply(&release));
		QCOMPARE(release.key(), 55);
	}

	//! A key range whose ends are swapped is a RANGE, and a range with no keys in
	//! it is not a setting an instrument track may hold: checked key by key the
	//! pair (100, 20) is two legal keys, and every note-on then fails the range
	//! test and is dropped. One bad value changes nothing, so the pair is
	//! refused together and the previous range stands.
	void anInvertedKeyRangeIsRefused()
	{
		MidiInputTransform transform;
		QVERIFY(set(transform, {{QStringLiteral("key_low"), 48}, {QStringLiteral("key_high"), 72}}));
		QVERIFY2(!set(transform, {{QStringLiteral("key_low"), 100}, {QStringLiteral("key_high"), 20}}),
			"a range whose low end is above its high end is refused");
		QVERIFY2(!set(transform, {{QStringLiteral("key_high"), 10}}),
			"and it is refused when only one end is named");
		QString why;
		const bool inverted = transform.fromJson({{QStringLiteral("key_low"), 100},
			{QStringLiteral("key_high"), 20}}, &why);
		QVERIFY2(!inverted, "the pair is refused whatever the shape of the request");
		QVERIFY2(!why.isEmpty(), "the refusal says why");

		// The range that stood is still the range: a note inside it passes.
		MidiEvent inside = noteOn(60, 100);
		QVERIFY(transform.apply(&inside));
		QCOMPARE(inside.key(), 60);
		MidiEvent outside = noteOn(80, 100);
		QVERIFY(!transform.apply(&outside));
	}

	//! Every setting is an integer. QJsonValue::toInt() answers the DEFAULT (0)
	//! for a number that is not whole, so a fractional value used to pass the
	//! range check and be stored as that default: 'transpose': 2.5 was accepted
	//! and transposed by 0.
	void aFractionalSettingIsRefused()
	{
		MidiInputTransform transform;
		QVERIFY(set(transform, {{QStringLiteral("transpose"), 12}}));
		QVERIFY2(!set(transform, {{QStringLiteral("transpose"), 2.5}}),
			"2.5 is not an integer and is refused");
		QVERIFY2(!set(transform, {{QStringLiteral("velocity_scale"), 99.5}}),
			"in every setting, not just the first");
		QVERIFY2(!set(transform, {{QStringLiteral("key_low"), 40.25},
			{QStringLiteral("key_high"), 90}}),
			"and nothing beside it is stored either");
		MidiEvent event = noteOn(60, 100);
		QVERIFY(transform.apply(&event));
		QCOMPARE(event.key(), 72);  // the transpose of 12 is still the one in force
	}

	void theChannelFilterAndTheScaleSnap()
	{
		MidiInputTransform transform;
		QVERIFY(set(transform, {{QStringLiteral("channel"), 2}}));
		MidiEvent wrong = noteOn(60, 100, 0);
		QVERIFY(!transform.apply(&wrong));
		MidiEvent right = noteOn(60, 100, 1);
		QVERIFY(transform.apply(&right));
		transform.reset();
		const int cMajor = (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11);
		QVERIFY(set(transform, {{QStringLiteral("scale_mask"), cMajor}}));
		MidiEvent sharp = noteOn(61, 100);  // C#: C and D are equally near - the lower wins
		QVERIFY(transform.apply(&sharp));
		QCOMPARE(sharp.key(), 60);
		MidiEvent inScale = noteOn(64, 100);
		QVERIFY(transform.apply(&inScale));
		QCOMPARE(inScale.key(), 64);
	}

	void theCommandsUndoAndTheFile()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		const QString id = control::trackIdOf(track);
		const ControlResult done = run(QStringLiteral("track.input_transform_set"), {{QStringLiteral("track"), id},
			{QStringLiteral("transpose"), -12}, {QStringLiteral("fixed_velocity"), 80}});
		QVERIFY2(done.ok, qPrintable(done.errorMessage));
		QCOMPARE(track->inputTransform().toJson().value(QStringLiteral("transpose")).toInt(), -12);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!track->inputTransform().active());
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY(track->inputTransform().active());
		QCOMPARE(run(QStringLiteral("track.input_transform_set"), {{QStringLiteral("track"), id}, {QStringLiteral("scale_snap"), true}}).errorKind,
			ControlErrorKind::Refused);  // no scale set
		QVERIFY(run(QStringLiteral("scale.root_set"), {{QStringLiteral("root"), 0}}).ok);
		QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("scale"), QStringLiteral("Major")}}).ok);
		QVERIFY(run(QStringLiteral("track.input_transform_set"), {{QStringLiteral("track"), id}, {QStringLiteral("scale_snap"), true}}).ok);
		QCOMPARE(track->inputTransform().toJson().value(QStringLiteral("scale_mask")).toInt(), 0xAB5);  // C D E F G A B
		QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("scale"), QString()}}).ok);

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement saved = track->saveState(doc, root);
		// The attribute is the instrument track's own (saveTrackSpecificSettings writes the <instrumenttrack> child).
		QVERIFY(saved.firstChildElement(QStringLiteral("instrumenttrack")).hasAttribute(QStringLiteral("inputtransform")));
		auto* other = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		other->restoreState(saved);
		QCOMPARE(other->inputTransform().toJson().value(QStringLiteral("fixed_velocity")).toInt(), 80);
		QVERIFY(run(QStringLiteral("track.input_transform_set"), {{QStringLiteral("track"), id}, {QStringLiteral("reset"), true}}).ok);
		QVERIFY(!track->saveState(doc, root).firstChildElement(QStringLiteral("instrumenttrack"))
			.hasAttribute(QStringLiteral("inputtransform")));  // the identity is not written
	}

	void liveInputPlaysTheTransformedNote()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		QVERIFY(run(QStringLiteral("track.input_transform_set"), {{QStringLiteral("track"), control::trackIdOf(track)},
			{QStringLiteral("transpose"), 7}}).ok);
		MidiEvent on = noteOn(60, 100);
		on.setSource(MidiEvent::Source::External);
		track->processInEvent(on, TimePos(0), 0);
		Engine::audioEngine()->renderNextPeriod();
		const NotePlayHandle* played = nullptr;
		for (PlayHandle* handle : Engine::audioEngine()->playHandles())
		{
			auto* note = dynamic_cast<NotePlayHandle*>(handle);
			if (note != nullptr && note->instrumentTrack() == track) { played = note; }
		}
		QVERIFY2(played != nullptr, "the live note never started");
		QCOMPARE(played->key(), 67);
		MidiEvent off = noteOff(60);
		off.setSource(MidiEvent::Source::External);
		track->processInEvent(off, TimePos(0), 0);
		Engine::audioEngine()->renderNextPeriod();
	}
};

QTEST_MAIN(MidiInputTransformTest)
#include "MidiInputTransformTest.moc"
