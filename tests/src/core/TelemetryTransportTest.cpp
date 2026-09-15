/*
 * TelemetryTransportTest.cpp - CODE-7: the telemetry transport is https-only
 *                               and never blocks its caller.
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
 * WHAT THIS PROVES. Two properties, both structural, neither of them a
 * configuration value:
 *
 *   https only   a plain-http endpoint is REFUSED - and refused before the
 *                delivery seam is reached, which is the measurable form of
 *                "before a socket exists": the recorder standing in for the
 *                network is never called, so nothing was handed anywhere.
 *
 *   no blocking  a send returns at once. The timing case uses TEST-NET-1
 *                (192.0.2.0/24, RFC 5737: reserved, never routed, so nothing
 *                will ever answer) - which is exactly the endpoint the previous
 *                implementation parked the calling thread on for ten seconds,
 *                because it waited in a nested QEventLoop behind a 10 s timer.
 *                The assertion is "send() returned in under two seconds", which
 *                the old code could not satisfy on any machine.
 *
 * The transport is compiled only when this build has telemetry enabled, so in a
 * -DZENE_TELEMETRY=OFF build the slots below are replaced by the one thing that
 * IS observable there: the absence of the group from the command registry.
 */

#include "lmmsconfig.h"

#ifndef ZENE_TELEMETRY_ENABLED
#include "ControlRegistry.h"
#endif

#include <QtTest>

#ifdef ZENE_TELEMETRY_ENABLED

#include <QByteArray>
#include <QElapsedTimer>
#include <QStringList>
#include <QUrl>

#include "TelemetryNetworkTransport.h"

using lmms::TelemetryNetworkTransport;

class TelemetryTransportTest : public QObject
{
	Q_OBJECT
private slots:

	//! THE POLICY, as a table. https is the only scheme that may be posted to;
	//! everything else is refused, and the refusal says which scheme it saw.
	void endpointPolicy_data()
	{
		QTest::addColumn<QString>("endpoint");
		QTest::addColumn<bool>("allowed");

		QTest::newRow("https") << QStringLiteral("https://ingest.example.org/telemetry") << true;
		QTest::newRow("https with port")
			<< QStringLiteral("https://ingest.example.org:8443/telemetry") << true;
		QTest::newRow("plain http")
			<< QStringLiteral("http://ingest.example.org/telemetry") << false;
		QTest::newRow("plain http on localhost")
			<< QStringLiteral("http://127.0.0.1:8080/ingest") << false;
		QTest::newRow("ftp") << QStringLiteral("ftp://ingest.example.org/telemetry") << false;
		QTest::newRow("file") << QStringLiteral("file:///tmp/telemetry.json") << false;
		QTest::newRow("scheme less") << QStringLiteral("ingest.example.org/telemetry") << false;
		QTest::newRow("empty") << QString() << false;
	}

	void endpointPolicy()
	{
		QFETCH(QString, endpoint);
		QFETCH(bool, allowed);

		QString reason;
		const bool verdict =
			TelemetryNetworkTransport::isAllowedEndpoint(QUrl(endpoint), &reason);
		QCOMPARE(verdict, allowed);
		if (allowed)
		{
			QVERIFY2(reason.isEmpty(), qPrintable(reason));
		}
		else
		{
			// A refusal is never a bare false: the reason says what is wrong,
			// and for a scheme that is not https it names https as the rule.
			QVERIFY2(!reason.isEmpty(), "the endpoint was refused with no reason");
			if (!endpoint.isEmpty())
			{
				QVERIFY2(reason.contains(QStringLiteral("https")), qPrintable(reason));
			}
		}
	}

	//! A plain-http endpoint is refused, and the refusal happens BEFORE the
	//! delivery seam: the recorder that stands in for the network is never
	//! called, so nothing was handed to anything. The call also returns at
	//! once - a refusal is not a failed connection attempt.
	void plainHttpEndpointIsRefusedWithoutHandingAnythingOver()
	{
		TelemetryNetworkTransport transport(QStringLiteral("http://ingest.example.org/telemetry"));
		int handedOver = 0;
		transport.setDeliverer([&handedOver](const QUrl&, const QByteArray&) {
			++handedOver;
			return true;
		});

		QElapsedTimer timer;
		timer.start();
		const bool accepted = transport.send(QByteArrayLiteral("{\"payload_schema_version\":\"1\"}"));
		const qint64 elapsed = timer.elapsed();

		QVERIFY2(!accepted, "a plain-http endpoint was accepted");
		QCOMPARE(handedOver, 0);
		QVERIFY2(elapsed < 2000,
			qPrintable(QStringLiteral("the refusal took %1 ms: it went somewhere first").arg(elapsed)));
		QVERIFY2(transport.lastRefusal().contains(QStringLiteral("https")),
			qPrintable(transport.lastRefusal()));
		QVERIFY2(transport.describe().contains(QStringLiteral("refused")),
			qPrintable(transport.describe()));
		QVERIFY2(TelemetryNetworkTransport::endpointPolicy().contains(QStringLiteral("https")),
			qPrintable(TelemetryNetworkTransport::endpointPolicy()));
	}

