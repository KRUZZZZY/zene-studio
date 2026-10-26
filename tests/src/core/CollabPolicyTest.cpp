/*
 * CollabPolicyTest.cpp - the collaboration rules, the invite format and the
 *                        registry tap (0.5.0 F6, WF-050/052)
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

#include "lmmsconfig.h"

#include <QtTest>

#include "CollabCommandTap.h"
#include "CollabPolicy.h"
#include "ControlRegistry.h"

#ifdef ZENE_HAVE_COLLAB
#include <memory>

#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include "CollabLink.h"
#include "CollabInvite.h"
#include "../../../src/core/CollabSessionAddress.h"
#include "PortMappingCodec.h"
#include "lmms_export.h"

// The deterministic invite test seeds a router result without depending on the host's real network.
#define private public
#include "CollabSession.h"
#include "PortMapping.h"
#undef private
#endif

using namespace lmms;
using namespace lmms::collab;

namespace
{

ControlCommand fakeCommand(const QString& id, bool mutating, const QStringList& needs = {})
{
	ControlCommand command;
	command.id = id;
	command.mutating = mutating;
	command.requiresDecl = needs;
	return command;
}

const QJsonObject NoArgs;

} // namespace

class CollabPolicyTest : public QObject
{
	Q_OBJECT
private slots:
	void cleanup() { CommandTap::clear(); }

	//! Where a command invoked on a joined peer goes, per role.
	void peerRouting()
	{
		QCOMPARE(peerRoute(QStringLiteral("note.add"), true, NoArgs, CollabRole::Editor).route, PeerRoute::Forward);
		QCOMPARE(peerRoute(QStringLiteral("note.add"), true, NoArgs, CollabRole::Viewer).route, PeerRoute::Refuse);
		QCOMPARE(peerRoute(QStringLiteral("transport.get_state"), false, NoArgs, CollabRole::Editor).route, PeerRoute::Local);
		QCOMPARE(peerRoute(QStringLiteral("control.undo"), false, NoArgs, CollabRole::Editor).route, PeerRoute::Forward);
		QCOMPARE(peerRoute(QStringLiteral("control.undo"), false, NoArgs, CollabRole::Viewer).route, PeerRoute::Refuse);
		QCOMPARE(peerRoute(QStringLiteral("project.open"), true, NoArgs, CollabRole::Editor).route, PeerRoute::Refuse);
		QCOMPARE(peerRoute(QStringLiteral("workflow.run"), false, NoArgs, CollabRole::Editor).route, PeerRoute::Refuse);
		QCOMPARE(peerRoute(QStringLiteral("settings.set"), true, NoArgs, CollabRole::Editor).route, PeerRoute::Local);
		QCOMPARE(peerRoute(QStringLiteral("transport.seek"), true, NoArgs, CollabRole::Editor).route, PeerRoute::Local);
		QCOMPARE(peerRoute(QStringLiteral("collab.leave"), false, NoArgs, CollabRole::Viewer).route, PeerRoute::Local);
		// Q050-217.1: the controller's bindings belong to one machine, exactly as the launcher's do - a peer's
		// note binding must not be streamed to the host as a project edit.
		QCOMPARE(peerRoute(QStringLiteral("session.bind_command"), true,
			{{QStringLiteral("trigger"), QStringLiteral("note")}, {QStringLiteral("key"), 60}}, CollabRole::Editor).route, PeerRoute::Local);
		QCOMPARE(peerRoute(QStringLiteral("session.unbind_command"), true,
			{{QStringLiteral("trigger"), QStringLiteral("cc")}}, CollabRole::Editor).route, PeerRoute::Local);
		QCOMPARE(peerRoute(QStringLiteral("session.bind"), true, NoArgs, CollabRole::Editor).route, PeerRoute::Local);
		const QJsonObject withPath{{QStringLiteral("path"), QStringLiteral("/home/me/kick.wav")}};
		const RouteDecision file = peerRoute(QStringLiteral("clip.add"), true, withPath, CollabRole::Editor);
		QCOMPARE(file.route, PeerRoute::Refuse);
		QVERIFY2(file.reason.contains(QStringLiteral("file")), qPrintable(file.reason));
	}

	//! A Zene instance joined with a remote invite sends the allow-list to the host and nothing else.
	void remoteRouting()
	{
		QCOMPARE(peerRoute(QStringLiteral("transport.play"), false, NoArgs, CollabRole::Remote).route, PeerRoute::Forward);
		QCOMPARE(peerRoute(QStringLiteral("mixer.set_volume"), true, NoArgs, CollabRole::Remote).route, PeerRoute::Forward);
		QCOMPARE(peerRoute(QStringLiteral("note.add"), true, NoArgs, CollabRole::Remote).route, PeerRoute::Refuse);
		QCOMPARE(peerRoute(QStringLiteral("collab.get_state"), false, NoArgs, CollabRole::Remote).route, PeerRoute::Local);
		QCOMPARE(peerRoute(QStringLiteral("track.list"), false, NoArgs, CollabRole::Remote).route, PeerRoute::Local);
	}

	//! What the host does after a top-level command.
	void hostBroadcasting()
	{
		QCOMPARE(hostBroadcast(QStringLiteral("note.add"), true, true, NoArgs), HostBroadcast::Stream);
		QCOMPARE(hostBroadcast(QStringLiteral("note.add"), true, false, NoArgs), HostBroadcast::None);
		QCOMPARE(hostBroadcast(QStringLiteral("control.undo"), false, true, NoArgs), HostBroadcast::Resync);
		QCOMPARE(hostBroadcast(QStringLiteral("project.open"), true, true, NoArgs), HostBroadcast::Resync);
		QCOMPARE(hostBroadcast(QStringLiteral("transport.seek"), true, true, NoArgs), HostBroadcast::None);
		QCOMPARE(hostBroadcast(QStringLiteral("transport.get_state"), false, true, NoArgs), HostBroadcast::None);
		QCOMPARE(hostBroadcast(QStringLiteral("collab.host"), false, true, NoArgs), HostBroadcast::None);
		const QJsonObject nested{{QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("file"), QStringLiteral("a.wav")}}}}};
		QCOMPARE(hostBroadcast(QStringLiteral("clip.add"), true, true, nested), HostBroadcast::Resync);
	}

	//! What the host runs for a peer: the same registry path, behind the role.
	void hostRefusals()
	{
		const ControlCommand edit = fakeCommand(QStringLiteral("note.add"), true);
		const ControlCommand read = fakeCommand(QStringLiteral("track.list"), false);
		const ControlCommand undo = fakeCommand(QStringLiteral("control.undo"), false);
		const ControlCommand dialog = fakeCommand(QStringLiteral("clip.rename_dialog"), true, {QStringLiteral("human")});
		const ControlCommand script = fakeCommand(QStringLiteral("script.run"), true);
		const ControlCommand fader = fakeCommand(QStringLiteral("mixer.set_volume"), true);
		const ControlCommand quit = fakeCommand(QStringLiteral("control.quit"), false);
		QVERIFY(hostRefusal(edit.id, &edit, NoArgs, CollabRole::Editor).isEmpty());
		QVERIFY(hostRefusal(undo.id, &undo, NoArgs, CollabRole::Editor).isEmpty());
		QVERIFY(!hostRefusal(read.id, &read, NoArgs, CollabRole::Editor).isEmpty());
		QVERIFY(!hostRefusal(dialog.id, &dialog, NoArgs, CollabRole::Editor).isEmpty());
		QVERIFY(!hostRefusal(script.id, &script, NoArgs, CollabRole::Editor).isEmpty());
		QVERIFY(!hostRefusal(quit.id, &quit, NoArgs, CollabRole::Editor).isEmpty());
		QVERIFY(!hostRefusal(edit.id, &edit, {{QStringLiteral("dir"), QStringLiteral("/etc")}}, CollabRole::Editor).isEmpty());
		QVERIFY(!hostRefusal(edit.id, &edit, NoArgs, CollabRole::Viewer).isEmpty());
		QVERIFY(hostRefusal(fader.id, &fader, NoArgs, CollabRole::Remote).isEmpty());
		QVERIFY(!hostRefusal(edit.id, &edit, NoArgs, CollabRole::Remote).isEmpty());
		QVERIFY(hostRefusal(QStringLiteral("no.such"), nullptr, NoArgs, CollabRole::Viewer).isEmpty());  // the dispatcher says not_found
	}

	void roleNames()
	{
		CollabRole role = CollabRole::Editor;
		QVERIFY(roleFromName(QStringLiteral("remote"), &role));
		QCOMPARE(role, CollabRole::Remote);
		QVERIFY(!roleFromName(QStringLiteral("admin"), &role));
		QCOMPARE(role, CollabRole::Remote);
		QCOMPARE(roleName(CollabRole::Viewer), QStringLiteral("viewer"));
	}

	//! The tap: nested commands and commands applied from the host are never intercepted; only top-level
	//! commands reach the observer.
	void tapSkipsNestedAndRemoteApplied()
	{
		int intercepted = 0;
		QStringList observed;
		CommandTap::install(
			[&intercepted](const QString&, bool, const QJsonObject&, ControlResult* result) {
				++intercepted;
				*result = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("elsewhere"));
				return true;
			},
			[&observed](const QString& id, const QJsonObject&, bool, bool) { observed.append(id); });
		ControlResult result;
		QVERIFY(CommandTap::intercepted(QStringLiteral("note.add"), true, 0, NoArgs, &result));
		QVERIFY(!CommandTap::intercepted(QStringLiteral("note.add"), true, 1, NoArgs, &result));
		{
			RemoteApplyScope applying;
			QVERIFY(!CommandTap::intercepted(QStringLiteral("note.add"), true, 0, NoArgs, &result));
		}
		QCOMPARE(intercepted, 1);
		CommandTap::observed(QStringLiteral("a.top"), NoArgs, true, true, 0);
		CommandTap::observed(QStringLiteral("a.nested"), NoArgs, true, true, 1);
		QCOMPARE(observed, QStringList{QStringLiteral("a.top")});
		CommandTap::clear();
		QVERIFY(!CommandTap::intercepted(QStringLiteral("note.add"), true, 0, NoArgs, &result));
	}

	//! The hook in ControlRegistry itself: a top-level invoke is offered to the tap, a nested one is not, and the
	//! observer hears the top-level command after it ran.
	void registryHookRoutesTopLevelCommandsOnly()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		int innerRuns = 0;
		for (const char* id : {"collabtest.inner", "collabtest.outer"})
		{
			ControlCommand command;
			command.id = QString::fromLatin1(id);
			command.group = QStringLiteral("collabtest");
			command.verb = command.id.section(QLatin1Char('.'), 1);
			command.requiresEngine = false;
			command.handler = QString::fromLatin1(id) == QLatin1String("collabtest.inner")
				? std::function<ControlResult(const QJsonObject&)>([&innerRuns](const QJsonObject&) { ++innerRuns; return ControlResult::success(); })
				: [registry](const QJsonObject&) { return registry->invoke(QStringLiteral("collabtest.inner")); };
			registry->registerCommand(command);
		}
		QStringList offered;
		QStringList observed;
		CommandTap::install(
			[&offered](const QString& id, bool, const QJsonObject&, ControlResult*) { offered.append(id); return false; },
			[&observed](const QString& id, const QJsonObject&, bool, bool) { observed.append(id); });
		QVERIFY(registry->invoke(QStringLiteral("collabtest.outer")).ok);
		QCOMPARE(offered, QStringList{QStringLiteral("collabtest.outer")});
		QCOMPARE(observed, QStringList{QStringLiteral("collabtest.outer")});
		QCOMPARE(innerRuns, 1);
		// An interceptor that claims the command: the handler does not run here.
		CommandTap::install(
			[](const QString&, bool, const QJsonObject&, ControlResult* result) { *result = ControlResult::success({{QStringLiteral("forwarded"), true}}); return true; },
			{});
		const ControlResult forwarded = registry->invoke(QStringLiteral("collabtest.inner"));
		QVERIFY(forwarded.result.value(QStringLiteral("forwarded")).toBool());
		QCOMPARE(innerRuns, 1);
	}

#ifdef ZENE_HAVE_COLLAB
	void base32Vectors()
	{
		// RFC 4648 section 10, without padding.
		QCOMPARE(base32Encode("f"), QStringLiteral("MY"));
		QCOMPARE(base32Encode("foobar"), QStringLiteral("MZXW6YTBOI"));
		QByteArray decoded;
		QVERIFY(base32Decode(QStringLiteral("mzxw-6ytb oi"), &decoded));
		QCOMPARE(decoded, QByteArray("foobar"));
		QVERIFY(!base32Decode(QStringLiteral("MZXW1"), &decoded));
	}

	void inviteRoundTrip()
	{
		for (const char* address : {"203.0.113.7", "2001:db8::42"})
		{
			Invite invite;
			invite.address = QHostAddress(QString::fromLatin1(address));
			invite.port = 41740;
			invite.role = CollabRole::Viewer;
			invite.key = randomInviteKey();
			const QString text = invite.encode();
			QVERIFY(text.startsWith(QStringLiteral("ZENE1-")));
			Invite back;
			QString error;
			QVERIFY2(Invite::decode(QStringLiteral("  ") + text.toLower() + QStringLiteral("\n"), &back, &error), qPrintable(error));
			QCOMPARE(back.address, invite.address);
			QCOMPARE(back.port, invite.port);
			QCOMPARE(back.role, invite.role);
			QCOMPARE(back.key, invite.key);
		}
		QCOMPARE(randomInviteKey().size(), InviteKeyBytes);
		QVERIFY(randomInviteKey() != randomInviteKey());
	}

	//! A missing LAN address must not produce an invite another machine cannot reach.
	void lanInviteRequiresDetectedAddress()
	{
		ControlResult refusal;
		const QString address = detail::lanInviteAddress(QHostAddress(), &refusal);
		QVERIFY(address.isEmpty());
		QVERIFY(!refusal.ok);
		QCOMPARE(refusal.errorKind, ControlErrorKind::Refused);
		QVERIFY(refusal.errorMessage.contains(QStringLiteral("LAN address")));
	}

	//! A mistyped, truncated or foreign paste is refused before any connection, with a reason.
	void inviteRejectsDamage()
	{
		Invite invite;
		invite.address = QHostAddress(QStringLiteral("192.168.1.20"));
		invite.port = 41740;
		invite.key = randomInviteKey();
		const QString text = invite.encode();
		QString flipped = text;
		const int at = text.size() - 10;
		flipped[at] = flipped.at(at) == QLatin1Char('A') ? QLatin1Char('B') : QLatin1Char('A');
		Invite out;
		QString error;
		QVERIFY(!Invite::decode(flipped, &out, &error));
		QVERIFY2(error.contains(QStringLiteral("checksum")), qPrintable(error));
		QString badPaddingBit = text;
		const QString alphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");
		const int lastValue = alphabet.indexOf(badPaddingBit.at(badPaddingBit.size() - 1));
		QVERIFY(lastValue >= 0);
		badPaddingBit[badPaddingBit.size() - 1] = alphabet.at(lastValue ^ 1);
		QVERIFY(!Invite::decode(badPaddingBit, &out, &error));
		QVERIFY(!Invite::decode(text.left(text.size() - 6), &out, &error));
		QVERIFY(!Invite::decode(QStringLiteral("https://example.org/join"), &out, &error));
		QVERIFY(!Invite::decode(QStringLiteral("ZENE1-0189"), &out, &error));
	}

	void inviteRejectsZeroPort()
	{
		Invite invite;
		invite.address = QHostAddress(QStringLiteral("192.168.1.20"));
		invite.port = 0;
		invite.key = QByteArray(InviteKeyBytes, 'k');

		Invite out;
		QString error;
		QVERIFY2(!Invite::decode(invite.encode(), &out, &error), qPrintable(error));
	}

	//! A public invite must not encode a router address hidden behind CGNAT.
	void publicInviteRefusesUnreachableMapping()
	{
		const QString tlsUnavailable = tlsPskUnavailableReason();
		if (!tlsUnavailable.isEmpty()) { QSKIP(qPrintable(tlsUnavailable)); }
		CollabSession* session = CollabSession::instance();
		struct StopSession
		{
			CollabSession* session;
			~StopSession() { if (session->mode() == CollabSession::Mode::Hosting) { session->stop(); } }
		} stop{session};
		const ControlResult hosted = session->host(0, false, QStringLiteral("invite test"));
		QVERIFY2(hosted.ok, qPrintable(hosted.errorMessage));
		QVERIFY(session->m_mapping);
		auto& mapped = session->m_mapping->m_result;
		mapped.ok = true;
		mapped.externalAddress = QHostAddress(QStringLiteral("100.64.12.34"));
		mapped.externalPort = 41741;
		mapped.message = QStringLiteral("the router reports a CGNAT address; resolve the upstream NAT");

		const ControlResult result = session->invite(CollabRole::Editor, QStringLiteral("public"));
		QVERIFY2(!result.ok, qPrintable(result.errorMessage));
		QCOMPARE(result.errorKind, ControlErrorKind::Refused);
		QVERIFY(result.errorMessage.contains(QStringLiteral("scope 'lan'")));
		QVERIFY(result.errorMessage.contains(QStringLiteral("upstream NAT")));
	}
#endif
};

QTEST_GUILESS_MAIN(CollabPolicyTest)
#include "CollabPolicyTest.moc"
