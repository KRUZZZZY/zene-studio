/*
 * MixingRoutingTest.cpp - send pan, sends for many channels, nested and linked VCA groups, multiple membership
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

/*! 0.5.0 L09: a send's own pan balances what reaches the receiver and is saved only when set
 *  (MX-048); mixer.send_many makes the same send from several channels as one undo step (MX-050);
 *  a VCA group inside another takes its gain, a loop is refused (MX-070); a channel can sit in two
 *  groups when asked and their gains multiply (MX-070); a group's links choose what reaches its
 *  members and suspend turns them off (MX-071); window.mixer_view answers its modes (MX-049) and, from
 *  0.5.0 theme 6, the return the faders show by name and what every fader drives. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerSendPan.h"
#include "VcaGroup.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString ch(int index) { return control::channelIdOf(Engine::mixer()->mixerChannel(index)); }

float vcaGainOf(int index) { return Engine::mixer()->mixerChannel(index)->vcaGain(); }

QString createGroup(const QString& name)
{
	return run(QStringLiteral("vca.create"), {{QStringLiteral("name"), name}}).result.value(QStringLiteral("id")).toString();
}

} // namespace

class MixingRoutingTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test reads and writes channel buffers itself
		Mixer* mixer = Engine::mixer();
		while (mixer->numChannels() <= 4) { mixer->createChannel(); }
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init() { Engine::mixer()->clearVcaGroups(); }

	void aSendHasItsOwnPan()
	{
		float left = 0.f;
		float right = 0.f;
		balanceGains(0.5f, &left, &right);
		QCOMPARE(left, 0.5f);
		QCOMPARE(right, 1.f);
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(3)},
			{QStringLiteral("amount"), 0.8}}).ok);
		const ControlResult panned = run(QStringLiteral("mixer.send_pan"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(3)},
			{QStringLiteral("pan"), -1.0}});
		QVERIFY2(panned.ok, qPrintable(panned.errorMessage));
		MixerRoute* route = control::findMixerRoute(1, 3);  // not front(): every channel already sends to the master
		QVERIFY(route != nullptr);
		QCOMPARE(route->pan()->value(), -1.f);
		// What the receiver gets: the left side only, scaled by the send.
		std::vector<SampleFrame> dst(4, SampleFrame(0.f, 0.f));
		const std::vector<SampleFrame> src(4, SampleFrame(1.f, 1.f));
		addPannedSend(dst.data(), src.data(), *route, *Engine::mixer()->mixerChannel(1), 4);
		QVERIFY(std::abs(dst[0][0] - 0.8f * Engine::mixer()->mixerChannel(1)->m_volumeModel.value()) < 1e-5f);
		QCOMPARE(dst[0][1], 0.f);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(route->pan()->value(), 0.f);
		QCOMPARE(run(QStringLiteral("mixer.send_pan"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("to"), ch(4)},
			{QStringLiteral("pan"), 0.5}}).errorKind, ControlErrorKind::NotFound);  // there is no such send
		run(QStringLiteral("mixer.route_remove"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(3)}});
	}

	void sendsFromManyChannelsAreOneStep()
	{
		// Counted from what each channel already sends (the master route, any a test left).
		const std::size_t one = Engine::mixer()->mixerChannel(1)->m_sends.size();
		const std::size_t two = Engine::mixer()->mixerChannel(2)->m_sends.size();
		const ControlResult made = run(QStringLiteral("mixer.send_many"), {{QStringLiteral("channels"), QJsonArray{ch(1), ch(2)}},
			{QStringLiteral("to"), ch(4)}, {QStringLiteral("amount"), 0.5}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		QCOMPARE(made.result.value(QStringLiteral("made")).toInt(), 2);
		QCOMPARE(Engine::mixer()->mixerChannel(1)->m_sends.size(), one + 1);
		QCOMPARE(Engine::mixer()->mixerChannel(2)->m_sends.size(), two + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one undo takes both away
		QCOMPARE(Engine::mixer()->mixerChannel(1)->m_sends.size(), one);
		QCOMPARE(Engine::mixer()->mixerChannel(2)->m_sends.size(), two);
		QCOMPARE(run(QStringLiteral("mixer.send_many"), {{QStringLiteral("channels"), QJsonArray{ch(1), QStringLiteral("ch-9999")}},
			{QStringLiteral("to"), ch(4)}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(Engine::mixer()->mixerChannel(1)->m_sends.size(), one);  // nothing written
	}

	void nestedGroupsMultiplyAndRefuseLoops()
	{
		const QString outer = createGroup(QStringLiteral("Outer"));
		const QString inner = createGroup(QStringLiteral("Inner"));
		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), inner}, {QStringLiteral("channel"), ch(1)}}).ok);
		QVERIFY(run(QStringLiteral("vca.set_gain"), {{QStringLiteral("group"), inner}, {QStringLiteral("gain"), 0.5}}).ok);
		QVERIFY(run(QStringLiteral("vca.set_gain"), {{QStringLiteral("group"), outer}, {QStringLiteral("gain"), 0.5}}).ok);
		QVERIFY(std::abs(vcaGainOf(1) - 0.5f) < 1e-5f);
		QVERIFY(run(QStringLiteral("vca.set_parent"), {{QStringLiteral("group"), inner}, {QStringLiteral("parent"), outer}}).ok);
		QVERIFY(std::abs(vcaGainOf(1) - 0.25f) < 1e-5f);  // the outer group scales the inner one's members
		QCOMPARE(run(QStringLiteral("vca.set_parent"), {{QStringLiteral("group"), outer}, {QStringLiteral("parent"), inner}}).errorKind,
			ControlErrorKind::Refused);  // a loop
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(std::abs(vcaGainOf(1) - 0.5f) < 1e-5f);
	}

	void aChannelInTwoGroupsWhenAsked()
	{
		const QString a = createGroup(QStringLiteral("A"));
		const QString b = createGroup(QStringLiteral("B"));
		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), a}, {QStringLiteral("channel"), ch(2)}}).ok);
		QCOMPARE(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), b}, {QStringLiteral("channel"), ch(2)}}).errorKind,
			ControlErrorKind::Refused);  // the default stays one group
		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), b}, {QStringLiteral("channel"), ch(2)},
			{QStringLiteral("also"), true}}).ok);
		QVERIFY(run(QStringLiteral("vca.set_gain"), {{QStringLiteral("group"), a}, {QStringLiteral("gain"), 0.5}}).ok);
		QVERIFY(run(QStringLiteral("vca.set_gain"), {{QStringLiteral("group"), b}, {QStringLiteral("gain"), 0.4}}).ok);
		QVERIFY(std::abs(vcaGainOf(2) - 0.2f) < 1e-5f);
	}

	void linksChooseWhatReachesTheMembers()
	{
		const QString group = createGroup(QStringLiteral("Links"));
		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), group}, {QStringLiteral("channel"), ch(3)}}).ok);
		QVERIFY(run(QStringLiteral("vca.set_gain"), {{QStringLiteral("group"), group}, {QStringLiteral("gain"), 0.5}}).ok);
		QVERIFY(run(QStringLiteral("vca.set_links"), {{QStringLiteral("group"), group}, {QStringLiteral("gain"), false}}).ok);
		QCOMPARE(vcaGainOf(3), 1.f);  // the level link is off
		QVERIFY(run(QStringLiteral("vca.set_links"), {{QStringLiteral("group"), group}, {QStringLiteral("gain"), true},
			{QStringLiteral("suspended"), true}}).ok);
		QCOMPARE(vcaGainOf(3), 1.f);  // suspended: nothing reaches the members
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(vcaGainOf(3), 1.f);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(std::abs(vcaGainOf(3) - 0.5f) < 1e-5f);

		// The links and the nesting are in the project file only when they differ from the default.
		VcaGroup* saved = Engine::mixer()->vcaGroups().back();
		saved->setLinks(true, false, true);
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		doc.appendChild(root);  // in the document, so its text holds what was saved
		const QDomElement element = Engine::mixer()->saveState(doc, root);
		QVERIFY(doc.toString().contains(QStringLiteral("links=\"gs\"")));
		saved->setLinks(true, true, true);
		Engine::mixer()->restoreState(element);
		QVERIFY(!Engine::mixer()->vcaGroups().back()->linksMute());
	}

	void theMixerViewModesAnswer()
	{
		const ControlResult state = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("sends_on_faders"), ch(4)}});
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		QCOMPARE(state.result.value(QStringLiteral("sends_on_faders")).toString(), ch(4));
		QCOMPARE(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("sends_on_faders"), QString()}}).result
			.value(QStringLiteral("sends_on_faders")).toString(), QString());
		// L08: the view options (MX-003 / MX-004 / MX-005 / MX-127 / MX-128), kept with the interface.
		const ControlResult options = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("filter"), QStringLiteral("returns")},
			{QStringLiteral("strip_width"), QStringLiteral("narrow")}, {QStringLiteral("show_pan"), false},
			{QStringLiteral("meter_mode"), QStringLiteral("rms")}});
		QVERIFY2(options.ok, qPrintable(options.errorMessage));
		QCOMPARE(options.result.value(QStringLiteral("filter")).toString(), QStringLiteral("returns"));
		QCOMPARE(options.result.value(QStringLiteral("show_pan")).toBool(true), false);
		QCOMPARE(run(QStringLiteral("window.mixer_view")).result.value(QStringLiteral("strip_width")).toString(), QStringLiteral("narrow"));
		QCOMPARE(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("meter_scale"), QStringLiteral("db99")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("filter"), QStringLiteral("all")},
			{QStringLiteral("strip_width"), QStringLiteral("normal")}, {QStringLiteral("show_pan"), true},
			{QStringLiteral("meter_mode"), QStringLiteral("peak")}}).ok);
	}

	//! 0.5.0 theme 6: the answer names the return the faders show and what every fader drives - a channel
	//! without that send keeps a "No send" fader; the return's own and the master's stay their level.
	void theSendsOnFadersAnswerNamesEveryFader()
	{
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(4)},
			{QStringLiteral("amount"), 0.5}}).ok);
		const ControlResult on = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("sends_on_faders"), ch(4)}});
		QVERIFY2(on.ok, qPrintable(on.errorMessage));
		QCOMPARE(on.result.value(QStringLiteral("sends_on_faders_name")).toString(), Engine::mixer()->mixerChannel(4)->m_name);
		QHash<QString, QString> drives;
		for (const QJsonValue fader : on.result.value(QStringLiteral("faders")).toArray())
		{
			drives.insert(fader[QStringLiteral("channel")].toString(), fader[QStringLiteral("drives")].toString());
		}
		QCOMPARE(drives.size(), static_cast<int>(Engine::mixer()->numChannels()));  // every fader, none collapsed
		QCOMPARE(drives.value(ch(0)), QStringLiteral("volume"));
		QCOMPARE(drives.value(ch(1)), QStringLiteral("send"));
		QCOMPARE(drives.value(ch(2)), QStringLiteral("no_send"));
		QCOMPARE(drives.value(ch(4)), QStringLiteral("volume"));
		const ControlResult off = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("sends_on_faders"), QString()}});
		QCOMPARE(off.result.value(QStringLiteral("sends_on_faders_name")).toString(), QString());
		for (const QJsonValue fader : off.result.value(QStringLiteral("faders")).toArray())
		{
			QCOMPARE(fader[QStringLiteral("drives")].toString(), QStringLiteral("volume"));
		}
		run(QStringLiteral("mixer.route_remove"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(4)}});
	}
};

QTEST_MAIN(MixingRoutingTest)
#include "MixingRoutingTest.moc"
