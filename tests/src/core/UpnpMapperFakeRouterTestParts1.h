/*
 * UpnpMapperFakeRouterTest.cpp - the UPnP mapper against a fake internet gateway device on this machine's LAN address
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

// Complete fixture method definitions; included once after the fixture declaration.

void UpnpMapperFakeRouterTest::initTestCase() {
	qRegisterMetaType<lmms::portmap::MappingResult>("lmms::portmap::MappingResult");
	QString why;
	QVERIFY2(m_router.start(&why), qPrintable(why));
	QVERIFY(m_gateway.start());
}
void UpnpMapperFakeRouterTest::init() {
	m_router.mode = Mode::Normal;
	m_router.requests.clear();
	m_router.holdActions = false;
	m_router.held.clear();
	m_gateway.silent = false;
	m_gateway.requests.clear();
}
void UpnpMapperFakeRouterTest::aMappingIsMadeRenewedAndRemoved() {
	UpnpMapper mapper;
	const MappingResult result = map(mapper, 41740, 3600);
	QVERIFY2(result.ok, qPrintable(result.message));
	QCOMPARE(result.method, QStringLiteral("upnp"));
	QCOMPARE(result.externalPort, quint16(41740));
	QCOMPARE(result.lifetime, quint32(3600));
	QCOMPARE(result.externalAddress, QHostAddress(QStringLiteral("203.0.113.9")));
	QVERIFY(result.message.contains(QStringLiteral("41740")));

	// The description was fetched once, then the two SOAP actions in order.
	QCOMPARE(m_router.count("GET /rootDesc.xml"), 1);
	const Recorded add = m_router.last("AddPortMapping");
	QVERIFY(add.soapAction.contains("urn:schemas-upnp-org:service:WANIPConnection:1#AddPortMapping"));
	QVERIFY(add.body.contains("<NewExternalPort>41740</NewExternalPort>"));
	QVERIFY(add.body.contains("<NewInternalPort>41740</NewInternalPort>"));
	QVERIFY(add.body.contains("<NewProtocol>TCP</NewProtocol>"));
	QVERIFY(add.body.contains("<NewInternalClient>" + m_lan.toLatin1() + "</NewInternalClient>"));
	QVERIFY(add.body.contains("<NewLeaseDuration>3600</NewLeaseDuration>"));
	QVERIFY(add.body.contains("Zene Studio collaboration"));
	QCOMPARE(m_router.count("GetExternalIPAddress"), 1);

	// A renewal knows the router already: no new search or description, just the mapping again.
	const MappingResult renewed = map(mapper, 41740, 3600);
	QVERIFY2(renewed.ok, qPrintable(renewed.message));
	QCOMPARE(m_router.count("GET /rootDesc.xml"), 1);
	QCOMPARE(m_router.count("AddPortMapping"), 2);

	// Removing it tells the router, once; a second removal has nothing to remove.
	mapper.unmap();
	QTRY_COMPARE_WITH_TIMEOUT(m_router.count("DeletePortMapping"), 1, 5000);
	const Recorded removal = m_router.last("DeletePortMapping");
	QVERIFY(removal.body.contains("<NewExternalPort>41740</NewExternalPort>"));
	QVERIFY(removal.body.contains("<NewProtocol>TCP</NewProtocol>"));
	mapper.unmap();
	QCOMPARE(m_router.count("DeletePortMapping"), 1);
}
void UpnpMapperFakeRouterTest::theFirstMappingAfterDiscoveryAsksForTheRequestedLease() {
	UpnpMapper mapper;
	const MappingResult result = map(mapper, 41760, 7200);
	QVERIFY2(result.ok, qPrintable(result.message));
	QCOMPARE(m_router.count("AddPortMapping"), 1);
	QVERIFY(m_router.last("AddPortMapping").body.contains("<NewLeaseDuration>7200</NewLeaseDuration>"));
	QCOMPARE(result.lifetime, quint32(7200));

	UpnpMapper shorter;
	const MappingResult shorterResult = map(shorter, 41761, 600);
	QVERIFY2(shorterResult.ok, qPrintable(shorterResult.message));
	QVERIFY(m_router.last("AddPortMapping").body.contains("<NewLeaseDuration>600</NewLeaseDuration>"));
	QCOMPARE(shorterResult.lifetime, quint32(600));
}
void UpnpMapperFakeRouterTest::aRouterThatRefusesIsReportedWithItsOwnWords() {
	m_router.mode = Mode::Refuse718;
	UpnpMapper mapper;
	const MappingResult result = map(mapper, 41741, 3600);
	QVERIFY(!result.ok);
	QVERIFY2(result.message.contains(QStringLiteral("refused the UPnP mapping")), qPrintable(result.message));
	QVERIFY2(result.message.contains(QStringLiteral("UPnP error 718: ConflictInMappingEntry")), qPrintable(result.message));
	QCOMPARE(m_router.count("GetExternalIPAddress"), 0);
	mapper.unmap();  // nothing was mapped: nothing is removed
	QCOMPARE(m_router.count("DeletePortMapping"), 0);
}
void UpnpMapperFakeRouterTest::aRouterThatOnlyTakesPermanentLeasesIsAskedAgainWithoutOne() {
	m_router.mode = Mode::PermanentOnly;
	UpnpMapper mapper;
	const MappingResult result = map(mapper, 41742, 3600);
	QVERIFY2(result.ok, qPrintable(result.message));
	QCOMPARE(result.lifetime, quint32(0));  // permanent
	QCOMPARE(m_router.count("AddPortMapping"), 2);
	QVERIFY(m_router.last("AddPortMapping").body.contains("<NewLeaseDuration>0</NewLeaseDuration>"));
}
void UpnpMapperFakeRouterTest::aDeviceThatIsNotAGatewayAndAnUnreadableDescriptionAreRefusedPlainly() {
	m_router.mode = Mode::NotAnIgd;
	{
		UpnpMapper mapper;
		const MappingResult result = map(mapper, 41743, 3600);
		QVERIFY(!result.ok);
		QVERIFY2(result.message.contains(QStringLiteral("not an internet gateway")), qPrintable(result.message));
	}
	m_router.mode = Mode::DescriptionError;
	{
		UpnpMapper mapper;
		const MappingResult result = map(mapper, 41744, 3600);
		QVERIFY(!result.ok);
		QVERIFY2(result.message.contains(QStringLiteral("description could not be read")), qPrintable(result.message));
	}
	QCOMPARE(m_router.count("AddPortMapping"), 0);
}
void UpnpMapperFakeRouterTest::aRedirectedDescriptionIsRefusedNotFollowed() {
	// Following the 302 would parse the description against the redirect target, not the router that answered.
	m_router.mode = Mode::Redirect;
	UpnpMapper mapper;
	const MappingResult result = map(mapper, 41746, 3600);
	QVERIFY2(!result.ok, "a redirected description was followed and a mapping was made");
	QVERIFY2(result.message.contains(QStringLiteral("redirected")), qPrintable(result.message));
	QCOMPARE(m_router.count("/moved/rootDesc.xml"), 0);
	QCOMPARE(m_router.count("AddPortMapping"), 0);
}
void UpnpMapperFakeRouterTest::noRouterAtAllEndsTheSearchWithAPlainMessage() {
	m_router.mode = Mode::Silent;
	UpnpMapper mapper;
	const MappingResult result = map(mapper, 41745, 3600, 8000);
	QVERIFY(!result.ok);
	QVERIFY2(result.message.contains(QStringLiteral("no UPnP router answered")), qPrintable(result.message));
	QCOMPARE(m_router.count("GET"), 0);
}
void UpnpMapperFakeRouterTest::startMapsThroughNatPmpWhenTheGatewayAnswers() {
	PortMapping mapping;
	aim(mapping);
	const MappingResult result = start(mapping, 41750);
	QVERIFY2(result.ok, qPrintable(result.message));
	QCOMPARE(result.method, QStringLiteral("nat-pmp"));
	QCOMPARE(result.externalPort, quint16(40000));
	QCOMPARE(m_router.requests.size(), 0);  // UPnP was never needed
	const QJsonObject json = mapping.toJson();
	QCOMPARE(json.value(QStringLiteral("gateway")).toString(), QStringLiteral("127.0.0.1"));
	QCOMPARE(json.value(QStringLiteral("gateway_guessed")).toBool(), false);
	QCOMPARE(json.value(QStringLiteral("lan_address")).toString(), QStringLiteral("127.0.0.1"));
	QCOMPARE(json.value(QStringLiteral("internal_port")).toInt(), 41750);
	QVERIFY(!mapping.busy());
	mapping.stop();
	QVERIFY(!mapping.result().ok);
}
void UpnpMapperFakeRouterTest::aSilentGatewayFallsBackToUpnpAndStopRemovesTheMapping() {
	m_gateway.silent = true;
	PortMapping mapping;
	aim(mapping);
	const MappingResult result = start(mapping, 41751);
	QVERIFY2(result.ok, qPrintable(result.message));
	QCOMPARE(result.method, QStringLiteral("upnp"));
	QVERIFY(m_gateway.requests.size() >= 2);  // PCP then NAT-PMP were tried first
	QVERIFY(m_router.last("AddPortMapping").body.contains("<NewInternalClient>127.0.0.1</NewInternalClient>"));
	QVERIFY(m_router.last("AddPortMapping").body.contains("<NewExternalPort>41751</NewExternalPort>"));
	mapping.stop();
	QTRY_COMPARE_WITH_TIMEOUT(m_router.count("DeletePortMapping"), 1, 5000);
}
void UpnpMapperFakeRouterTest::staleAddAfterStopAndRestartKeepsTheNewerMapping() {
	m_router.holdActions = true;
	UpnpMapper mapper;
	mapper.setSearchEndpoint(QHostAddress(QHostAddress::LocalHost), m_router.searchPort(), true);
	QSignalSpy finished(&mapper, &UpnpMapper::finished); auto* http = mapper.findChild<QNetworkAccessManager*>(); QVERIFY(http);
	bool staleAddConsumed = false, externalAddressReplyConsumed = false;
	connect(http, &QNetworkAccessManager::finished, this, [&staleAddConsumed, &externalAddressReplyConsumed](QNetworkReply* reply) {
		if (reply->request().rawHeader("SOAPAction").contains("GetExternalIPAddress")) { externalAddressReplyConsumed = true; }
		if (reply->request().rawHeader("SOAPAction").contains("AddPortMapping")) { staleAddConsumed = true; }
	}, Qt::QueuedConnection);
	mapper.map(41740, 41740, 3600, QHostAddress(m_lan));  // attempt 1: search, description, then AddPortMapping held
	QTRY_COMPARE_WITH_TIMEOUT(m_router.count("AddPortMapping"), 1, 5000);
	mapper.unmap();  // stop() with nothing mapped yet: nothing is deleted
	mapper.map(41740, 41740, 3600, QHostAddress(m_lan));  // restart: the router is known, so attempt 2 adds at once
	QTRY_COMPARE_WITH_TIMEOUT(m_router.count("AddPortMapping"), 2, 5000);
	const int finishedBefore = finished.count();
	QCOMPARE(m_router.held.size(), 2);
	m_router.holdActions = false;  // keep only attempt 2's held AddPortMapping reply; its follow-ups answer normally
	m_router.releaseHeld(0);  // attempt 1's stale answer: the router did make a mapping for the port
	QTRY_VERIFY_WITH_TIMEOUT(staleAddConsumed, 5000);
	const auto replies = http->findChildren<QNetworkReply*>();
	const bool hasAddressReply = std::any_of(replies.cbegin(), replies.cend(),
		[](QNetworkReply* reply) { return reply->request().rawHeader("SOAPAction").contains("GetExternalIPAddress"); });
	if (hasAddressReply) { QTRY_VERIFY_WITH_TIMEOUT(externalAddressReplyConsumed, 5000); }
	QVERIFY(m_router.held.size() == 1 && m_router.held.first().request.soapAction.contains("AddPortMapping"));
	QCOMPARE(finished.count(), finishedBefore);  // the stale reply must not complete the newer attempt
	QCOMPARE(m_router.count("DeletePortMapping"), 0);  // a delete now would remove attempt 2's mapping
	m_router.releaseHeld(0);  // attempt 2's own answer; its address request is answered at once
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), finishedBefore + 1, 5000);
	const MappingResult result = finished.first().first().value<MappingResult>();
	QVERIFY2(result.ok, qPrintable(result.message));
	QCOMPARE(result.method, QStringLiteral("upnp"));
	QCOMPARE(m_router.count("DeletePortMapping"), 0);
	mapper.unmap();  // the attempt that holds the mapping removes it when it stops
	QTRY_COMPARE_WITH_TIMEOUT(m_router.count("DeletePortMapping"), 1, 5000);
}
void UpnpMapperFakeRouterTest::staleAddAfterStopIsTakenBack() {
	m_router.holdActions = true;
	UpnpMapper mapper;
	mapper.setSearchEndpoint(QHostAddress(QHostAddress::LocalHost), m_router.searchPort(), true);
	QSignalSpy finished(&mapper, &UpnpMapper::finished);
	mapper.map(41740, 41740, 3600, QHostAddress(m_lan));
	QTRY_COMPARE_WITH_TIMEOUT(m_router.count("AddPortMapping"), 1, 5000);
	mapper.unmap();
	m_router.holdActions = false;
	m_router.releaseHeld(0);  // the router answers after stop()
	QTRY_COMPARE_WITH_TIMEOUT(m_router.count("DeletePortMapping"), 1, 5000);
	QCOMPARE(finished.count(), 0);
}
void UpnpMapperFakeRouterTest::whenBothFailTheMessageNamesBothAndWhatToDo() {
	m_gateway.silent = true;
	m_router.mode = Mode::Refuse718;
	PortMapping mapping;
	aim(mapping);
	const MappingResult result = start(mapping, 41752);
	QVERIFY(!result.ok);
	QVERIFY2(result.message.contains(QStringLiteral("did not answer PCP or NAT-PMP")), qPrintable(result.message));
	QVERIFY2(result.message.contains(QStringLiteral("UPnP error 718")), qPrintable(result.message));
	QVERIFY2(result.message.contains(QStringLiteral("forward TCP port 41752 to 127.0.0.1")), qPrintable(result.message));
	QVERIFY(!mapping.busy());
}
void UpnpMapperFakeRouterTest::withNoGatewayItGoesStraightToUpnp() {
	PortMapping mapping;
	PortMapping::TestEndpoints endpoints;  // no gateway at all
	endpoints.local = QHostAddress(QHostAddress::LocalHost);
	endpoints.upnpSearch = QHostAddress(QHostAddress::LocalHost);
	endpoints.upnpSearchPort = m_router.searchPort();
	mapping.setTestEndpoints(endpoints);
	const MappingResult result = start(mapping, 41753);
	QVERIFY2(result.ok, qPrintable(result.message));
	QCOMPARE(result.method, QStringLiteral("upnp"));
	QCOMPARE(m_gateway.requests.size(), 0);
	mapping.stop();
}
