/*
 * UpgradeExtendedNoteRangeTest.cpp - malformed note keys survive the range upgrade
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QDomDocument>

#include "Track.h"
#include "../../../src/core/UpgradeExtendedNoteRange.h"

using namespace lmms;

namespace
{

QString upgradeKey(QString const& key)
{
	QDomDocument document;
	QDomElement root = document.createElement("lmms");
	document.appendChild(root);
	QDomElement song = document.createElement("song");
	root.appendChild(song);
	QDomElement container = document.createElement("trackcontainer");
	song.appendChild(container);
	QDomElement track = document.createElement("track");
	track.setAttribute("type", static_cast<int>(Track::Type::Instrument));
	container.appendChild(track);
	QDomElement instrumentTrack = document.createElement("instrumenttrack");
	instrumentTrack.setAttribute("basenote", 57);
	track.appendChild(instrumentTrack);
	QDomElement instrument = document.createElement("instrument");
	instrument.setAttribute("name", "test-instrument");
	instrumentTrack.appendChild(instrument);
	QDomElement pattern = document.createElement("pattern");
	track.appendChild(pattern);
	QDomElement note = document.createElement("note");
	note.setAttribute("key", key);
	pattern.appendChild(note);

	UpgradeExtendedNoteRange upgrade(root);
	upgrade.upgrade();
	return note.attribute("key");
}

} // namespace

class UpgradeExtendedNoteRangeTest : public QObject
{
	Q_OBJECT

private slots:
	void validKeyStillShifts()
	{
		QCOMPARE(upgradeKey(QStringLiteral("60")), QStringLiteral("72"));
	}

	void malformedKeyIsPreserved()
	{
		QCOMPARE(upgradeKey(QStringLiteral("garbage")), QStringLiteral("garbage"));
	}

	void maximalKeyIsPreserved()
	{
		QCOMPARE(upgradeKey(QStringLiteral("2147483647")), QStringLiteral("2147483647"));
	}

	void highestShiftableKeyStillShifts()
	{
		QCOMPARE(upgradeKey(QStringLiteral("2147483635")), QStringLiteral("2147483647"));
	}
};

QTEST_GUILESS_MAIN(UpgradeExtendedNoteRangeTest)

#include "UpgradeExtendedNoteRangeTest.moc"
