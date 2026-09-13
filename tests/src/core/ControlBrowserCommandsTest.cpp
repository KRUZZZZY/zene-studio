/*
 * ControlBrowserCommandsTest.cpp - the browser.* command surface (SPEC A11-A16):
 *                                  the ids and schemas the release contract
 *                                  requires, the typed refusals that let the
 *                                  whole registry be swept with junk arguments,
 *                                  and - the part that is not optional - an
 *                                  inverse that actually works.
 *
 * The load-bearing case is `tagEditsAreReversibleThroughTheRecordedInverse`.
 * A tag lives in a JSON file in the user's config directory, and that file is
 * not a JournallingObject, so there is no ProjectJournal checkpoint to pop: the
 * class is SNAPSHOT and the inverse is the PAIRED COMMAND, which control.undo
 * dispatches through the registry. This test applies the edit, asks
 * control.undo to take it back, and then reads the TAG STORE FILE back - a
 * transaction record that merely claimed an inverse would pass a
 * `lastTransaction()`-only assertion and fail this one.
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
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "BrowserCatalog.h"
#include "BrowserPeakCache.h"
#include "BrowserTestSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"

using namespace lmms;
using namespace browserfixture;

namespace
{

const QStringList kMutating = {QStringLiteral("browser.tag.add"), QStringLiteral("browser.tag.remove")};
const QStringList kReadOnly = {QStringLiteral("browser.roots"), QStringLiteral("browser.query"),
	QStringLiteral("browser.tags"), QStringLiteral("browser.peaks")};

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

const control::ReversibilityEntry* contractRow(const QString& id)
{
	return control::ReversibilityTable::instance().lookup(id);
}

//! The tags of \a path as the STORE FILE holds them, read back from disk: the
//! only evidence that a write (or an undo) actually reached the user's library.
QStringList tagsOnDisk(const QString& storePath, const QString& path)
{
	const QString saved = BrowserTagStore::instance().storePath();
	BrowserTagStore::instance().openAt(storePath);
	const QStringList tags = BrowserTagStore::instance().tagsOf(path);
	BrowserTagStore::instance().openAt(saved);
	return tags;
}

} // namespace

class ControlBrowserCommandsTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY2(m_dir.isValid(), "no temporary directory for the browser fixtures");
		m_wave = QDir(m_dir.path()).filePath(QStringLiteral("kick_48k.wav"));
		QVERIFY(writeWave(m_wave, WaveSpec()));
		m_text = QDir(m_dir.path()).filePath(QStringLiteral("notes.txt"));
		QVERIFY(writeText(m_text, QStringLiteral("not audio")));
		m_store = QDir(m_dir.path()).filePath(QStringLiteral("browser-tags.json"));
		// The store is a singleton shared with the engine test's fixtures; point
		// it at this test's own file so nothing outside the temporary tree is
		// read or written.
		QVERIFY(BrowserTagStore::instance().openAt(m_store));
	}

	void cleanupTestCase()
	{
		BrowserTagStore::instance().openAt(QDir(m_dir.path()).filePath(QStringLiteral("done.json")));
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Every command of the group declares the contract's parts: a group.verb id,
	//! both schemas, a description, an empty `requires` (headless-safe) and the
	//! right mutating flag.
	void requiredCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList all = kReadOnly + kMutating;
		for (const QString& id : all)
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(QStringLiteral("missing command %1").arg(id)));
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			QCOMPARE(cmd->group, QStringLiteral("browser"));
			QVERIFY(!cmd->verb.isEmpty());
			QVERIFY(!cmd->description.isEmpty());
			QVERIFY(!cmd->argsSchema.isEmpty());
			QVERIFY(!cmd->resultSchema.isEmpty());
			// SPEC A13 headless parity: the declaration exists and is empty.
			QVERIFY2(cmd->requiresDecl.isEmpty(), qPrintable(id + " declares a requires excuse"));
			QCOMPARE(cmd->mutating, kMutating.contains(id));
		}
		// The peak reader takes the path it measures as a REQUIRED argument.
		QCOMPARE(registry->command(QStringLiteral("browser.peaks"))
			->argsSchema.value(QStringLiteral("required")).toArray().size(), 1);
	}

	//! The contract table classifies the group, and the class the handler records
	//! is the class the registry stamps (SPEC A16: one definition, no drift).
	void contractRowsClassifyTheGroup()
	{
		for (const QString& id : kMutating)
		{
			const control::ReversibilityEntry* row = contractRow(id);
			QVERIFY2(row != nullptr, qPrintable(id + " has no contract row"));
			QCOMPARE(control::reversibilityClassName(row->cls), QStringLiteral("snapshot"));
			QVERIFY(row->reversible);
			QVERIFY(!row->reason.isEmpty());
			QVERIFY(!row->mechanism.isEmpty());
		}
		for (const QString& id : kReadOnly)
		{
			const control::ReversibilityEntry* row = contractRow(id);
			QVERIFY2(row != nullptr, qPrintable(id + " has no contract row"));
			QCOMPARE(control::reversibilityClassName(row->cls), QStringLiteral("not_mutating"));
			QCOMPARE(row->reversible, false);
			QVERIFY(!row->mechanism.isEmpty());
		}
	}

	//! browser.roots reports the browser's own directories and the store's path.
	void rootsReportTheBrowsersDirectories()
	{
		const ControlResult roots = run(QStringLiteral("browser.roots"));
		QVERIFY2(roots.ok, qPrintable(roots.errorMessage));
		const QJsonArray list = roots.result.value(QStringLiteral("roots")).toArray();
		QCOMPARE(list.size(), 6);
		QCOMPARE(list.first().toObject().value(QStringLiteral("id")).toString(),
			QStringLiteral("samples"));
		QVERIFY(list.first().toObject().contains(QStringLiteral("exists")));
		QCOMPARE(roots.result.value(QStringLiteral("store_path")).toString(),
			BrowserTagStore::instance().storePath());
	}

	//! A query over an explicit directory: the kind filter, the metadata probe,
	//! and the reported bounds.
	void queryFindsByNameAndByMetadata()
	{
		QJsonObject args;
		args.insert(QStringLiteral("path"), m_dir.path());
		args.insert(QStringLiteral("text"), QStringLiteral("kick"));
		const ControlResult byName = run(QStringLiteral("browser.query"), args);
		QVERIFY2(byName.ok, qPrintable(byName.errorMessage));
		QCOMPARE(byName.result.value(QStringLiteral("matched")).toInt(), 1);
		QCOMPARE(byName.result.value(QStringLiteral("probed")).toInt(), 0);
		const QJsonObject item = byName.result.value(QStringLiteral("items")).toArray()
			.first().toObject();
		QCOMPARE(item.value(QStringLiteral("name")).toString(), QStringLiteral("kick_48k.wav"));
		QCOMPARE(item.value(QStringLiteral("kind")).toString(), QStringLiteral("audio"));
		QVERIFY(!item.contains(QStringLiteral("metadata")));

		// "drum" is only in the file's embedded title. The kind filter keeps the
		// probe to the one audio file, so the counter measures the metadata match
		// rather than the number of files in the directory.
		QJsonObject probed = args;
		probed.insert(QStringLiteral("text"), QStringLiteral("drum"));
		probed.insert(QStringLiteral("probe"), true);
		probed.insert(QStringLiteral("kind"), QStringLiteral("audio"));
		const ControlResult byMetadata = run(QStringLiteral("browser.query"), probed);
		QVERIFY2(byMetadata.ok, qPrintable(byMetadata.errorMessage));
		QCOMPARE(byMetadata.result.value(QStringLiteral("matched")).toInt(), 1);
		QCOMPARE(byMetadata.result.value(QStringLiteral("probed")).toInt(), 1);
		const QJsonObject metadata = byMetadata.result.value(QStringLiteral("items")).toArray()
			.first().toObject().value(QStringLiteral("metadata")).toObject();
		QCOMPARE(metadata.value(QStringLiteral("sample_rate")).toInt(), 48000);
		QCOMPARE(metadata.value(QStringLiteral("channels")).toInt(), 2);
		QCOMPARE(metadata.value(QStringLiteral("tags")).toObject()
			.value(QStringLiteral("title")).toString(), QStringLiteral("Kick Drum"));

		// The scan and probe bounds are reported, not implied.
		QCOMPARE(byMetadata.result.value(QStringLiteral("scan_limit")).toInt(), BrowserMaxScanEntries);
		QCOMPARE(byMetadata.result.value(QStringLiteral("probe_limit")).toInt(), BrowserMaxProbes);
		QCOMPARE(byMetadata.result.value(QStringLiteral("truncated")).toBool(), false);
	}

	//! The peaks command answers the waterfall for a file, and the cache is
	//! observable through the socket: the second call for the same file says
	//! `cached: true`.
	void peaksAnswerAndReportTheCache()
	{
		BrowserPeakCache::instance().clear();
		QJsonObject args;
		args.insert(QStringLiteral("path"), m_wave);
		args.insert(QStringLiteral("buckets"), 32);

		const ControlResult first = run(QStringLiteral("browser.peaks"), args);
		QVERIFY2(first.ok, qPrintable(first.errorMessage));
		QCOMPARE(first.result.value(QStringLiteral("cached")).toBool(), false);
		QCOMPARE(first.result.value(QStringLiteral("buckets")).toInt(), 32);
		QCOMPARE(first.result.value(QStringLiteral("bucket_frames")).toDouble(), 64.0);
		QCOMPARE(first.result.value(QStringLiteral("frames")).toDouble(), 2048.0);
		QCOMPARE(first.result.value(QStringLiteral("sample_rate")).toInt(), 48000);
		QCOMPARE(first.result.value(QStringLiteral("channels")).toInt(), 2);
		const QJsonArray peaks = first.result.value(QStringLiteral("peaks")).toArray();
		QCOMPARE(peaks.size(), 32);
		// The fixture's burst is exactly bucket 16 of 32 (frames 1024..1087).
		const QJsonObject burst = peaks.at(16).toObject();
		QVERIFY2(qAbs(burst.value(QStringLiteral("max")).toDouble() - 20000.0 / 32768.0) < 0.001,
			qPrintable(QString::number(burst.value(QStringLiteral("max")).toDouble())));
		QVERIFY(qAbs(burst.value(QStringLiteral("min")).toDouble() + 20000.0 / 32768.0) < 0.001);
		QCOMPARE(peaks.at(0).toObject().value(QStringLiteral("max")).toDouble(), 0.0);

		const ControlResult second = run(QStringLiteral("browser.peaks"), args);
		QVERIFY2(second.ok, qPrintable(second.errorMessage));
		QCOMPARE(second.result.value(QStringLiteral("cached")).toBool(), true);
		const QJsonObject cache = second.result.value(QStringLiteral("cache")).toObject();
		QCOMPARE(cache.value(QStringLiteral("capacity")).toInt(), BrowserPeakCache::Capacity);
		QCOMPARE(cache.value(QStringLiteral("base_buckets")).toInt(), BrowserPeakCache::BaseBuckets);
		QCOMPARE(cache.value(QStringLiteral("hits")).toInt(), 1);
		QCOMPARE(cache.value(QStringLiteral("misses")).toInt(), 1);
	}

	//! Tag add/remove through the commands, with the store FILE read back.
	void tagCommandsWriteTheStore()
	{
		const ControlResult added = run(QStringLiteral("browser.tag.add"),
			{{QStringLiteral("path"), m_wave}, {QStringLiteral("tag"), QStringLiteral("bass")}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(added.result.value(QStringLiteral("action")).toString(), QStringLiteral("added"));
		QCOMPARE(tagsOnDisk(m_store, m_wave), QStringList{QStringLiteral("bass")});

		const ControlResult second = run(QStringLiteral("browser.tag.add"),
			{{QStringLiteral("path"), m_wave}, {QStringLiteral("tag"), QStringLiteral("BASS")}});
		QCOMPARE(second.ok, false);
		QCOMPARE(second.errorKind, ControlErrorKind::Refused);
		QCOMPARE(tagsOnDisk(m_store, m_wave).size(), 1);

		const ControlResult vocabulary = run(QStringLiteral("browser.tags"));
		QVERIFY2(vocabulary.ok, qPrintable(vocabulary.errorMessage));
		QCOMPARE(vocabulary.result.value(QStringLiteral("distinct")).toInt(), 1);
		QCOMPARE(vocabulary.result.value(QStringLiteral("entries")).toInt(), 1);
		QCOMPARE(vocabulary.result.value(QStringLiteral("tags")).toArray().first().toObject()
			.value(QStringLiteral("files")).toInt(), 1);

		// A query filtered by that tag finds the file.
		const ControlResult byTag = run(QStringLiteral("browser.query"),
			{{QStringLiteral("path"), m_dir.path()},
				{QStringLiteral("tags"), QJsonArray{QStringLiteral("bass")}}});
		QVERIFY2(byTag.ok, qPrintable(byTag.errorMessage));
		QCOMPARE(byTag.result.value(QStringLiteral("matched")).toInt(), 1);

		const ControlResult removed = run(QStringLiteral("browser.tag.remove"),
			{{QStringLiteral("path"), m_wave}, {QStringLiteral("tag"), QStringLiteral("bass")}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(removed.result.value(QStringLiteral("action")).toString(), QStringLiteral("removed"));
		QCOMPARE(tagsOnDisk(m_store, m_wave).size(), 0);
		QCOMPARE(run(QStringLiteral("browser.tag.remove"),
			{{QStringLiteral("path"), m_wave}, {QStringLiteral("tag"), QStringLiteral("bass")}}).errorKind,
			ControlErrorKind::NotFound);
	}

	//! THE A16 PROOF. add -> control.undo -> the tag is gone from the STORE FILE,
	//! and remove -> control.undo -> it is back. The recorded inverse is a
	//! command, so control.undo dispatches it rather than unwinding a journal
	//! that holds nothing for a file in the config directory.
	void tagEditsAreReversibleThroughTheRecordedInverse()
	{
		QVERIFY(run(QStringLiteral("browser.tag.add"),
			{{QStringLiteral("path"), m_wave}, {QStringLiteral("tag"), QStringLiteral("kick")}}).ok);

		const ControlRegistry::Transaction* tx = ControlRegistry::instance()->lastTransaction();
		QVERIFY(tx != nullptr);
		QCOMPARE(tx->command, QStringLiteral("browser.tag.add"));
		QCOMPARE(tx->cls, QStringLiteral("snapshot"));
		QCOMPARE(tx->reversible, true);
		QVERIFY2(tx->mechanism.contains(QStringLiteral("snapshot")), qPrintable(tx->mechanism));
		const QJsonObject inverse = tx->inverse;
		QCOMPARE(inverse.value(QStringLiteral("op")).toString(), QStringLiteral("browser.tag.remove"));
		QCOMPARE(inverse.value(QStringLiteral("applies")).toString(), QStringLiteral("command"));
		QCOMPARE(inverse.value(QStringLiteral("args")).toObject()
			.value(QStringLiteral("tag")).toString(), QStringLiteral("kick"));

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(undone.result.value(QStringLiteral("restored_by")).toString(),
			QStringLiteral("browser.tag.remove"));
		QCOMPARE(tagsOnDisk(m_store, m_wave).size(), 0);
		QCOMPARE(run(QStringLiteral("browser.tags")).result.value(QStringLiteral("entries")).toInt(), 0);

		// The other direction, and the harder one: removing the LAST tag drops
		// the file's entry from the store, and the recorded inverse recreates it.
		QVERIFY(run(QStringLiteral("browser.tag.add"),
			{{QStringLiteral("path"), m_wave}, {QStringLiteral("tag"), QStringLiteral("snare")}}).ok);
		QVERIFY(run(QStringLiteral("browser.tag.add"),
			{{QStringLiteral("path"), m_wave}, {QStringLiteral("tag"), QStringLiteral("clap")}}).ok);
		QVERIFY(run(QStringLiteral("browser.tag.remove"),
			{{QStringLiteral("path"), m_wave}, {QStringLiteral("tag"), QStringLiteral("snare")}}).ok);
		QCOMPARE(tagsOnDisk(m_store, m_wave), QStringList{QStringLiteral("clap")});

		const ControlRegistry::Transaction* removal = ControlRegistry::instance()->lastTransaction();
		QVERIFY(removal != nullptr);
		QCOMPARE(removal->command, QStringLiteral("browser.tag.remove"));
		QCOMPARE(removal->inverse.value(QStringLiteral("op")).toString(),
			QStringLiteral("browser.tag.add"));
		const ControlResult restored = run(QStringLiteral("control.undo"));
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(restored.result.value(QStringLiteral("restored_by")).toString(),
			QStringLiteral("browser.tag.add"));
		QCOMPARE(tagsOnDisk(m_store, m_wave),
			QStringList({QStringLiteral("clap"), QStringLiteral("snare")}));

		// Leave the store empty for the tests that follow.
		QVERIFY(run(QStringLiteral("browser.tag.remove"),
			{{QStringLiteral("path"), m_wave}, {QStringLiteral("tag"), QStringLiteral("clap")}}).ok);
		QVERIFY(run(QStringLiteral("browser.tag.remove"),
			{{QStringLiteral("path"), m_wave}, {QStringLiteral("tag"), QStringLiteral("snare")}}).ok);
		QCOMPARE(tagsOnDisk(m_store, m_wave).size(), 0);
	}

	//! Every refusal is typed with a message, and nothing is written.
	void refusalsAreTypedAndChangeNothing()
	{
		// Junk arguments: the sweep that every registered command must survive.
		QCOMPARE(run(QStringLiteral("browser.query"),
			{{QStringLiteral("kind"), QStringLiteral("nope")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("browser.query"),
			{{QStringLiteral("root"), QStringLiteral("nope")}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("browser.query"),
			{{QStringLiteral("path"), QStringLiteral("relative/dir")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("browser.query"),
			{{QStringLiteral("path"), m_text}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("browser.query"),
			{{QStringLiteral("tags"), QStringLiteral("not-an-array")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("browser.query"), QJsonObject()).ok, true);
		QCOMPARE(run(QStringLiteral("browser.peaks"), QJsonObject()).errorKind,
			ControlErrorKind::InvalidArgs);

		// The file arguments: empty, relative, absent, a directory, non-audio.
		for (const QString& id : kMutating)
		{
			QCOMPARE(run(id, {{QStringLiteral("path"), QString()},
				{QStringLiteral("tag"), QStringLiteral("x")}}).errorKind,
				ControlErrorKind::InvalidArgs);
			QCOMPARE(run(id, {{QStringLiteral("path"), QStringLiteral("rel.wav")},
				{QStringLiteral("tag"), QStringLiteral("x")}}).errorKind,
				ControlErrorKind::InvalidArgs);
			QCOMPARE(run(id, {{QStringLiteral("path"), m_dir.path() + QStringLiteral("/absent.wav")},
				{QStringLiteral("tag"), QStringLiteral("x")}}).errorKind, ControlErrorKind::NotFound);
			QCOMPARE(run(id, {{QStringLiteral("path"), m_dir.path()},
				{QStringLiteral("tag"), QStringLiteral("x")}}).errorKind, ControlErrorKind::NotFound);
			QCOMPARE(run(id, {{QStringLiteral("path"), m_wave},
				{QStringLiteral("tag"), QString()}}).errorKind, ControlErrorKind::InvalidArgs);
			QCOMPARE(run(id, {{QStringLiteral("path"), m_wave},
				{QStringLiteral("tag"), QString(80, QLatin1Char('x'))}}).errorKind,
				ControlErrorKind::InvalidArgs);
			QCOMPARE(run(id, {{QStringLiteral("path"), m_wave},
				{QStringLiteral("tag"), QStringLiteral("bad\nvalue")}}).errorKind,
				ControlErrorKind::InvalidArgs);
		}
		QCOMPARE(run(QStringLiteral("browser.tag.remove"),
			{{QStringLiteral("path"), m_wave}, {QStringLiteral("tag"), QStringLiteral("never")}}).errorKind,
			ControlErrorKind::NotFound);

		// A file libsndfile cannot read is refused by the peak reader, and the
		// cache holds nothing for it.
		BrowserPeakCache::instance().clear();
		const ControlResult refused = run(QStringLiteral("browser.peaks"),
			{{QStringLiteral("path"), m_text}, {QStringLiteral("buckets"), 32}});
		QCOMPARE(refused.ok, false);
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY(refused.errorMessage.contains(QStringLiteral("no peak map")));
		QCOMPARE(BrowserPeakCache::instance().entryCount(), 0);

		// Nothing above wrote a tag.
		QCOMPARE(tagsOnDisk(m_store, m_wave).size(), 0);
		QCOMPARE(run(QStringLiteral("browser.tags")).result.value(QStringLiteral("entries")).toInt(), 0);
	}

private:
	QTemporaryDir m_dir;
	QString m_wave;
	QString m_text;
	QString m_store;
};

QTEST_GUILESS_MAIN(ControlBrowserCommandsTest)
#include "ControlBrowserCommandsTest.moc"
