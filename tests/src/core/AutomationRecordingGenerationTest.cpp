/*
 * AutomationRecordingGenerationTest.cpp - project generations for queued recording
 *
 * This file is part of LMMS.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>

#include <bit>
#include <cstdint>

#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>

#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "Engine.h"
#include "JournallingObject.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;

namespace
{

std::uint32_t bits(float value)
{
	return std::bit_cast<std::uint32_t>(value);
}

} // namespace

class AutomationRecordingGenerationTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void pendingRecordingCannotRetargetAClipRestoredByProjectLoad()
	{
		auto* song = Engine::getSong();
		song->drainAutomationRecordingEvents();
		song->stop();
		song->clearProject();

		auto* track = new AutomationTrack(song);
		auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->putValue(TimePos(0), 0.25f, false, true);
		clip->changeLength(TimePos(32));
		const auto clipId = static_cast<const JournallingObject&>(*clip).id();

		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString projectPath = directory.filePath(QStringLiteral("generation.mmp"));
		QVERIFY(song->saveProjectFile(projectPath, false));
		// Project saves normally strip the undo-only journallingObject metadata.
		// Put it back in this fixture to exercise the supported restore path that
		// can reuse a clip's saved journal ID after the old object is destroyed.
		QFile projectFile(projectPath);
		QVERIFY(projectFile.open(QIODevice::ReadOnly));
		QDomDocument projectDocument;
		QVERIFY(projectDocument.setContent(&projectFile));
		projectFile.close();
		auto automationElements = projectDocument.elementsByTagName(QStringLiteral("automationclip"));
		QVERIFY(!automationElements.isEmpty());
		QDomElement journalId = projectDocument.createElement(QStringLiteral("journallingObject"));
		journalId.setAttribute(QStringLiteral("id"), clipId);
		automationElements.at(0).toElement().appendChild(journalId);
		QVERIFY(projectFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
		const QByteArray projectBytes = projectDocument.toByteArray();
		QCOMPARE(projectFile.write(projectBytes), static_cast<qint64>(projectBytes.size()));
		projectFile.close();
		QVERIFY(song->enqueueAutomationRecord(clipId, clip->recordingMapTime(TimePos(4)), 0.875f));

		const auto oldGeneration = song->automationProjectGeneration();
		song->clearProject();
		QVERIFY(song->automationProjectGeneration() != oldGeneration);
		song->loadProject(projectPath);
		auto* restored = dynamic_cast<AutomationClip*>(Engine::projectJournal()->journallingObject(clipId));
		QVERIFY2(restored != nullptr, "the saved clip journal ID was not restored by loadProject");
		QCOMPARE(bits(restored->valueAt(TimePos(4))), bits(0.25f));
		QVERIFY(!restored->getTimeMap().contains(4));

		const auto beforeDrain = song->automationRecordingStatus();
		song->drainAutomationRecordingEvents();
		QCOMPARE(song->automationRecordingStatus().staleClipIds, beforeDrain.staleClipIds + 1);
		QVERIFY(!restored->getTimeMap().contains(4));
		QCOMPARE(bits(restored->valueAt(TimePos(4))), bits(0.25f));
		song->clearProject();
	}
};

QTEST_MAIN(AutomationRecordingGenerationTest)

#include "AutomationRecordingGenerationTest.moc"
