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

// Builds a song with an instrument track that has an automated (element-based)
// base note with id 7, plus an automation track that holds one pattern
// targeting that base note and one pattern without any target. Returns the
// upgraded time values of both patterns.
void upgradeAutomationPatterns(int baseNoteValue, int targetlessValue,
	int& upgradedBaseNoteValue, int& upgradedTargetlessValue)
{
	QDomDocument document;
	QDomElement root = document.createElement("lmms");
	document.appendChild(root);
	QDomElement song = document.createElement("song");
	root.appendChild(song);
	QDomElement container = document.createElement("trackcontainer");
	song.appendChild(container);

	QDomElement instrumentTrackElement = document.createElement("track");
	instrumentTrackElement.setAttribute("type", static_cast<int>(Track::Type::Instrument));
	container.appendChild(instrumentTrackElement);
	QDomElement instrumentTrack = document.createElement("instrumenttrack");
	instrumentTrackElement.appendChild(instrumentTrack);
	QDomElement baseNote = document.createElement("basenote");
	baseNote.setAttribute("id", 7);
	baseNote.setAttribute("value", 57);
	instrumentTrack.appendChild(baseNote);
	QDomElement instrument = document.createElement("instrument");
	instrument.setAttribute("name", "test-instrument");
	instrumentTrack.appendChild(instrument);

	QDomElement automationTrack = document.createElement("track");
	automationTrack.setAttribute("type", static_cast<int>(Track::Type::Automation));
	container.appendChild(automationTrack);

	QDomElement baseNotePattern = document.createElement("automationpattern");
	automationTrack.appendChild(baseNotePattern);
	QDomElement baseNoteObject = document.createElement("object");
	baseNoteObject.setAttribute("id", 7);
	baseNotePattern.appendChild(baseNoteObject);
	QDomElement baseNoteTime = document.createElement("time");
	baseNoteTime.setAttribute("value", baseNoteValue);
	baseNoteTime.setAttribute("outValue", baseNoteValue);
	baseNotePattern.appendChild(baseNoteTime);

	QDomElement targetlessPattern = document.createElement("automationpattern");
	automationTrack.appendChild(targetlessPattern);
	QDomElement targetlessTime = document.createElement("time");
	targetlessTime.setAttribute("value", targetlessValue);
	targetlessTime.setAttribute("outValue", targetlessValue);
	targetlessPattern.appendChild(targetlessTime);

	UpgradeExtendedNoteRange upgrade(root);
	upgrade.upgrade();
	upgradedBaseNoteValue = baseNoteTime.attribute("value").toInt();
	upgradedTargetlessValue = targetlessTime.attribute("value").toInt();
}

// Builds a song with one instrument track whose base note is saved as the
// "basenote" attribute with the given text and returns the attribute after the upgrade.
QString upgradeBaseNoteAttribute(QString const& baseNote)
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
	instrumentTrack.setAttribute("basenote", baseNote);
	track.appendChild(instrumentTrack);
	QDomElement instrument = document.createElement("instrument");
	instrument.setAttribute("name", "test-instrument");
	instrumentTrack.appendChild(instrument);

	UpgradeExtendedNoteRange upgrade(root);
	upgrade.upgrade();
	return instrumentTrack.attribute("basenote");
}

// Builds a song with one instrument track whose base note is automated: it is
// saved as an element with id 7 and the given value. Returns the value after the upgrade.
QString upgradeBaseNoteElement(QString const& value)
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
	track.appendChild(instrumentTrack);
	QDomElement baseNote = document.createElement("basenote");
	baseNote.setAttribute("id", 7);
	baseNote.setAttribute("value", value);
	instrumentTrack.appendChild(baseNote);
	QDomElement instrument = document.createElement("instrument");
	instrument.setAttribute("name", "test-instrument");
	instrumentTrack.appendChild(instrument);

	UpgradeExtendedNoteRange upgrade(root);
	upgrade.upgrade();
	return baseNote.attribute("value");
}

struct UpgradedTime
{
	QString value;
	QString outValue;
};

// Builds a song with an automated base note (id 7, value 57) and an automation
// track whose pattern targets it with one time node. A null outValue leaves the
// attribute out of the saved node. Returns the node's attributes after the upgrade.
UpgradedTime upgradeAutomationTime(QString const& value, QString const& outValue)
{
	QDomDocument document;
	QDomElement root = document.createElement("lmms");
	document.appendChild(root);
	QDomElement song = document.createElement("song");
	root.appendChild(song);
	QDomElement container = document.createElement("trackcontainer");
	song.appendChild(container);

	QDomElement instrumentTrackElement = document.createElement("track");
	instrumentTrackElement.setAttribute("type", static_cast<int>(Track::Type::Instrument));
	container.appendChild(instrumentTrackElement);
	QDomElement instrumentTrack = document.createElement("instrumenttrack");
	instrumentTrackElement.appendChild(instrumentTrack);
	QDomElement baseNote = document.createElement("basenote");
	baseNote.setAttribute("id", 7);
	baseNote.setAttribute("value", 57);
	instrumentTrack.appendChild(baseNote);
	QDomElement instrument = document.createElement("instrument");
	instrument.setAttribute("name", "test-instrument");
	instrumentTrack.appendChild(instrument);

	QDomElement automationTrack = document.createElement("track");
	automationTrack.setAttribute("type", static_cast<int>(Track::Type::Automation));
	container.appendChild(automationTrack);
	QDomElement pattern = document.createElement("automationpattern");
	automationTrack.appendChild(pattern);
	QDomElement object = document.createElement("object");
	object.setAttribute("id", 7);
	pattern.appendChild(object);
	QDomElement time = document.createElement("time");
	time.setAttribute("value", value);
	if (!outValue.isNull())
	{
		time.setAttribute("outValue", outValue);
	}
	pattern.appendChild(time);

	UpgradeExtendedNoteRange upgrade(root);
	upgrade.upgrade();
	return UpgradedTime{time.attribute("value"), time.attribute("outValue")};
}

