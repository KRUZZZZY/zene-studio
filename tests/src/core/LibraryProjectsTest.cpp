/*
 * LibraryProjectsTest.cpp - search expressions, collections, track presets, importing tracks, the hand-off folder
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
/*! 0.5.0 L11 / L12: browser.query's expressions match what they say and a plain text still matches
 *  as one substring (WF-003); a saved collection runs, follows the library and is undone (WF-006), and
 *  runs over its own browser root with the caller's limit;
 *  a track preset round-trips and a preset for another kind of track is refused (WF-024); a named
 *  template refuses a folder in its name (WF-027); project.import_tracks lists another project's
 *  tracks and imports them in one undo step (WF-036); project.handoff writes the copy, the media
 *  folder and HANDOFF.json (WF-049). */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "BrowserCatalog.h"
#include "BrowserSearch.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

BrowserItem itemOf(const QString& path, BrowserKind kind, const QStringList& tags = {})
{
	BrowserItem item;
	item.path = path;
	item.name = QFileInfo(path).fileName();
	item.kind = kind;
	item.tags = tags;
	return item;
}

void touch(const QString& path)
{
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write("x");
}

} // namespace

class LibraryProjectsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		qputenv("ZENE_BROWSER_COLLECTIONS", m_dir.filePath(QStringLiteral("collections.json")).toLocal8Bit());
		Engine::init(true);
		// A private working directory: the browser's "samples" root is a folder this test owns.
		ConfigManager::inst()->setWorkingDir(m_dir.filePath(QStringLiteral("work")));
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void searchExpressionsMatchWhatTheySay()
	{
		const BrowserItem kick = itemOf(QStringLiteral("/lib/drums/kick 808.wav"), BrowserKind::Audio, {QStringLiteral("Drums")});
		const BrowserItem loop = itemOf(QStringLiteral("/lib/loops/kick loop.ogg"), BrowserKind::Audio);
		const BrowserItem bass = itemOf(QStringLiteral("/lib/presets/bass drum.xpf"), BrowserKind::Preset);
		QVERIFY(!BrowserSearch(QStringLiteral("kick 808")).isExpression());  // plain: one substring, as before
		QVERIFY(BrowserSearch(QStringLiteral("kick 808")).matches(kick));
		QVERIFY(!BrowserSearch(QStringLiteral("808 kick")).matches(kick));
		QVERIFY(BrowserSearch(QStringLiteral("kick -loop")).matches(kick));
		QVERIFY(!BrowserSearch(QStringLiteral("kick -loop")).matches(loop));
		QVERIFY(BrowserSearch(QStringLiteral("tag:drums")).matches(kick));
		QVERIFY(!BrowserSearch(QStringLiteral("tag:drums")).matches(loop));
		QVERIFY(BrowserSearch(QStringLiteral("kind:preset")).matches(bass));
		QVERIFY(BrowserSearch(QStringLiteral("ext:ogg OR ext:xpf")).matches(loop));
		QVERIFY(BrowserSearch(QStringLiteral("ext:ogg OR ext:xpf")).matches(bass));
		QVERIFY(!BrowserSearch(QStringLiteral("ext:ogg OR ext:xpf")).matches(kick));
		QVERIFY(BrowserSearch(QStringLiteral("\"bass drum\"")).matches(bass));
		QVERIFY(!BrowserSearch(QStringLiteral("\"drum bass\"")).matches(bass));
	}

	void aCollectionRunsAndIsUndone()
	{
		const QString lib = m_dir.filePath(QStringLiteral("lib"));
		QDir().mkpath(lib);
		touch(lib + QStringLiteral("/snare.wav"));
		touch(lib + QStringLiteral("/kick.wav"));
		const auto count = [] { return run(QStringLiteral("browser.collections")).result.value(QStringLiteral("collections")).toArray().size(); };
		const ControlResult saved = run(QStringLiteral("browser.collection_save"), {{QStringLiteral("name"), QStringLiteral("Snares")},
			{QStringLiteral("text"), QStringLiteral("name:snare")}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QCOMPARE(count(), 1);
		QVERIFY(run(QStringLiteral("browser.collection_remove"), {{QStringLiteral("name"), QStringLiteral("Snares")}}).ok);
		QCOMPARE(count(), 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the removal undone
		QCOMPARE(count(), 1);
		// What the collection's query finds in a folder: the snare, not the kick.
		const ControlResult found = run(QStringLiteral("browser.query"), {{QStringLiteral("text"), QStringLiteral("name:snare")},
			{QStringLiteral("path"), lib}});
		QCOMPARE(found.result.value(QStringLiteral("returned")).toInt(), 1);
		QCOMPARE(run(QStringLiteral("browser.collection_run"), {{QStringLiteral("name"), QStringLiteral("Nothing")}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("browser.collection_save"), {{QStringLiteral("name"), QStringLiteral("Bad")},
			{QStringLiteral("kind"), QStringLiteral("video")}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	//! A damaged store file is a corruption error, not an empty store: before this, readAll()
	//! dropped readStore's failure, so browser.collections answered success with an empty list
	//! and collection_run answered "no collection 'X'" for a collection that was on disk.
	void aDamagedCollectionsFileIsRefusedNotReadAsEmpty()
	{
		const QString store = m_dir.filePath(QStringLiteral("collections.json"));
		QVERIFY(run(QStringLiteral("browser.collection_save"), {{QStringLiteral("name"), QStringLiteral("Snares")},
			{QStringLiteral("text"), QStringLiteral("name:snare")}}).ok);
		QFile broken(store);
		QVERIFY(broken.open(QIODevice::WriteOnly));
		broken.write("{ this is not json");
		broken.close();

		const ControlResult listed = run(QStringLiteral("browser.collections"));
		QVERIFY2(!listed.ok, "a damaged collections file was listed as an empty one");
		QCOMPARE(listed.errorKind, ControlErrorKind::Refused);
		QVERIFY2(listed.errorMessage.contains(QStringLiteral("collections.json")), qPrintable(listed.errorMessage));

		const ControlResult ran = run(QStringLiteral("browser.collection_run"), {{QStringLiteral("name"), QStringLiteral("Snares")}});
		QVERIFY2(!ran.ok, "a saved collection behind a damaged store was reported as missing");
		QCOMPARE(ran.errorKind, ControlErrorKind::Refused);

		// Writing over the damaged file stays refused (that is what readForEdit is for), so the
		// repair here is the file itself: hand-edit it back and the collection reads again.
		QVERIFY(!run(QStringLiteral("browser.collection_save"), {{QStringLiteral("name"), QStringLiteral("Snares")},
			{QStringLiteral("text"), QStringLiteral("name:snare")}}).ok);
		QFile repaired(store);
		QVERIFY(repaired.open(QIODevice::WriteOnly));
		repaired.write(R"({"Snares":{"text":"name:snare"}})");
		repaired.close();
		const ControlResult listedAgain = run(QStringLiteral("browser.collections"));
		QVERIFY2(listedAgain.ok, qPrintable(listedAgain.errorMessage));
		QCOMPARE(listedAgain.result.value(QStringLiteral("collections")).toArray().size(), 1);
		QVERIFY(run(QStringLiteral("browser.collection_remove"), {{QStringLiteral("name"), QStringLiteral("Snares")}}).ok);
	}

	void aCollectionRunsOverItsRoot()
	{
		const QString samples = ConfigManager::inst()->userSamplesDir();
		QVERIFY2(samples.startsWith(m_dir.path()), qPrintable(samples));
		QVERIFY(QDir().mkpath(samples));
		for (const char* name : {"hat open.wav", "hat closed.wav", "kick.wav"}) { touch(samples + QLatin1String(name)); }
		QVERIFY(run(QStringLiteral("browser.collection_save"), {{QStringLiteral("name"), QStringLiteral("Hats")},
			{QStringLiteral("text"), QStringLiteral("name:hat")}, {QStringLiteral("root"), QStringLiteral("samples")}}).ok);
		const ControlResult found = run(QStringLiteral("browser.collection_run"), {{QStringLiteral("name"), QStringLiteral("Hats")},
			{QStringLiteral("limit"), 1}});
		QVERIFY2(found.ok, qPrintable(found.errorMessage));
		QCOMPARE(found.result.value(QStringLiteral("collection")).toString(), QStringLiteral("Hats"));
		QCOMPARE(found.result.value(QStringLiteral("matched")).toInt(), 2);   // both hats, not the kick
		QCOMPARE(found.result.value(QStringLiteral("returned")).toInt(), 1);  // the run's own limit
		const QJsonArray items = found.result.value(QStringLiteral("items")).toArray();
		QCOMPARE(items.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("hat closed.wav"));  // path order
		const QJsonArray roots = found.result.value(QStringLiteral("roots")).toArray();
		QCOMPARE(roots.size(), 1);
		QCOMPARE(roots.first().toString(), QStringLiteral("samples"));  // the stored root, not every root
	}

	void aTrackPresetRoundTrips()
	{
		Track* sample = Track::create(Track::Type::Sample, Engine::getSong());
		Track* instrument = Track::create(Track::Type::Instrument, Engine::getSong());
		sample->setName(QStringLiteral("Vox"));
		const QString path = m_dir.filePath(QStringLiteral("vox.xpf"));
		QVERIFY(run(QStringLiteral("track.preset_save"), {{QStringLiteral("track"), control::trackIdOf(sample)}, {QStringLiteral("path"), path}}).ok);
		QVERIFY(QFileInfo(path).isFile());
		QCOMPARE(run(QStringLiteral("track.preset_load"), {{QStringLiteral("track"), control::trackIdOf(instrument)},
			{QStringLiteral("path"), path}}).errorKind, ControlErrorKind::Refused);  // a sample track's preset on an instrument track
		Track* other = Track::create(Track::Type::Sample, Engine::getSong());
		other->setName(QStringLiteral("Keep"));
		QVERIFY(run(QStringLiteral("track.preset_load"), {{QStringLiteral("track"), control::trackIdOf(other)}, {QStringLiteral("path"), path}}).ok);
		QCOMPARE(other->name(), QStringLiteral("Keep"));  // the track keeps its name
		QCOMPARE(run(QStringLiteral("project.save_as_template"), {{QStringLiteral("name"), QStringLiteral("../escape")}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void aPresetWithoutATrackTypeIsRefused()
	{
		Track* instrument = Track::create(Track::Type::Instrument, Engine::getSong());
		const QString path = m_dir.filePath(QStringLiteral("typeless.xpf"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		// A structurally valid preset file whose content element carries no
		// track type at all - the value that check reads with toInt(), which
		// answers 0 for a missing or nonnumeric attribute - and 0 is the
		// instrument track's own type. Such a file must be refused, not loaded
		// onto the instrument track.
		file.write("<?xml version=\"1.0\"?>\n<!DOCTYPE zene-project>\n"
			"<zene-project version=\"40\" type=\"instrumenttracksettings\" creator=\"Zene Studio\">\n"
			"<head/>\n<instrumenttracksettings presettype=\"instrument\"/>\n</zene-project>\n");
		file.close();
		const ControlResult refused = run(QStringLiteral("track.preset_load"),
			{{QStringLiteral("track"), control::trackIdOf(instrument)}, {QStringLiteral("path"), path}});
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QCOMPARE(refused.ok, false);
	}

	void tracksComeFromAnotherProject()
	{
		const QString other = m_dir.filePath(QStringLiteral("other.mmpz"));
		QVERIFY(Engine::getSong()->saveProjectFile(other));
		const ControlResult listed = run(QStringLiteral("project.import_tracks"), {{QStringLiteral("path"), other}, {QStringLiteral("list"), true}});
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		const int theirs = listed.result.value(QStringLiteral("tracks")).toArray().size();
		QVERIFY(theirs >= 3);
		const int before = static_cast<int>(Engine::getSong()->tracks().size());
		const ControlResult imported = run(QStringLiteral("project.import_tracks"), {{QStringLiteral("path"), other},
			{QStringLiteral("tracks"), QJsonArray{0, 1}}});
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before + 2);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before);
		// The import's undo finds its tracks by id: track.remove and its undo rebuild one as a new object.
		const ControlResult again = run(QStringLiteral("project.import_tracks"), {{QStringLiteral("path"), other},
			{QStringLiteral("tracks"), QJsonArray{0}}});
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
		QVERIFY(revtest::makerUndoFindsTrackAfterRemovalUndone(again.result.value(QStringLiteral("imported")).toArray().first().toString()));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before);
		QCOMPARE(run(QStringLiteral("project.import_tracks"), {{QStringLiteral("path"), other}, {QStringLiteral("tracks"), QJsonArray{999}}})
			.errorKind, ControlErrorKind::NotFound);
	}

	void aHandOffFolderCarriesItsManifest()
	{
		const QString dir = m_dir.filePath(QStringLiteral("handoff"));
		const ControlResult done = run(QStringLiteral("project.handoff"), {{QStringLiteral("dir"), dir}});
		QVERIFY2(done.ok, qPrintable(done.errorMessage));
		QVERIFY(QFileInfo(done.result.value(QStringLiteral("file")).toString()).isFile());
		QFile manifest(dir + QStringLiteral("/HANDOFF.json"));
		QVERIFY(manifest.open(QIODevice::ReadOnly));
		const QJsonObject info = QJsonDocument::fromJson(manifest.readAll()).object();
		QVERIFY(info.contains(QStringLiteral("devices")) && info.contains(QStringLiteral("tempo")));
		QCOMPARE(run(QStringLiteral("project.handoff"), {{QStringLiteral("dir"), QStringLiteral("relative/dir")}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	//! A dangling link where the session copy or the manifest goes is refused, not replaced: the link stays.
	void aHandOffNeverReplacesADanglingLink()
	{
		for (const bool manifestSlot : {false, true})
		{
			const QString dir = m_dir.filePath(manifestSlot ? QStringLiteral("dangling-manifest") : QStringLiteral("dangling-copy"));
			const ControlResult first = run(QStringLiteral("project.handoff"), {{QStringLiteral("dir"), dir}});
			QVERIFY2(first.ok, qPrintable(first.errorMessage));
			const QString copy = first.result.value(QStringLiteral("file")).toString();
			const QString manifest = QDir(dir).filePath(QStringLiteral("HANDOFF.json"));
			const QString slot = manifestSlot ? manifest : copy;
			QVERIFY(QFile::remove(copy));
			QVERIFY(QFile::remove(manifest));
			QVERIFY(QDir(QDir(dir).filePath(QStringLiteral("media"))).removeRecursively());  // the first hand off's media
			const QString missing = m_dir.filePath(QStringLiteral("gone-%1.target").arg(manifestSlot ? 1 : 0));
			QVERIFY(QFile::link(missing, slot));
			QVERIFY(QFileInfo(slot).isSymLink() && !QFileInfo::exists(slot));  // a dangling link

			const ControlResult refused = run(QStringLiteral("project.handoff"), {{QStringLiteral("dir"), dir}});
			QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
			QVERIFY2(refused.errorMessage.contains(QStringLiteral("already exists")), qPrintable(refused.errorMessage));
			QVERIFY2(QFileInfo(slot).isSymLink(), "the hand off replaced a dangling link it did not write");
			QCOMPARE(QFile::symLinkTarget(slot), missing);
			QVERIFY(!QFileInfo::exists(missing));
		}
	}

	//! The session save's temporary '<copy>.new' is held like the copy: a regular file or a dangling link there
	//! is refused and left alone, not renamed over.
	void aHandOffNeverReplacesTheSaveTemporary()
	{
		for (const bool dangling : {false, true})
		{
			const QString dir = m_dir.filePath(dangling ? QStringLiteral("temp-link") : QStringLiteral("temp-file"));
			const ControlResult first = run(QStringLiteral("project.handoff"), {{QStringLiteral("dir"), dir}});
			QVERIFY2(first.ok, qPrintable(first.errorMessage));
			const QString copy = first.result.value(QStringLiteral("file")).toString();
			const QString temp = copy + QStringLiteral(".new");
			QVERIFY(QFile::remove(copy));
			QVERIFY(QFile::remove(QDir(dir).filePath(QStringLiteral("HANDOFF.json"))));
			QVERIFY(QDir(QDir(dir).filePath(QStringLiteral("media"))).removeRecursively());
			const QString missing = m_dir.filePath(QStringLiteral("gone-temp.target"));
			if (dangling)
			{
				QVERIFY(QFile::link(missing, temp));
			}
			else
			{
				QFile theirs(temp);
				QVERIFY(theirs.open(QIODevice::WriteOnly));
				theirs.write("not a save temporary");
				theirs.close();
			}

			const ControlResult refused = run(QStringLiteral("project.handoff"), {{QStringLiteral("dir"), dir}});
			QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
			QVERIFY2(refused.errorMessage.contains(QStringLiteral("already exists")), qPrintable(refused.errorMessage));
			if (dangling)
			{
				QVERIFY2(QFileInfo(temp).isSymLink(), "the hand off replaced a dangling link at its save temporary");
				QCOMPARE(QFile::symLinkTarget(temp), missing);
			}
			else
			{
				QFile check(temp);
				QVERIFY(check.open(QIODevice::ReadOnly));
				QCOMPARE(check.readAll(), QByteArray("not a save temporary"));
			}
		}
	}

	// A second hand off into a folder that already carries someone else's
	// session copy and manifest must be refused, leaving both files alone.
	void aHandOffNeverOverwritesTheDestinationFolder()
	{
		const QString dir = m_dir.filePath(QStringLiteral("busy"));
		const ControlResult first = run(QStringLiteral("project.handoff"), {{QStringLiteral("dir"), dir}});
		QVERIFY2(first.ok, qPrintable(first.errorMessage));
		const QString copy = first.result.value(QStringLiteral("file")).toString();
		const QString manifest = QDir(dir).filePath(QStringLiteral("HANDOFF.json"));
		QFile theirs(copy);
		QVERIFY(theirs.open(QIODevice::WriteOnly));
		theirs.write("not our project");
		theirs.close();
		QFile theirManifest(manifest);
		QVERIFY(theirManifest.open(QIODevice::WriteOnly));
		theirManifest.write("{\"project\":\"theirs\"}");
		theirManifest.close();

		const ControlResult refused = run(QStringLiteral("project.handoff"), {{QStringLiteral("dir"), dir}});
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QFile check(copy);
		QVERIFY(check.open(QIODevice::ReadOnly));
		QCOMPARE(check.readAll(), QByteArray("not our project"));
		check.close();
		QFile checkManifest(manifest);
		QVERIFY(checkManifest.open(QIODevice::ReadOnly));
		QCOMPARE(checkManifest.readAll(), QByteArray("{\"project\":\"theirs\"}"));
	}

// A hand off that fails after the session copy was written must not leave that copy
	// behind: the folder either holds a complete bundle or nothing at all.
	void aFailedHandOffLeavesNoBundleBehind()
	{
		const QString dir = m_dir.filePath(QStringLiteral("halfway"));
		QVERIFY(QDir().mkpath(dir));
		touch(QDir(dir).filePath(QStringLiteral("media")));  // a file where the media folder goes

		const ControlResult failed = run(QStringLiteral("project.handoff"), {{QStringLiteral("dir"), dir}});
		QCOMPARE(failed.errorKind, ControlErrorKind::Refused);
		QVERIFY2(!QFileInfo(QDir(dir).filePath(QStringLiteral("HANDOFF.json"))).exists(), "no manifest was written");
		const QStringList left = QDir(dir).entryList({QStringLiteral("*.mmpz"), QStringLiteral("*.mmp")}, QDir::Files);
		QVERIFY2(left.isEmpty(), qPrintable(QStringLiteral("the session copy survived a failed hand off: ") + left.join(QStringLiteral(", "))));
		QVERIFY(QFileInfo(QDir(dir).filePath(QStringLiteral("media"))).isFile());  // what was there before is untouched
	}

	//! A hand off into a folder whose media folder already holds files is refused before anything is written, and
	//! the files stay: a failed hand off's rollback removes the media folder, so it must never own one that was there.
	void aHandOffNeverWritesIntoOrRemovesAnExistingMediaFolder()
	{
		const QString dir = m_dir.filePath(QStringLiteral("occupied"));
		const QString media = QDir(dir).filePath(QStringLiteral("media"));
		QVERIFY(QDir().mkpath(media));
		const QString sentinel = QDir(media).filePath(QStringLiteral("keep.txt"));
		touch(sentinel);

		const ControlResult refused = run(QStringLiteral("project.handoff"), {{QStringLiteral("dir"), dir}});
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY2(QFileInfo::exists(sentinel), "the existing media folder's file was removed");
		QVERIFY2(!QFileInfo::exists(QDir(dir).filePath(QStringLiteral("HANDOFF.json"))), "a manifest was written");
		QVERIFY(QDir(dir).entryList({QStringLiteral("*.mmpz"), QStringLiteral("*.mmp")}, QDir::Files).isEmpty());
	}

	//! A media link to a folder elsewhere is refused too: the rollback's recursive removal would reach through it.
	void aHandOffNeverRemovesWhatAMediaLinkPointsAt()
	{
		const QString outside = m_dir.filePath(QStringLiteral("outside"));
		QVERIFY(QDir().mkpath(outside));
		const QString sentinel = QDir(outside).filePath(QStringLiteral("keep.txt"));
		touch(sentinel);
		const QString dir = m_dir.filePath(QStringLiteral("linked"));
		QVERIFY(QDir().mkpath(dir));
		QVERIFY(QFile::link(outside, QDir(dir).filePath(QStringLiteral("media"))));

		const ControlResult refused = run(QStringLiteral("project.handoff"), {{QStringLiteral("dir"), dir}});
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY2(QFileInfo::exists(sentinel), "a rollback removed the file the media link points at");
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(LibraryProjectsTest)
#include "LibraryProjectsTest.moc"
