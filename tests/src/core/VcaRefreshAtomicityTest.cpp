/*
 * VcaRefreshAtomicityTest.cpp - a grouped channel never publishes a transient gain
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

//! Mixer::refreshGroups is called from the control thread whenever a group's
//! fader, membership or the channel list changes, while the audio thread reads
//! each channel's published VCA gain once per period (Mixer.cpp, doProcessing).
//! It used to reset EVERY channel to unity and then re-apply the group gains,
//! so between the two stores the audio thread could read 1.0 for a channel
//! whose VCA sits at -12 dB - a period played at the wrong level.
//!
//! The reader here plays the audio thread: it spins on the member's published
//! gain while the control thread refreshes. The member is the LAST channel of a
//! wide mixer, so the old reset pass kept it at unity for as long as possible;
//! against the old code this reader sees the transient within a few thousand
//! refreshes. The pass criterion is exact: no reading other than the group's
//! gain, ever. VcaGroupTest carries the semantics; this file carries only the
//! publication's atomicity.

#include <QtTest>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Mixer.h"
#include "VcaGroup.h"

using namespace lmms;

class VcaRefreshAtomicityTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
		QVERIFY(Engine::mixer() != nullptr);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aGroupedChannelNeverReadsATransientGain()
	{
		Mixer* mixer = Engine::mixer();
		constexpr int kChannels = 64;
		while (mixer->numChannels() < kChannels) { mixer->createChannel(); }
		const auto member = static_cast<mix_ch_t>(kChannels - 1);

		VcaGroup* group = mixer->createVcaGroup(QStringLiteral("Atomicity"));
		QVERIFY(group != nullptr);
		QVERIFY(group->addMember(member));
		group->vcaModel()->setValue(0.25f);
		const float expected = group->gain();
		QVERIFY2(expected != 1.0f, "the group must sit away from unity for the check to mean anything");
		QCOMPARE(mixer->mixerChannel(member)->vcaGain(), expected);

		std::atomic<bool> stop{false};
		std::atomic<bool> readerStarted{false};
		std::atomic<std::uint64_t> reads{0};
		std::atomic<std::uint64_t> wrong{0};
		std::thread reader([&] {
			const MixerChannel* channel = mixer->mixerChannel(member);
			std::uint64_t r = 0;
			std::uint64_t w = 0;
			readerStarted.store(true, std::memory_order_release);
			while (!stop.load(std::memory_order_relaxed))
			{
				if (channel->vcaGain() != expected) { ++w; }
				++r;
			}
			reads.store(r);
			wrong.store(w);
		});
		while (!readerStarted.load(std::memory_order_acquire)) { std::this_thread::yield(); }

		constexpr int kRefreshes = 200000;
		bool commandRoundTrip = false;
		bool soloInverseRestored = false;
		for (int i = 0; i < kRefreshes; ++i)
		{
			mixer->refreshGroups();
			if (i != kRefreshes / 2) { continue; }

			const QString firstId = QStringLiteral("vca-%1").arg(group->id());
			const QString channelOneId = QStringLiteral("ch-%1").arg(mixer->mixerChannel(1)->id());
			const ControlResult assigned = ControlRegistry::instance()->invoke(
				QStringLiteral("vca.assign"), {{QStringLiteral("group"), firstId},
					{QStringLiteral("channel"), channelOneId}});
			const ControlResult undone = assigned.ok
				? ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{})
				: ControlResult::failure(ControlErrorKind::Refused, assigned.errorMessage);
			commandRoundTrip = assigned.ok && undone.ok
				&& mixer->mixerChannel(member)->vcaGain() == expected;

			const ControlResult secondGroup = ControlRegistry::instance()->invoke(
				QStringLiteral("vca.create"), {{QStringLiteral("name"), QStringLiteral("Atomic solo")}});
			const QString secondId = secondGroup.result.value(QStringLiteral("group")).toString();
			const QString channelTwoId = QStringLiteral("ch-%1").arg(mixer->mixerChannel(2)->id());
			const ControlResult secondAssigned = secondGroup.ok
				? ControlRegistry::instance()->invoke(QStringLiteral("vca.assign"),
					{{QStringLiteral("group"), secondId}, {QStringLiteral("channel"), channelTwoId}})
				: ControlResult::failure(ControlErrorKind::Refused, secondGroup.errorMessage);
			const ControlResult firstSolo = secondAssigned.ok
				? ControlRegistry::instance()->invoke(QStringLiteral("vca.set_solo"),
					{{QStringLiteral("group"), firstId}, {QStringLiteral("soloed"), true}})
				: ControlResult::failure(ControlErrorKind::Refused, secondAssigned.errorMessage);
			const ControlResult secondSolo = firstSolo.ok
				? ControlRegistry::instance()->invoke(QStringLiteral("vca.set_solo"),
					{{QStringLiteral("group"), secondId}, {QStringLiteral("soloed"), true}})
				: ControlResult::failure(ControlErrorKind::Refused, firstSolo.errorMessage);
			QJsonObject inverse;
			if (secondSolo.ok)
			{
				const QJsonArray transactions = ControlRegistry::instance()->invoke(
					QStringLiteral("control.transactions"), QJsonObject{})
					.result.value(QStringLiteral("transactions")).toArray();
				if (!transactions.isEmpty())
				{
					inverse = transactions.last().toObject().value(QStringLiteral("inverse")).toObject();
				}
			}
			const ControlResult inverseResult = !inverse.isEmpty()
				? ControlRegistry::instance()->invoke(inverse.value(QStringLiteral("op")).toString(),
					inverse.value(QStringLiteral("args")).toObject())
				: ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("solo inverse missing"));
			const ControlResult unsolo = inverseResult.ok
				? ControlRegistry::instance()->invoke(QStringLiteral("vca.set_solo"),
					{{QStringLiteral("group"), firstId}, {QStringLiteral("soloed"), false}})
				: ControlResult::failure(ControlErrorKind::Refused, inverseResult.errorMessage);
			soloInverseRestored = secondAssigned.ok && firstSolo.ok && secondSolo.ok
				&& inverseResult.ok && unsolo.ok
				&& mixer->mixerChannel(member)->vcaGain() == expected;
		}
		stop.store(true);
		reader.join();

		std::printf("EVIDENCE refreshes=%d reads=%llu wrong_readings=%llu assignment_undo=%d "
			"solo_inverse=%d\n", kRefreshes, static_cast<unsigned long long>(reads.load()),
			static_cast<unsigned long long>(wrong.load()), commandRoundTrip ? 1 : 0,
			soloInverseRestored ? 1 : 0);
		QVERIFY2(reads.load() > 0, "the reader never ran");
		QCOMPARE(wrong.load(), std::uint64_t{0});
		QVERIFY2(commandRoundTrip, "assignment and undo changed the observed member gain");
		QVERIFY2(soloInverseRestored, "solo inverse and restoration changed the observed member gain");

		mixer->clearVcaGroups();
		QCOMPARE(mixer->mixerChannel(member)->vcaGain(), 1.0f);
	}
};

QTEST_GUILESS_MAIN(VcaRefreshAtomicityTest)
#include "VcaRefreshAtomicityTest.moc"
