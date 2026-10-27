/*
 * ProjectStateTest.cpp - mixer snapshots, project settings and metadata, edit locks
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
/*! 0.5.0 L12 (W3): a mixer snapshot recalls all of a mix or only its faders, skips what is gone and
 *  undoes in one step, and round-trips through its project element (MX-170); project.settings and
 *  project.metadata write and undo, and an untouched project writes neither element (WF-043, WF-047,
 *  WF-048); a locked track refuses edits to its clips but not its mute, and unlocking is undoable (WF-044). */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include <cmath>

#include "Clip.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "EditLocks.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "MixerSnapshots.h"
#include "MixerSurround.h"
#include "ProjectInfo.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

MixerChannel* channel(int index) { return Engine::mixer()->mixerChannel(index); }

QString ch(int index) { return control::channelIdOf(channel(index)); }

void verifyLastUndoTransaction(const QString& command)
{
	const QJsonArray records = run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray();
	QVERIFY(!records.isEmpty());
	if (records.isEmpty()) { return; }
	const QJsonObject record = records.last().toObject();
	QCOMPARE(record.value(QStringLiteral("command")).toString(), command);
	QVERIFY(!record.value(QStringLiteral("before")).toObject().isEmpty());
	QCOMPARE(record.value(QStringLiteral("inverse")).toObject().value(QStringLiteral("op")).toString(), QStringLiteral("control.undo"));
	QVERIFY(record.value(QStringLiteral("reversible")).toBool());
}

//! The send from channel 1 to channel 2 (channel 1 also sends to the master).
FloatModel* sendLevel() { return control::findMixerRoute(1, 2)->amount(); }

} // namespace

class ProjectStateTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aSnapshotRecallsTheMix()
	{
		QVERIFY(!MixerSnapshots::instance().shouldPersist());
		channel(1)->m_volumeModel.setValue(0.5f);
		channel(1)->m_muteModel.setValue(false);
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(2)},
			{QStringLiteral("amount"), 0.25}}).ok);
		const ControlResult stored = run(QStringLiteral("mixer.snapshot_store"), {{QStringLiteral("name"), QStringLiteral("Verse")}});
		QVERIFY2(stored.ok, qPrintable(stored.errorMessage));
		verifyLastUndoTransaction(QStringLiteral("mixer.snapshot_store"));
		QVERIFY(MixerSnapshots::instance().shouldPersist());

		channel(1)->m_volumeModel.setValue(1.f);
		channel(1)->m_muteModel.setValue(true);
		sendLevel()->setValue(1.f);
		const ControlResult faders = run(QStringLiteral("mixer.snapshot_recall"), {{QStringLiteral("name"), QStringLiteral("Verse")},
			{QStringLiteral("parts"), QJsonArray{QStringLiteral("volume")}}});
		QVERIFY2(faders.ok, qPrintable(faders.errorMessage));
		verifyLastUndoTransaction(QStringLiteral("mixer.snapshot_recall"));
		QCOMPARE(channel(1)->m_volumeModel.value(), 0.5f);
		QVERIFY(channel(1)->m_muteModel.value());  // not a part this recall wrote
		QCOMPARE(sendLevel()->value(), 1.f);

		QVERIFY(run(QStringLiteral("mixer.snapshot_recall"), {{QStringLiteral("name"), QStringLiteral("Verse")}}).ok);
		QVERIFY(!channel(1)->m_muteModel.value());
		QCOMPARE(sendLevel()->value(), 0.25f);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(channel(1)->m_muteModel.value());
		QCOMPARE(sendLevel()->value(), 1.f);

		QCOMPARE(run(QStringLiteral("mixer.snapshot_recall"), {{QStringLiteral("name"), QStringLiteral("Chorus")}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("mixer.snapshot_recall"), {{QStringLiteral("name"), QStringLiteral("Verse")},
			{QStringLiteral("parts"), QJsonArray{QStringLiteral("eq")}}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void aSnapshotRejectsAnEmptyPartsListWithoutAnUndoStep()
	{
		const QString name = QStringLiteral("Empty parts");
		MixerSnapshots::instance().remove(name);
		QVERIFY(MixerSnapshots::instance().store(MixerSnapshots::capture(name)));
		const int depthBefore = Engine::projectJournal()->undoDepth();
		const bool modifiedBefore = Engine::getSong()->isModified();

		const ControlResult recalled = run(QStringLiteral("mixer.snapshot_recall"), {{QStringLiteral("name"), name},
			{QStringLiteral("parts"), QJsonArray{}}});
		const ControlErrorKind errorKind = recalled.errorKind;
		const int depthAfter = Engine::projectJournal()->undoDepth();
		const bool modifiedAfter = Engine::getSong()->isModified();
		QVERIFY(MixerSnapshots::instance().remove(name));

		QCOMPARE(errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(depthAfter, depthBefore);
		QCOMPARE(modifiedAfter, modifiedBefore);
	}

	void aSnapshotSurvivesTheProjectFile()
	{
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("song"));
		MixerSnapshots::instance().save(doc, root);
		const QDomElement element = root.firstChildElement(QStringLiteral("mixer-snapshots"));
		QVERIFY(!element.isNull());
		const std::size_t channels = MixerSnapshots::instance().find(QStringLiteral("Verse"))->channels.size();
		MixerSnapshots::instance().clear();
		QVERIFY(MixerSnapshots::instance().find(QStringLiteral("Verse")) == nullptr);
		MixerSnapshots::instance().load(element);
		const MixerSnapshot* back = MixerSnapshots::instance().find(QStringLiteral("Verse"));
		QVERIFY(back != nullptr);
		QCOMPARE(back->channels.size(), channels);
		QCOMPARE(back->channels.at(1).volume, 0.5f);
		QCOMPARE(back->channels.at(1).sends.size(), std::size_t{2});  // to the master and to channel 2

		QVERIFY(run(QStringLiteral("mixer.snapshot_remove"), {{QStringLiteral("name"), QStringLiteral("Verse")}}).ok);
		verifyLastUndoTransaction(QStringLiteral("mixer.snapshot_remove"));
		QVERIFY(!MixerSnapshots::instance().shouldPersist());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(MixerSnapshots::instance().find(QStringLiteral("Verse")) != nullptr);
	}

	//! A non-finite snapshot float must load as the same default a missing attribute gets.
	void nonFiniteSnapshotFloatsAreReplaced()
	{
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("mixer-snapshots"));
		doc.appendChild(root);
		QDomElement snapshot = doc.createElement(QStringLiteral("snapshot"));
		snapshot.setAttribute(QStringLiteral("name"), QStringLiteral("Invalid floats"));
		root.appendChild(snapshot);
		QDomElement savedChannel = doc.createElement(QStringLiteral("channel"));
		savedChannel.setAttribute(QStringLiteral("id"), QStringLiteral("123"));
		savedChannel.setAttribute(QStringLiteral("volume"), QStringLiteral("nan"));
		savedChannel.setAttribute(QStringLiteral("pan"), QStringLiteral("inf"));
		snapshot.appendChild(savedChannel);
		QDomElement send = doc.createElement(QStringLiteral("send"));
		send.setAttribute(QStringLiteral("to"), QStringLiteral("456"));
		send.setAttribute(QStringLiteral("amount"), QStringLiteral("nan"));
		send.setAttribute(QStringLiteral("pan"), QStringLiteral("-inf"));
		savedChannel.appendChild(send);

		MixerSnapshots::instance().load(root);

		const MixerSnapshot* loaded = MixerSnapshots::instance().find(QStringLiteral("Invalid floats"));
		QVERIFY(loaded != nullptr);
		QCOMPARE(loaded->channels.size(), std::size_t{1});
		QCOMPARE(loaded->channels.front().volume, 1.f);
		QCOMPARE(loaded->channels.front().pan, 0.f);
		QCOMPARE(loaded->channels.front().sends.size(), std::size_t{1});
		QCOMPARE(loaded->channels.front().sends.front().amount, 1.f);
		QCOMPARE(loaded->channels.front().sends.front().pan, 0.f);
		MixerSnapshots::instance().clear();
	}

	//! A malformed finite-looking value must take the persisted field's default, not QString's 0.
	void malformedSnapshotFloatsUseFallbacks()
	{
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("mixer-snapshots"));
		doc.appendChild(root);
		QDomElement snapshot = doc.createElement(QStringLiteral("snapshot"));
		snapshot.setAttribute(QStringLiteral("name"), QStringLiteral("Malformed floats"));
		root.appendChild(snapshot);
		QDomElement savedChannel = doc.createElement(QStringLiteral("channel"));
		savedChannel.setAttribute(QStringLiteral("id"), QStringLiteral("123"));
		savedChannel.setAttribute(QStringLiteral("volume"), QStringLiteral("not-a-float"));
		savedChannel.setAttribute(QStringLiteral("pan"), QStringLiteral("not-a-float"));
		snapshot.appendChild(savedChannel);
		QDomElement send = doc.createElement(QStringLiteral("send"));
		send.setAttribute(QStringLiteral("to"), QStringLiteral("456"));
		send.setAttribute(QStringLiteral("amount"), QStringLiteral("not-a-float"));
		send.setAttribute(QStringLiteral("pan"), QStringLiteral("not-a-float"));
		savedChannel.appendChild(send);

		MixerSnapshots::instance().load(root);

		const MixerSnapshot* loaded = MixerSnapshots::instance().find(QStringLiteral("Malformed floats"));
		QVERIFY(loaded != nullptr);
		QCOMPARE(loaded->channels.front().volume, 1.f);
		QCOMPARE(loaded->channels.front().pan, 0.f);
		QCOMPARE(loaded->channels.front().sends.front().amount, 1.f);
		QCOMPARE(loaded->channels.front().sends.front().pan, 0.f);
		MixerSnapshots::instance().clear();
	}

	//! Duplicate persisted names collapse to the first definition and remain unique on replacement.
	void duplicateSnapshotNamesKeepTheFirstDefinition()
	{
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("mixer-snapshots"));
		doc.appendChild(root);
		for (float volume : {0.25f, 0.75f})
		{
			QDomElement snapshot = doc.createElement(QStringLiteral("snapshot"));
			snapshot.setAttribute(QStringLiteral("name"), QStringLiteral("Mix"));
			QDomElement savedChannel = doc.createElement(QStringLiteral("channel"));
			savedChannel.setAttribute(QStringLiteral("id"), QStringLiteral("123"));
			savedChannel.setAttribute(QStringLiteral("volume"), volume);
			snapshot.appendChild(savedChannel);
			root.appendChild(snapshot);
		}

		MixerSnapshots::instance().load(root);

		QCOMPARE(MixerSnapshots::instance().snapshots().size(), std::size_t{1});
		const MixerSnapshot* loaded = MixerSnapshots::instance().find(QStringLiteral("Mix"));
		QVERIFY(loaded != nullptr);
		QCOMPARE(loaded->channels.front().volume, 0.25f);

		MixerSnapshot replacement;
		replacement.name = QStringLiteral("Mix");
		MixerChannelState channelState;
		channelState.volume = 0.5f;
		replacement.channels.push_back(channelState);
		QVERIFY(MixerSnapshots::instance().store(replacement));
		QCOMPARE(MixerSnapshots::instance().snapshots().size(), std::size_t{1});
		QCOMPARE(MixerSnapshots::instance().find(QStringLiteral("Mix"))->channels.front().volume, 0.5f);
		MixerSnapshots::instance().clear();
	}

	//! A non-finite downmix coefficient in the project XML must not reach a matrix.
	void nonFiniteDownmixSettingsAreIgnored()
	{
		surround::resetMixer();
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(element);
		QDomElement downmix = doc.createElement(QStringLiteral("surrounddownmix"));
		downmix.setAttribute(QStringLiteral("centre"), QStringLiteral("nan"));
		downmix.setAttribute(QStringLiteral("surround"), QStringLiteral("1e999"));
		element.appendChild(downmix);

		surround::loadMixer(element);  // the reader Mixer::loadSettings() calls for this element

		QCOMPARE(surround::downmixOptions().centre, surround::DownmixOptions{}.centre);
		QCOMPARE(surround::downmixOptions().surround, surround::DownmixOptions{}.surround);
		QVERIFY(std::isfinite(surround::matrix(surround::Format::Surround51, surround::Format::Stereo)[0][2]));
		QVERIFY(std::isfinite(surround::matrix(surround::Format::Surround51, surround::Format::Stereo)[0][4]));
		surround::resetMixer();
	}

	//! A panner value that parses as NaN or infinity in the project XML loads the panner's default, and the
	//! gains of every format stay finite (NaN in the position or spread would make every gain NaN).
	void nonFinitePannerAttributesFallBackToTheDefaults()
	{
		QDomDocument doc;
		QDomElement channelElement = doc.createElement(QStringLiteral("channel"));
		doc.appendChild(channelElement);
		QDomElement panElement = doc.createElement(QStringLiteral("surround"));
		panElement.setAttribute(QStringLiteral("format"), QStringLiteral("5.1"));
		panElement.setAttribute(QStringLiteral("pan"), QStringLiteral("1"));
		panElement.setAttribute(QStringLiteral("x"), QStringLiteral("nan"));
		panElement.setAttribute(QStringLiteral("y"), QStringLiteral("inf"));
		panElement.setAttribute(QStringLiteral("lfe"), QStringLiteral("-inf"));
		panElement.setAttribute(QStringLiteral("spread"), QStringLiteral("nan"));
		channelElement.appendChild(panElement);

		ChannelSurround side;
		side.init(64);
		side.load(channelElement);  // the reader Mixer::loadSettings() calls for each channel

		const surround::PanPosition defaults{};
		QVERIFY(side.pan().enabled);
		QCOMPARE(side.pan().x, defaults.x);
		QCOMPARE(side.pan().y, defaults.y);
		QCOMPARE(side.pan().lfe, defaults.lfe);
		QCOMPARE(side.pan().spread, defaults.spread);
		for (int k = 0; k < surround::FormatCount; ++k)
		{
			for (const float gain : side.gains(static_cast<surround::Format>(k))) { QVERIFY(std::isfinite(gain)); }
		}
	}

	void projectSettingsWriteAndUndo()
	{
		QVERIFY(!ProjectInfo::instance().shouldPersist());
		Song* song = Engine::getSong();
		const int tempo = song->tempoModel().value();
		const ControlResult set = run(QStringLiteral("project.settings"), {{QStringLiteral("tempo"), 97}, {QStringLiteral("numerator"), 3},
			{QStringLiteral("document_type"), QStringLiteral("mastering")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(song->tempoModel().value(), 97);
		QCOMPARE(song->getTimeSigModel().numeratorModel().value(), 3);
		QCOMPARE(set.result.value(QStringLiteral("document_type")).toString(), QStringLiteral("mastering"));
		QVERIFY(ProjectInfo::instance().shouldPersist());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(song->tempoModel().value(), tempo);
		QCOMPARE(song->getTimeSigModel().numeratorModel().value(), 4);
		QCOMPARE(ProjectInfo::instance().documentType(), QStringLiteral("production"));
		QCOMPARE(run(QStringLiteral("project.settings"), {{QStringLiteral("tempo"), 5}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void metadataWritesAndRoundTrips()
	{
		const ControlResult set = run(QStringLiteral("project.metadata"), {{QStringLiteral("title"), QStringLiteral("Night Drive")},
			{QStringLiteral("artist"), QStringLiteral("Zene")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("title")).toString(), QStringLiteral("Night Drive"));
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("song"));
		ProjectInfo::instance().save(doc, root);
		const QDomElement element = root.firstChildElement(QStringLiteral("project-info"));
		QCOMPARE(element.attribute(QStringLiteral("artist")), QStringLiteral("Zene"));
		QVERIFY(!element.hasAttribute(QStringLiteral("type")));  // production is not written
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(ProjectInfo::instance().field(QStringLiteral("title")).isEmpty());
		ProjectInfo::instance().load(element);
		QCOMPARE(ProjectInfo::instance().field(QStringLiteral("title")), QStringLiteral("Night Drive"));
		ProjectInfo::instance().clear();
	}

	void aLockedTrackRefusesEdits()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		Clip* clip = track->createClip(TimePos(0));
		const QString clipId = control::clipIdOf(clip);
		const QString trackId = control::trackIdOf(track);
		QVERIFY(run(QStringLiteral("track.set_locked"), {{QStringLiteral("track"), trackId}}).ok);
		QVERIFY(EditLocks::instance().locks(clip));
		const ControlResult moved = run(QStringLiteral("clip.move"), {{QStringLiteral("clip"), clipId}, {QStringLiteral("position"), 192}});
		QCOMPARE(moved.errorKind, ControlErrorKind::Refused);
		QCOMPARE(clip->startPosition().getTicks(), tick_t{0});
		QVERIFY(run(QStringLiteral("track.set_mute"), {{QStringLiteral("track"), trackId}, {QStringLiteral("muted"), true}}).ok);
		QCOMPARE(run(QStringLiteral("project.locks")).result.value(QStringLiteral("tracks")).toArray().size(), 1);
		QVERIFY(EditLocks::instance().shouldPersist());

		QVERIFY(run(QStringLiteral("track.set_locked"), {{QStringLiteral("track"), trackId}, {QStringLiteral("locked"), false}}).ok);
		QVERIFY(run(QStringLiteral("clip.move"), {{QStringLiteral("clip"), clipId}, {QStringLiteral("position"), 192}}).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the move
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the unlock
		QVERIFY(EditLocks::instance().trackLocked(track->id()));
		EditLocks::instance().clear();
		QVERIFY(!EditLocks::instance().shouldPersist());
	}

	void aLockedClipAloneIsRefused()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		Clip* clip = track->createClip(TimePos(0));
		const QString clipId = control::clipIdOf(clip);
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), clipId}}).ok);
		QCOMPARE(run(QStringLiteral("clip.move"), {{QStringLiteral("clip"), clipId}, {QStringLiteral("position"), 96}}).errorKind,
			ControlErrorKind::Refused);
		QCOMPARE(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), QStringLiteral("clip-999999")}}).errorKind,
			ControlErrorKind::NotFound);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!EditLocks::instance().clipLocked(clip->id()));
	}

	void aLockedClipRefusesTrackRemoval()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		Clip* clip = track->createClip(TimePos(0));
		const QString clipId = control::clipIdOf(clip);
		const QString trackId = control::trackIdOf(track);
		const auto trackCount = Engine::getSong()->tracks().size();
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), clipId}}).ok);

		QCOMPARE(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), trackId}}).errorKind, ControlErrorKind::Refused);
		QCOMPARE(Engine::getSong()->tracks().size(), trackCount);
		QCOMPARE(track->numOfClips(), 1);

		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), clipId}, {QStringLiteral("locked"), false}}).ok);
		QVERIFY(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), trackId}}).ok);
	}

	// Luna cluster 1501: the fader tap point is part of the send a snapshot holds, so a recall puts a
	// send switched to the other side of the fader back where the capture found it - and the tap point
	// survives the project file.
	void aSnapshotRestoresASendsFaderTap()
	{
		MixerRoute* route = control::findMixerRoute(1, 2);
		QVERIFY(route != nullptr);
		route->setPreFader(true);
		QVERIFY(run(QStringLiteral("mixer.snapshot_store"), {{QStringLiteral("name"), QStringLiteral("Tap")}}).ok);

		route->setPreFader(false);
		QVERIFY(!route->preFader());
		const ControlResult recalled = run(QStringLiteral("mixer.snapshot_recall"),
			{{QStringLiteral("name"), QStringLiteral("Tap")}, {QStringLiteral("parts"), QJsonArray{QStringLiteral("sends")}}});
		QVERIFY2(recalled.ok, qPrintable(recalled.errorMessage));
		QVERIFY(route->preFader());  // back to the tap the capture took

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("song"));
		MixerSnapshots::instance().save(doc, root);
		MixerSnapshots::instance().clear();
		QVERIFY(MixerSnapshots::instance().find(QStringLiteral("Tap")) == nullptr);
		MixerSnapshots::instance().load(root.firstChildElement(QStringLiteral("mixer-snapshots")));
		const MixerSnapshot* back = MixerSnapshots::instance().find(QStringLiteral("Tap"));
		QVERIFY(back != nullptr);
		bool sawSend = false;
		const int target = route->receiver()->id();
		for (const MixerChannelState& state : back->channels)
		{
			for (const MixerSendState& send : state.sends)
			{
				if (send.to == target) { sawSend = true; QVERIFY(send.preFader); }
			}
		}
		QVERIFY(sawSend);  // the reloaded snapshot still holds a pre-fader send to channel 2

		QVERIFY(run(QStringLiteral("mixer.snapshot_remove"), {{QStringLiteral("name"), QStringLiteral("Tap")}}).ok);
		route->setPreFader(false);
	}
};

QTEST_MAIN(ProjectStateTest)
#include "ProjectStateTest.moc"
