/*
 * DataFileSaveIntegrityTestParts3.h - cluster new-p10-89: a save and recovery
 * of the same project coordinate through the save lock (DataFileSaveLock), so
 * recovery cannot
 * adopt a temp that belongs to a save still running in another instance, and a
 * save cannot run without the lock it announces itself with.
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

// Complete fixture method definitions for cluster new-p10-89; included once
// after the fixture declaration and after Parts2 (it uses pendingSong() and
// setModifiedTime() from there).

#ifdef Q_OS_UNIX
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

//! True when \a path's modification time is before 1971: the fixture aged it to
//! the epoch, which no lock a running save leaves behind has.
bool isAgedBefore1971( const QString& path )
{
	const QDateTime startOf1971 = QDateTime::fromSecsSinceEpoch( 31536000 );	// 1971-01-01 UTC
	const QDateTime stamp = QFileInfo( path ).lastModified();
	return stamp.isValid() && stamp < startOf1971;
}

//! The longest project basename a save can support in \a dirPath. A save derives
//! <name>.new, <name>.bak and <name>.bak.new from the project name, and the last
//! of those is the longest at 8 bytes, so a filesystem with NAME_MAX bytes per
//! name supports NAME_MAX - 8. The save lock spends its own suffix inside that
//! same budget instead of adding a longer one.
int longestProjectBasename( const QString& dirPath )
{
#ifdef Q_OS_UNIX
	const long fromFilesystem =
		::pathconf( QFile::encodeName( dirPath ).constData(), _PC_NAME_MAX );
	if( fromFilesystem > 0 && fromFilesystem < 4096 )
	{
		return int( fromFilesystem ) - int( QByteArray( ".bak.new" ).size() );
	}
#endif
	return 247;	// NAME_MAX 255, the value the supported filesystems use
}

//! The project path of a file in \a dirPath whose basename is exactly \a length
//! bytes. ".mmp" is a name DataFile::normalisedName() leaves alone.
QString projectNameOfLength( const QString& dirPath, int length )
{
	const QString extension = QStringLiteral( ".mmp" );
	QString name( length - extension.size(), QLatin1Char( 'x' ) );
	name += extension;
	return QDir( dirPath ).filePath( name );
}

#ifdef Q_OS_UNIX
//! Takes \a lockPath's lock in a forked child that exits WITHOUT unlocking, so
//! what stays on disk is a real QLockFile whose recorded holder is a process
//! that is gone - what a writer that crashed leaves behind, and the only state a
//! stale-lock reclaim has to work on. Returns false when the child could not
//! take the lock or did not exit cleanly.
bool leaveLockOfDeadProcess( const QString& lockPath )
{
	const pid_t pid = ::fork();
	if( pid < 0 )
	{
		return false;
	}
	if( pid == 0 )
	{
		// _exit() runs no destructor, so the sidecar stays behind to be reclaimed.
		QLockFile lock( lockPath );
		::_exit( lock.tryLock( 0 ) ? 0 : 1 );
	}
	int status = 0;
	if( ::waitpid( pid, &status, 0 ) != pid )
	{
		return false;
	}
	return WIFEXITED( status ) && WEXITSTATUS( status ) == 0;
}
#endif

} // namespace

void DataFileSaveIntegrityTest::liveSaveInAnotherInstanceIsNotAdopted() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString temp = target + QStringLiteral( ".new" );
	const QString bak = target + QStringLiteral( ".bak" );
	const QString lockPath = DataFileSaveLock::lockPathFor( target );

	DataFile first( DataFile::Type::SongProject );
	first.content().setAttribute( "s5probe", "old" );
	QVERIFY( first.writeFile( target ) );
	const QString oldText = readText( target );
	QVERIFY( !QFileInfo::exists( bak ) );

	// The other instance's save caught in its window: its complete temp is on
	// disk and its save lock is held, so the writer is alive and about to
	// rename the temp over the project. The lock is held here, in this process,
	// because that is what the writer's own lock looks like to a second
	// opener: QLockFile refuses a lock whose recorded pid is a live process.
	QVERIFY( writeText( temp, pendingSong( QStringLiteral( "live" ) ) ) );
	QVERIFY( setModifiedTime( target, 1000000 ) );
	QVERIFY( setModifiedTime( temp, 2000000 ) );

	QLockFile holder( lockPath );
	QVERIFY2( holder.tryLock( 0 ), "the fixture could not take the save lock" );
	// A real save can run far longer than QLockFile's 30 second default
	// staleness age (a bundle copies its resources, a big project, a remote
	// folder). Age the live sidecar to 1970 so the fixture is such a save: an
	// aged lock of a LIVE process must still hold, or the writer's own lock is
	// taken away from it and recovery consumes the temp this test keeps.
	QVERIFY( setModifiedTime( lockPath, 1000000 ) );
	// The age really landed: a fixture whose aging silently did nothing would
	// leave the age rule this test exists for unexercised.
	QVERIFY2( isAgedBefore1971( lockPath ), "the fixture did not age the live save's lock" );

	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
		const QString report = recorder.messages();
		QVERIFY2( report.contains( QStringLiteral( "may still be in flight" ) ),
			qPrintable( QStringLiteral( "a save in flight was not reported; captured: [%1]" )
				.arg( report ) ) );
	}

	// Nothing of the live save was touched: the writer still has the temp it is
	// about to publish, the project is still the previous version, and no
	// backup was written from under it.
	QVERIFY2( QFile::exists( temp ),
		"recovery consumed the temp of a save that is still running" );
	QCOMPARE( probe, QStringLiteral( "old" ) );
	QCOMPARE( readText( target ), oldText );
	QVERIFY2( !QFileInfo::exists( bak ),
		"recovery backed the project up while its writer was still running" );
	// The aged lock is still held by its live holder, not reclaimed as stale.
	QVERIFY2( holder.isLocked(), "the live save's lock was taken away from it" );
	QVERIFY2( QFile::exists( lockPath ), "the live save's sidecar was removed as stale" );

	// The holder goes away - what a crashed writer leaves behind is a lock with
	// a dead pid, which QLockFile discards. The very same temp is then the
	// orphan adoption is for, so recovery must still make it the project.
	holder.unlock();
	QVERIFY( !QFileInfo::exists( lockPath ) );
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
		QVERIFY2( recorder.messages().contains( QStringLiteral( "Recovered interrupted save" ) ),
			qPrintable( QStringLiteral( "the orphan was not adopted once the holder was gone; "
				"captured: [%1]" ).arg( recorder.messages() ) ) );
	}

	QCOMPARE( probe, QStringLiteral( "live" ) );
	QVERIFY2( !QFile::exists( temp ), "the adopted temp was not consumed" );
	QCOMPARE( readText( bak ), oldText );
}
void DataFileSaveIntegrityTest::saveIsRefusedWhileAnotherInstanceHoldsTheLock() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString previous = QStringLiteral( "the previous project" );
	const QString olderBackup = QStringLiteral( "the generation before that" );
	QVERIFY( writeText( target, previous ) );
	QVERIFY( writeText( target + QStringLiteral( ".bak" ), olderBackup ) );
	const QString lockPath = DataFileSaveLock::lockPathFor( target );

	{
		QLockFile holder( lockPath );
		QVERIFY2( holder.tryLock( 0 ), "the fixture could not take the save lock" );
		// Aged to 1970, as a save that has been running for minutes leaves it.
		QVERIFY( setModifiedTime( lockPath, 1000000 ) );
		QVERIFY2( isAgedBefore1971( lockPath ), "the fixture did not age the writer's lock" );

		DataFile dataFile( DataFile::Type::SongProject );
		QTest::ignoreMessage( QtWarningMsg,
			QRegularExpression( QStringLiteral( "was NOT saved" ) ) );

		QVERIFY2( !dataFile.writeFile( target ),
			"a save that another instance is running was reported as a success" );

		// A refused save discards nothing and publishes nothing: no second temp
		// written over the writer's, the project and the older backup intact,
		// and the writer's aged lock still its own rather than reclaimed.
		QCOMPARE( readText( target ), previous );
		QCOMPARE( readText( target + QStringLiteral( ".bak" ) ), olderBackup );
		QVERIFY2( !QFileInfo::exists( target + QStringLiteral( ".new" ) ),
			"the refused save left a second temp beside the writer's" );
		QVERIFY2( holder.isLocked() && QFile::exists( lockPath ),
			"the save took the aged lock of a live writer away from it" );
	}

	DataFile dataFile( DataFile::Type::SongProject );
	QVERIFY2( dataFile.writeFile( target ),
		"the save did not land once the other instance's lock was gone" );
	QVERIFY( readText( target ).contains( QStringLiteral( "<zene-project" ) ) );
	QCOMPARE( readText( target + QStringLiteral( ".bak" ) ), previous );
	QVERIFY2( !QFileInfo::exists( lockPath ),
		"a completed save left its save lock behind, which would refuse the next one" );
	QVERIFY2( !QFileInfo::exists( target + QStringLiteral( ".new" ) ),
		"a completed save left its temp behind" );
}
void DataFileSaveIntegrityTest::unusableSaveLockFailsClosed() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString temp = target + QStringLiteral( ".new" );
	const QString lockPath = DataFileSaveLock::lockPathFor( target );

	DataFile first( DataFile::Type::SongProject );
	first.content().setAttribute( "s5probe", "old" );
	QVERIFY( first.writeFile( target ) );
	const QString oldText = readText( target );

	// Something that is not a lock file occupies the sidecar name, so the
	// acquisition fails for a reason that is not "another live instance holds
	// it". Writing <name>.new with no lock in hand is exactly the state this
	// fix exists to prevent, so both sides must fail closed.
	QVERIFY( occupy( lockPath ) );

	DataFile writer( DataFile::Type::SongProject );
	QTest::ignoreMessage( QtWarningMsg, QRegularExpression( QStringLiteral( "was NOT saved" ) ) );
	QVERIFY2( !writer.writeFile( target ),
		"a save wrote its temp although it could not take the save lock" );
	QCOMPARE( readText( target ), oldText );
	QVERIFY2( !QFileInfo::exists( temp ),
		"a save that could not take its lock left a temp behind" );
	QVERIFY2( !QFileInfo::exists( target + QStringLiteral( ".bak" ) ),
		"a save that could not take its lock backed the project up" );

	// Recovery is fail-closed too: the temp stays for the next open, which is
	// the harmless direction, and the project stands.
	QVERIFY( writeText( temp, pendingSong( QStringLiteral( "pending" ) ) ) );
	QVERIFY( setModifiedTime( target, 1000000 ) );
	QVERIFY( setModifiedTime( temp, 2000000 ) );
	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
		QVERIFY2( recorder.messages().contains( QStringLiteral( "may still be in flight" ) ),
			qPrintable( QStringLiteral( "an unusable save lock was not reported; captured: [%1]" )
				.arg( recorder.messages() ) ) );
	}
	QCOMPARE( probe, QStringLiteral( "old" ) );
	QCOMPARE( readText( target ), oldText );
	QVERIFY2( QFile::exists( temp ),
		"recovery consumed a temp whose save lock it could not take" );

	// The same refusal when the folder itself refuses the lock file - a
	// permission failure, not a competing instance. Where the platform does not
	// enforce permission bits on directories this half proves nothing, so it is
	// built only for the systems that do.
#ifdef Q_OS_UNIX
	QVERIFY( QFile::remove( lockPath + QStringLiteral( "/occupied" ) ) );
	QVERIFY( QDir().rmdir( lockPath ) );
	QVERIFY( QFile::setPermissions( dir.path(),
		QFileDevice::ReadOwner | QFileDevice::ExeOwner ) );
	{
		DataFile blocked( DataFile::Type::SongProject );
		QTest::ignoreMessage( QtWarningMsg,
			QRegularExpression( QStringLiteral( "was NOT saved" ) ) );
		QVERIFY2( !blocked.writeFile( target ),
			"a save in a folder that refuses the lock file reported success" );
	}
	QCOMPARE( readText( target ), oldText );
	QVERIFY( QFile::setPermissions( dir.path(), QFileDevice::ReadOwner
		| QFileDevice::WriteOwner | QFileDevice::ExeOwner ) );
#endif
}
void DataFileSaveIntegrityTest::aHeldSaveLockIsExclusiveAndHandedToWriteFile() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );
	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString other = dir.filePath( QStringLiteral( "other.mmp" ) );
	const QString lockPath = DataFileSaveLock::lockPathFor( target );
	QCOMPARE( lockPath, target + QLatin1Char( '#' ) );
	// The name a save writes is the name its lock is taken for.
	QCOMPARE( DataFile::normalisedName( DataFile::Type::SongProject, dir.filePath( QStringLiteral( "bare" ) ) ),
		dir.filePath( QStringLiteral( "bare" ) ) + QStringLiteral( ".mmpz" ) );
	QCOMPARE( DataFile::normalisedName( DataFile::Type::SongProject, target ), target );

	QString refusal;
	std::unique_ptr<DataFileSaveLock> held = DataFileSaveLock::tryAcquire( target, &refusal );
	QVERIFY2( held, qPrintable( refusal ) );
	QVERIFY( QFile::exists( lockPath ) );

	// Exclusive: a second contender in this process is refused with the reason.
	QString second;
	QVERIFY( !DataFileSaveLock::tryAcquire( target, &second ) );
	QVERIFY2( second.contains( QStringLiteral( "another instance" ) ), qPrintable( second ) );

	// A lock for another file is not this file's lock.
	{
		DataFile mismatched( DataFile::Type::SongProject );
		QTest::ignoreMessage( QtWarningMsg, QRegularExpression( QStringLiteral( "was NOT saved" ) ) );
		QVERIFY( !mismatched.writeFile( other, false, held.get() ) );
		QVERIFY( !QFileInfo::exists( other ) );
		QVERIFY( !QFileInfo::exists( other + QStringLiteral( ".new" ) ) );
	}

	// The held lock is used, not re-acquired: the save lands and the caller
	// still owns the lock afterwards.
	DataFile writer( DataFile::Type::SongProject );
	QVERIFY2( writer.writeFile( target, false, held.get() ), "a save given its held lock failed" );
	QVERIFY( readText( target ).contains( QStringLiteral( "<zene-project" ) ) );
	QVERIFY2( QFile::exists( lockPath ), "writeFile released a lock it was only handed" );
	QVERIFY( !DataFileSaveLock::tryAcquire( target ) );

	held.reset();
	QVERIFY2( !QFileInfo::exists( lockPath ), "releasing the lock left its sidecar behind" );
	QVERIFY( DataFileSaveLock::tryAcquire( target ) );
}
void DataFileSaveIntegrityTest::restoreIsRefusedBeforeRotatingWhileASaveHoldsTheLock() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString rev0 = control::projectRevisionPath( target, 0 );
	const QString rev1 = control::projectRevisionPath( target, 1 );
	QVERIFY( writeText( target, QStringLiteral( "live" ) ) );
	QVERIFY( writeText( rev0, QStringLiteral( "zero" ) ) );
	QVERIFY( writeText( rev1, QStringLiteral( "one" ) ) );
	QVERIFY( writeText( target + QStringLiteral( ".bak" ), QStringLiteral( "bak" ) ) );

	QString error;
	{
		const std::unique_ptr<DataFileSaveLock> writer = DataFileSaveLock::tryAcquire( target );
		QVERIFY( writer );

		// The keep-3 restore: refused, and NOTHING moved - no rotation of the
		// set, no replacement, no staging file left beside the project.
		QVERIFY2( !control::restoreProjectRevision( target, 0, &error ), "restore ran during a save" );
		QVERIFY2( error.contains( QStringLiteral( "NOT restored" ) ), qPrintable( error ) );
		// The timeline's own restore (the backup slot) takes the other path.
		QVERIFY2( !control::restoreTimelineRevision( target, QString(), QStringLiteral( "backup" ), &error ),
			"a timeline restore ran during a save" );
		QVERIFY2( error.contains( QStringLiteral( "NOT restored" ) ), qPrintable( error ) );

		QCOMPARE( readText( target ), QStringLiteral( "live" ) );
		QCOMPARE( readText( rev0 ), QStringLiteral( "zero" ) );
		QCOMPARE( readText( rev1 ), QStringLiteral( "one" ) );
		QVERIFY( !QFileInfo::exists( control::projectRevisionPath( target, 2 ) ) );
		QCOMPARE( readText( target + QStringLiteral( ".bak" ) ), QStringLiteral( "bak" ) );
		QStringList left = QDir( dir.path() ).entryList( QDir::Files | QDir::Hidden );
		left.removeAll( QFileInfo( DataFileSaveLock::lockPathFor( target ) ).fileName() );
		left.sort();
		QCOMPARE( left, ( QStringList{ QStringLiteral( "song.mmp" ), QStringLiteral( "song.mmp.bak" ),
			QStringLiteral( "song.mmp.rev0" ), QStringLiteral( "song.mmp.rev1" ) } ) );
	}

	// Lock gone: both restores proceed and rotate as they always did.
	QVERIFY2( control::restoreProjectRevision( target, 0, &error ), qPrintable( error ) );
	QCOMPARE( readText( target ), QStringLiteral( "zero" ) );
	QCOMPARE( readText( rev0 ), QStringLiteral( "live" ) );
	QVERIFY2( control::restoreTimelineRevision( target, QString(), QStringLiteral( "backup" ), &error ),
		qPrintable( error ) );
	QCOMPARE( readText( target ), QStringLiteral( "bak" ) );
	QVERIFY( !QFileInfo::exists( DataFileSaveLock::lockPathFor( target ) ) );
}
void DataFileSaveIntegrityTest::saveLockNameFitsTheSavesOwnNameBudget() {
	// Any file: the rule is about the NAME, not about a particular project.
	const QString target = QDir( QDir::tempPath() ).filePath( QStringLiteral( "song.mmp" ) );
	const QString lockPath = DataFileSaveLock::lockPathFor( target );
	const QFileInfo targetInfo( target );
	const QFileInfo lockInfo( lockPath );

	// Same directory, one byte appended to the FULL file name (extension
	// included): one lock per project file, and a name no save writes, so the
	// sidecar cannot be mistaken for a project or data file.
	QCOMPARE( lockInfo.absolutePath(), targetInfo.absolutePath() );
	QCOMPARE( lockInfo.fileName(), targetInfo.fileName() + QLatin1Char( '#' ) );
	QVERIFY2( DataFile::normalisedName( DataFile::Type::SongProject, lockPath ) != lockPath,
		"the save lock name is one a project save writes" );

	// The budget. QLockFile derives <lock>.rmlock (7 bytes) for the step that
	// reclaims a stale lock, and the longest name a save derives from the
	// project name is <name>.bak.new (8 bytes). A lock whose own step fits in
	// that budget cannot be the thing that refuses a save, or the recovery of
	// its interrupted temp, at a name the save itself supports.
	const int lockNeed = lockPath.size() + QByteArray( ".rmlock" ).size();
	const int saveNeed = target.size() + QByteArray( ".bak.new" ).size();
	QVERIFY2( lockNeed <= saveNeed,
		qPrintable( QStringLiteral( "the save lock plus QLockFile's reclaim marker needs %1 bytes, "
			"more than the %2 the save's longest name needs" )
			.arg( QString::number( lockNeed ), QString::number( saveNeed ) ) ) );
}
void DataFileSaveIntegrityTest::saveAtTheNameLengthLimitLandsAndLeavesNoLock() {
#ifdef Q_OS_WIN
	QSKIP( "MAX_PATH, not the lock name, is the limit for a name this long on Windows" );
#endif
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	// The longest project name the SAVE supports: <name>.bak.new is exactly
	// NAME_MAX bytes, so the second save below writes the longest name a save
	// derives from the project name at all.
	const int limit = longestProjectBasename( dir.path() );
	const QString target = projectNameOfLength( dir.path(), limit );
	QCOMPARE( QFileInfo( target ).fileName().size(), limit );
	const QString lockPath = DataFileSaveLock::lockPathFor( target );

	// The sidecar stays inside that budget instead of spending more than it.
	QVERIFY2( lockPath.size() <= target.size() + 1, qPrintable( lockPath ) );
	QVERIFY2( QFileInfo( lockPath + QStringLiteral( ".rmlock" ) ).fileName().size() <= limit + 8,
		"the lock's own reclaim marker outgrows the save's longest name" );

	// A save must land at the limit - with backups on, so the .bak.new copy that
	// reaches NAME_MAX bytes runs too - and leave no lock behind.
	DataFile first( DataFile::Type::SongProject );
	first.content().setAttribute( "s5probe", "first" );
	QVERIFY2( first.writeFile( target ),
		qPrintable( QStringLiteral( "a save with a %1 byte name was refused" )
			.arg( QString::number( limit ) ) ) );
	QVERIFY( readText( target ).contains( QStringLiteral( "s5probe=\"first\"" ) ) );

	DataFile second( DataFile::Type::SongProject );
	second.content().setAttribute( "s5probe", "second" );
	QVERIFY2( second.writeFile( target ), "the second save at the name length limit was refused" );

	QVERIFY( readText( target ).contains( QStringLiteral( "s5probe=\"second\"" ) ) );
	QVERIFY2( QFile::exists( target + QStringLiteral( ".bak" ) ),
		"the boundary save wrote no backup" );
	QVERIFY2( readText( target + QStringLiteral( ".bak" ) ).contains( QStringLiteral( "s5probe=\"first\"" ) ),
		"the boundary save's backup is not the previous generation" );
	QVERIFY2( !QFileInfo::exists( lockPath ), "the boundary save left its lock behind" );
	QVERIFY2( !QFileInfo::exists( lockPath + QStringLiteral( ".rmlock" ) ),
		"the boundary save left QLockFile's reclaim marker behind" );
}
#ifdef Q_OS_UNIX
void DataFileSaveIntegrityTest::deadWritersLockIsReclaimedAtTheNameLengthLimit() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	// The window this regression is about: a name long enough that the lock's own
	// reclaim step outgrows NAME_MAX while every name the SAVE uses still fits.
	// 15 = ".save-lock" (10) + ".rmlock" (7) - ".bak.new" (8).
	const QString target = projectNameOfLength( dir.path(), longestProjectBasename( dir.path() ) - 7 );
	const QString temp = target + QStringLiteral( ".new" );
	const QString lockPath = DataFileSaveLock::lockPathFor( target );

	DataFile first( DataFile::Type::SongProject );
	first.content().setAttribute( "s5probe", "old" );
	QVERIFY2( first.writeFile( target ), "the project could not be saved before the dead write" );
	QVERIFY( !QFile::exists( temp ) );

	// The interrupted save: a COMPLETE temp, and the lock of a writer that is
	// gone. Nothing here aged or hand-wrote the lock - a real QLockFile is left
	// by a real process that died without unlocking it.
	QVERIFY( writeText( temp, pendingSong( QStringLiteral( "crash" ) ) ) );
	QVERIFY( setModifiedTime( target, 1000000 ) );
	QVERIFY( setModifiedTime( temp, 2000000 ) );
	QVERIFY2( leaveLockOfDeadProcess( lockPath ), "the dead holder could not take the save lock" );
	QVERIFY2( QFileInfo::exists( lockPath ), "the dead writer left no sidecar" );

	// The next open must reclaim that lock and adopt the temp. It cannot when the
	// lock's own reclaim step does not fit, and then a complete interrupted save
	// stays pending forever.
	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
		QVERIFY2( recorder.messages().contains( QStringLiteral( "Recovered interrupted save" ) ),
			qPrintable( QStringLiteral( "the dead writer's lock was not reclaimed, so the "
				"interrupted save stayed pending; captured: [%1]" ).arg( recorder.messages() ) ) );
	}

	QCOMPARE( probe, QStringLiteral( "crash" ) );
	QVERIFY2( !QFile::exists( temp ), "the adopted temp was not consumed" );
	QVERIFY( readText( target ).contains( QStringLiteral( "s5probe=\"crash\"" ) ) );
	QVERIFY2( !QFileInfo::exists( lockPath ), "the reclaimed lock was left behind" );
	QVERIFY2( !QFileInfo::exists( lockPath + QStringLiteral( ".rmlock" ) ),
		"the reclaim left QLockFile's marker behind" );
}
#else
void DataFileSaveIntegrityTest::deadWritersLockIsReclaimedAtTheNameLengthLimit() {
	// A dead holder needs a process that can die without unwinding (fork/_exit),
	// and a name this long needs a filesystem that counts bytes, not MAX_PATH.
	QSKIP( "the dead-holder fixture needs fork() and a NAME_MAX filesystem" );
}
#endif
