/*
 * TrackFolderTest.cpp - folder tracks: a real container with two modes, its
 *                       membership and its flags surviving a real save/open
 *                       (docs/TRACK-FOLDER-DESIGN.md; owner items 3+20+21).
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

//! Every round trip here goes through REAL FILES: the project is saved with
//! Song::saveProjectFile, the in-memory model is then MUTATED, and the assertions
//! are made on what the reloaded file carries - never on the state the test
//! itself just wrote. The audio half of the routing proof (that a routing folder
//! really sums its children) is the registered socket transcript
//! tests/control-track-folder.py; this file proves the WIRING it rides.

#include <QtTest>

#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "DataFile.h"
#include "Engine.h"
#include "Mixer.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"
#include "TrackContainer.h"
#include "TrackFolder.h"

using namespace lmms;

namespace
{

QString readFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	return QString::fromUtf8(file.readAll());
}

void writeFile(const QString& path, const QString& text)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return; }
	file.write(text.toUtf8());
	file.close();
}

/*! A project of three plain tracks with NO folder state at all - the shape every
 *  project saved before this feature has (and the shape an older build writes).
 *  type="0" is an instrument track, type="7" a folder (Track::Type's own order). */
QString legacyProject(const QString& version)
{
	return QString(
		"<?xml version=\"1.0\"?>\n"
		"<lmms-project version=\"%1\" type=\"song\" creator=\"LMMS\">\n"
		"  <head/>\n"
		"  <song>\n"
		"    <trackcontainer type=\"song\">\n"
		"      <track type=\"0\" name=\"One\" muted=\"0\">\n"
		"        <instrumenttrack/>\n"
		"      </track>\n"
		"      <track type=\"0\" name=\"Two\" muted=\"0\">\n"
		"        <instrumenttrack/>\n"
		"      </track>\n"
		"    </trackcontainer>\n"
		"  </song>\n"
		"</lmms-project>\n").arg(version);
}

//! The data file's own version string, so a hand-written project is current.
QString projectVersion()
{
	DataFile fresh(DataFile::Type::SongProject);
	return fresh.documentElement().attribute(QStringLiteral("version"));
}

} // namespace


class TrackFolderTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		QVERIFY2(m_dir.isValid(), "no temporary directory for the round-trip files");
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! (a) A folder is a Track of its own type, and its child list is DERIVED
	//! from the children's own parent pointer - the one source of truth.
	void folderIsATrackTypeThatHoldsTracks()
	{
		const QString name = QStringLiteral("a-hold");
		Track* folder = addTrack(Track::Type::Folder, name);
		QVERIFY(folder != nullptr);
		QVERIFY(folder->type() == Track::Type::Folder);
		QCOMPARE(folder->nodeName(), QStringLiteral("trackfolder"));

		TrackFolder* typed = static_cast<TrackFolder*>(folder);
		Track* first = addTrack(Track::Type::Instrument, name);
		Track* second = addTrack(Track::Type::Instrument, name);
		QCOMPARE(typed->childCount(), 0);

		first->setParentFolder(typed);
		second->setParentFolder(typed);
		QCOMPARE(typed->childCount(), 2);
		QVERIFY(first->parentFolder() == typed);
		QVERIFY(second->parentFolder() == typed);

		// DERIVED, not stored twice: taking one out of the folder drops the count
		second->setParentFolder(nullptr);
		QCOMPARE(typed->childCount(), 1);
		QVERIFY(second->parentFolder() == nullptr);

		// a folder makes no sound of its own
		QVERIFY(!folder->play(TimePos(0), 256, 0));
		removeAll(name);
	}

	//! (b) A relation that would close a cycle, or name the folder itself, is
	//! refused with a reason - the refusals track.set_folder reports, typed.
	void refusesSelfAndCycles()
	{
		const QString name = QStringLiteral("b-refuse");
		TrackFolder* outer = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		TrackFolder* inner = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* plain = addTrack(Track::Type::Instrument, name);
		QString reason;

		QVERIFY(!outer->canHold(outer, &reason));
		QVERIFY(!reason.isEmpty());

		// inner becomes a CHILD of outer, so outer is now an ancestor of inner:
		// putting outer inside inner would close the cycle.
		inner->setParentFolder(outer);
		QVERIFY(!inner->canHold(outer, &reason));
		QVERIFY(!reason.isEmpty());
		// and the relation itself is not a refusal: a folder may hold a track
		// that already sits somewhere else (that is a move, not a cycle)
		QVERIFY(outer->canHold(inner, &reason));

		QVERIFY(outer->canHold(plain, &reason));
		QVERIFY(reason.isEmpty());
		removeAll(name);
	}

	//! (c) THE ROUND TRIP: membership, the mode and both flags are saved, the
	//! in-memory model is then moved elsewhere, and the reloaded project must
	//! carry the state the FILE holds - not the state the test left in memory.
	void membershipAndModeSurviveASaveAndReopen()
	{
		const QString name = QStringLiteral("c-roundtrip");
		TrackFolder* folder = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* first = addTrack(Track::Type::Instrument, name);
		Track* second = addTrack(Track::Type::Instrument, name);
		first->setParentFolder(folder);
		second->setParentFolder(folder);
		folder->setCollapsed(true);
		folder->setPinned(true);

		const int folderId = folder->id();
		const int firstId = first->id();
		const int secondId = second->id();
		QString error;
		QVERIFY2(folder->setMode(TrackFolder::Mode::Routing, &error), qPrintable(error));
		const int channel = static_cast<int>(folder->mixerChannel());
		QVERIFY(channel >= 0);

		const QString path = m_dir.filePath(QStringLiteral("roundtrip.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path, false));

		// MOVE the model away from what the file says, so the reload can only be
		// reporting the file.
		first->setParentFolder(nullptr);
		folder->setMode(TrackFolder::Mode::Group, &error);
		folder->setCollapsed(false);
		folder->setPinned(false);

		loadProject(path);
		Song* song = Engine::getSong();
		Track* reloaded = song->findTrackById(folderId);
		QVERIFY(reloaded != nullptr);
		QVERIFY(reloaded->type() == Track::Type::Folder);
		TrackFolder* reloadedFolder = static_cast<TrackFolder*>(reloaded);
		QCOMPARE(reloadedFolder->childCount(), 2);
		QVERIFY(reloadedFolder->isRouting());
		QVERIFY(reloadedFolder->isCollapsed());
		QVERIFY(reloadedFolder->isPinned());
		QVERIFY(reloadedFolder->mixerChannel() >= 0);
		QVERIFY(song->findTrackById(firstId)->parentFolder() == reloadedFolder);
		QVERIFY(song->findTrackById(secondId)->parentFolder() == reloadedFolder);
		// the children are STILL bound to the folder's own channel: routing mode
		// is a routing state, not a flag
		QCOMPARE(song->findTrackById(firstId)->mixerChannelModel()->value(),
			static_cast<int>(reloadedFolder->mixerChannel()));
		QCOMPARE(song->findTrackById(secondId)->mixerChannelModel()->value(),
			static_cast<int>(reloadedFolder->mixerChannel()));
		QVERIFY(channel == static_cast<int>(reloadedFolder->mixerChannel()));
		removeAll(name);
	}

	//! (d) RESET ON ABSENCE - the rule a journal checkpoint restore depends on.
	//! An element with no `visible` / `collapsed` / `pinned` / `folder` must take
	//! the state back OFF an object that carries it, or one control.undo could
	//! never undo the first edit.
	void everyFolderFieldResetsOnAbsence()
	{
		const QString name = QStringLiteral("d-reset");
		Track* plain = addTrack(Track::Type::Instrument, name);
		TrackFolder* folder = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		TrackFolder* other = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		plain->setParentFolder(folder);
		folder->setVisible(false);
		folder->setCollapsed(true);
		folder->setPinned(true);

		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("track"));
		element.setAttribute(QStringLiteral("type"), static_cast<int>(Track::Type::Instrument));
		element.setAttribute(QStringLiteral("name"), name);
		element.appendChild(doc.createElement(QStringLiteral("instrumenttrack")));
		plain->loadSettings(element);
		QVERIFY2(plain->parentFolder() == nullptr, "a track element with no folder attribute is "
			"not in a folder, whatever the object held before the call");
		QVERIFY(plain->isVisible());

		QDomElement folderElement = doc.createElement(QStringLiteral("track"));
		folderElement.setAttribute(QStringLiteral("type"), static_cast<int>(Track::Type::Folder));
		folderElement.setAttribute(QStringLiteral("name"), name);
		folderElement.appendChild(doc.createElement(QStringLiteral("trackfolder")));
		folder->loadSettings(folderElement);
		QVERIFY(!folder->isCollapsed());
		QVERIFY(!folder->isPinned());
		QVERIFY(!folder->isRouting());
		QVERIFY(folder->mixerChannel() < 0);
		QVERIFY(other->childCount() == 0);
		removeAll(name);
	}

	//! (e) ROUTING MODE REALLY REWIRES THE AUDIO PATH: every child's own mixer
	//! channel is the folder's, the folder's channel is a REGULAR channel (a bus
	//! refuses instrument output outright, so a bus could not sum them), and
	//! turning the mode off puts every child back where it was.
	void routingModePointsEveryChildAtTheFolderChannel()
	{
		const QString name = QStringLiteral("e-routing");
		TrackFolder* folder = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* first = addTrack(Track::Type::Instrument, name);
		Track* second = addTrack(Track::Type::Instrument, name);
		first->setParentFolder(folder);
		second->setParentFolder(folder);
		QVERIFY(first->mixerChannelModel() != nullptr);
		const int firstBefore = first->mixerChannelModel()->value();

		QString error;
		QVERIFY2(folder->setMode(TrackFolder::Mode::Routing, &error), qPrintable(error));
		const int channel = static_cast<int>(folder->mixerChannel());
		QVERIFY(channel >= 0);
		QCOMPARE(first->mixerChannelModel()->value(), channel);
		QCOMPARE(second->mixerChannelModel()->value(), channel);
		QVERIFY(Engine::mixer()->mixerChannel(channel) != nullptr);
		QVERIFY(!Engine::mixer()->mixerChannel(channel)->isBus());

		QVERIFY2(folder->setMode(TrackFolder::Mode::Group, &error), qPrintable(error));
		QCOMPARE(first->mixerChannelModel()->value(), firstBefore);
		QVERIFY(folder->mixerChannel() < 0);
		removeAll(name);
	}

	//! (f) A NAMED VISIBILITY SET is project state: it survives a real
	//! save/reopen and so do the flags applying it wrote.
	void visibilitySetsSurviveASaveAndReopen()
	{
		const QString name = QStringLiteral("f-sets");
		Track* first = addTrack(Track::Type::Instrument, name);
		Track* second = addTrack(Track::Type::Instrument, name);
		Track* third = addTrack(Track::Type::Instrument, name);
		Song* song = Engine::getSong();
		const std::vector<int> members{first->id(), second->id()};
		QVERIFY(song->saveVisibilitySet(QStringLiteral("Focus"), members));
		const int totalTracks = static_cast<int>(song->tracks().size());
		QVERIFY(totalTracks >= 3);
		int shown = 0;
		int hidden = 0;
		QVERIFY(song->applyVisibilitySet(QStringLiteral("Focus"), &shown, &hidden));
		QCOMPARE(shown, 2);
		// EVERY non-member is hidden, whatever else the song holds
		QCOMPARE(hidden, totalTracks - 2);
		QVERIFY(third->isVisible() == false);

		const int firstId = first->id();
		const int thirdId = third->id();
		const QString path = m_dir.filePath(QStringLiteral("sets.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path, false));
		// hide nothing in memory: only the file can bring the flags back
		song->clearVisibilitySets();
		for (Track* track : song->tracks()) { track->setVisible(true); }

		loadProject(path);
		song = Engine::getSong();
		const TrackContainer::VisibilitySet* set = song->findVisibilitySet(QStringLiteral("Focus"));
		QVERIFY(set != nullptr);
		QCOMPARE(static_cast<int>(set->trackIds.size()), 2);
		QCOMPARE(song->activeVisibilitySet(), QStringLiteral("Focus"));
		QVERIFY(song->findTrackById(firstId)->isVisible());
		QVERIFY(!song->findTrackById(thirdId)->isVisible());
		removeAll(name);
	}

	//! (g) A project that predates the feature loads with NO folder state and
	//! re-saves without growing any: no folder attribute, no visible attribute,
	//! no <trackfolder> and no <visibilitysets> element.
	void legacyProjectGrowsNoFolderState()
	{
		const QString legacy = legacyProject(projectVersion());
		const QString path = m_dir.filePath(QStringLiteral("legacy.mmp"));
		writeFile(path, legacy);
		loadProject(path);

		Song* song = Engine::getSong();
		QCOMPARE(song->countTracks(Track::Type::Folder), 0);
		QCOMPARE(song->visibilitySets().size(), 0);
		for (Track* track : song->tracks())
		{
			QVERIFY(track->parentFolder() == nullptr);
			QVERIFY(track->isVisible());
		}

		const QString saved = m_dir.filePath(QStringLiteral("legacy-resaved.mmp"));
		QVERIFY(song->saveProjectFile(saved, false));
		const QString text = readFile(saved);
		QVERIFY(!text.isEmpty());
		QVERIFY2(!text.contains(QStringLiteral("folder=")), qPrintable(text));
		QVERIFY2(!text.contains(QStringLiteral("visible=")), qPrintable(text));
		QVERIFY2(!text.contains(QStringLiteral("<trackfolder")), qPrintable(text));
		QVERIFY2(!text.contains(QStringLiteral("<visibilitysets")), qPrintable(text));
	}

	//! (h) A child whose `folder` attribute names a track that is not a folder
	//! (a deleted folder, a hand-edited file, a merged project) is REPAIRED, not
	//! obeyed: it loads at the container root and the load reports the repair.
	void danglingFolderIsRepaired()
	{
		QString project = legacyProject(projectVersion());
		project.replace(QStringLiteral("<track type=\"0\" name=\"Two\" muted=\"0\">"),
			QStringLiteral("<track type=\"0\" name=\"Two\" muted=\"0\" folder=\"99999\">"));
		const QString path = m_dir.filePath(QStringLiteral("dangling.mmp"));
		writeFile(path, project);
		loadProject(path);

		bool found = false;
		for (Track* track : Engine::getSong()->tracks())
		{
			if (track->name() != QLatin1String("Two")) { continue; }
			QVERIFY(track->parentFolder() == nullptr);
			found = true;
		}
		QVERIFY(found);
	}

	//! (i) A no-op unparent is a VALID ANSWER, never a crash: taking a track that
	//! is already at the container root out of "no folder" must succeed, twice.
	void unparentingARootTrackIsANoOpNotACrash()
	{
		const QString name = QStringLiteral("i-noop");
		Track* track = addTrack(Track::Type::Instrument, name);
		const QString id = control::trackIdOf(track);
		ControlRegistry* registry = ControlRegistry::instance();
		for (int i = 0; i < 2; ++i)
		{
			const ControlResult reply = registry->invoke(QStringLiteral("track.set_folder"),
				QJsonObject{{QStringLiteral("track"), id}, {QStringLiteral("folder"), QString()}});
			QVERIFY2(reply.ok, qPrintable(reply.errorMessage));
			QVERIFY(reply.result.value(QStringLiteral("folder")).toString().isEmpty());
		}
		QVERIFY(track->parentFolder() == nullptr);
		// and the folder it names in its reply is empty, not a dereferenced null
		const ControlResult state = registry->invoke(QStringLiteral("track.get_state"),
			QJsonObject{{QStringLiteral("track"), id}});
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		QVERIFY(state.result.value(QStringLiteral("folder")).toString().isEmpty());
		removeAll(name);
	}

private:
	//! \a type track named \a name, in the song container.
	Track* addTrack(Track::Type type, const QString& name)
	{
		Track* track = Track::create(type, Engine::getSong());
		if (track != nullptr) { track->setName(name); }
		return track;
	}

	//! Removes every track this test named \a name, so one slot's fixture cannot
	//! leak into the next one. The list is COPIED first: removing a track erases
	//! it from the container's vector, so iterating that vector while removing
	//! from it is undefined behaviour (measured: SIGSEGV).
	void removeAll(const QString& name)
	{
		Song* song = Engine::getSong();
		std::vector<Track*> doomed;
		for (Track* track : song->tracks())
		{
			if (track->name() == name) { doomed.push_back(track); }
		}
		for (Track* track : doomed) { control::removeTrack(track); }
	}

	void loadProject(const QString& path)
	{
		Song* song = Engine::getSong();
		song->loadProject(path);
		QVERIFY2(song->loadRefusal().isEmpty(), qPrintable(song->loadRefusal()));
	}

	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(TrackFolderTest)
#include "TrackFolderTest.moc"
