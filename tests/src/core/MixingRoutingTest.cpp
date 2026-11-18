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
 *  (MX-048), and since 0.5.0 W4 follows its sample-exact buffer frame by frame; mixer.send_many makes the same send from several channels as one undo step (MX-050);
 *  a VCA group inside another takes its gain, a loop is refused (MX-070); a channel can sit in two
 *  groups when asked and their gains multiply (MX-070); a group's links choose what reaches its
 *  members and suspend turns them off (MX-071); window.mixer_view answers its modes (MX-049) and, from
 *  0.5.0 theme 6, the return the faders show by name and what every fader drives. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include <algorithm>
#include <vector>

#include "AutomationClip.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerSendPan.h"
#include "Song.h"
#include "Track.h"
#include "VcaGroup.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString ch(int index) { return control::channelIdOf(Engine::mixer()->mixerChannel(index)); }

//! One period of a constant 0.5 on channel 1 through the real render path (mixToChannel ->
//! masterMix, as MixerChannelPanTest renders); the master's output.
std::vector<SampleFrame> renderPeriod()
{
	Mixer* mixer = Engine::mixer();
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> input(fpp, SampleFrame(0.5f, 0.5f));
	std::vector<SampleFrame> out(fpp, SampleFrame(0.f, 0.f));
	SampleFrame* busData[1] = {input.data()};
	const AudioBus bus{busData, 1, fpp};
	mixer->mixToChannel(bus, static_cast<mix_ch_t>(1));
	mixer->prepareMasterMix();
	mixer->masterMix(out.data());
	AutomatableModel::incrementPeriodCounter();  // what the engine does once per period
	return out;
}

float vcaGainOf(int index) { return Engine::mixer()->mixerChannel(index)->vcaGain(); }

QString createGroup(const QString& name)
{
	return run(QStringLiteral("vca.create"), {{QStringLiteral("name"), name}}).result.value(QStringLiteral("id")).toString();
}

