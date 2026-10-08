/*
 * MidiClipControllersLoadTest.cpp - 0.5.0 L07 (ED-130): a MIDI clip's controller lanes in the file
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program. If not, see <http://www.gnu.org/licenses>.
 */

/*! What a lane's <p> elements carry into playback: a tick or a value that is not one is dropped
 *  (it used to convert to 0, landing the point at tick zero with value zero), the value stays
 *  inside the lane's range, and a well formed lane still round-trips through save/load. */

#include <QtTest>

#include <QDomDocument>

#include "MidiClipControllers.h"

using namespace lmms;

namespace
{

//! A <clip> element carrying \a points as one cc:7 lane: each pair the raw "t" and "v"
//! attribute text, so a malformed one reaches the loader as it would from a damaged file.
QDomElement clipWith(const QVector<QPair<QString, QString>>& points)
{
	QDomDocument doc;
	QDomElement clip = doc.createElement(QStringLiteral("clip"));
	QDomElement root = doc.createElement(QStringLiteral("controllers"));
	root.setAttribute(QStringLiteral("metadata"), 1);
	QDomElement lane = doc.createElement(QStringLiteral("lane"));
	lane.setAttribute(QStringLiteral("id"), QStringLiteral("cc:7"));
	for (const auto& point : points)
	{
		QDomElement p = doc.createElement(QStringLiteral("p"));
		p.setAttribute(QStringLiteral("t"), point.first);
		p.setAttribute(QStringLiteral("v"), point.second);
		lane.appendChild(p);
	}
	root.appendChild(lane);
	clip.appendChild(root);
	return clip;
}

//! One <p> element's attributes, as the loader will read them.
QPair<QString, QString> point(const QString& tick, const QString& value)
{
	return {tick, value};
}

} // namespace

class MidiClipControllersLoadTest : public QObject
{
	Q_OBJECT

private slots:
	void aMalformedTickOrValueIsDroppedInsteadOfBecomingAPointAtZero()
	{
		MidiClipControllers controllers;
		controllers.load(clipWith({point(QStringLiteral("bad"), QStringLiteral("64")), point(QStringLiteral("96"), QStringLiteral("3"))}));
		QCOMPARE(controllers.lanes().size(), std::size_t{1});
		const ControllerLane* lane = controllers.lane(ControllerKind::ControlChange, 7);
		QVERIFY(lane != nullptr);
		QCOMPARE(lane->points.size(), std::size_t{1});
		QCOMPARE(lane->points[0].tick, tick_t{96});
		QCOMPARE(lane->points[0].value, 3);

		// A value too large for an int, and a missing attribute, are dropped the same way.
		controllers.load(clipWith({point(QStringLiteral("48"), QStringLiteral("99999999999")), point(QStringLiteral("192"), QStringLiteral("64"))}));
		const ControllerLane* only = controllers.lane(ControllerKind::ControlChange, 7);
		QVERIFY(only != nullptr);
		QCOMPARE(only->points.size(), std::size_t{1});
		QCOMPARE(only->points[0].tick, tick_t{192});
	}

	void aGoodLaneRoundTripsAndTheValueStaysInRange()
	{
		MidiClipControllers written;
		written.setPoints(ControllerKind::ControlChange, 7, {{0, 0}, {96, 100}});
		QDomDocument doc;
		QDomElement clip = doc.createElement(QStringLiteral("clip"));
		written.save(doc, clip);

		MidiClipControllers read;
		read.load(clip);
		QCOMPARE(read.lanes().size(), std::size_t{1});
		const ControllerLane* lane = read.lane(ControllerKind::ControlChange, 7);
		QVERIFY(lane != nullptr);
		QCOMPARE(lane->points.size(), std::size_t{2});
		QCOMPARE(lane->points[1].tick, tick_t{96});
		QCOMPARE(lane->points[1].value, 100);

		// A CC value above 127 is clamped to it, as it always was.
		MidiClipControllers clamped;
		clamped.load(clipWith({point(QStringLiteral("0"), QStringLiteral("200"))}));
		QCOMPARE(clamped.lane(ControllerKind::ControlChange, 7)->points[0].value, 127);
	}
};

QTEST_GUILESS_MAIN(MidiClipControllersLoadTest)
#include "MidiClipControllersLoadTest.moc"