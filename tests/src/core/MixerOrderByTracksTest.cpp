/*
 * MixerOrderByTracksTest.cpp - mixer.order_by_tracks: the channels follow their tracks, and an index that names no channel is refused
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
/*! cluster new-p14-29 (BUG 14), on mixer.order_by_tracks (MX-018): the used channels come first in track order, each once,
 *  and the unused ones follow in their own order; a track whose index names no channel is refused before the channel list
 *  is read. The refusal is observed through the LMMS_TESTING seam Mixer::takeRefusedLookupsForTesting(), not through the
 *  memory a read would touch. */

#include <QtTest>

#include <QJsonObject>
#include <QList>

#include <algorithm>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id)
{
	return ControlRegistry::instance()->invoke(id, {});
}

MixerChannel* channel(int index) { return Engine::mixer()->mixerChannel(index); }

//! The channels' persistent ids in mixer order.
QList<int> order()
{
	QList<int> ids;
	for (int i = 0; i < static_cast<int>(Engine::mixer()->numChannels()); ++i) { ids.append(channel(i)->id()); }
	return ids;
}

bool moved(const ControlResult& result) { return result.result.value(QStringLiteral("moved")).toBool(); }

//! Exactly @a count channels. Channels are deleted from the end, which moves none of the others.
void setChannelCount(int count)
{
	while (static_cast<int>(Engine::mixer()->numChannels()) < count) { Engine::mixer()->createChannel(); }
	while (static_cast<int>(Engine::mixer()->numChannels()) > count) { Engine::mixer()->deleteChannel(Engine::mixer()->numChannels() - 1); }
}

//! A new instrument track playing into channel @a index. Its model's range is widened first, as a headless session keeps
//! it, so an index past the last channel can be held.
InstrumentTrack* trackOn(int index)
{
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	track->mixerChannelModel()->setRange(0, static_cast<float>(std::max(index, static_cast<int>(Engine::mixer()->numChannels()))));
	track->mixerChannelModel()->setValue(index);
	return track;
}

} // namespace

class MixerOrderByTracksTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test reads and writes channel state itself
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		setChannelCount(5);
		Engine::mixer()->takeRefusedLookupsForTesting();  // every test starts with no refusals counted
	}

	//! Used channels first, in track order (4 before 2), then the unused ones (1 and 3) in their own order.
	void theUsedChannelsComeFirstInTrackOrder()
	{
		trackOn(4);
		trackOn(2);
		const QList<int> before = order();
		const ControlResult result = run(QStringLiteral("mixer.order_by_tracks"));
		QVERIFY(result.ok);
		QVERIFY(moved(result));
		QVERIFY(order() == QList<int>({before[0], before[4], before[2], before[1], before[3]}));
	}

	//! A channel two tracks use appears once, at the place of its first track.
	void aChannelUsedTwiceAppearsOnce()
	{
		trackOn(3);
		trackOn(3);
		trackOn(1);
		const QList<int> before = order();
		QVERIFY(run(QStringLiteral("mixer.order_by_tracks")).ok);
		QVERIFY(order() == QList<int>({before[0], before[3], before[1], before[2], before[4]}));
	}

	//! A track on the master, and nothing else used, leaves the order as it is.
	void theMasterAndTheUnusedChannelsKeepTheirOrder()
	{
		trackOn(0);
		const QList<int> before = order();
		const ControlResult result = run(QStringLiteral("mixer.order_by_tracks"));
		QVERIFY(result.ok);
		QVERIFY(!moved(result));
		QVERIFY(order() == before);
	}

	//! One undo step puts the order back.
	void undoPutsTheChannelsBack()
	{
		trackOn(4);
		const QList<int> before = order();
		QVERIFY(run(QStringLiteral("mixer.order_by_tracks")).ok);
		QVERIFY(order() != before);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(order() == before);
	}

	//! Once the order follows the tracks, a second run changes nothing.
	void aSecondRunChangesNothing()
	{
		trackOn(4);
		QVERIFY(run(QStringLiteral("mixer.order_by_tracks")).ok);
		const QList<int> settled = order();
		const ControlResult again = run(QStringLiteral("mixer.order_by_tracks"));
		QVERIFY(again.ok);
		QVERIFY(!moved(again));
		QVERIFY(order() == settled);
	}

	//! BUG 14 regression. Tracks at the first index past the last channel and past it again: the command must refuse both
	//! before reading the channel list, leave the order and the tracks alone, and count no lookup that reached the list.
	void anIndexPastTheLastChannelIsRefusedBeforeTheRead()
	{
		const int past = static_cast<int>(Engine::mixer()->numChannels());
		InstrumentTrack* first = trackOn(past);
		InstrumentTrack* far = trackOn(past + 3);
		const QList<int> before = order();
		const ControlResult result = run(QStringLiteral("mixer.order_by_tracks"));
		QVERIFY(result.ok);
		QVERIFY(!moved(result));
		QVERIFY(order() == before);
		QCOMPARE(first->mixerChannelModel()->value(), past);
		QCOMPARE(far->mixerChannelModel()->value(), past + 3);
		QCOMPARE(Engine::mixer()->takeRefusedLookupsForTesting(), 0);  // nothing was looked up past the list
	}

	//! A master-only mixer: every track index but the master's names no channel, so every one is refused.
	void aMasterOnlyMixerRefusesEveryTrackIndex()
	{
		setChannelCount(1);
		trackOn(1);
		trackOn(4);
		const QList<int> before = order();
		const ControlResult result = run(QStringLiteral("mixer.order_by_tracks"));
		QVERIFY(result.ok);
		QVERIFY(!moved(result));
		QVERIFY(order() == before);
		QCOMPARE(Engine::mixer()->takeRefusedLookupsForTesting(), 0);
	}
};

QTEST_MAIN(MixerOrderByTracksTest)

#include "MixerOrderByTracksTest.moc"
