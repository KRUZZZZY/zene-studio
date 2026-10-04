/*
 * ClipJoinRippleTest.cpp - joining MIDI clips, ripple on move and on insert, MIDI mappings made over the socket
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

/*! 0.5.0 W2 gaps found by the row audit (research/reviews/audit-rows-1.md): clip.join glues MIDI
 *  clips (ED-031), ripple moves what follows a moved or repeated clip (ED-043), and controller.map /
 *  unmap make and remove a MIDI mapping without learn (WF-124) - each one undo step. */

#include <QtTest>

#include <QJsonArray>

#include "ArrangementEdits.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControllerConnection.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiController.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QList<int> startsOf(const Track* track)
{
	QList<int> out;
	for (const Clip* clip : track->getClips()) { out << clip->startPosition().getTicks(); }
	std::sort(out.begin(), out.end());
	return out;
}

} // namespace

class ClipJoinRippleTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;

	MidiClip* clipAt(int position, int length, int key)
	{
		auto* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(position)));
		clip->addNote(Note(TimePos(12), TimePos(0), key), false);
		clip->setAutoResize(false);
		clip->changeLength(TimePos(length));
		return clip;
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	}

	void cleanup() { run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("off")}}); }

	void joiningGluesTheNotesIntoTheFirstClip()
	{
		MidiClip* a = clipAt(0, 96, 60);
		MidiClip* b = clipAt(192, 96, 64);
		const ControlResult joined = run(QStringLiteral("clip.join"), {{QStringLiteral("clips"),
			QJsonArray{control::clipIdOf(b), control::clipIdOf(a)}}});
		QVERIFY2(joined.ok, qPrintable(joined.errorMessage));
		QCOMPARE(m_track->getClips().size(), std::size_t{1});
		QCOMPARE(static_cast<int>(a->length().getTicks()), 288);
		QCOMPARE(a->notes().size(), std::size_t{2});
		QCOMPARE(static_cast<int>(a->notes().back()->pos().getTicks()), 192);  // at its song position
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_track->getClips().size(), std::size_t{2});
		QCOMPARE(run(QStringLiteral("clip.join"), {{QStringLiteral("clips"), QJsonArray{control::clipIdOf(m_track->getClips().front())}}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void rippleCarriesWhatFollowsAMove()
	{
		MidiClip* a = clipAt(0, 96, 60);
		clipAt(96, 96, 62);
		clipAt(192, 96, 64);
		QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("track")}}).ok);
		QVERIFY(run(QStringLiteral("clip.move"), {{QStringLiteral("clip"), control::clipIdOf(a)}, {QStringLiteral("position"), 48}}).ok);
		QCOMPARE(startsOf(m_track), (QList<int>{48, 144, 240}));  // the two after it moved by the same 48
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(startsOf(m_track), (QList<int>{0, 96, 192}));
		// Overlap free: the move lands on the next clip, which the default (trim) would cut.
		QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("off")}, {QStringLiteral("overlap"), QStringLiteral("free")}}).ok);
		QVERIFY(run(QStringLiteral("clip.move"), {{QStringLiteral("clip"), control::clipIdOf(m_track->getClips().front())},
			{QStringLiteral("position"), 24}}).ok);
		QVERIFY(startsOf(m_track).contains(96) && startsOf(m_track).contains(192));  // without ripple nothing else moves
	}

	void rippleMakesRoomForRepeats()
	{
		MidiClip* a = clipAt(0, 96, 60);
		clipAt(96, 96, 62);
		QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("track")}}).ok);
		QVERIFY(run(QStringLiteral("clip.repeat"), {{QStringLiteral("clip"), control::clipIdOf(a)}, {QStringLiteral("count"), 2}}).ok);
		QCOMPARE(startsOf(m_track), (QList<int>{0, 96, 192, 288}));  // the follower moved 192 later
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(startsOf(m_track), (QList<int>{0, 96}));
	}

	void aMappingIsMadeAndRemovedOverTheSocket()
	{
		// The track's volume, by the name the mapping list shows (instrument and effect parameters can
		// also be given the automation way, track + parameter).
		const QString target = m_track->volumeModel()->fullDisplayName();
		const ControlResult mapped = run(QStringLiteral("controller.map"), {{QStringLiteral("target"), target},
			{QStringLiteral("channel"), 2}, {QStringLiteral("controller"), 7}});
		QVERIFY2(mapped.ok, qPrintable(mapped.errorMessage));
		const ControllerConnection* connection = m_track->volumeModel()->controllerConnection();
		QVERIFY(connection != nullptr);
		auto* midi = dynamic_cast<MidiController*>(const_cast<ControllerConnection*>(connection)->getController());
		QVERIFY(midi != nullptr);
		QCOMPARE(midi->midiPort().inputChannel(), 2);
		QCOMPARE(midi->midiPort().inputController(), 7);
		const QJsonArray controls = run(QStringLiteral("controller.surface_state")).result.value(QStringLiteral("controls")).toArray();
		QCOMPARE(controls.size(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(m_track->volumeModel()->controllerConnection() == nullptr);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY(m_track->volumeModel()->controllerConnection() != nullptr);
		const QString name = controls.first().toObject().value(QStringLiteral("control")).toString();
		QVERIFY(run(QStringLiteral("controller.unmap"), {{QStringLiteral("target"), name}}).ok);
		QVERIFY(m_track->volumeModel()->controllerConnection() == nullptr);
		QCOMPARE(run(QStringLiteral("controller.unmap"), {{QStringLiteral("target"), name}}).errorKind, ControlErrorKind::NotFound);
	}
};

QTEST_MAIN(ClipJoinRippleTest)
#include "ClipJoinRippleTest.moc"
