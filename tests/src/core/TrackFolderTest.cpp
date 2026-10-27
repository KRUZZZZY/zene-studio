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
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "DataFile.h"
#include "AutomatableModel.h"
#include "Engine.h"
#include "Mixer.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"
#include "TrackContainer.h"
#include "TrackFolder.h"
#include "TrackFolderNoChannelTestSupport.h"

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

//! A current project whose visibility set names tracks with a token that is
//! NOT a number ("bogus") beside two parseable IDs. The shape a corrupted or
//! hand-edited project file carries.
QString projectWithUnparseableSetId(const QString& version)
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
		"    </trackcontainer>\n"
		"    <visibilitysets>\n"
		"      <set name=\"Mixed\" tracks=\"1,bogus,0\"/>\n"
		"    </visibilitysets>\n"
		"  </song>\n"
		"</lmms-project>\n").arg(version);
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

	//! A song checkpoint restores its tracks through TrackContainer::loadSettings.
	//! Its external visibility-set definitions and active name are still project
	//! state and must survive that track-only restore.
	void visibilitySetsSurviveTrackContainerJournalRestore()
	{
		const QString name = QStringLiteral("f-undo-sets");
		Track* first = addTrack(Track::Type::Instrument, name);
		Track* second = addTrack(Track::Type::Instrument, name);
		Song* song = Engine::getSong();
		const int firstId = first->id();
		const int secondId = second->id();
		const std::vector<int> members{firstId};
		const QString setName = QStringLiteral("Undo focus");
		QVERIFY(song->saveVisibilitySet(setName, members));
		int shown = 0;
		int hidden = 0;
		QVERIFY(song->applyVisibilitySet(setName, &shown, &hidden));
		QCOMPARE(shown, 1);
		QCOMPARE(hidden, static_cast<int>(song->tracks().size()) - 1);

		const int originalTempo = song->getTempo();
		const int changedTempo = originalTempo == 100 ? 101 : 100;
		const ControlResult changed = ControlRegistry::instance()->invoke(
			QStringLiteral("transport.set_tempo"),
			QJsonObject{{QStringLiteral("bpm"), changedTempo}});
		QVERIFY2(changed.ok, qPrintable(QStringLiteral("transport.set_tempo failed: ") + changed.errorMessage));
		const ControlResult undone = ControlRegistry::instance()->invoke(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(QStringLiteral("control.undo failed: ") + undone.errorMessage));
		QCOMPARE(song->getTempo(), originalTempo);

		const TrackContainer::VisibilitySet* restored = song->findVisibilitySet(setName);
		QVERIFY2(restored != nullptr, "journal restore erased the named visibility set");
		QCOMPARE(static_cast<int>(restored->trackIds.size()), 1);
		QCOMPARE(restored->trackIds.front(), firstId);
		QCOMPARE(song->activeVisibilitySet(), setName);
		QVERIFY(song->findTrackById(firstId) != nullptr);
		QVERIFY(song->findTrackById(secondId) != nullptr);
		QVERIFY(song->findTrackById(firstId)->isVisible());
		QVERIFY(!song->findTrackById(secondId)->isVisible());

		song->removeVisibilitySet(setName);
		removeAll(name);
	}

	//! (f2) An unparseable track ID in a saved visibility set must NOT silently
	//! become track ID 0 - 0 is a real, valid track ID, so a bogus token would
	//! make the set claim a track the file never named. Only parseable IDs
	//! survive the load, in file order.
	void invalidVisibilitySetIdsAreDroppedOnLoad()
	{
		const QString path = m_dir.filePath(QStringLiteral("bad-ids.mmp"));
		writeFile(path, projectWithUnparseableSetId(projectVersion()));
		loadProject(path);

		Song* song = Engine::getSong();
		const TrackContainer::VisibilitySet* set = song->findVisibilitySet(QStringLiteral("Mixed"));
		QVERIFY(set != nullptr);
		// "bogus" must not join the set as a second 0: the set carries exactly the
		// two parseable IDs, in file order.
		QCOMPARE(static_cast<int>(set->trackIds.size()), 2);
		QCOMPARE(set->trackIds.at(0), 1);
		QCOMPARE(set->trackIds.at(1), 0);
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

	//! A file that names folder A the parent of B and B the parent of A must not
	//! BUILD the cycle: every walk that follows parent pointers (hiddenByFolder in
	//! the track view, canHold itself) terminates on an acyclic relation only, so a
	//! cycle loaded here loops the UI forever. The post-load pass is the one place
	//! that closes it, because a folder may be constructed after the track naming
	//! it - so the pass must refuse the link the way the control command does.
	void aFolderCycleIsRefusedNotBuilt()
	{
		const QString name = QStringLiteral("e-cycle");
		TrackFolder* first = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		TrackFolder* second = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		// each names the other, exactly what the two `folder` attributes of a
		// hand-edited or merged file carry
		first->setPendingFolderId(second->id());
		second->setPendingFolderId(first->id());

		const int repaired = Engine::getSong()->resolveTrackFolders();
		QVERIFY2(repaired > 0, "a cycle-closing link was accepted without a repair");

		// the parent chain TERMINATES on both, which is the property every walker
		// needs; a cycle never reaches a null parent
		for (const Track* start : {static_cast<const Track*>(first), static_cast<const Track*>(second)})
		{
			int depth = 0;
			for (const Track* walker = start; walker != nullptr; walker = walker->parentFolder())
			{
				QVERIFY2(++depth <= 2, "the parent chain does not terminate: a folder cycle was built");
			}
		}
		removeAll(name);
	}

	//! A malformed `prevch` pair must not become a channel-zero binding: the ids
	//! are read with an unchecked toInt(), so "bad:4" and "1:bad" each yield 0 -
	//! and 0 is the MASTER channel, a value the model treats as real. A child
	//! taken out of such a folder would then be sent to the master instead of
	//! staying where it was.
	void aMalformedSavedBindingIsDiscardedNotTakenAsChannelZero()
	{
		QString project = legacyProject(projectVersion());
		project.replace(QStringLiteral("    </trackcontainer>"),
			QStringLiteral(
				"      <track type=\"7\" name=\"Holder\" muted=\"0\">\n"
				"        <trackfolder mode=\"routing\" mixch=\"1\" prevch=\"bad:4,1:bad\"/>\n"
				"      </track>\n"
				"    </trackcontainer>"));
		const QString path = m_dir.filePath(QStringLiteral("malformed-bindings.mmp"));
		writeFile(path, project);
		loadProject(path);

		TrackFolder* folder = nullptr;
		for (Track* track : Engine::getSong()->tracks())
		{
			if (track->type() == Track::Type::Folder)
			{
				folder = static_cast<TrackFolder*>(track);
			}
		}
		QVERIFY(folder != nullptr);
		QVERIFY2(folder->savedChildChannels().empty(),
			"a prevch pair that is not two numbers is discarded, not adopted as 0:<n>");
	}

	//! A loaded `mixch="0"` is malformed: releasing or deleting that folder must not delete the mixer master.
	void aMalformedFolderChannelCannotDeleteTheMaster()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clear();
		QCOMPARE(mixer->numChannels(), mix_ch_t{1});

		QDomDocument settings;
		QVERIFY(settings.setContent(QStringLiteral("<trackfolder mode=\"routing\" mixch=\"0\"/>")));
		const QString name = QStringLiteral("a-master-index");
		TrackFolder* released = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		released->loadTrackSpecificSettings(settings.documentElement());
		QString error;
		QVERIFY2(released->setMode(TrackFolder::Mode::Group, &error), qPrintable(error));
		const bool masterSurvivedRelease = mixer->numChannels() == 1;
		if (!masterSurvivedRelease) { mixer->createChannel(); }

		TrackFolder* deleted = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		deleted->loadTrackSpecificSettings(settings.documentElement());
		removeAll(name);
		const bool masterSurvivedDelete = mixer->numChannels() == 1;
		if (!masterSurvivedDelete) { mixer->createChannel(); }

		QVERIFY2(masterSurvivedRelease, "leaving routing with a saved master index deleted mixer channel 0");
		QVERIFY2(masterSurvivedDelete, "deleting a folder with a saved master index deleted mixer channel 0");
	}

	//! A child that LEAVES a routing folder keeps its binding, so it can be routed
	//! again without inventing a channel for it. But the channel it is given while it
	//! is OUTSIDE the folder is the one a later release has to restore: routeChild()
	//! appended the new binding behind the stale one and savedChannelFor() returns the
	//! FIRST match, so the child went back to the channel it was on before it ever
	//! left the folder, losing the assignment made in between.
	void rejoiningARoutingFolderKeepsTheNewestSavedChannel()
	{
		const QString name = QStringLiteral("l-rejoin");
		TrackFolder* folder = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* child = addTrack(Track::Type::Instrument, name);
		// Two channels of the child's own, so "where it was" and "where it was put
		// while it was out" are both channels that exist. Mixer::createChannel() does
		// NOT re-range the tracks - TrackFolder::routeChild widens its own children
		// for exactly that reason - so the model is widened here too.
		QVERIFY(Engine::mixer()->createChannel() >= 0);
		QVERIFY(Engine::mixer()->createChannel() >= 0);
		IntModel* model = child->mixerChannelModel();
		QVERIFY(model != nullptr);
		model->setRange(0, static_cast<float>(Engine::mixer()->numChannels() - 1));
		const int original = 1;
		const int whileOut = 2;
		model->setValue(original);
		child->setParentFolder(folder);

		QString error;
		QVERIFY2(folder->setMode(TrackFolder::Mode::Routing, &error), qPrintable(error));
		QCOMPARE(model->value(), static_cast<int>(folder->mixerChannel()));

		// out of the folder: back on its own channel, and free to be moved
		child->setParentFolder(nullptr);
		QCOMPARE(model->value(), original);
		model->setValue(whileOut);

		child->setParentFolder(folder);
		QCOMPARE(model->value(), static_cast<int>(folder->mixerChannel()));

		QVERIFY2(folder->setMode(TrackFolder::Mode::Group, &error), qPrintable(error));
		QVERIFY2(model->value() == whileOut,
			"a child that rejoined on a new channel goes back to THAT channel, "
			"not to the one it held before it first left the folder");
		removeAll(name);
	}

	//! A finding read -1 as a valid prior channel, the same value savedChannelFor()
	//! returns for "no binding". A child's channel model has a minimum of 0 and fits
	//! every value into its range, so no live child can sit on -1: a recorded binding
	//! is never the sentinel. This pins that contract. A -1 can still arrive from a
	//! hand-edited `prevch`, which this test does not cover.
	void aChildsMixerChannelCannotHoldMinusOneSoTheSentinelIsNeverALiveBinding()
	{
		const QString name = QStringLiteral("l-minus-one");
		TrackFolder* folder = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* child = addTrack(Track::Type::Instrument, name);
		IntModel* model = child->mixerChannelModel();
		QVERIFY(model != nullptr);
		QCOMPARE(model->minValue(), 0.0f);
		model->setValue(-1.0f);
		QCOMPARE(static_cast<int>(model->value()), 0);

		child->setParentFolder(folder);
		QString error;
		QVERIFY2(folder->setMode(TrackFolder::Mode::Routing, &error), qPrintable(error));
		QCOMPARE(folder->savedChannelFor(child->id()), 0);

		QVERIFY2(folder->setMode(TrackFolder::Mode::Group, &error), qPrintable(error));
		QCOMPARE(static_cast<int>(model->value()), 0);
		removeAll(name);
	}

	//! Releasing routing has to give the child back its channel RANGE as well as
	//! its value: routeChild() widens the model so it can hold the folder's
	//! channel, and unrouteChild() only put the value back. The deleted channel's
	//! index stayed selectable, and the mixer has no channel there - the child was
	//! feeding a hole.
	void releasingRoutingTakesTheChildRangeBackOffTheDeletedChannel()
	{
		const QString name = QStringLiteral("m-range");
		TrackFolder* folder = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* child = addTrack(Track::Type::Instrument, name);
		child->setParentFolder(folder);
		IntModel* model = child->mixerChannelModel();
		QVERIFY(model != nullptr);
		const int before = model->value();

		QString error;
		QVERIFY2(folder->setMode(TrackFolder::Mode::Routing, &error), qPrintable(error));
		const int channel = static_cast<int>(folder->mixerChannel());
		QVERIFY(channel >= 0);
		// the widening really happened, so the range below proves the release undid it
		QCOMPARE(model->maxValue(), static_cast<float>(channel));

		QVERIFY2(folder->setMode(TrackFolder::Mode::Group, &error), qPrintable(error));
		QCOMPARE(model->value(), before);
		QVERIFY2(channel >= static_cast<int>(Engine::mixer()->numChannels()),
			"the folder's channel was deleted, so its index is past the last channel");
		QCOMPARE(model->maxValue(), static_cast<float>(Engine::mixer()->numChannels() - 1));

		// and the deleted index cannot be selected any more
		model->setValue(channel);
		QVERIFY2(model->value() < static_cast<int>(Engine::mixer()->numChannels()),
			"a child must not be able to select a channel that has been deleted");
		removeAll(name);
	}

	//! A routing folder's mixer channel is NAMED after it, and the name is shown on
	//! the channel's caption, tooltip and LCD - and saved with the project. It was
	//! copied once, when routing was enabled, so renaming the folder afterwards left
	//! the channel advertising a name the folder no longer has.
	void renamingARoutingFolderRenamesItsMixerChannel()
	{
		const QString name = QStringLiteral("n-rename");
		TrackFolder* folder = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* child = addTrack(Track::Type::Instrument, name);
		child->setParentFolder(folder);
		QString error;
		QVERIFY2(folder->setMode(TrackFolder::Mode::Routing, &error), qPrintable(error));
		const int channel = static_cast<int>(folder->mixerChannel());
		MixerChannel* mixerChannel = Engine::mixer()->mixerChannel(channel);
		QVERIFY(mixerChannel != nullptr);
		QCOMPARE(mixerChannel->m_name, name);

		const QString renamed = QStringLiteral("n-rename-again");
		folder->setName(renamed);
		QVERIFY2(mixerChannel->m_name == renamed,
			"the mixer channel of a routing folder carries the folder's CURRENT name");
		// back under the fixture name, so removeAll() can find the folder again
		folder->setName(name);
		removeAll(name);
	}

	//! Deleting a folder must give its mixer channel back. ~TrackFolder unlinked
	//! the children but never released the folder's OWN channel, unlike
	//! releaseRouting() - so each deleted routing folder left an orphan channel
	//! in the mixer, and the channel list grew without bound over a session's
	//! deletions.
	void deletingARoutingFolderGivesItsMixerChannelBack()
	{
		const QString name = QStringLiteral("j-leak");
		const mix_ch_t before = Engine::mixer()->numChannels();

		TrackFolder* folder = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* child = addTrack(Track::Type::Instrument, name);
		child->setParentFolder(folder);
		QString error;
		QVERIFY2(folder->setMode(TrackFolder::Mode::Routing, &error), qPrintable(error));
		const mix_ch_t routed = Engine::mixer()->numChannels();
		QVERIFY2(routed == before + 1,
			"routing must have taken one channel for the folder, or this slot proves nothing");

		control::removeTrack(folder);
		QCOMPARE(Engine::mixer()->numChannels(), before);
		removeAll(name);
	}

	//! Deleting a routing folder must not leave the OTHER routing folders holding a
	//! channel index that has moved. Mixer::deleteChannel renumbers the instrument
	//! and sample channel models, and a TrackFolder's own channel is an index into
	//! that same vector: two routing folders on channels 3 and 4, delete the lower,
	//! and the upper used to keep saying 4 - which by then names the strip above
	//! its own. Its children were routed to the wrong strip, and deleting IT
	//! removed a strip it did not own.
	void deletingOneRoutingFolderRenumbersTheOther()
	{
		const QString name = QStringLiteral("j-renumber");
		const mix_ch_t baseline = Engine::mixer()->numChannels();

		TrackFolder* lower = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* lowerChild = addTrack(Track::Type::Instrument, name);
		lowerChild->setParentFolder(lower);
		QString error;
		QVERIFY2(lower->setMode(TrackFolder::Mode::Routing, &error), qPrintable(error));

		TrackFolder* upper = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* upperChild = addTrack(Track::Type::Instrument, name);
		upperChild->setParentFolder(upper);
		QVERIFY2(upper->setMode(TrackFolder::Mode::Routing, &error), qPrintable(error));

		const int lowerChannel = lower->mixerChannel();
		const int upperChannel = upper->mixerChannel();
		QVERIFY2(upperChannel == lowerChannel + 1,
			"the second routing folder must have taken the next channel, or this slot proves nothing");

		// Delete the LOWER one. Its channel goes, and everything above it comes down.
		control::removeTrack(lower);
		QCOMPARE(Engine::mixer()->numChannels(), baseline + 1);

		QVERIFY2(upper->mixerChannel() == upperChannel - 1,
			"the surviving folder follows its channel down when a lower one is deleted");
		QVERIFY2(upper->mixerChannel() == lowerChannel,
			"the surviving folder now holds the index the deleted folder had");
		QVERIFY(Engine::mixer()->mixerChannel(upper->mixerChannel()) != nullptr);
		// and its ROUTING followed it: the child still sums into the folder's channel.
		QCOMPARE(upperChild->mixerChannelModel()->value(), static_cast<float>(upper->mixerChannel()));

		// Deleting the survivor now removes ITS OWN strip and only that one.
		control::removeTrack(upper);
		QCOMPARE(Engine::mixer()->numChannels(), baseline);
		removeAll(name);
	}

	void aRoutingFolderWithoutAChannelDoesNotStealNewChildren()
	{
		const QString name = QStringLiteral("j-no-channel");
		Mixer* mixer = Engine::mixer();
		const int channel = mixer->createChannel();
		QVERIFY(channel >= 0);

		TrackFolder* folder = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* child = addTrack(Track::Type::Instrument, name);
		IntModel* model = child->mixerChannelModel();
		QVERIFY(model != nullptr);
		model->setRange(0, static_cast<float>(channel));
		model->setValue(channel);
		track_folder_test::linkToRoutingFolderWithoutChannel(folder, child);
		QVERIFY(folder->isRouting());
		QCOMPARE(folder->mixerChannel(), -1);
		const int resultingChannel = static_cast<int>(model->value());
		const int savedChannel = folder->savedChannelFor(child->id());

		removeAll(name);
		mixer->deleteChannel(channel);
		QVERIFY2(resultingChannel == channel,
			"a routing folder with no mixer channel must leave its child on its own channel");
		QCOMPARE(savedChannel, -1);
	}

	//! Undoing a routing folder's delete must bring its channel back. The
	//! checkpoint carries `mixch`, an index read from a file - and the delete being
	//! reversed gave that channel back to the mixer, so the restored folder used to
	//! adopt an index naming a strip it does not own, routing its children into
	//! somebody else's channel. It has to take a channel that is really there.
	void undoingARoutingFolderDeleteRestoresItsChannel()
	{
		const QString name = QStringLiteral("j-undo-folder");
		const mix_ch_t baseline = Engine::mixer()->numChannels();

		TrackFolder* folder = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* child = addTrack(Track::Type::Instrument, name);
		child->setParentFolder(folder);
		QString error;
		QVERIFY2(folder->setMode(TrackFolder::Mode::Routing, &error), qPrintable(error));
		QCOMPARE(Engine::mixer()->numChannels(), baseline + 1);

		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult removed = registry->invoke(QStringLiteral("track.remove"),
			QJsonObject{{QStringLiteral("track"), control::trackIdOf(folder)}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(Engine::mixer()->numChannels(), baseline);

		QVERIFY2(registry->invoke(QStringLiteral("control.undo")).ok, "the undo of a folder delete must apply");

		// The folder is back...
		Track* restored = nullptr;
		for (Track* t : Engine::getSong()->tracks())
		{
			if (t->type() == Track::Type::Folder && t->name() == name) { restored = t; }
		}
		QVERIFY2(restored != nullptr, "the undo must bring the routing folder back");
		TrackFolder* typed = static_cast<TrackFolder*>(restored);
		QVERIFY(typed->isRouting());

		// ... on a channel that EXISTS, which is not an assertion about which index:
		// the deleted channel was given back, so a fresh one is what a correct
		// restore creates. What must not happen is adopting an index it does not own.
		QVERIFY2(typed->mixerChannel() >= 0, "the restored folder must own a channel");
		QVERIFY2(typed->mixerChannel() < static_cast<int>(Engine::mixer()->numChannels()),
			"the restored folder must not adopt an index past the end of the mixer");
		QVERIFY(Engine::mixer()->mixerChannel(typed->mixerChannel()) != nullptr);
		QCOMPARE(Engine::mixer()->numChannels(), baseline + 1);
		// and the channel it took is its own, named for the folder that is back.
		QCOMPARE(Engine::mixer()->mixerChannel(typed->mixerChannel())->m_name, name);
		removeAll(name);
	}

	//! A routed child's SAVED channel is a mixer index too: deleting a channel
	//! below it must move it with its strip, so releasing routing puts the child
	//! back on the strip it came from, not on whatever slid into that index.
	void aSavedChildChannelFollowsItsStripWhenALowerChannelIsDeleted()
	{
		const QString name = QStringLiteral("k-saved-follows");
		Mixer* mixer = Engine::mixer();
		const int low = mixer->createChannel();
		const int high = mixer->createChannel();
		mixer->mixerChannel(high)->m_name = QStringLiteral("k-home-strip");

		TrackFolder* folder = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* child = addTrack(Track::Type::Instrument, name);
		child->mixerChannelModel()->setRange(0, static_cast<float>(mixer->numChannels() - 1));
		child->mixerChannelModel()->setValue(high);
		child->setParentFolder(folder);
		QString error;
		QVERIFY2(folder->setMode(TrackFolder::Mode::Routing, &error), qPrintable(error));

		mixer->deleteChannel(low);  // the home strip comes down to `low`
		QVERIFY2(folder->setMode(TrackFolder::Mode::Group, &error), qPrintable(error));
		const int home = static_cast<int>(child->mixerChannelModel()->value());
		QVERIFY(mixer->mixerChannel(home) != nullptr);
		QCOMPARE(mixer->mixerChannel(home)->m_name, QStringLiteral("k-home-strip"));

		removeAll(name);
		mixer->deleteChannel(home);
	}

	//! The undo of a routing folder's delete must not adopt its saved index when
	//! that index is still IN RANGE: the delete gave the folder's channel back,
	//! so the strip above came down into it, and adopting it would rename and
	//! route into somebody else's strip.
	void undoingAFolderDeleteDoesNotAdoptTheStripThatSlidIntoItsIndex()
	{
		const QString name = QStringLiteral("k-undo-no-adopt");
		Mixer* mixer = Engine::mixer();
		TrackFolder* folder = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* child = addTrack(Track::Type::Instrument, name);
		child->setParentFolder(folder);
		QString error;
		QVERIFY2(folder->setMode(TrackFolder::Mode::Routing, &error), qPrintable(error));
		const int folderChannel = folder->mixerChannel();
		const int other = mixer->createChannel();
		mixer->mixerChannel(other)->m_name = QStringLiteral("k-other-strip");
		QCOMPARE(other, folderChannel + 1);

		ControlRegistry* registry = ControlRegistry::instance();
		QVERIFY(registry->invoke(QStringLiteral("track.remove"),
			QJsonObject{{QStringLiteral("track"), control::trackIdOf(folder)}}).ok);
		QCOMPARE(mixer->mixerChannel(folderChannel)->m_name, QStringLiteral("k-other-strip"));
		QVERIFY(registry->invoke(QStringLiteral("control.undo")).ok);

		TrackFolder* restored = nullptr;
		for (Track* t : Engine::getSong()->tracks())
		{
			if (t->type() == Track::Type::Folder && t->name() == name) { restored = static_cast<TrackFolder*>(t); }
		}
		QVERIFY(restored != nullptr);
		QVERIFY(restored->mixerChannel() != folderChannel);
		QCOMPARE(mixer->mixerChannel(folderChannel)->m_name, QStringLiteral("k-other-strip"));
		QCOMPARE(mixer->mixerChannel(restored->mixerChannel())->m_name, name);

		removeAll(name);
		for (int i = static_cast<int>(mixer->numChannels()) - 1; i > 0; --i)
		{
			if (mixer->mixerChannel(i)->m_name == QStringLiteral("k-other-strip")) { mixer->deleteChannel(i); }
		}
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

	//! A visibility set is a SET: naming the same track twice must still leave it a set of ONE
	//! member. The stored definition and the `track_count` the save reply reports both count
	//! MEMBERS, so the duplicate has to go before either is taken.
	void aRepeatedTrackIdIsSavedAsOneMember()
	{
		const QString name = QStringLiteral("j-dup-ids");
		const QString firstId = control::trackIdOf(addTrack(Track::Type::Instrument, name));
		const QString secondId = control::trackIdOf(addTrack(Track::Type::Instrument, name));
		Song* song = Engine::getSong();
		ControlResult saved = ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_save"),
			QJsonObject{{QStringLiteral("name"), QStringLiteral("Doubled")},
				{QStringLiteral("tracks"), QJsonArray{firstId, firstId, secondId, firstId}}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		// Four ids named, two distinct tracks: the reply counts MEMBERS.
		QCOMPARE(saved.result.value(QStringLiteral("track_count")).toInt(), 2);
		QCOMPARE(saved.result.value(QStringLiteral("tracks")).toArray().size(), 2);
		const TrackContainer::VisibilitySet* set = song->findVisibilitySet(QStringLiteral("Doubled"));
		QVERIFY(set != nullptr);
		QCOMPARE(static_cast<int>(set->trackIds.size()), 2);
		// Applying it shows each member ONCE, so the duplicate is gone from the engine's
		// own view too - not only from the reply.
		int shown = 0;
		int hidden = 0;
		QVERIFY(song->applyVisibilitySet(QStringLiteral("Doubled"), &shown, &hidden));
		QCOMPARE(shown, 2);
		removeAll(name);
	}

	//! A visibility-set save needs an array of track-id strings. Missing or
	//! wrongly typed input must be refused instead of silently creating an empty set.
	void visibilitySetSaveRefusesMalformedTrackLists()
	{
		const QJsonArray malformedValues{
			QJsonObject{{QStringLiteral("id"), QStringLiteral("track")}},
			7,
			QStringLiteral("trk-1"),
			QJsonArray{7},
		};
		Song* song = Engine::getSong();
		for (int index = 0; index <= malformedValues.size(); ++index)
		{
			const QString setName = QStringLiteral("Malformed-%1").arg(index);
			QJsonObject args{{QStringLiteral("name"), setName}};
			if (index < malformedValues.size())
			{
				args.insert(QStringLiteral("tracks"), malformedValues.at(index));
			}
			const ControlResult result = ControlRegistry::instance()->invoke(
				QStringLiteral("track.visibility_set_save"), args);
			QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
			QVERIFY(song->findVisibilitySet(setName) == nullptr);
		}
	}

	//! The inverse control.transactions REPORTS is a promise a client may replay,
	//! so it has to name a command that really puts the state back. Removing a set
	//! cannot name itself: the set is already gone, so that replay restores nothing
	//! (and refuses on the missing name).
	void removeSetReportsAnInverseThatRestoresIt()
	{
		const QString name = QStringLiteral("k-inverse-remove");
		Track* first = addTrack(Track::Type::Instrument, name);
		Track* second = addTrack(Track::Type::Instrument, name);
		const std::vector<Track*> tracks{first, second};
		const QString setName = QStringLiteral("Removable");
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_save"),
			QJsonObject{{QStringLiteral("name"), setName},
				{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(first)}}}).ok,
			qPrintable(QStringLiteral("track.visibility_set_save failed")));
		const QString before = visibilityState(tracks);
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_remove"),
			QJsonObject{{QStringLiteral("name"), setName}}).ok,
			qPrintable(QStringLiteral("track.visibility_set_remove failed")));
		QVERIFY(Engine::getSong()->findVisibilitySet(setName) == nullptr);
		replayInverse(lastInverse());
		QCOMPARE(visibilityState(tracks), before);
		removeAll(name);
	}

	//! A set can outlive one of its tracks. Its reported inverse must still be
	//! replayable when a later save replaces the definition or removes the set.
	void staleTrackIdsUseUndoForReplaceAndRemoveInverse()
	{
		const QString trackName = QStringLiteral("l-stale-inverse");
		Track* deleted = addTrack(Track::Type::Instrument, trackName);
		Track* remaining = addTrack(Track::Type::Instrument, trackName);
		const int deletedId = deleted->id();
		const QString deletedTrackId = control::trackIdOf(deleted);
		const QString remainingTrackId = control::trackIdOf(remaining);
		const QString setName = QStringLiteral("StaleMembers");
		Song* song = Engine::getSong();

		const ControlResult saved = ControlRegistry::instance()->invoke(
			QStringLiteral("track.visibility_set_save"),
			QJsonObject{{QStringLiteral("name"), setName},
				{QStringLiteral("tracks"), QJsonArray{deletedTrackId, remainingTrackId}}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		const ControlResult removedTrack = ControlRegistry::instance()->invoke(
			QStringLiteral("track.remove"), QJsonObject{{QStringLiteral("track"), deletedTrackId}});
		QVERIFY2(removedTrack.ok, qPrintable(removedTrack.errorMessage));
		QVERIFY(song->findTrackById(deletedId) == nullptr);

		const ControlResult replaced = ControlRegistry::instance()->invoke(
			QStringLiteral("track.visibility_set_save"),
			QJsonObject{{QStringLiteral("name"), setName},
				{QStringLiteral("tracks"), QJsonArray{remainingTrackId}}});
		QVERIFY2(replaced.ok, qPrintable(replaced.errorMessage));
		QJsonObject inverse = lastInverse();
		QString inverseOp = inverse.value(QStringLiteral("op")).toString();
		ControlResult replayed = ControlRegistry::instance()->invoke(inverseOp,
			inverse.value(QStringLiteral("args")).toObject());
		QVERIFY2(replayed.ok, qPrintable(QStringLiteral("replacement inverse '%1' failed: %2")
			.arg(inverseOp, replayed.errorMessage)));
		QCOMPARE(inverseOp, QStringLiteral("control.undo"));
		const TrackContainer::VisibilitySet* restored = song->findVisibilitySet(setName);
		QVERIFY(restored != nullptr);
		QCOMPARE(restored->trackIds.size(), std::size_t(2));
		QCOMPARE(restored->trackIds[0], deletedId);
		QCOMPARE(restored->trackIds[1], remaining->id());

		const ControlResult removedSet = ControlRegistry::instance()->invoke(
			QStringLiteral("track.visibility_set_remove"), QJsonObject{{QStringLiteral("name"), setName}});
		QVERIFY2(removedSet.ok, qPrintable(removedSet.errorMessage));
		inverse = lastInverse();
		inverseOp = inverse.value(QStringLiteral("op")).toString();
		replayed = ControlRegistry::instance()->invoke(inverseOp,
			inverse.value(QStringLiteral("args")).toObject());
		QVERIFY2(replayed.ok, qPrintable(QStringLiteral("removal inverse '%1' failed: %2")
			.arg(inverseOp, replayed.errorMessage)));
		QCOMPARE(inverseOp, QStringLiteral("control.undo"));
		restored = song->findVisibilitySet(setName);
		QVERIFY(restored != nullptr);
		QCOMPARE(restored->trackIds.size(), std::size_t(2));
		QCOMPARE(restored->trackIds[0], deletedId);
		QCOMPARE(restored->trackIds[1], remaining->id());

		song->removeVisibilitySet(setName);
		removeAll(trackName);
	}

	//! The same promise for a save: the save that CREATED a set is undone by
	//! removing it again, not by writing an empty definition back, and the save
	//! that REPLACED one is undone by writing the captured definition back.
	void saveSetReportsAnInverseThatRestoresIt()
	{
		const QString name = QStringLiteral("k-inverse-save");
		Track* first = addTrack(Track::Type::Instrument, name);
		Track* second = addTrack(Track::Type::Instrument, name);
		const std::vector<Track*> tracks{first, second};
		const QString setName = QStringLiteral("Fresh");
		const QString absent = visibilityState(tracks);
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_save"),
			QJsonObject{{QStringLiteral("name"), setName},
				{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(first)}}}).ok,
			qPrintable(QStringLiteral("track.visibility_set_save failed")));
		replayInverse(lastInverse());
		QCOMPARE(visibilityState(tracks), absent);
		// Now the replace case: the previous definition is what must come back.
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_save"),
			QJsonObject{{QStringLiteral("name"), setName},
				{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(second)}}}).ok,
			qPrintable(QStringLiteral("track.visibility_set_save failed")));
		const QString beforeOverwrite = visibilityState(tracks);
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_save"),
			QJsonObject{{QStringLiteral("name"), setName},
				{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(first), control::trackIdOf(second)}}}).ok,
			qPrintable(QStringLiteral("track.visibility_set_save failed")));
		replayInverse(lastInverse());
		QCOMPARE(visibilityState(tracks), beforeOverwrite);
		removeAll(name);
	}

	//! An apply writes the visible flag of EVERY track, so no single command puts
	//! the prior flags back: the inverse has to be the recorded step itself, as the
	//! comp group's ManualCompositeInverse already is.
	void applyReportsAnInverseThatRestoresEveryFlag()
	{
		const QString name = QStringLiteral("k-inverse-apply");
		Track* first = addTrack(Track::Type::Instrument, name);
		addTrack(Track::Type::Instrument, name);
		addTrack(Track::Type::Instrument, name);
		const std::vector<Track*> tracks{first};
		const QString setName = QStringLiteral("Subset");
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_save"),
			QJsonObject{{QStringLiteral("name"), setName},
				{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(first)}}}).ok,
			qPrintable(QStringLiteral("track.visibility_set_save failed")));
		const QString before = visibilityState(tracks);
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_apply"),
			QJsonObject{{QStringLiteral("name"), setName}}).ok,
			qPrintable(QStringLiteral("track.visibility_set_apply failed")));
		// The reported inverse is what a client replays; it must restore the flags.
		QVERIFY(!first->isVisible() == false);  // the member is visible after the apply
		replayInverse(lastInverse());
		QCOMPARE(visibilityState(tracks), before);
		removeAll(name);
	}

	//! The same for show-all: it clears the active name and shows every track, so
	//! the reported inverse is the recorded step rather than show-all again.
	void showAllReportsAnInverseThatRestoresEveryFlag()
	{
		const QString name = QStringLiteral("k-inverse-showall");
		Track* first = addTrack(Track::Type::Instrument, name);
		addTrack(Track::Type::Instrument, name);
		const std::vector<Track*> tracks{first};
		const QString setName = QStringLiteral("Shown");
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_save"),
			QJsonObject{{QStringLiteral("name"), setName},
				{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(first)}}}).ok,
			qPrintable(QStringLiteral("track.visibility_set_save failed")));
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_apply"),
			QJsonObject{{QStringLiteral("name"), setName}}).ok,
			qPrintable(QStringLiteral("track.visibility_set_apply failed")));
		const QString before = visibilityState(tracks);
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_show_all")).ok,
			qPrintable(QStringLiteral("track.visibility_show_all failed")));
		replayInverse(lastInverse());
		QCOMPARE(visibilityState(tracks), before);
		removeAll(name);
	}

	//! Redo runs TrackContainer::showAllTracks(), whose signal must refresh the
	//! view just as the initial call and undo do.
	void showAllRedoEmitsVisibilityChanged()
	{
		const QString name = QStringLiteral("m-showall-redo-signal");
		Track* visible = addTrack(Track::Type::Instrument, name);
		Track* hidden = addTrack(Track::Type::Instrument, name);
		Song* song = Engine::getSong();
		QSignalSpy visibilitySpy(song, &TrackContainer::visibilityChanged);
		const QString setName = QStringLiteral("RedoSignal");
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_save"),
			QJsonObject{{QStringLiteral("name"), setName},
				{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(visible)}}}).ok,
			qPrintable(QStringLiteral("track.visibility_set_save failed")));
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_apply"),
			QJsonObject{{QStringLiteral("name"), setName}}).ok,
			qPrintable(QStringLiteral("track.visibility_set_apply failed")));
		QVERIFY(visible->isVisible());
		QVERIFY(!hidden->isVisible());
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_show_all")).ok,
			qPrintable(QStringLiteral("track.visibility_show_all failed")));
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok,
			qPrintable(QStringLiteral("undo failed")));
		QVERIFY(visible->isVisible());
		QVERIFY(!hidden->isVisible());
		QCOMPARE(song->activeVisibilitySet(), setName);

		visibilitySpy.clear();
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("control.redo")).ok,
			qPrintable(QStringLiteral("redo failed")));
		QCOMPARE(visibilitySpy.count(), 1);
		QVERIFY(visible->isVisible());
		QVERIFY(hidden->isVisible());
		QVERIFY(song->activeVisibilitySet().isEmpty());
		song->removeVisibilitySet(setName);
		removeAll(name);
	}

