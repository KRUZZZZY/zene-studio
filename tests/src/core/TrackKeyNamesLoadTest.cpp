/*
 * TrackKeyNamesLoadTest.cpp - the <key-names> project section: malformed <key> elements
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

/*! Luna cluster 5429: toInt() returns 0 when the `key` attribute is missing or invalid, so
 *  malformed project data assigned that label to key 0 and could overwrite key 0's real name.
 *  A <key> element without a numeric key attribute is skipped; valid keys still load. */

#include <QtTest>

#include <QDomDocument>

#include "TrackKeyNames.h"

using namespace lmms;

class TrackKeyNamesLoadTest : public QObject
{
	Q_OBJECT

private slots:
	void malformedKeyAttributeIsNotKeyZero()
	{
		QDomDocument doc;
		QString error;
		int at = 0;
		QVERIFY(doc.setContent(QStringLiteral("<key-names>"
			"<track id=\"7\"><key key=\"0\" name=\"Real\"/><key key=\"5\" name=\"Kick\"/><key name=\"Ghost\"/></track>"
			"</key-names>"), &error, &at));
		TrackKeyNames::instance().load(doc.firstChildElement(QStringLiteral("key-names")));
		QCOMPARE(TrackKeyNames::instance().name(7, 5), QStringLiteral("Kick"));  // valid keys still load
		QCOMPARE(TrackKeyNames::instance().name(7, 0), QStringLiteral("Real"));  // the ghost did not become key 0
	}
};

QTEST_MAIN(TrackKeyNamesLoadTest)
#include "TrackKeyNamesLoadTest.moc"
