/*
 * LegacyPatternUpgradeTest.cpp - long MIDI patterns survive the 1.2 legacy upgrade and round trip.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>

#include "Engine.h"
#include "MidiClip.h"
#include "Note.h"
#include "PatternStore.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

using namespace lmms;

namespace
{

constexpr int PatternTicks = 3072;  // 16 bars at 192 ticks per bar
constexpr int PatternSteps = PatternTicks / 12;

MidiClip* legacyClip()
{
	PatternStore* store = Engine::patternStore();
	if (store == nullptr || store->tracks().size() != 1) { return nullptr; }
	Track* track = store->tracks().front();
	return track != nullptr && track->numOfClips() == 1
		? dynamic_cast<MidiClip*>(track->getClip(0)) : nullptr;
}

bool hasNote(const MidiClip* clip, int key, int position)
{
	for (const Note* note : clip->notes())
	{
		if (note->key() == key && note->pos() == position && note->length() == 96) { return true; }
	}
	return false;
}

bool hasFixtureNotes(const MidiClip* clip)
{
	return clip != nullptr && clip->notes().size() == 2
		&& hasNote(clip, 60, 0) && hasNote(clip, 64, 2976);
}

int savedStepCount(MidiClip* clip)
{
	QDomDocument document;
	QDomElement parent = document.createElement(QStringLiteral("track"));
	return clip->saveState(document, parent).attribute(QStringLiteral("steps")).toInt();
}

bool savedPatternAttributes(const QString& path, int* steps, int* length)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return false; }
	QDomDocument document;
	if (!document.setContent(file.readAll())) { return false; }
	const QDomNodeList clips = document.elementsByTagName(QStringLiteral("midiclip"));
	if (clips.size() != 1) { return false; }
	const QDomElement clip = clips.item(0).toElement();
	*steps = clip.attribute(QStringLiteral("steps")).toInt();
	*length = clip.attribute(QStringLiteral("len")).toInt();
	return true;
}

} // namespace

class LegacyPatternUpgradeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }
	void init() { Engine::getSong()->clearProject(); }
	void cleanup() { Engine::getSong()->clearProject(); }

	//! Q050-042: the legacy tick length upgrades to a 256-step pattern and survives save/reopen.
	void aLegacyMultiBarPatternSurvivesUpgradeSaveAndReopen()
	{
		const QString fixture = QStringLiteral(LMMS_TEST_DATA_DIR "/legacy-long-pattern-upgrade.mmp");
		QFile original(fixture);
		QVERIFY2(original.open(QIODevice::ReadOnly), qPrintable(original.errorString()));
		QDomDocument source;
		QVERIFY2(source.setContent(original.readAll()), "the checked-in legacy fixture is not valid XML");
		const QDomNodeList oldPatterns = source.elementsByTagName(QStringLiteral("pattern"));
		QCOMPARE(oldPatterns.size(), 1);
		const QDomElement oldPattern = oldPatterns.item(0).toElement();
		QCOMPARE(oldPattern.attribute(QStringLiteral("len")).toInt(), PatternTicks);
		QVERIFY2(!oldPattern.hasAttribute(QStringLiteral("steps")),
			"the fixture no longer exercises the old len-to-steps upgrade");
		QCOMPARE(oldPattern.elementsByTagName(QStringLiteral("note")).size(), 2);

		Song* song = Engine::getSong();
		song->setLoadOnLaunch(false);
		song->loadProject(fixture);
		MidiClip* clip = legacyClip();
		QVERIFY2(clip != nullptr, "the upgraded legacy pattern did not load as a MIDI clip");
		QVERIFY2(hasFixtureNotes(clip), "the legacy notes were not preserved by the version upgrade");
		QCOMPARE(clip->length().getTicks(), PatternTicks);
		QCOMPARE(savedStepCount(clip), PatternSteps);
		QCOMPARE(Engine::patternStore()->lengthOfPattern(0), bar_t{16});

		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString saved = directory.filePath(QStringLiteral("upgraded.mmp"));
		QVERIFY(song->saveProjectFile(saved));
		int savedSteps = 0;
		int savedLength = 0;
		QVERIFY(savedPatternAttributes(saved, &savedSteps, &savedLength));
		QCOMPARE(savedSteps, PatternSteps);
		QCOMPARE(savedLength, PatternTicks);

		song->loadProject(saved);
		clip = legacyClip();
		QVERIFY2(clip != nullptr, "the saved upgraded pattern did not reopen as a MIDI clip");
		QVERIFY2(hasFixtureNotes(clip), "the legacy notes were not preserved through save and reopen");
		QCOMPARE(clip->length().getTicks(), PatternTicks);
		QCOMPARE(savedStepCount(clip), PatternSteps);
		QCOMPARE(Engine::patternStore()->lengthOfPattern(0), bar_t{16});
	}
};

QTEST_GUILESS_MAIN(LegacyPatternUpgradeTest)
#include "LegacyPatternUpgradeTest.moc"