private:
	//! The state the visibility-set commands change: the active name, every set
	//! with its members, and every track's visible flag. One string so a replay can
	//! be asserted against the state the command found.
	QString visibilityState(const std::vector<Track*>& tracks)
	{
		Song* song = Engine::getSong();
		QString state = QStringLiteral("active=") + song->activeVisibilitySet();
		for (const TrackContainer::VisibilitySet& set : song->visibilitySets())
		{
			QStringList ids;
			for (int id : set.trackIds) { ids.append(QString::number(id)); }
			state += QStringLiteral(";set:") + set.name + QLatin1Char('=') + ids.join(QLatin1Char(','));
		}
		for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
		{
			state += QStringLiteral(";v") + QString::number(i)
				+ (tracks[static_cast<size_t>(i)]->isVisible() ? QStringLiteral("=1") : QStringLiteral("=0"));
		}
		return state;
	}

	//! The inverse the newest A16 record reports - the op and args a client reads.
	QJsonObject lastInverse()
	{
		const QJsonArray all = ControlRegistry::instance()->invoke(QStringLiteral("control.transactions"))
			.result.value(QStringLiteral("transactions")).toArray();
		return all.isEmpty() ? QJsonObject() : all.last().toObject().value(QStringLiteral("inverse")).toObject();
	}

	//! Replays a reported inverse through the registry, exactly as a client would.
	void replayInverse(const QJsonObject& inverse)
	{
		const ControlResult replayed = ControlRegistry::instance()->invoke(
			inverse.value(QStringLiteral("op")).toString(),
			inverse.value(QStringLiteral("args")).toObject());
		QVERIFY2(replayed.ok, qPrintable(QStringLiteral("the reported inverse did not restore the state: ")
			+ inverse.value(QStringLiteral("op")).toString() + " -> " + replayed.errorMessage));
	}
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

	//! A no-op track.set_routing answers with a transaction deliberately (the comment at
	//! src/core/ControlCommandsTrackFolder.cpp:234 says an undo of a call that changed nothing
	//! must change nothing, so its inverse is the state the folder is already in). The question
	//! is whether that costs the NEXT undo - it must not.
	void aNoOpRoutingReportsReversibleWithoutStealingTheNextUndo()
	{
		const QString name = QStringLiteral("h-noop-routing");
		TrackFolder* folder = static_cast<TrackFolder*>(addTrack(Track::Type::Folder, name));
		Track* child = addTrack(Track::Type::Instrument, name);
		child->setParentFolder(folder);
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.set_pinned"),
			QJsonObject{{QStringLiteral("track"), control::trackIdOf(folder)}, {QStringLiteral("pinned"), true}}).ok,
			qPrintable(QStringLiteral("track.set_pinned failed")));
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("track.set_routing"),
			QJsonObject{{QStringLiteral("track"), control::trackIdOf(folder)}, {QStringLiteral("routing"), true}}).ok,
			qPrintable(QStringLiteral("track.set_routing failed")));
		QVERIFY(folder->isRouting());
		const ControlResult noop = ControlRegistry::instance()->invoke(QStringLiteral("track.set_routing"),
			QJsonObject{{QStringLiteral("track"), control::trackIdOf(folder)}, {QStringLiteral("routing"), true}});
		QVERIFY2(noop.ok, qPrintable(noop.errorMessage));
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok, qPrintable(QStringLiteral("undo failed")));
		QVERIFY(!folder->isRouting());  // the no-op did not swallow the real undo
		removeAll(name);
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
