/*
 * ProjectContainerSaveTest.cpp - Q050-007: the opt-in v2 project container as
 *                                something a user can SAVE and OPEN, not just a
 *                                writer nobody called.
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

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "DataFile.h"
#include "Engine.h"
#include "ProjectContainer.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run( const QString& id, const QJsonObject& args = {} )
{
	return ControlRegistry::instance()->invoke( id, args );
}

QByteArray readBytes( const QString& path )
{
	QFile file( path );
	if( !file.open( QIODevice::ReadOnly ) ) { return QByteArray(); }
	return file.readAll();
}

bool writeBytes( const QString& path, const QByteArray& bytes )
{
	QFile file( path );
	if( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) { return false; }
	return file.write( bytes ) == bytes.size();
}

//! What a project IS, for comparing two sessions: tempo, and each track's
//! type, name and mute flag in order. Read off the live song, so it measures
//! the loaded state and not the bytes the file happened to hold.
QStringList stateOf()
{
	Song* song = Engine::getSong();
	QStringList state;
	state << QStringLiteral( "tempo=%1" ).arg( song->getTempo() );
	state << QStringLiteral( "tracks=%1" ).arg( song->tracks().size() );
	state << QStringLiteral( "mixer-channels=%1" ).arg( run( QStringLiteral( "mixer.get_state" ) ).result
		.value( QStringLiteral( "channels" ) ).toArray().size() );
	state << QStringLiteral( "master-volume=%1" ).arg( song->masterVolumeModel().value() );
	for( const Track* track : song->tracks() )
	{
		state << QStringLiteral( "%1|%2|%3" ).arg( static_cast<int>( track->type() ) )
			.arg( track->name() ).arg( track->isMuted() );
	}
	return state;
}

//! A fresh session with a tempo and three named tracks of two kinds - enough
//! sections (trackcontainer, tempo map, ...) for a container to cut.
void buildSession()
{
	Song* song = Engine::getSong();
	song->clearProject();
	song->setLoadOnLaunch( false );
	QVERIFY2( run( QStringLiteral( "transport.set_tempo" ), {{ QStringLiteral( "bpm" ), 133 }} ).ok, "set tempo" );
	for( const char* name : { "Drums", "Loops", "Vox" } )
	{
		QVERIFY2( run( QStringLiteral( "track.add" ), {{ QStringLiteral( "type" ), QStringLiteral( "sample" ) },
			{ QStringLiteral( "name" ), QString::fromLatin1( name ) }} ).ok, "add track" );
	}
}

ControlResult saveTo( const QString& path, const QString& format = QString() )
{
	QJsonObject args{{ QStringLiteral( "path" ), path }};
	if( !format.isEmpty() ) { args.insert( QStringLiteral( "format" ), format ); }
	return run( QStringLiteral( "project.save" ), args );
}

//! The undo byte budget for one test: an empty journal under \a bytes, and the default back however the test leaves.
struct BudgetGuard
{
	explicit BudgetGuard( qint64 bytes ) { Engine::projectJournal()->clearJournal(); Engine::projectJournal()->setMaxUndoBytes( bytes ); }
	~BudgetGuard() { Engine::projectJournal()->setMaxUndoBytes( ProjectJournal::DefaultMaxUndoBytes ); }
};

} // namespace


class ProjectContainerSaveTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase();

	void cleanupTestCase();

	void init();

	// -- Acceptance 1: ordinary Save stays legacy ---------------------------

	//! A bare project.save - no `format`, no setting - writes the legacy
	//! qCompress() blob, byte-shape for byte-shape what every earlier build wrote.
	void ordinarySaveStillWritesTheLegacyBlob();

	// -- Acceptance 3: project.save opt-in, project.open auto-detects --------

	void formatV2WritesAContainerAndOpenAutoDetectsBothFormats();

	//! A project opened from a v2 container stays one on a bare save, and
	//! format "legacy" is the way back; a legacy project is not converted by
	//! anything but an explicit request.
	void aV2ProjectStaysV2AndLegacyIsTheWayBack();

	void aContainerIsRefusedForAPathThatIsNotMmpz();

	// -- Acceptance 1 (setting half): the global opt-in ----------------------

	void theGlobalSettingMakesBareSavesContainersForMmpzOnly();

	//! The GUI's own save path: the Save As dialog's answer (a ticked box)
	//! writes a container, an unticked one writes legacy even over the setting,
	//! and a v2 request for a name that is not .mmpz is refused with a reason.
	void theSaveAsChoiceDecidesTheFormat();

	// -- Acceptance 2: fixtures, compared after save/load ---------------------

	//! A legacy fixture and a v2 fixture of ONE document load to the same
	//! project, and saving each back (in its own format) and loading again
	//! changes nothing - the comparison of project state after save/load.
	void legacyAndV2FixturesLoadAndResaveToTheSameProject();

	//! The container carries the SAME document: reassembled, it is the bytes a
	//! legacy save of the same session inflates to, except for the <z:index>
	//! the container needs and the legacy save leaves out.
	void theContainerReassemblesToTheLegacyDocument();

	// -- Interrupted saves -----------------------------------------------------

	//! A v2 save that died after writing <file>.new and before the rename is
	//! adopted on the next open when complete and discarded when truncated -
	//! the same rule a legacy save's temp follows (DataFile recoverPendingWrite).
	void anInterruptedV2SaveIsRecoveredLikeALegacyOne();

	//! Review round 2: the writer recognises exactly the lowercase `.mmpz` suffix (anything
	//! else is renamed to X.<ext>.mmpz by nameWithExtension), so a v2 request for `X.MMPZ`
	//! must be refused up front instead of saving a different file than the one named and
	//! reporting success; the global opt-in must not change what such a path gets either.
	void anUppercaseSuffixIsRefusedForV2AndNeverMislabelled();

	//! Review round 2: the recovery size bound measures the DOCUMENT, not the ZIP file. A
	//! container whose file is a little over the limit because of its framing, but whose
	//! document is within it, is adopted; one whose document is over it is left in place.
	void recoveryMeasuresTheContainersDocumentNotItsFile();

	//! A damaged container is refused by name by project.open and leaves the
	//! session as it was.
	void aDamagedContainerIsRefusedAndTheSessionSurvives();

	//! The revision rotation project.save performs is byte-level and needs no
	//! change: a v2 file replaced by a save is kept as revision 0, still a container.
	void theReplacedContainerIsKeptAsARevision();

	// -- Undo keeps the session's container format ---------------------------

	//! An undo step restores a snapshot document, which is plain XML. The load
	//! that restore runs must not turn the session into a legacy one: the next
	//! bare save to .mmpz stays a v2 container, as it was before the undo.
	void undoingAV2ProjectKeepsItsContainerFormat();

	//! The other side of the same restore: a legacy session stays legacy across
	//! an undo, so the fix changes nothing for projects that never were containers.
	void undoingALegacyProjectKeepsItsLegacyFormat();

	//! A restore the loader refuses leaves the document, and so its container
	//! format, exactly as it was.
	void aRefusedUndoRestoreKeepsTheContainerFormat();

	// -- Checkpoints put back the format of the session they restore ----------

	//! project.open is a checkpoint too, and a restore takes the format of the session it puts back, not
	//! the one it replaces: undo restores the displaced legacy session as legacy, redo the opened v2 project.
	void undoingAProjectOpenRestoresTheDisplacedContainerFormat();

	//! A checkpoint that cannot be kept rolls the open back after the v2 project has loaded. The displaced
	//! legacy session must come back as legacy, not keep the loaded project's container format.
	void aRefusedProjectOpenRollsBackToTheDisplacedContainerFormat();

	//! A v2 save read back through the document entry point: an ordinary read
	//! reassembles it, and a read that gives back nothing is refused AS A READ, so
	//! the recovery keeps the file. The read fault is ZENE_TEST_CONTAINER_READ_FAULT.
	void aFailedReadOfASavedContainerIsNamedAsARead();

	//! Control: a saved container cut in half is malformed - its directory is gone -
	//! and is refused as such, never as an unreadable file.
	void aTruncatedSavedContainerIsRefusedAsMalformed();
};

#include "ProjectContainerSaveTestParts1.h"
#include "ProjectContainerSaveTestParts2.h"

QTEST_GUILESS_MAIN( ProjectContainerSaveTest )
#include "ProjectContainerSaveTest.moc"