	//! An endpoint with no endpoint configured is refused the same way (the
	//! shipping default: there is no ingest service yet, so an enabled build
	//! still opens nothing), and no socket is involved.
	void anUnconfiguredEndpointIsRefused()
	{
		TelemetryNetworkTransport transport(QString());
		int handedOver = 0;
		transport.setDeliverer([&handedOver](const QUrl&, const QByteArray&) {
			++handedOver;
			return true;
		});
		QVERIFY2(!transport.send(QByteArrayLiteral("{}")), "an empty endpoint was accepted");
		QCOMPARE(handedOver, 0);
		QVERIFY2(transport.describe().contains(QStringLiteral("no endpoint")),
			qPrintable(transport.describe()));
	}

	//! THE DIFFERENTIAL. TEST-NET-1 never answers, so a transport that waits for
	//! the reply waits for its own timeout. send() must return at once.
	void aSlowEndpointDoesNotBlockTheCaller()
	{
		TelemetryNetworkTransport transport(QStringLiteral("https://192.0.2.1/telemetry"));

		QElapsedTimer timer;
		timer.start();
		const bool accepted = transport.send(QByteArrayLiteral("{\"payload_schema_version\":\"1\"}"));
		const qint64 elapsed = timer.elapsed();

		QVERIFY2(accepted, qPrintable(QStringLiteral("an https endpoint was refused: %1")
			.arg(transport.lastRefusal())));
		QVERIFY2(elapsed < 2000,
			qPrintable(QStringLiteral("send() took %1 ms: it waited for a reply that never comes. "
				"The transport must hand the POST off and return (CODE-7)").arg(elapsed)));
		qInfo() << "a send to a blackholed endpoint returned in" << elapsed << "ms";
	}

	//! The seam itself: an accepted endpoint hands (url, payload) over exactly
	//! once, unchanged, and 'true' means "queued", not "delivered".
	void anAcceptedEndpointHandsThePayloadOver()
	{
		TelemetryNetworkTransport transport(QStringLiteral("https://ingest.example.org/telemetry"));
		QByteArray seen;
		QUrl lastUrl;
		int calls = 0;
		transport.setDeliverer([&seen, &lastUrl, &calls](const QUrl& url, const QByteArray& payload) {
			++calls;
			lastUrl = url;
			seen = payload;
			return true;
		});

		const QByteArray payload = QByteArrayLiteral("{\"os_family\":\"linux\"}");
		QVERIFY2(transport.send(payload), "an https endpoint was refused");
		QCOMPARE(calls, 1);
		QCOMPARE(seen, payload);
		QCOMPARE(lastUrl.scheme(), QStringLiteral("https"));
		QVERIFY2(transport.lastRefusal().isEmpty(),
			qPrintable(QStringLiteral("a queued send reported a refusal: %1")
				.arg(transport.lastRefusal())));
	}
};

QTEST_GUILESS_MAIN(TelemetryTransportTest)
#include "TelemetryTransportTest.moc"

#else // !ZENE_TELEMETRY_ENABLED

// --- the packager kill switch (compiled-out build) -------------------------
// There is no transport in this binary to test, and that is the answer: the
// client is not compiled, so neither the group nor its send path exists. The
// observable is the registry (the TelemetryTest idiom).

class TelemetryTransportTest : public QObject
{
	Q_OBJECT
private slots:
	void theTransportIsAbsentWhenTheClientIsCompiledOut()
	{
		lmms::ControlRegistry* registry = lmms::ControlRegistry::instance();
		QVERIFY2(registry->command(QStringLiteral("telemetry.status")) == nullptr,
			"a compiled-out build still declares telemetry.status");
		QVERIFY2(registry->command(QStringLiteral("telemetry.consent")) == nullptr,
			"a compiled-out build still declares telemetry.consent");
	}
};

QTEST_GUILESS_MAIN(TelemetryTransportTest)
#include "TelemetryTransportTest.moc"

#endif // ZENE_TELEMETRY_ENABLED
