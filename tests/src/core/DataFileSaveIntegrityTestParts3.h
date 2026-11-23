/*
 * DataFileSaveIntegrityTestParts3.h - cluster new-p10-89: a save and recovery
 * of the same project coordinate through <name>.save-lock, so recovery cannot
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

void DataFileSaveIntegrityTest::liveSaveInAnotherInstanceIsNotAdopted() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString temp = target + QStringLiteral( ".new" );
	const QString bak = target + QStringLiteral( ".bak" );

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

	QLockFile holder( target + QStringLiteral( ".save-lock" ) );
	QVERIFY2( holder.tryLock( 0 ), "the fixture could not take the save lock" );
	// A real save can run far longer than QLockFile's 30 second default
	// staleness age (a bundle copies its resources, a big project, a remote
	// folder). Age the live sidecar to 1970 so the fixture is such a save: an
	// aged lock of a LIVE process must still hold, or the writer's own lock is
	// taken away from it and recovery consumes the temp this test keeps.
	QVERIFY( setModifiedTime( target + QStringLiteral( ".save-lock" ), 1000000 ) );

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
	QVERIFY2( QFile::exists( target + QStringLiteral( ".save-lock" ) ),
		"the live save's sidecar was removed as stale" );

	// The holder goes away - what a crashed writer leaves behind is a lock with
	// a dead pid, which QLockFile discards. The very same temp is then the
	// orphan adoption is for, so recovery must still make it the project.
	holder.unlock();
	QVERIFY( !QFileInfo::exists( target + QStringLiteral( ".save-lock" ) ) );
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

	{
		QLockFile holder( target + QStringLiteral( ".save-lock" ) );
		QVERIFY2( holder.tryLock( 0 ), "the fixture could not take the save lock" );
		// Aged to 1970, as a save that has been running for minutes leaves it.
		QVERIFY( setModifiedTime( target + QStringLiteral( ".save-lock" ), 1000000 ) );

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
		QVERIFY2( holder.isLocked() && QFile::exists( target + QStringLiteral( ".save-lock" ) ),
			"the save took the aged lock of a live writer away from it" );
	}

	DataFile dataFile( DataFile::Type::SongProject );
	QVERIFY2( dataFile.writeFile( target ),
		"the save did not land once the other instance's lock was gone" );
	QVERIFY( readText( target ).contains( QStringLiteral( "<zene-project" ) ) );
	QCOMPARE( readText( target + QStringLiteral( ".bak" ) ), previous );
	QVERIFY2( !QFileInfo::exists( target + QStringLiteral( ".save-lock" ) ),
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
	const QString lockPath = target + QStringLiteral( ".save-lock" );

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