// Builds a preset, i.e. a document without a song, whose instrument track has the
// given "basenote" attribute (a null string leaves it out). Returns the attribute
// after the upgrade.
QString upgradePresetBaseNote(QString const& baseNote, QString const& instrumentName)
{
	QDomDocument document;
	QDomElement root = document.createElement("lmms");
	document.appendChild(root);
	QDomElement instrumentTrack = document.createElement("instrumenttrack");
	if (!baseNote.isNull())
	{
		instrumentTrack.setAttribute("basenote", baseNote);
	}
	root.appendChild(instrumentTrack);
	QDomElement instrument = document.createElement("instrument");
	instrument.setAttribute("name", instrumentName);
	instrumentTrack.appendChild(instrument);

	UpgradeExtendedNoteRange upgrade(root);
	upgrade.upgrade();
	return instrumentTrack.attribute("basenote");
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

	void baseNoteAutomationIsStillShifted()
	{
		int upgradedBaseNoteValue = 0;
		int upgradedTargetlessValue = 0;
		upgradeAutomationPatterns(10, 3, upgradedBaseNoteValue, upgradedTargetlessValue);
		QCOMPARE(upgradedBaseNoteValue, 22);
	}

	void targetlessAutomationValueIsPreserved()
	{
		int upgradedBaseNoteValue = 0;
		int upgradedTargetlessValue = 0;
		upgradeAutomationPatterns(10, 3, upgradedBaseNoteValue, upgradedTargetlessValue);
		QCOMPARE(upgradedTargetlessValue, 3);
	}

	void plainBaseNoteAttributeStillShifts()
	{
		QCOMPARE(upgradeBaseNoteAttribute(QStringLiteral("57")), QStringLiteral("69"));
	}

	void malformedBaseNoteAttributeIsPreserved()
	{
		QCOMPARE(upgradeBaseNoteAttribute(QStringLiteral("bad")), QStringLiteral("bad"));
	}

	void fractionalBaseNoteAttributeKeepsFraction()
	{
		QCOMPARE(upgradeBaseNoteAttribute(QStringLiteral("57.5")), QStringLiteral("69.5"));
	}

	void highestShiftableBaseNoteAttributeStillShifts()
	{
		QCOMPARE(upgradeBaseNoteAttribute(QStringLiteral("2147483635")), QStringLiteral("2147483647"));
	}

	void overflowingBaseNoteAttributeIsPreserved()
	{
		QCOMPARE(upgradeBaseNoteAttribute(QStringLiteral("2147483636")), QStringLiteral("2147483636"));
	}

	void malformedAutomatedBaseNoteIsPreserved()
	{
		QCOMPARE(upgradeBaseNoteElement(QStringLiteral("bad")), QStringLiteral("bad"));
	}

	void automationTimeStillShiftsBothValues()
	{
		UpgradedTime const upgraded = upgradeAutomationTime(QStringLiteral("10"), QStringLiteral("10"));
		QCOMPARE(upgraded.value, QStringLiteral("22"));
		QCOMPARE(upgraded.outValue, QStringLiteral("22"));
	}

	void malformedAutomationTimeIsPreserved()
	{
		UpgradedTime const upgraded = upgradeAutomationTime(QStringLiteral("bad"), QStringLiteral("0"));
		QCOMPARE(upgraded.value, QStringLiteral("bad"));
		QCOMPARE(upgraded.outValue, QStringLiteral("0"));
	}

	void missingAutomationOutValueIsNotInvented()
	{
		UpgradedTime const upgraded = upgradeAutomationTime(QStringLiteral("10"), QString());
		QCOMPARE(upgraded.value, QStringLiteral("22"));
		QVERIFY(upgraded.outValue.isNull());
	}

	void absentPresetBaseNoteDefaultsTo57()
	{
		QCOMPARE(upgradePresetBaseNote(QString(), QStringLiteral("test-instrument")), QStringLiteral("69"));
	}

	void malformedPresetBaseNoteIsPreserved()
	{
		QCOMPARE(upgradePresetBaseNote(QStringLiteral("bad"), QStringLiteral("test-instrument")), QStringLiteral("bad"));
	}

	void fractionalPresetBaseNoteKeepsFraction()
	{
		QCOMPARE(upgradePresetBaseNote(QStringLiteral("57.5"), QStringLiteral("test-instrument")), QStringLiteral("69.5"));
	}

	void overflowingPresetBaseNoteIsPreserved()
	{
		QCOMPARE(upgradePresetBaseNote(QStringLiteral("2147483636"), QStringLiteral("test-instrument")), QStringLiteral("2147483636"));
	}

	void affectedPresetBaseNoteShiftsTwice()
	{
		QCOMPARE(upgradePresetBaseNote(QStringLiteral("57"), QStringLiteral("zynaddsubfx")), QStringLiteral("81"));
	}
};

QTEST_GUILESS_MAIN(UpgradeExtendedNoteRangeTest)

#include "UpgradeExtendedNoteRangeTest.moc"
