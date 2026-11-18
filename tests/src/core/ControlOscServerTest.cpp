/*
 * ControlOscServerTest.cpp - the opt-in OSC control server over real UDP loopback (Q050-205)
 *                                       reversibility contract (0.5.0 F6)
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

/*! Q050-205's acceptance, over a real UDP socket on 127.0.0.1 (nothing leaves loopback):
 *   1. /zene/<command-id> runs the registered command and the sender gets the reply - transport play,
 *      state and stop, and a fader write whose reply and mixer.get_state read-back carry the channel and value
 *      (the write is still one undo step);
 *   2. nothing listens by default; osc.start binds, osc.stop closes; the address map is the documented one;
 *   3. malformed, oversized and unsupported requests get bounded /zene/error replies and the server keeps
 *      answering afterwards. */

#include <QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QUdpSocket>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "OscBridge.h"
#include "OscServer.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

class ControlOscServerTest : public QObject
{
	Q_OBJECT

private:
	QUdpSocket m_client;
	quint16 m_port = 0;

	//! Send @a packet to the server and return the one reply it sends back (an empty message on timeout).
	osc::Message exchange(const QByteArray& packet)
	{
		while (m_client.hasPendingDatagrams()) { m_client.receiveDatagram(); }
		m_client.writeDatagram(packet, QHostAddress::LocalHost, m_port);
		// The server reads on this (the UI) thread, so the wait spins the event loop. A reply that never comes
		// is an empty message, which every caller's address check then reports.
		QElapsedTimer waited;
		waited.start();
		while (!m_client.hasPendingDatagrams() && waited.elapsed() < 3000) { QTest::qWait(5); }
		osc::Message message;
		if (!m_client.hasPendingDatagrams()) { return message; }
		const QNetworkDatagram datagram = m_client.receiveDatagram();
		QString error;
		if (!osc::decode(datagram.data(), &message, &error)) { qWarning("undecodable reply: %s", qPrintable(error)); }
		return message;
	}

	osc::Message call(const QString& id, const QJsonArray& args = {})
	{
		return exchange(osc::encode(osc::addressPrefix() + id, args));
	}

	static QJsonObject replyResult(const osc::Message& reply)
	{
		return QJsonDocument::fromJson(reply.args.at(1).toString().toUtf8()).object();
	}

