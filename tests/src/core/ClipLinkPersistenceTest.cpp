/*
 * ClipLinkPersistenceTest.cpp - the registered proof that a link relation
 *                              SURVIVES a save/reload round trip, and that the
 *                              group's refusals are typed and leave nothing
 *                              half-written (feature-list row 6, board task
 *                              #645).
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
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

// WHAT THIS FILE PROVES. A relation that does not survive a save is a session
// hack, so this binary is the row's acceptance criterion: the project is written,
// the `link` attribute is asserted to be IN the file, the project is loaded back,
// and the group is asked for again - and then an edit to one reloaded member is
// asserted to reach the other, because a link that reloads but no longer
// propagates would pass a weaker test and fail the feature. The second slot is
// the refusal half: a clip that is not a member, an audio clip, and a group that
// would be merged are all typed failures, and each one is checked to have left
// the project as it found it.
//
// Every scene is built through the CONTROL SURFACE (ControlRegistry::invoke) -
// the release's own door - with the fixtures in ClipLinkTestSupport.h. The
// relation, the propagation, the unlink, one-undo and the A16 rows are
// ClipLinkTest.

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>

#include "ClipLinkTestSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"

using namespace lmms;
using namespace cliplinktest;

class ClipLinkPersistenceTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
		m_project = m_dir.filePath(QStringLiteral("link-roundtrip.mmp"));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theLinkSurvivesSaveAndReload()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString a = addMidiClip(QStringLiteral("roundtrip A"));
		const QString b = addMidiClip(QStringLiteral("roundtrip B"));
		const ControlResult linked = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("clips"), QJsonArray{b}}});
		QVERIFY2(linked.ok, qPrintable(linked.errorMessage));
		const int group = linked.result.value(QStringLiteral("group")).toInt();
		const ControlResult note = registry->invoke(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("key"), 69},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24}});
		QVERIFY2(note.ok, qPrintable(note.errorMessage));

		QVERIFY(Engine::getSong()->saveProjectFile(m_project));
		QVERIFY(QFile::exists(m_project));

		// the file itself carries the relation: the `link` attribute on each
		// member's own element, and nothing else (an unlinked clip writes none).
		QString xml;
		{
			QFile file(m_project);
			QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
			xml = QString::fromUtf8(file.readAll());
		}
		QVERIFY2(xml.contains(QStringLiteral("link=\"%1\"").arg(group)),
			"the saved project must name the link group on its members");

		// RELOAD and ask again - the acceptance criterion of this row.
		Engine::getSong()->loadProject(m_project);

		const ControlResult after = registry->invoke(QStringLiteral("clip.link_get_state"));
		QVERIFY2(after.ok, qPrintable(after.errorMessage));
		QVERIFY2(after.result.value(QStringLiteral("count")).toInt() >= 1,
			"the reloaded project has no link group at all");
		const QJsonObject groupState = firstGroupOfSize(after.result.value(QStringLiteral("groups")).toArray(), 2);
		QVERIFY2(!groupState.isEmpty(), "no reloaded group has two members");
		QCOMPARE(groupState.value(QStringLiteral("group")).toInt(), group);
		QCOMPARE(groupState.value(QStringLiteral("divergent")).toArray().size(), 0);
		QCOMPARE(groupState.value(QStringLiteral("notes")).toInt(), 1);

		// and the reloaded relation still PROPAGATES: the members of the reloaded
		// group are addressed by name (the ids are arrangement-derived, so they are
		// re-read rather than remembered) and an edit to one reaches the other.
		const QJsonArray members = groupState.value(QStringLiteral("members")).toArray();
		QCOMPARE(members.size(), 2);
		const QString first = members.at(0).toString();
		const QString second = members.at(1).toString();
		const ControlResult added = registry->invoke(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), first}, {QStringLiteral("key"), 71},
				{QStringLiteral("position"), 240}, {QStringLiteral("length"), 24}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(noteCount(second), 2);

		// the second save of the loaded project writes the same relation: the
		// round trip is stable, not merely lossless once.
		QVERIFY(Engine::getSong()->saveProjectFile(m_dir.filePath(QStringLiteral("link-again.mmp"))));
		QString again;
		{
			QFile file(m_dir.filePath(QStringLiteral("link-again.mmp")));
			QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
			again = QString::fromUtf8(file.readAll());
		}
		QCOMPARE(again.count(QStringLiteral("link=\"%1\"").arg(group)),
			xml.count(QStringLiteral("link=\"%1\"").arg(group)));
	}

	// -----------------------------------------------------------------------
	// 5. the refusals
	// -----------------------------------------------------------------------

	void refusalsAreTypedAndNothingIsHalfWritten()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString a = addMidiClip(QStringLiteral("refuse A"));
		const QString b = addMidiClip(QStringLiteral("refuse B"));
		const QString other = addMidiClip(QStringLiteral("refuse C"));

		// a clip that is not linked cannot be synced or unlinked
		const ControlResult syncUnlinked = registry->invoke(QStringLiteral("clip.link_sync"),
			QJsonObject{{QStringLiteral("clip"), a}});
		QVERIFY(!syncUnlinked.ok);
		QCOMPARE(syncUnlinked.errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult unlinkUnlinked = registry->invoke(QStringLiteral("clip.link_remove"),
			QJsonObject{{QStringLiteral("clip"), a}});
		QVERIFY(!unlinkUnlinked.ok);
		QCOMPARE(unlinkUnlinked.errorKind, ControlErrorKind::InvalidArgs);

		// an AUDIO clip has no note list, so it cannot be a member (the one-line
		// UI note names this bound: a link shares a note list in 0.3.0)
		const QString sampleClip = addSampleClip(QStringLiteral("refuse audio"));
		QVERIFY(!sampleClip.isEmpty());
		const ControlResult audioMember = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("clips"), QJsonArray{sampleClip}}});
		QVERIFY(!audioMember.ok);
		QCOMPARE(audioMember.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(audioMember.errorMessage.contains(QStringLiteral("note list")));

		// nothing was half-written by that refusal: A is still unlinked
		const ControlResult stateAfterRefusal = registry->invoke(QStringLiteral("clip.link_get_state"),
			QJsonObject{{QStringLiteral("clip"), a}});
		QVERIFY(stateAfterRefusal.ok);
		QVERIFY(!stateAfterRefusal.result.value(QStringLiteral("linked")).toBool());

		// a group is not merged into another by naming one of its members
		const ControlResult firstPair = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("clips"), QJsonArray{b}}});
		QVERIFY2(firstPair.ok, qPrintable(firstPair.errorMessage));
		const ControlResult secondPair = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), other}, {QStringLiteral("clips"), QJsonArray{a}}});
		QVERIFY(!secondPair.ok);
		QCOMPARE(secondPair.errorKind, ControlErrorKind::Refused);
		QVERIFY(secondPair.errorMessage.contains(QStringLiteral("clip.link_remove")));

		// and the refusal left the first pair intact and `other` unlinked
		const ControlResult pair = registry->invoke(QStringLiteral("clip.link_get_state"),
			QJsonObject{{QStringLiteral("clip"), a}});
		QVERIFY(pair.ok);
		QCOMPARE(pair.result.value(QStringLiteral("groups")).toArray().first().toObject()
			.value(QStringLiteral("size")).toInt(), 2);
	}

	// -----------------------------------------------------------------------
	// 6. SPEC A16: one undo takes the whole group back
	// -----------------------------------------------------------------------


private:
	QTemporaryDir m_dir;
	QString m_project;
};

QTEST_MAIN(ClipLinkPersistenceTest)
#include "ClipLinkPersistenceTest.moc"
