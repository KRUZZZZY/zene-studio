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
#include "ModulationLayer.h"
#include "MidiEvent.h"
#include "MidiInputTransform.h"
#include "NotePlayHandle.h"
#include "ProjectJournal.h"
#include "RackZones.h"
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

	//! The refusal is reported through an OUTPUT pointer, and nothing in the
	//! header says it may not be null: both error paths wrote through it
	//! unguarded, so a caller that does not want the message crashed on a bad
	//! value instead of being refused.
	void aRefusalIsRefusedWithoutAnErrorPointer()
	{
		MidiInputTransform transform;
		QVERIFY(set(transform, {{QStringLiteral("transpose"), 12}}));
		QVERIFY2(!transform.fromJson({{QStringLiteral("transpose"), 49}}, nullptr),
			"a value out of range is refused with nowhere to report it");
		QVERIFY2(!transform.fromJson({{QStringLiteral("key_low"), 100},
			{QStringLiteral("key_high"), 20}}, nullptr),
			"and so is an inverted key range");
		QVERIFY2(transform.fromJson({{QStringLiteral("transpose"), 3}}, nullptr),
			"a good value is still applied with no error pointer");
		MidiEvent event = noteOn(60, 100);
		QVERIFY(transform.apply(&event));
		QCOMPARE(event.key(), 63);
	}

	//! The attribute a project's track element carries is the WHOLE transform.
	//! load() reset() first and parsed afterwards, ignoring the refusal, so an
	//! attribute that would not parse or would not validate left the track at
	//! the identity: the settings the user had were gone with no word.
	void aMalformedSavedTransformKeepsTheOneInForce()
	{
		QDomDocument doc;
		MidiInputTransform transform;
		QVERIFY(set(transform, {{QStringLiteral("transpose"), 12}}));
		QDomElement track = doc.createElement(QStringLiteral("instrumenttrack"));

		track.setAttribute(QStringLiteral("inputtransform"), QStringLiteral("{\"transpose\": 49}"));
		transform.load(track);  // out of range
		QCOMPARE(transform.toJson().value(QStringLiteral("transpose")).toInt(), 12);

		track.setAttribute(QStringLiteral("inputtransform"), QStringLiteral("{\"transpose\": \"x\"}"));
		transform.load(track);  // not a number at all
		QCOMPARE(transform.toJson().value(QStringLiteral("transpose")).toInt(), 12);

		track.setAttribute(QStringLiteral("inputtransform"), QStringLiteral("{not json at all"));
		transform.load(track);  // not JSON
		QCOMPARE(transform.toJson().value(QStringLiteral("transpose")).toInt(), 12);
		QVERIFY(transform.active());

		// An attribute that IS valid still replaces what was in force.
		track.setAttribute(QStringLiteral("inputtransform"),
			QStringLiteral("{\"transpose\": -3, \"fixed_velocity\": 90}"));
		transform.load(track);
		QCOMPARE(transform.toJson().value(QStringLiteral("transpose")).toInt(), -3);
		QCOMPARE(transform.toJson().value(QStringLiteral("fixed_velocity")).toInt(), 90);
	}

	//! The attribute must be an OBJECT to be a transform. "[]" is valid JSON,
	//! so it passed the parse guard, but document.object() answers an empty
	//! object for an array - the candidate accepted it and the track's
	//! transform was replaced with the identity: what the user had was gone.
	void aValidJsonArrayIsNotATransform()
	{
		QDomDocument doc;
		MidiInputTransform transform;
		QVERIFY(set(transform, {{QStringLiteral("transpose"), 12}}));
		QDomElement track = doc.createElement(QStringLiteral("instrumenttrack"));

		track.setAttribute(QStringLiteral("inputtransform"), QStringLiteral("[]"));
		transform.load(track);  // valid JSON, but an array
		QCOMPARE(transform.toJson().value(QStringLiteral("transpose")).toInt(), 12);

		track.setAttribute(QStringLiteral("inputtransform"), QStringLiteral("\"transpose\""));
		transform.load(track);  // valid JSON, but a string
		QCOMPARE(transform.toJson().value(QStringLiteral("transpose")).toInt(), 12);
		QVERIFY(transform.active());
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

	//! Luna cluster 5643: the probe started from the identity transform, not the track's, and the
	//! real fromJson() result was thrown away - so a partial update that only conflicts with
	//! what the track ALREADY holds was accepted, refused, and still reported as success with an
	//! undo step that restores a state the call never reached.
	void aPartialUpdateThatConflictsWithTheCurrentTransformIsRefused()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		const QString id = control::trackIdOf(track);
		QVERIFY(run(QStringLiteral("track.input_transform_set"), {{QStringLiteral("track"), id},
			{QStringLiteral("key_low"), 60}}).ok);
		const int highBefore = track->inputTransform().toJson().value(QStringLiteral("key_high")).toInt();

		// key_high below the track's CURRENT key_low: the range would hold no key. The identity
		// probe sees 0..40 and accepts it; the track's own state says otherwise.
		const ControlResult refused = run(QStringLiteral("track.input_transform_set"), {{QStringLiteral("track"), id},
			{QStringLiteral("key_high"), 40}});
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		// Nothing changed, and no undo step was pushed for a call that changed nothing.
		QCOMPARE(track->inputTransform().toJson().value(QStringLiteral("key_low")).toInt(), 60);
		QCOMPARE(track->inputTransform().toJson().value(QStringLiteral("key_high")).toInt(), highBefore);
		// The refused call pushed no step of its own, so one undo reaches the step that IS
		// there - the key_low set - rather than the refused call's phantom record.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(track->inputTransform().toJson().value(QStringLiteral("key_low")).toInt(), 0);

		// A value that fits the track's range still goes through.
		QVERIFY(run(QStringLiteral("track.input_transform_set"), {{QStringLiteral("track"), id},
			{QStringLiteral("key_high"), 72}}).ok);
		QCOMPARE(track->inputTransform().toJson().value(QStringLiteral("key_high")).toInt(), 72);
	}

	//! BUG 12 (new-p14-18): a track-only update changes no transform, yet the handler recorded an
	//! undo step and marked the song modified - so the next control.undo was spent on that no-op.
	void aTrackOnlyUpdateChangesNothingAndTakesNoUndoStep()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		QVERIFY(track != nullptr);
		const QString id = control::trackIdOf(track);
		song->noteProjectSaved(QString());  // the project as if saved: a call that changes nothing must leave it so
		const int beforeNoOp = Engine::projectJournal()->undoDepth();
		const ControlResult noOp = run(QStringLiteral("track.input_transform_set"), {{QStringLiteral("track"), id}});
		QVERIFY2(noOp.ok, qPrintable(noOp.errorMessage));
		QVERIFY(!track->inputTransform().active());
		QCOMPARE(Engine::projectJournal()->undoDepth(), beforeNoOp);  // no step for a call that changed nothing
		QVERIFY2(!song->isModified(), "a track-only update dirtied the project");
		QVERIFY(!noOp.result.contains(QStringLiteral("__transaction")));

		// A real edit, then the no-op: the one undo must reach the real edit, not the no-op.
		QVERIFY(run(QStringLiteral("track.input_transform_set"), {{QStringLiteral("track"), id},
			{QStringLiteral("transpose"), -12}}).ok);
		QVERIFY(track->inputTransform().active());
		QVERIFY(run(QStringLiteral("track.input_transform_set"), {{QStringLiteral("track"), id}}).ok);
		QCOMPARE(track->inputTransform().toJson().value(QStringLiteral("transpose")).toInt(), -12);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(!track->inputTransform().active(), "the undo reverted the no-op, not the real edit");
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

	//! A forwarded external event re-enters the owner but must still get the layer track's own transform.
	void nestedLayerAppliesItsOwnInputTransform()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* owner = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		QVERIFY(owner != nullptr);

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		QDomElement layers = doc.createElement(QStringLiteral("layers"));
		QDomElement layerEntry = doc.createElement(QStringLiteral("layer"));
		QDomElement layerTrackState = doc.createElement(QStringLiteral("track"));
		layerTrackState.setAttribute(QStringLiteral("type"), static_cast<int>(Track::Type::Instrument));
		layerTrackState.appendChild(doc.createElement(QStringLiteral("instrumenttrack")));
		layerEntry.appendChild(layerTrackState);
		layers.appendChild(layerEntry);
		root.appendChild(layers);
		owner->layers().loadSettings(root);
		RackZone zone;
		zone.chain = 1;
		QVERIFY(owner->layers().zones().addZone(zone) >= 0);
		InstrumentTrack* layer = owner->layers().layer(1);
		QVERIFY(layer != nullptr);
		QVERIFY(set(owner->inputTransform(), {{QStringLiteral("transpose"), 1}}));
		QVERIFY(set(layer->inputTransform(), {{QStringLiteral("transpose"), 2}}));

		MidiEvent on = noteOn(60, 100);
		on.setSource(MidiEvent::Source::External);
		owner->processInEvent(on, TimePos(0), 0);
		QVERIFY(layer->playingNote(63) != nullptr);
		QVERIFY(layer->playingNote(61) == nullptr);

		MidiEvent off = noteOff(60);
		off.setSource(MidiEvent::Source::External);
		owner->processInEvent(off, TimePos(0), 0);
		Engine::audioEngine()->renderNextPeriod();
		QVERIFY(layer->playingNote(63) == nullptr);
	}

	void aStrayNoteOffDoesNotReleaseAnOverlappingVoice()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		QVERIFY(track != nullptr);
		QVERIFY(track->loadInstrument(QStringLiteral("missing_test_instrument")) != nullptr);

		auto& modulation = song->modulationLayer();
		modulation.edit([trackId = track->id()](ModulationLayer&, ModulationRuntime& runtime) {
			runtime = ModulationRuntime{};
			runtime.sourceCount = 1;
			runtime.sources[0].triggerTrack = trackId;
			runtime.sources[0].perVoice = true;
			return true;
		});

		track->processOutEvent(noteOff(60));
		track->processOutEvent(noteOn(60, 100));
		track->processOutEvent(noteOn(60, 100));
		QCOMPARE(modulation.voices().activeCount(), 1);
		track->processOutEvent(noteOff(60));
		QCOMPARE(modulation.voices().activeCount(), 1);
		track->processOutEvent(noteOff(60));
		QCOMPARE(modulation.voices().activeCount(), 0);

		modulation.edit([](ModulationLayer& layer, ModulationRuntime& runtime) {
			layer.clear();
			runtime = ModulationRuntime{};
			return true;
		});
	}
};

QTEST_MAIN(MidiInputTransformTest)
#include "MidiInputTransformTest.moc"