	void startServer(const QString& scope = QStringLiteral("remote"))
	{
		const ControlResult started = run(QStringLiteral("osc.start"), {{QStringLiteral("port"), 0},
			{QStringLiteral("scope"), scope}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		m_port = static_cast<quint16>(started.result.value(QStringLiteral("port")).toInt());
		QVERIFY(m_port > 0);
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_client.bind(QHostAddress::LocalHost, 0));
	}

	void cleanupTestCase()
	{
		osc::OscServer::stop();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void cleanup()
	{
		run(QStringLiteral("osc.stop"));
	}

	//! Criterion 2: off by default, binds only on osc.start, osc.stop closes the port again.
	void offByDefaultBindsOnOptInAndStopCloses()
	{
		QVERIFY(osc::OscServer::instance() == nullptr);
		const ControlResult idle = run(QStringLiteral("osc.get_state"));
		QVERIFY(idle.ok);
		QCOMPARE(idle.result.value(QStringLiteral("running")).toBool(), false);
		QCOMPARE(idle.result.value(QStringLiteral("available")).toBool(), true);

		startServer();
		const ControlResult running = run(QStringLiteral("osc.get_state"));
		QCOMPARE(running.result.value(QStringLiteral("running")).toBool(), true);
		QCOMPARE(running.result.value(QStringLiteral("address")).toString(), QStringLiteral("127.0.0.1"));
		QCOMPARE(running.result.value(QStringLiteral("scope")).toString(), QStringLiteral("remote"));
		QCOMPARE(running.result.value(QStringLiteral("port")).toInt(), static_cast<int>(m_port));
		// The port is taken while it runs ...
		QUdpSocket probe;
		QVERIFY(!probe.bind(QHostAddress::LocalHost, m_port));
		QCOMPARE(call(QStringLiteral("transport.get_state")).address, QStringLiteral("/zene/reply"));

		const ControlResult stopped = run(QStringLiteral("osc.stop"));
		QVERIFY(stopped.ok);
		QCOMPARE(stopped.result.value(QStringLiteral("was_running")).toBool(), true);
		QCOMPARE(run(QStringLiteral("osc.get_state")).result.value(QStringLiteral("running")).toBool(), false);
		// ... and free once it stopped.
		QUdpSocket after;
		QVERIFY2(after.bind(QHostAddress::LocalHost, m_port), "osc.stop left the port bound");

		// The full scope is for this machine only.
		const ControlResult open = run(QStringLiteral("osc.start"), {{QStringLiteral("address"), QStringLiteral("0.0.0.0")},
			{QStringLiteral("port"), 0}, {QStringLiteral("scope"), QStringLiteral("full")}});
		QVERIFY(!open.ok);
		QCOMPARE(open.errorKind, ControlErrorKind::Refused);
		QVERIFY(osc::OscServer::instance() == nullptr);
		const ControlResult bad = run(QStringLiteral("osc.start"), {{QStringLiteral("address"), QStringLiteral("localhost")}});
		QVERIFY(!bad.ok);
		QVERIFY(osc::OscServer::instance() == nullptr);
	}

	//! Criterion 2: the documented address map is the one the server answers.
	void addressMapIsDocumented()
	{
		for (const char* file : {"/docs/050-features/Q050-205.md", "/data/manual/index.html"})
		{
			QFile doc(QStringLiteral(ZENE_SOURCE_DIR) + QLatin1String(file));
			QVERIFY2(doc.open(QIODevice::ReadOnly), file);
			const QString text = QString::fromUtf8(doc.readAll());
			for (const char* needle : {"/zene/&lt;command-id&gt;", "/zene/reply", "/zene/error", "osc.start", "osc.stop",
				"127.0.0.1", "remote", "full"})
			{
				QString wanted = QLatin1String(needle);
				if (QLatin1String(file).endsWith(QLatin1String(".md"))) { wanted.replace(QStringLiteral("&lt;"), QStringLiteral("<")).replace(QStringLiteral("&gt;"), QStringLiteral(">")); }
				QVERIFY2(text.contains(wanted), qPrintable(QStringLiteral("%1 does not document '%2'").arg(QLatin1String(file), wanted)));
			}
		}
		QCOMPARE(osc::addressPrefix(), QStringLiteral("/zene/"));
	}

	//! Criterion 1: transport and fader over a real loopback socket, with their state fed back.
	void transportAndFaderRoundTripOverLoopback()
	{
		startServer();
		Song* song = Engine::getSong();
		if (song->isPlaying()) { song->stop(); }

		osc::Message reply = call(QStringLiteral("transport.play"));
		QCOMPARE(reply.address, QStringLiteral("/zene/reply"));
		QCOMPARE(reply.typeTags, QStringLiteral("ss"));
		QCOMPARE(reply.args.at(0).toString(), QStringLiteral("transport.play"));
		reply = call(QStringLiteral("transport.get_state"));
		QCOMPARE(reply.address, QStringLiteral("/zene/reply"));
		QCOMPARE(replyResult(reply).value(QStringLiteral("playing")).toBool(), true);
		QVERIFY(song->isPlaying());

		reply = call(QStringLiteral("transport.stop"));
		QCOMPARE(reply.address, QStringLiteral("/zene/reply"));
		reply = call(QStringLiteral("transport.get_state"));
		QCOMPARE(replyResult(reply).value(QStringLiteral("playing")).toBool(), false);
		QVERIFY(!song->isPlaying());

		// A fader: key/value pairs, then the JSON-object form; the reply carries the channel and the applied value.
		MixerChannel* master = Engine::mixer()->mixerChannel(0);
		const QString channel = control::channelIdOf(master);
		master->m_volumeModel.setValue(1.0f);
		// Strict one-command-per-step granularity, so the undo below names exactly the second write.
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), 0}}).ok);
		reply = call(QStringLiteral("mixer.set_volume"), {QStringLiteral("channel"), channel, QStringLiteral("volume"), 0.5});
		QCOMPARE(reply.address, QStringLiteral("/zene/reply"));
		QJsonObject fader = replyResult(reply);
		QCOMPARE(fader.value(QStringLiteral("channel")).toString(), channel);
		QCOMPARE(fader.value(QStringLiteral("volume")).toDouble(), 0.5);
		QCOMPARE(master->m_volumeModel.value(), 0.5f);
		reply = call(QStringLiteral("mixer.set_volume"), {QString::fromUtf8(QJsonDocument(QJsonObject{
			{QStringLiteral("channel"), channel}, {QStringLiteral("volume"), 0.25}}).toJson(QJsonDocument::Compact))});
		QCOMPARE(replyResult(reply).value(QStringLiteral("volume")).toDouble(), 0.25);

		reply = call(QStringLiteral("mixer.get_state"));
		QCOMPARE(reply.address, QStringLiteral("/zene/reply"));
		const QJsonArray channels = replyResult(reply).value(QStringLiteral("channels")).toArray();
		QVERIFY(!channels.isEmpty());
		QCOMPARE(channels.at(0).toObject().value(QStringLiteral("volume")).toDouble(), 0.25);

		// The OSC write is the registry's own: it recorded mixer.set_volume, and one undo takes it back.
		const QJsonArray records = run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray();
		QVERIFY(!records.isEmpty());
		QCOMPARE(records.last().toObject().value(QStringLiteral("command")).toString(), QStringLiteral("mixer.set_volume"));
		QCOMPARE(records.last().toObject().value(QStringLiteral("class")).toString(), QStringLiteral("true_inverse"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(master->m_volumeModel.value(), 0.5f);

		const QJsonObject state = run(QStringLiteral("osc.get_state")).result;
		QCOMPARE(state.value(QStringLiteral("errors")).toInt(), 0);
		QCOMPARE(state.value(QStringLiteral("succeeded")).toInt(), 7);
	}

	//! Criterion 3: every malformed or unsupported request gets one bounded /zene/error and the server
	//! keeps answering.
	void malformedAndUnsupportedRequestsGetBoundedErrors()
	{
		startServer();
		struct Case
		{
			QByteArray packet;
			QString kind;
		};
		QByteArray blob = osc::encode(QStringLiteral("/zene/transport.play"), {});
		blob.chop(4);
		blob.append(",b\0\0", 4);
		blob.append("\0\0\0\4abcd", 8);
		const QList<Case> cases{
			{QByteArray("\x01\x02\x03", 3), QStringLiteral("malformed")},
			{QByteArray("/zene/transport.play"), QStringLiteral("malformed")},  // not NUL-terminated nor aligned
			{osc::encode(QStringLiteral("/zene/transport.get_state"), {}) + QByteArray(4, '\0'), QStringLiteral("malformed")},
			{QByteArray("#bundle\0\0\0\0\0\0\0\0\1", 16), QStringLiteral("malformed")},
			{QByteArray(osc::MaxPacketBytes + 64, 'x'), QStringLiteral("malformed")},
			{blob, QStringLiteral("malformed")},
			{osc::encode(QStringLiteral("/other/transport.play"), {}), QStringLiteral("unsupported")},
			{osc::encode(QStringLiteral("/zene/no.such_command"), {}), QStringLiteral("not_found")},
			{osc::encode(QStringLiteral("/zene/script.run"), {QStringLiteral("path"), QStringLiteral("/tmp/x.lua")}),
				QStringLiteral("refused")},
			{osc::encode(QStringLiteral("/zene/control.quit"), {}), QStringLiteral("refused")},
			{osc::encode(QStringLiteral("/zene/track.add"), {}), QStringLiteral("refused")},  // not in the remote scope
			{osc::encode(QStringLiteral("/zene/mixer.set_volume"), {QStringLiteral("channel")}), QStringLiteral("invalid_args")},
			{osc::encode(QStringLiteral("/zene/mixer.set_volume"), {QStringLiteral("{nope")}), QStringLiteral("invalid_args")},
			{osc::encode(QStringLiteral("/zene/mixer.set_volume"), {QStringLiteral("channel"), QStringLiteral("ch-0"),
				QStringLiteral("volume"), 7.0}), QStringLiteral("invalid_args")},
		};
		for (const Case& c : cases)
		{
			const osc::Message reply = exchange(c.packet);
			QCOMPARE(reply.address, QStringLiteral("/zene/error"));
			QCOMPARE(reply.typeTags, QStringLiteral("sss"));
			QCOMPARE(reply.args.at(1).toString(), c.kind);
			QVERIFY(reply.args.at(2).toString().size() <= osc::MaxMessageChars);
			QVERIFY(reply.args.at(0).toString().size() <= osc::MaxMessageChars);
		}
		// A long address is cut, not echoed whole.
		const osc::Message longAddress = exchange(osc::encode(QStringLiteral("/zene/") + QString(4000, QLatin1Char('a')), {}));
		QCOMPARE(longAddress.address, QStringLiteral("/zene/error"));
		QVERIFY(longAddress.args.at(0).toString().size() <= osc::MaxMessageChars);

		// Still answering, and the counters say what happened.
		QCOMPARE(call(QStringLiteral("transport.get_state")).address, QStringLiteral("/zene/reply"));
		const QJsonObject state = run(QStringLiteral("osc.get_state")).result;
		QCOMPARE(state.value(QStringLiteral("errors")).toInt(), cases.size() + 1);
		QCOMPARE(state.value(QStringLiteral("oversized")).toInt(), 1);
		QCOMPARE(state.value(QStringLiteral("succeeded")).toInt(), 1);
	}

	//! Criterion 3: a flood is drained in bounded turns and every request is answered.
	void aBurstIsAnsweredInBoundedTurns()
	{
		startServer();
		const int burst = osc::OscServer::MaxDatagramsPerTurn * 3;
		for (int i = 0; i < burst; ++i)
		{
			m_client.writeDatagram(osc::encode(QStringLiteral("/zene/transport.get_state"), {}), QHostAddress::LocalHost, m_port);
		}
		int replies = 0;
		QTRY_VERIFY_WITH_TIMEOUT((replies += [this] {
			int n = 0;
			while (m_client.hasPendingDatagrams()) { m_client.receiveDatagram(); ++n; }
			return n;
		}()) >= burst, 5000);
		QCOMPARE(replies, burst);
	}

	//! The full scope runs commands outside the remote allow-list, still refusing the closed set.
	void fullScopeRunsOtherCommandsButNotScripts()
	{
		startServer(QStringLiteral("full"));
		QCOMPARE(call(QStringLiteral("track.list")).address, QStringLiteral("/zene/reply"));
		const osc::Message refused = call(QStringLiteral("script.list"));
		QCOMPARE(refused.address, QStringLiteral("/zene/error"));
		QCOMPARE(refused.args.at(1).toString(), QStringLiteral("refused"));
		const osc::Message path = call(QStringLiteral("project.save"), {QStringLiteral("path"), QStringLiteral("/tmp/osc.mmpz")});
		QCOMPARE(path.args.at(1).toString(), QStringLiteral("refused"));
	}
};

QTEST_GUILESS_MAIN(ControlOscServerTest)
#include "ControlOscServerTest.moc"
