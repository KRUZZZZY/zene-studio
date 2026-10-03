/*
 * MixerToolsTest.cpp - pan law, swap, sends switched off, returns, finding, ganging, ordering, group colours
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
/*! 0.5.0 L08 (W3): the pan law cuts a centred channel only when chosen (MX-025); swap exchanges the
 *  sides (MX-033); a switched-off send keeps its level, saves only when off and undoes (MX-047); a
 *  return is a new bus fed by a send, one undo step (MX-051); mixer.find matches names (MX-009);
 *  mixer.adjust moves faders together (MX-028); mixer.order_by_tracks follows the tracks (MX-018);
 *  a group's members take its colour (MX-067). */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "MixerOptions.h"
#include "MixerStrip.h"
#include "SampleFrame.h"
#include "Song.h"
#include "VcaGroup.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

MixerChannel* channel(int index) { return Engine::mixer()->mixerChannel(index); }

QString ch(int index) { return control::channelIdOf(channel(index)); }

void fill(int index, float left, float right)
{
	for (int f = 0; f < 8; ++f) { channel(index)->m_buffer[f] = SampleFrame(left, right); }
}

} // namespace

class MixerToolsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		while (Engine::mixer()->numChannels() <= 4) { Engine::mixer()->createChannel(); }
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void thePanLawCutsTheCentreOnlyWhenChosen()
	{
		fill(1, 1.f, 1.f);
		QVERIFY(!mixerstrip::applyPanMode(*channel(1), 8));  // balance: the classic stage, untouched here
		QVERIFY(run(QStringLiteral("mixer.options"), {{QStringLiteral("pan_law"), QStringLiteral("constant_power")}}).ok);
		QVERIFY(mixerstrip::applyPanMode(*channel(1), 8));
		QVERIFY(std::abs(channel(1)->m_buffer[0][0] - 0.7071f) < 1e-3f);
		float left = 0.f;
		float right = 0.f;
		MixerOptions::gains(PanLaw::Linear, 1.f, &left, &right);
		QCOMPARE(left, 0.f);
		QCOMPARE(right, 1.f);
		QVERIFY(MixerOptions::instance().shouldPersist());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(MixerOptions::instance().panLaw(), PanLaw::Balance);
	}

	void swapExchangesTheSides()
	{
		QVERIFY(run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("swap"), true}}).ok);
		fill(1, 1.f, 0.25f);
		mixerstrip::applyOutput(*channel(1), 8);
		QCOMPARE(channel(1)->m_buffer[0][0], 0.25f);
		QCOMPARE(channel(1)->m_buffer[0][1], 1.f);
		QDomDocument doc;
		QDomElement e = doc.createElement(QStringLiteral("mixerchannel"));
		mixerstrip::save(*channel(1), e);
		QCOMPARE(e.attribute(QStringLiteral("swap")), QStringLiteral("1"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!channel(1)->m_swapModel.value());
	}

	void aSendSwitchedOffKeepsItsLevel()
	{
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(2)},
			{QStringLiteral("amount"), 0.5}}).ok);
		QVERIFY(run(QStringLiteral("mixer.send_enable"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(2)},
			{QStringLiteral("enabled"), false}}).ok);
		MixerRoute* route = control::findMixerRoute(1, 2);
		QVERIFY(route != nullptr && !route->enabled());
		QCOMPARE(route->amount()->value(), 0.5f);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(control::findMixerRoute(1, 2)->enabled());
		QCOMPARE(run(QStringLiteral("mixer.send_enable"), {{QStringLiteral("channel"), ch(3)}, {QStringLiteral("to"), ch(4)},
			{QStringLiteral("enabled"), false}}).errorKind, ControlErrorKind::NotFound);
	}

	void aReturnIsABusFedByASend()
	{
		const int before = Engine::mixer()->numChannels();
		const ControlResult made = run(QStringLiteral("mixer.create_return"), {{QStringLiteral("channel"), ch(1)},
			{QStringLiteral("name"), QStringLiteral("Plate")}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		QCOMPARE(Engine::mixer()->numChannels(), before + 1);
		MixerChannel* back = channel(before);
		QVERIFY(back->isBus());
		QCOMPARE(back->m_name, QStringLiteral("Plate"));
		QVERIFY(control::findMixerRoute(1, static_cast<mix_ch_t>(before)) != nullptr);
		const ControlResult found = run(QStringLiteral("mixer.find"), {{QStringLiteral("text"), QStringLiteral("pla")}});
		QCOMPARE(found.result.value(QStringLiteral("channels")).toArray().size(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::mixer()->numChannels(), before);
	}

	void channelsMoveTogether()
	{
		channel(1)->m_volumeModel.setValue(1.f);
		channel(2)->m_volumeModel.setValue(0.5f);
		QVERIFY(run(QStringLiteral("mixer.adjust"), {{QStringLiteral("channels"), QJsonArray{ch(1), ch(2)}},
			{QStringLiteral("volume_db"), -6.0206}, {QStringLiteral("pan"), 0.25}}).ok);
		QVERIFY(std::abs(channel(1)->m_volumeModel.value() - 0.5f) < 1e-3f);
		QVERIFY(std::abs(channel(2)->m_volumeModel.value() - 0.25f) < 1e-3f);
		QCOMPARE(channel(2)->m_panModel.value(), 0.25f);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(channel(2)->m_volumeModel.value(), 0.5f);
	}

	void theMixerFollowsTheTracks()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		while (Engine::mixer()->numChannels() <= 4) { Engine::mixer()->createChannel(); }
		const int third = channel(3)->id();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		track->mixerChannelModel()->setValue(3);
		QVERIFY(run(QStringLiteral("mixer.order_by_tracks")).ok);
		QCOMPARE(channel(1)->id(), third);
		QCOMPARE(track->mixerChannelModel()->value(), 1);  // the track follows its channel
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(channel(3)->id(), third);
	}

	//! MX-085 / MX-059 / MX-137: the strip EQ is skipped while flat and changes the signal when not;
	//! a mono input sums the sides before the inserts; the correlation reads in phase and out of phase.
	void theStripsEqMonoInputAndCorrelation()
	{
		fill(4, 0.5f, 0.5f);
		mixerstrip::applyChannelEq(*channel(4), 8);
		QCOMPARE(channel(4)->m_buffer[3][0], 0.5f);  // flat: untouched
		QVERIFY(run(QStringLiteral("mixer.channel_eq"), {{QStringLiteral("channel"), ch(4)}, {QStringLiteral("low"), 12.0}}).ok);
		fill(4, 0.5f, 0.5f);
		mixerstrip::applyChannelEq(*channel(4), 8);
		QVERIFY(channel(4)->m_buffer[3][0] != 0.5f);  // a DC-ish step through a +12 dB low shelf grows
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(channel(4)->m_eqLowModel.value(), 0.f);
		QVERIFY(run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(4)}, {QStringLiteral("input_mono"), true}}).ok);
		fill(4, 1.f, 0.f);
		mixerstrip::applyInput(*channel(4), 8);
		QCOMPARE(channel(4)->m_buffer[0][1], 0.5f);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		fill(4, 0.5f, 0.5f);
		mixerstrip::updateMeters(*channel(4), channel(4)->m_buffer, 1.f, 8);
		QCOMPARE(channel(4)->m_correlation.get(), 1.f);
		fill(4, 0.5f, -0.5f);
		mixerstrip::updateMeters(*channel(4), channel(4)->m_buffer, 1.f, 8);
		QCOMPARE(channel(4)->m_correlation.get(), -1.f);
	}

	void theRoutingMatrix()
	{
		const QJsonArray channels = run(QStringLiteral("routing.matrix")).result.value(QStringLiteral("channels")).toArray();
		QCOMPARE(channels.size(), Engine::mixer()->numChannels());
		QCOMPARE(channels.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Master"));
	}

	void aGroupColoursItsMembers()
	{
		VcaGroup* group = Engine::mixer()->createVcaGroup(QStringLiteral("Drums"));
		group->addMember(1);
		group->addMember(2);
		QVERIFY(run(QStringLiteral("vca.color_members"), {{QStringLiteral("group"), group->id()}, {QStringLiteral("color"), QStringLiteral("#336699")}}).ok);
		QCOMPARE(channel(2)->color().value_or(QColor()).name(), QStringLiteral("#336699"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!channel(2)->color().has_value());
		QCOMPARE(run(QStringLiteral("vca.color_members"), {{QStringLiteral("group"), group->id()}, {QStringLiteral("color"), QStringLiteral("nope")}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}
};

QTEST_MAIN(MixerToolsTest)
#include "MixerToolsTest.moc"