QJsonObject transactionFor(const QString& command)
{
	QJsonObject found;
	const QJsonArray records = run(QStringLiteral("control.transactions")).result
		.value(QStringLiteral("transactions")).toArray();
	for (const QJsonValue& value : records)
	{
		const QJsonObject record = value.toObject();
		if (record.value(QStringLiteral("command")).toString() == command) { found = record; }
	}
	return found;
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
		// What the receiver gets. 0.5.0 W4: the send pan is sample-exact, so the block the pan moved
		// in ramps from where it was (centre) to hard left frame by frame - the one-period smoothing
		// the send level and the channel pan already have (MixerChannelPanTest::settledRender). The
		// old expectation, the right side already silent at frame 0 of that block, held only because
		// the pan was applied once per block: the step the review reported (lane14 luna BUG 1).
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		const float sent = 0.8f * Engine::mixer()->mixerChannel(1)->m_volumeModel.value();
		std::vector<SampleFrame> dst(fpp, SampleFrame(0.f, 0.f));
		const std::vector<SampleFrame> src(fpp, SampleFrame(1.f, 1.f));
		addPannedSend(dst.data(), src.data(), *route, *Engine::mixer()->mixerChannel(1), fpp);
		QVERIFY(std::abs(dst[0][0] - sent) < 1e-5f);
		QVERIFY(std::abs(dst[0][1] - sent) < 1e-5f);  // frame 0: still centred
		QVERIFY(std::abs(dst[fpp - 1][1] - sent / static_cast<float>(fpp)) < 1e-5f);  // the last: all but hard left
		QVERIFY(dst[fpp / 2][1] < dst[0][1] && dst[fpp - 1][1] < dst[fpp / 2][1]);
		// The next block, the pan settled: the left side only, scaled by the send - on every frame.
		AutomatableModel::incrementPeriodCounter();  // what the engine does once per period
		std::fill(dst.begin(), dst.end(), SampleFrame(0.f, 0.f));
		addPannedSend(dst.data(), src.data(), *route, *Engine::mixer()->mixerChannel(1), fpp);
		QVERIFY(std::abs(dst[0][0] - sent) < 1e-5f);
		QVERIFY(std::abs(dst[fpp - 1][0] - sent) < 1e-5f);
		QCOMPARE(dst[0][1], 0.f);
		QCOMPARE(dst[fpp - 1][1], 0.f);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(route->pan()->value(), 0.f);
		QCOMPARE(run(QStringLiteral("mixer.send_pan"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("to"), ch(4)},
			{QStringLiteral("pan"), 0.5}}).errorKind, ControlErrorKind::NotFound);  // there is no such send
		run(QStringLiteral("mixer.route_remove"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(3)}});
	}

	/*! A retained route step resolves its channels BY ID when it runs, not by the
	 *  positions they held when it was recorded (SPEC-stable-ids.md slice 2).
	 *
	 *  The write creates ch-1 -> ch-3, then a channel BELOW both is removed
	 *  straight on the mixer (no undo step of its own, so the route step is still
	 *  the one on top). The removal renumbers the channels, so the recorded
	 *  positions 1 and 3 now name ch-1 and a channel that never sent anything:
	 *  a step that kept them would delete (or restore) a route between the wrong
	 *  two channels. The undo must find the route by id and take back THAT one.
	 */
	void aRouteUndoStepFollowsItsChannelsThroughARenumbering()
	{
		Mixer* mixer = Engine::mixer();
		const QString fromId = ch(1);
		const QString toId = ch(3);
		QVERIFY2(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), fromId},
			{QStringLiteral("to"), toId}, {QStringLiteral("amount"), 0.8}}).ok, "send_to must be accepted");
		QVERIFY(control::findMixerRoute(1, 3) != nullptr);
		// Remove channel 2 straight on the mixer: every later channel moves down
		// one and NO undo step is recorded, so the send's step is still the top.
		mixer->deleteChannel(2);
		QVERIFY(control::findMixerRoute(1, 2) != nullptr);  // the same route, at its new position
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		// Gone by id: the retained step resolved ch-1/ch-3 to their live
		// positions (1 and 2) and deleted that route.
		QVERIFY2(control::findMixerRoute(1, 2) == nullptr,
			"the undo did not remove the route it was recorded for");
		mixer->createChannel();  // restore the strip count for the cases below
	}

	/*! 0.5.0 W4 (review lane14 luna BUG 1): a send's pan follows its sample-exact buffer through the
	 *  real render path. The pan moves from hard left back to centre: before, the mixer picked the
	 *  panned path from pan()->value() alone, so a block that ends at centre went out centred from
	 *  frame 0 and the move was one step at the block boundary. Measured on the master, where
	 *  channel 1's own route adds the same amount to every frame. */
	void aMovingSendPanIsFollowedFrameByFrame()
	{
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(3)},
			{QStringLiteral("amount"), 1.0}}).ok);
		MixerRoute* route = control::findMixerRoute(1, 3);
		QVERIFY(route != nullptr);
		const f_cnt_t last = Engine::audioEngine()->framesPerPeriod() - 1;
		route->pan()->setValue(-1.f);
		renderPeriod();  // the move to hard left ramps over this period
		const std::vector<SampleFrame> left = renderPeriod();
		QVERIFY(left[0][1] < left[0][0]);  // the send's right side is gone
		route->pan()->setValue(0.f);
		QVERIFY(sendIsPanned(*route));  // value() is centre, but this period's buffer still moves
		const std::vector<SampleFrame> moving = renderPeriod();
		QVERIFY(!sendIsPanned(*route));  // settled at centre: the inherited branches again
		const std::vector<SampleFrame> centred = renderPeriod();
		QVERIFY(centred[0][1] > left[0][1]);
		QVERIFY(std::abs(moving[0][1] - left[0][1]) < 1e-5f);  // frame 0: still hard left
		const float ramp = static_cast<float>(last) / static_cast<float>(last + 1);
		QVERIFY(std::abs(moving[last][1] - (left[0][1] + (centred[0][1] - left[0][1]) * ramp)) < 1e-5f);
		QVERIFY(moving[last / 2][1] > moving[0][1] && moving[last][1] > moving[last / 2][1]);
		QVERIFY(std::abs(moving[last][0] - left[last][0]) < 1e-5f);  // panning left of centre leaves the left side alone
		run(QStringLiteral("mixer.route_remove"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(3)}});
	}

	void removingThenUndoingSendRestoresItsPanAndEnabledState()
	{
		const QJsonObject ends{{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(3)}};
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(3)},
			{QStringLiteral("amount"), 0.37}}).ok);
		QVERIFY(run(QStringLiteral("mixer.send_pan"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(3)},
			{QStringLiteral("pan"), -0.65}}).ok);
		QVERIFY(run(QStringLiteral("mixer.send_enable"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(3)},
			{QStringLiteral("enabled"), false}}).ok);
		const ControlResult removed = run(QStringLiteral("mixer.route_remove"), ends);
		const QJsonObject record = transactionFor(QStringLiteral("mixer.route_remove"));
		const QJsonObject before = record.value(QStringLiteral("before")).toObject();
		const QJsonObject inverse = record.value(QStringLiteral("inverse")).toObject();
		const QJsonObject inverseArgs = inverse.value(QStringLiteral("args")).toObject();
		const QString inverseOp = inverse.value(QStringLiteral("op")).toString();
		const QString mechanism = record.value(QStringLiteral("mechanism")).toString();
		const ControlResult undone = run(inverseOp, inverseArgs);
		if (!undone.ok) { run(QStringLiteral("control.undo")); }

		MixerRoute* restored = control::findMixerRoute(1, 3);
		const bool restoredExists = restored != nullptr;
		const float restoredPan = restoredExists ? restored->pan()->value() : 0.f;
		const bool restoredEnabled = restoredExists && restored->enabled();
		if (restoredExists) { run(QStringLiteral("mixer.route_remove"), ends); }

		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QVERIFY(restoredExists);
		QCOMPARE(restoredPan, -0.65f);
		QVERIFY(!restoredEnabled);
		QVERIFY(!record.isEmpty());
		QCOMPARE(static_cast<float>(before.value(QStringLiteral("pan")).toDouble()), -0.65f);
		QCOMPARE(before.value(QStringLiteral("enabled")).toBool(), false);
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(inverseOp, QStringLiteral("control.undo"));
		QVERIFY(inverseArgs.isEmpty());
		QVERIFY(mechanism.contains(QStringLiteral("captured send amount")));
		QVERIFY(mechanism.contains(QStringLiteral("pan automation mode")));
		QVERIFY(mechanism.contains(QStringLiteral("pan automation clips")));
	}

	void removingThenUndoingSendReattachesPanAutomation()
	{
		const QJsonObject ends{{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(3)}};
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(3)},
			{QStringLiteral("amount"), 0.42}}).ok);
		MixerRoute* route = control::findMixerRoute(1, 3);
		QVERIFY(route != nullptr);
		auto* track = Track::create(Track::Type::Automation, Engine::getSong());
		auto* automation = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
		QVERIFY(automation != nullptr);
		QVERIFY(automation->addObject(route->pan()));
		automation->setProgressionType(AutomationClip::ProgressionType::Linear);
		automation->putValue(TimePos(0), -0.4f, false);
		automation->putValue(TimePos(192), 0.8f, false);
		route->pan()->setAutomationMode(AutomatableModel::AutomationMode::Read);
		const float automatedAtMidpoint = automation->valueAt(TimePos(96));

		const ControlResult removed = run(QStringLiteral("mixer.route_remove"), ends);
		const ControlResult undone = run(QStringLiteral("control.undo"));
		MixerRoute* restored = control::findMixerRoute(1, 3);
		bool automationAttached = false;
		if (restored != nullptr)
		{
			for (const QPointer<AutomatableModel>& object : automation->objects())
			{
				if (object == restored->pan()) { automationAttached = true; }
			}
		}
		const bool isAutomated = restored != nullptr && restored->pan()->isAutomated();
		const float restoredAutomationValue = automation->valueAt(TimePos(96));
		if (restored != nullptr) { run(QStringLiteral("mixer.route_remove"), ends); }

		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY(restored != nullptr);
		QVERIFY(automationAttached);
		QVERIFY(isAutomated);
		QCOMPARE(restoredAutomationValue, automatedAtMidpoint);
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

	void movingAChannelRefreshesRouteModelNames()
	{
		Mixer* mixer = Engine::mixer();
		MixerRoute* outgoing = mixer->createChannelSend(1, 3, 0.5f);
		MixerRoute* incoming = mixer->createChannelSend(4, 1, 0.5f);
		MixerSidechainRoute* sidechain = mixer->createSidechainSend(1, 2, 0.5f);
		QVERIFY(outgoing != nullptr);
		QVERIFY(incoming != nullptr);
		QVERIFY(sidechain != nullptr);

		mixer->moveChannelLeft(2);

		QCOMPARE(outgoing->senderIndex(), static_cast<mix_ch_t>(2));
		QVERIFY(outgoing->amount()->displayName().contains(QStringLiteral("channel 2 to channel 3")));
		QVERIFY(outgoing->pan()->displayName().contains(QStringLiteral("channel 2 to channel 3")));
		QCOMPARE(incoming->receiverIndex(), static_cast<mix_ch_t>(2));
		QVERIFY(incoming->amount()->displayName().contains(QStringLiteral("channel 4 to channel 2")));
		QVERIFY(incoming->pan()->displayName().contains(QStringLiteral("channel 4 to channel 2")));
		QVERIFY(sidechain->amount()->displayName().contains(QStringLiteral("channel 2 to channel 1")));

		mixer->moveChannelLeft(2);
		mixer->deleteChannelSend(outgoing);
		mixer->deleteChannelSend(incoming);
		mixer->deleteSidechainSend(sidechain);
	}

	void routeChangesMarkTheProjectModified()
	{
		// A route is part of the saved project: creating, changing or removing one must mark the song
		// modified, or closing it without a save skips the prompt and the change is lost.
		Song* song = Engine::getSong();
		QVERIFY(song != nullptr);
		const QJsonObject sendEnds{{QStringLiteral("channel"), ch(2)}, {QStringLiteral("to"), ch(4)}};
		const QJsonObject sidechainEnds{{QStringLiteral("channel"), ch(3)}, {QStringLiteral("to"), ch(1)}};
		QJsonObject sidechainRemove = sidechainEnds;
		sidechainRemove.insert(QStringLiteral("sidechain"), true);
		// Start with neither route present, whatever earlier cases left behind.
		run(QStringLiteral("mixer.route_remove"), sendEnds);
		run(QStringLiteral("mixer.route_remove"), sidechainRemove);

		song->noteProjectSaved(QStringLiteral("route-clean.mmpz"));  // the product's way back to clean
		QVERIFY2(run(QStringLiteral("mixer.route_to"), sendEnds).ok, "mixer.route_to failed");
		QVERIFY2(song->isModified(), "mixer.route_to created a send without marking the song modified");

		song->noteProjectSaved(QStringLiteral("route-clean.mmpz"));  // the product's way back to clean
		QJsonObject auxiliary = sendEnds;
		auxiliary.insert(QStringLiteral("amount"), 0.5);
		QVERIFY2(run(QStringLiteral("mixer.send_to"), auxiliary).ok, "mixer.send_to failed");
		QVERIFY2(song->isModified(), "mixer.send_to changed a send without marking the song modified");

		song->noteProjectSaved(QStringLiteral("route-clean.mmpz"));  // the product's way back to clean
		QVERIFY2(run(QStringLiteral("mixer.route_remove"), sendEnds).ok, "mixer.route_remove failed");
		QVERIFY2(song->isModified(), "mixer.route_remove removed a send without marking the song modified");

		// A refused change writes nothing, so it must not mark the song.
		song->noteProjectSaved(QStringLiteral("route-clean.mmpz"));  // the product's way back to clean
		QVERIFY(!run(QStringLiteral("mixer.route_remove"), sendEnds).ok);
		QVERIFY2(!song->isModified(), "a refused mixer.route_remove marked the song modified");

		song->noteProjectSaved(QStringLiteral("route-clean.mmpz"));  // the product's way back to clean
		QJsonObject sidechain = sidechainEnds;
		sidechain.insert(QStringLiteral("amount"), 0.5);
		QVERIFY2(run(QStringLiteral("mixer.sidechain_to"), sidechain).ok, "mixer.sidechain_to failed");
		QVERIFY2(song->isModified(), "mixer.sidechain_to created a sidechain without marking the song modified");

		song->noteProjectSaved(QStringLiteral("route-clean.mmpz"));  // the product's way back to clean
		QVERIFY2(run(QStringLiteral("mixer.route_remove"), sidechainRemove).ok, "sidechain route_remove failed");
		QVERIFY2(song->isModified(), "mixer.route_remove removed a sidechain without marking the song modified");
	}
};

QTEST_MAIN(MixingRoutingTest)
#include "MixingRoutingTest.moc"
