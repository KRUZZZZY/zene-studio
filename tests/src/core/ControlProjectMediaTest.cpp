/*
 * ControlProjectMediaTest.cpp - Q050-121's agent surface and its measured effect:
 *                               per-file usage counts (repeated references add
 *                               up), the unused files in the managed media
 *                               folder, distinct missing/external states, and a
 *                               cleanup that previews, rechecks and removes only
 *                               still-unused local files.
 *
 * The engine half is include/ControlProjectMedia.h over the saved project
 * document; this test drives the registered ids through ControlRegistry::invoke,
 * the same entry the socket and MCP use.
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
 */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	return file.write(bytes) == bytes.size();
}

QString quoted(const QString& path)
{
	return QString(path).replace(QLatin1Char('&'), QStringLiteral("&amp;"))
		.replace(QLatin1Char('<'), QStringLiteral("&lt;")).replace(QLatin1Char('"'), QStringLiteral("&quot;"));
}

QJsonObject fileNamed(const QJsonArray& files, const QString& name)
{
	for (const QJsonValue& value : files)
	{
		if (value.toObject().value(QStringLiteral("name")).toString() == name) { return value.toObject(); }
	}
	return QJsonObject();
}

QStringList pathsOf(const QJsonArray& entries)
{
	QStringList out;
	for (const QJsonValue& value : entries) { out << value.toObject().value(QStringLiteral("path")).toString(); }
	return out;
}

} // namespace

class ControlProjectMediaTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY2(m_dir.isValid(), "no temporary directory for the project-media fixtures");
		m_media = QDir(m_dir.path()).filePath(QStringLiteral("media"));
		QVERIFY(QDir().mkpath(m_media + QStringLiteral("/sub")));
		m_used = m_media + QStringLiteral("/used.wav");
		m_unused = m_media + QStringLiteral("/unused.wav");
		m_unusedSub = m_media + QStringLiteral("/sub/unused2.wav");
		m_external = QDir(m_dir.path()).filePath(QStringLiteral("external.wav"));
		m_missing = m_media + QStringLiteral("/gone.wav");
		for (const QString& path : {m_used, m_unused, m_unusedSub, m_external})
		{
			QVERIFY2(writeBytes(path, QByteArray("RIFF....WAVEfmt ")), qPrintable(path));
		}
		m_project = QDir(m_dir.path()).filePath(QStringLiteral("song.mmp"));
		const QString xml = QStringLiteral(
			"<?xml version=\"1.0\"?>\n"
			"<zene-project creator=\"Zene Studio\" version=\"1.0\" type=\"song\" creatorversion=\"0.5.0\">\n"
			"  <head bpm=\"120\" timesig_numerator=\"4\" timesig_denominator=\"4\"/>\n"
			"  <song><trackcontainer type=\"song\"><track name=\"Audio\" type=\"0\">\n"
			"    <instrumenttrack><instrument name=\"audiofileprocessor\">\n"
			"      <audiofileprocessor src=\"%1\" amp=\"100\"/></instrument>\n"
			"      <sampleclip pos=\"0\" len=\"192\" src=\"%1\"/>\n"
			"      <sampleclip pos=\"192\" len=\"192\" src=\"%1\"/>\n"
			"      <sampleclip pos=\"384\" len=\"192\" src=\"%2\"/>\n"
			"      <sampleclip pos=\"576\" len=\"192\" src=\"%3\"/>\n"
			"    </instrumenttrack>\n"
			"  </track></trackcontainer></song>\n"
			"</zene-project>\n").arg(quoted(m_used), quoted(m_missing), quoted(m_external));
		QVERIFY(writeBytes(m_project, xml.toUtf8()));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theMediaVerbsAreRegisteredWithHonestClasses()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		for (const QString& id : {QStringLiteral("project.media_usage"), QStringLiteral("project.media_cleanup")})
		{
			const ControlCommand* cmd = registry->command(id);
			QVERIFY2(cmd != nullptr, qPrintable(id));
			QCOMPARE(cmd->group, QStringLiteral("project"));
			QVERIFY(!cmd->argsSchema.isEmpty() && !cmd->resultSchema.isEmpty() && !cmd->description.isEmpty());
		}
		QCOMPARE(registry->command(QStringLiteral("project.media_usage"))->mutating, false);
		QCOMPARE(registry->command(QStringLiteral("project.media_cleanup"))->mutating, true);

		const control::ReversibilityEntry* usage = control::ReversibilityTable::instance()
			.lookup(QStringLiteral("project.media_usage"));
		const control::ReversibilityEntry* cleanup = control::ReversibilityTable::instance()
			.lookup(QStringLiteral("project.media_cleanup"));
		QVERIFY(usage != nullptr && cleanup != nullptr);
		QCOMPARE(usage->cls, control::ReversibilityClass::NotMutating);
		QCOMPARE(cleanup->cls, control::ReversibilityClass::Irreversible);
	}

	//! One file referenced by an instrument and two clips counts 3; a file outside
	//! media is external; a reference with nothing on disk is missing; and only
	//! the unreferenced files in media/ are cleanup candidates.
	void usageCountsRepeatedReferencesAndSeparatesStates()
	{
		const ControlResult listed = run(QStringLiteral("project.media_usage"),
			{{QStringLiteral("project"), m_project}});
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		const QJsonArray files = listed.result.value(QStringLiteral("files")).toArray();

		const QJsonObject used = fileNamed(files, QStringLiteral("used.wav"));
		QCOMPARE(used.value(QStringLiteral("usage_count")).toInt(), 3);
		QCOMPARE(used.value(QStringLiteral("state")).toString(), QStringLiteral("used"));
		QCOMPARE(used.value(QStringLiteral("cleanup_candidate")).toBool(), false);

		const QJsonObject external = fileNamed(files, QStringLiteral("external.wav"));
		QCOMPARE(external.value(QStringLiteral("usage_count")).toInt(), 1);
		QCOMPARE(external.value(QStringLiteral("state")).toString(), QStringLiteral("external"));
		QCOMPARE(external.value(QStringLiteral("cleanup_candidate")).toBool(), false);

		const QJsonObject missing = fileNamed(files, QStringLiteral("gone.wav"));
		QCOMPARE(missing.value(QStringLiteral("state")).toString(), QStringLiteral("missing"));
		QCOMPARE(missing.value(QStringLiteral("cleanup_candidate")).toBool(), false);

		const QJsonObject unused = fileNamed(files, QStringLiteral("unused.wav"));
		QCOMPARE(unused.value(QStringLiteral("usage_count")).toInt(), 0);
		QCOMPARE(unused.value(QStringLiteral("state")).toString(), QStringLiteral("unused"));
		QCOMPARE(unused.value(QStringLiteral("cleanup_candidate")).toBool(), true);

		QCOMPARE(listed.result.value(QStringLiteral("unused_count")).toInt(), 2);  // unused.wav + sub/unused2.wav
		QCOMPARE(listed.result.value(QStringLiteral("missing_count")).toInt(), 1);
		QCOMPARE(listed.result.value(QStringLiteral("external_count")).toInt(), 1);

		// The old reference list reports the same per-file count, so an existing
		// project.missing_assets client sees it without a second parser.
		const ControlResult refs = run(QStringLiteral("project.missing_assets"),
			{{QStringLiteral("project"), m_project}});
		QVERIFY(refs.ok);
		for (const QJsonValue& value : refs.result.value(QStringLiteral("references")).toArray())
		{
			const QJsonObject ref = value.toObject();
			if (ref.value(QStringLiteral("path")).toString() == QFileInfo(m_used).canonicalFilePath())
			{
				QCOMPARE(ref.value(QStringLiteral("usage_count")).toInt(), 3);
			}
		}
	}

	//! A dry run names exactly the selection; a real cleanup removes only the
	//! still-unused local files and leaves every referenced/external file alone.
	void cleanupPreviewsRechecksAndLeavesReferencedAssetsAlone()
	{
		const QStringList selection{m_unused, m_used, m_external, m_missing};
		const ControlResult preview = run(QStringLiteral("project.media_cleanup"),
			{{QStringLiteral("project"), m_project}, {QStringLiteral("paths"), QJsonArray::fromStringList(selection)},
				{QStringLiteral("dry_run"), true}});
		QVERIFY2(preview.ok, qPrintable(preview.errorMessage));
		QCOMPARE(preview.result.value(QStringLiteral("dry_run")).toBool(), true);
		QCOMPARE(pathsOf(preview.result.value(QStringLiteral("removed")).toArray()),
			QStringList{QFileInfo(m_unused).absoluteFilePath()});
		QCOMPARE(preview.result.value(QStringLiteral("refused")).toArray().size(), 3);
		QVERIFY(QFileInfo::exists(m_unused));  // a preview writes nothing
		QVERIFY(QFileInfo::exists(m_used));

		// A real removal needs the authorization flag; without it nothing is deleted.
		const ControlResult unconfirmed = run(QStringLiteral("project.media_cleanup"),
			{{QStringLiteral("project"), m_project}, {QStringLiteral("paths"), QJsonArray::fromStringList(
				QStringList{m_unused})}});
		QCOMPARE(unconfirmed.ok, false);
		QCOMPARE(unconfirmed.errorKind, ControlErrorKind::Refused);
		QVERIFY(QFileInfo::exists(m_unused));

		const ControlResult cleaned = run(QStringLiteral("project.media_cleanup"),
			{{QStringLiteral("project"), m_project}, {QStringLiteral("paths"), QJsonArray::fromStringList(
				QStringList{m_unused, m_used, m_external})}, {QStringLiteral("confirm"), true}});
		QVERIFY2(cleaned.ok, qPrintable(cleaned.errorMessage));
		QCOMPARE(cleaned.result.value(QStringLiteral("removed")).toArray().size(), 1);
		QCOMPARE(cleaned.result.value(QStringLiteral("refused")).toArray().size(), 2);
		QVERIFY(!QFileInfo::exists(m_unused));
		QVERIFY2(QFileInfo::exists(m_used), "a referenced file was removed");
		QVERIFY2(QFileInfo::exists(m_external), "an external file was removed");
		QVERIFY2(!QFileInfo::exists(m_missing), "a missing path was invented");
		// The remaining unused file is still the only candidate.
		QCOMPARE(cleaned.result.value(QStringLiteral("unused_after")).toArray().size(), 1);
	}

	//! A preview is a promise about the state it saw, not a token to delete: a
	//! file the project starts referencing after the preview is refused by the
	//! fresh scan the real cleanup takes immediately before removing anything.
	void cleanupRefusesASelectionReferencedAfterThePreview()
	{
		const QStringList selection{m_unusedSub};
		const ControlResult preview = run(QStringLiteral("project.media_cleanup"),
			{{QStringLiteral("project"), m_project}, {QStringLiteral("paths"), QJsonArray::fromStringList(selection)},
				{QStringLiteral("dry_run"), true}});
		QVERIFY2(preview.ok, qPrintable(preview.errorMessage));
		QCOMPARE(pathsOf(preview.result.value(QStringLiteral("removed")).toArray()),
			QStringList{QFileInfo(m_unusedSub).absoluteFilePath()});
		QVERIFY(QFileInfo::exists(m_unusedSub));

		// The project document changes after the preview: the file is referenced now.
		QFile project(m_project);
		QVERIFY(project.open(QIODevice::ReadOnly));
		const QByteArray original = project.readAll();
		project.close();
		QByteArray updated = original;
		updated.replace("</instrumenttrack>",
			QStringLiteral("<sampleclip pos=\"768\" len=\"192\" src=\"%1\"/></instrumenttrack>")
				.arg(quoted(m_unusedSub)).toUtf8());
		QVERIFY(updated != original);
		QVERIFY(writeBytes(m_project, updated));

		const ControlResult stale = run(QStringLiteral("project.media_cleanup"),
			{{QStringLiteral("project"), m_project}, {QStringLiteral("paths"), QJsonArray::fromStringList(selection)},
				{QStringLiteral("confirm"), true}});
		QVERIFY2(stale.ok, qPrintable(stale.errorMessage));
		QCOMPARE(stale.result.value(QStringLiteral("removed")).toArray().size(), 0);
		const QJsonArray refused = stale.result.value(QStringLiteral("refused")).toArray();
		QCOMPARE(refused.size(), 1);
		QVERIFY2(refused.first().toObject().value(QStringLiteral("why")).toString()
			.contains(QStringLiteral("still referenced")), qPrintable(refused.first().toObject()
				.value(QStringLiteral("why")).toString()));
		QVERIFY2(QFileInfo::exists(m_unusedSub), "a stale preview removed a newly referenced file");

		QVERIFY(writeBytes(m_project, original));  // the later dirty-project test sees the first document
	}

	//! The scan is of the saved file; a dirty open project could reference a file
	//! the scan has not seen. Cleanup refuses rather than delete it.
	void cleanupRefusesWhenTheOpenProjectIsDirty()
	{
		Song* song = Engine::getSong();
		QVERIFY(song != nullptr);
		const QString previousFile = song->projectFileName();
		song->noteProjectSaved(m_project);  // the open project names the fixture
		song->setModified();                 // ... and has unsaved changes
		const ControlResult refused = run(QStringLiteral("project.media_cleanup"),
			{{QStringLiteral("project"), m_project}, {QStringLiteral("paths"), QJsonArray::fromStringList(
				QStringList{m_unusedSub})}});
		QCOMPARE(refused.ok, false);
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY2(QFileInfo::exists(m_unusedSub), "cleanup deleted from a dirty project");
		song->noteProjectSaved(previousFile);
	}

private:
	QTemporaryDir m_dir;
	QString m_media;
	QString m_used;
	QString m_unused;
	QString m_unusedSub;
	QString m_external;
	QString m_missing;
	QString m_project;
};

QTEST_GUILESS_MAIN(ControlProjectMediaTest)
#include "ControlProjectMediaTest.moc"
