/*
 * ControlMidiReconnectSharedTest.cpp - the helpers midi.reconnect_* report and resolve live MIDI ports with
 *
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

/*! The midi.reconnect_status / clients_list / reconnect_arm / reconnect_set commands share one header of helpers
 *  (src/core/ControlCommandsMidiReconnectShared.h): which engine object a port belongs to, how a remembered
 *  assignment is reported, which assignment holds a name, and how the live port a call names is resolved - by exact
 *  name, or by identity, where none and several are both typed refusals. The dummy MIDI client lists no ports, so
 *  the resolver is driven here with port lists of its own, and the assignments with a MidiReconnect memory fed the
 *  way a subscription feeds it. The commands themselves are run against the engine's real (dummy) client. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlCommandsMidiReconnectShared.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::midireconnectcontrol;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

const QString KeysName = QStringLiteral("20:0 Keystation 49:Keystation 49 MIDI 1");

} // namespace

class ControlMidiReconnectSharedTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theEnginesClientIsFoundAndAnsweredHonestly()
	{
		QVERIFY(liveClient() != nullptr);
		QVERIFY(liveReconnect() != nullptr);
		QVERIFY(&liveClient()->reconnect() == liveReconnect());
		// The dummy client publishes no port-list changes: "none", from the client class's own answer.
		QCOMPARE(noticeName(liveClient()), QStringLiteral("none"));
		QVERIFY(noticeNames().contains(noticeName(liveClient())));
		const ControlResult none = noClient();
		QCOMPARE(none.errorKind, ControlErrorKind::Refused);
		QVERIFY(none.errorMessage.contains(QStringLiteral("no MIDI client")));
	}

	void anAssignmentIsReportedWithItsIdentityAndTheTrackThatHoldsIt()
	{
		Song* song = Engine::getSong();
		auto* track = qobject_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		QVERIFY(track != nullptr);
		MidiPort* port = track->midiPort();
		MidiReconnect memory(liveClient());
		memory.remember(port, true, KeysName);

		QCOMPARE(enginePortId(port), control::trackIdOf(track));
		QCOMPARE(enginePortId(nullptr), QString());

		const QJsonArray all = assignmentsJson(memory);
		QCOMPARE(all.size(), 1);
		const QJsonObject entry = all.at(0).toObject();
		QCOMPARE(entry.value(QStringLiteral("port")).toString(), control::trackIdOf(track));
		QCOMPARE(entry.value(QStringLiteral("direction")).toString(), QStringLiteral("read"));
		QCOMPARE(entry.value(QStringLiteral("identity")).toString(), QStringLiteral("Keystation 49:Keystation 49 MIDI 1"));
		QCOMPARE(entry.value(QStringLiteral("name")).toString(), KeysName);
		QCOMPARE(entry.value(QStringLiteral("address")).toString(), QStringLiteral("20:0"));
		QVERIFY(entry.value(QStringLiteral("live")).toBool());
		QVERIFY(!entry.value(QStringLiteral("lost")).toBool());
		QCOMPARE(entry.value(QStringLiteral("reconnects")).toInt(), 0);
		QCOMPARE(entry.value(QStringLiteral("identity_matches")).toInt(), 1);

		// Which port holds a name, in which direction.
		QCOMPARE(boundPortOf(memory, true, KeysName), control::trackIdOf(track));
		QCOMPARE(boundPortOf(memory, false, KeysName), QString());
		QCOMPARE(boundPortOf(memory, true, QStringLiteral("21:0 Other:Port")), QString());

		// The device goes away: the same assignment is reported lost, not live.
		memory.reconcile({}, {});
		const QJsonObject gone = assignmentsJson(memory).at(0).toObject();
		QVERIFY(!gone.value(QStringLiteral("live")).toBool());
		QVERIFY(gone.value(QStringLiteral("lost")).toBool());

		memory.forgetPort(port);
		delete track;
	}

	void aPortThatBelongsToNoTrackIsNamedByItsOwnDisplayName()
	{
		MidiPort stray(QStringLiteral("Stray controller"), liveClient(), nullptr, nullptr, MidiPort::Mode::Input);
		QCOMPARE(enginePortId(&stray), stray.displayName());
	}

	void theTargetPortIsResolvedByNameOrByIdentity()
	{
		const QStringList live{QStringLiteral("20:0 Alpha:Out"), QStringLiteral("21:0 Beta:Out"), QStringLiteral("22:0 Beta:Out")};
		ControlResult error;

		QCOMPARE(resolveTargetName(live, {{QStringLiteral("name"), QStringLiteral("21:0 Beta:Out")}}, &error), QStringLiteral("21:0 Beta:Out"));

		// A name nothing lists, however plausible, is NotFound and says where to look.
		QCOMPARE(resolveTargetName(live, {{QStringLiteral("name"), QStringLiteral("99:0 Alpha:Out")}}, &error), QString());
		QCOMPARE(error.errorKind, ControlErrorKind::NotFound);
		QVERIFY(error.errorMessage.contains(QStringLiteral("midi.clients_list")));

		// An identity that names exactly one live port resolves to its full current name.
		error = ControlResult();
		QCOMPARE(resolveTargetName(live, {{QStringLiteral("identity"), QStringLiteral("Alpha:Out")}}, &error), QStringLiteral("20:0 Alpha:Out"));

		// Two ports sharing an identity are ambiguous: refused, not guessed.
		QCOMPARE(resolveTargetName(live, {{QStringLiteral("identity"), QStringLiteral("Beta:Out")}}, &error), QString());
		QCOMPARE(error.errorKind, ControlErrorKind::Refused);
		QVERIFY(error.errorMessage.contains(QStringLiteral("2 live ports")));

		// An identity nothing has.
		QCOMPARE(resolveTargetName(live, {{QStringLiteral("identity"), QStringLiteral("Gamma:Out")}}, &error), QString());
		QCOMPARE(error.errorKind, ControlErrorKind::NotFound);

		// Neither argument.
		QCOMPARE(resolveTargetName(live, {}, &error), QString());
		QCOMPARE(error.errorKind, ControlErrorKind::InvalidArgs);
	}

	void theReadCommandsReportTheDummyClientsEmptyMemory()
	{
		const ControlResult status = run(QStringLiteral("midi.reconnect_status"));
		QVERIFY2(status.ok, qPrintable(status.errorMessage));
		QCOMPARE(status.result.value(QStringLiteral("client")).toString(), Engine::audioEngine()->midiClientName());
		QCOMPARE(status.result.value(QStringLiteral("notice")).toString(), QStringLiteral("none"));
		QCOMPARE(status.result.value(QStringLiteral("enabled")).toBool(), liveReconnect()->isEnabled());
		QCOMPARE(status.result.value(QStringLiteral("assignment_count")).toInt(), static_cast<int>(liveReconnect()->assignments().size()));
		QCOMPARE(status.result.value(QStringLiteral("lost_count")).toInt(), liveReconnect()->lostCount());

		const ControlResult clients = run(QStringLiteral("midi.clients_list"));
		QVERIFY2(clients.ok, qPrintable(clients.errorMessage));
		QCOMPARE(clients.result.value(QStringLiteral("count")).toInt(), 0);
		QCOMPARE(clients.result.value(QStringLiteral("client_count")).toInt(), 0);
		QCOMPARE(clients.result.value(QStringLiteral("readable_count")).toInt(), 0);
		QCOMPARE(clients.result.value(QStringLiteral("writable_count")).toInt(), 0);
	}

	void arming_AnAlreadyArmedEngineChangesNothing()
	{
		if (!liveReconnect()->isEnabled()) { QSKIP("this user's config has re-connection off; arming would rewrite it"); }
		const ControlResult armed = run(QStringLiteral("midi.reconnect_arm"));
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QVERIFY(armed.result.value(QStringLiteral("enabled")).toBool());
		QVERIFY(!armed.result.value(QStringLiteral("changed")).toBool());
		QCOMPARE(armed.result.value(QStringLiteral("notice")).toString(), QStringLiteral("none"));
	}

	void bindingARefusesWhatItCannotResolve()
	{
		Song* song = Engine::getSong();
		Track* instrument = Track::create(Track::Type::Instrument, song);
		Track* sample = Track::create(Track::Type::Sample, song);
		const QString inst = control::trackIdOf(instrument);

		QCOMPARE(run(QStringLiteral("midi.reconnect_set"), {{QStringLiteral("port"), QStringLiteral("trk-999999")},
			{QStringLiteral("name"), KeysName}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("midi.reconnect_set"), {{QStringLiteral("port"), control::trackIdOf(sample)},
			{QStringLiteral("name"), KeysName}}).errorKind, ControlErrorKind::Refused);  // a sample track has no MIDI port
		QCOMPARE(run(QStringLiteral("midi.reconnect_set"), {{QStringLiteral("port"), inst}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("midi.reconnect_set"), {{QStringLiteral("port"), inst}, {QStringLiteral("name"), KeysName}}).errorKind,
			ControlErrorKind::NotFound);  // the dummy client lists no such port
		QCOMPARE(run(QStringLiteral("midi.reconnect_set"), {{QStringLiteral("port"), inst}, {QStringLiteral("identity"), QStringLiteral("Keystation 49:Keystation 49 MIDI 1")},
			{QStringLiteral("direction"), QStringLiteral("write")}}).errorKind, ControlErrorKind::NotFound);
		delete instrument;
		delete sample;
	}
};

QTEST_MAIN(ControlMidiReconnectSharedTest)
#include "ControlMidiReconnectSharedTest.moc"
