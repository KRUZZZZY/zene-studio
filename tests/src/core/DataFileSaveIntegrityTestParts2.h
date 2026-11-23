/*
 * DataFileSaveIntegrityTest.cpp - D3: a save whose final renames the
 * filesystem refuses must be reported as a failure, not as a success.
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

// Complete fixture method definitions; included once after the fixture declaration.

void DataFileSaveIntegrityTest::newFileWithNestedContentBeforeDirectContentIsNotAdopted() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString temp = target + QStringLiteral( ".new" );

	DataFile first( DataFile::Type::SongProject );
	first.content().setAttribute( "s5probe", "old" );
	QVERIFY( first.writeFile( target ) );
	const QString oldText = readText( target );

	QVERIFY( writeText( temp, QStringLiteral( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<zene-project version=\"40\" type=\"song\"><head/>"
		"<wrapper><song s5probe=\"nested\"/></wrapper>"
		"<song s5probe=\"direct\"/></zene-project>\n" ) ) );

	QString report;
	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		report = recorder.messages();
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
	}

	QVERIFY2( report.contains( QStringLiteral( "Discarded incomplete" ) ),
		qPrintable( QStringLiteral( "a nested-before-direct .new was adopted; captured: [%1]" ).arg( report ) ) );
	QVERIFY2( !QFile::exists( temp ), "the nested-before-direct .new was not discarded" );
	QCOMPARE( probe, QStringLiteral( "old" ) );
	QCOMPARE( readText( target ), oldText );
}
void DataFileSaveIntegrityTest::deeplyNestedNewFileIsRefusedBeforeParsing() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString temp = target + QStringLiteral( ".new" );

	DataFile first( DataFile::Type::SongProject );
	first.content().setAttribute( "s5probe", "old" );
	QVERIFY( first.writeFile( target ) );
	const QString oldText = readText( target );

	QString nested = QStringLiteral(
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<zene-project version=\"40\" type=\"song\"><head/><song>" );
	for( int i = 0; i < MaxProjectXmlDepth + 16; ++i ) { nested += QStringLiteral( "<x>" ); }
	for( int i = 0; i < MaxProjectXmlDepth + 16; ++i ) { nested += QStringLiteral( "</x>" ); }
	nested += QStringLiteral( "</song></zene-project>\n" );
	QVERIFY( writeText( temp, nested ) );

	QString report;
	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		report = recorder.messages();
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
	}

	QVERIFY2( report.contains( QStringLiteral( "Discarded incomplete" ) ),
		qPrintable( QStringLiteral( "a deeply nested .new was adopted; captured: [%1]" ).arg( report ) ) );
	QVERIFY2( !QFile::exists( temp ), "the deeply nested .new was not discarded" );
	QCOMPARE( probe, QStringLiteral( "old" ) );
	QCOMPARE( readText( target ), oldText );
}
void DataFileSaveIntegrityTest::newFileDeclaringAnEnormousSizeIsRefusedBeforeDecompressing() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString temp = target + QStringLiteral( ".new" );

	DataFile first( DataFile::Type::SongProject );
	first.content().setAttribute( "s5probe", "old" );
	QVERIFY( first.writeFile( target ) );
	const QString oldText = readText( target );

	// A genuine qCompress stream whose 4-byte size header is rewritten to
	// claim a gigabyte: small on disk, enormous as declared.
	QByteArray crafted = qCompress( QByteArrayLiteral(
		"<?xml version=\"1.0\"?>\n<zene-project version=\"40\" type=\"song\">"
		"<head/><song><trackcontainer type=\"song\"/></song></zene-project>\n" ) );
	QVERIFY( crafted.size() > 4 );
	crafted.replace( 0, 4, QByteArray::fromRawData( "\x40\x00\x00\x00", 4 ) );
	QVERIFY( writeBytes( temp, crafted ) );

	QString report;
	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		report = recorder.messages();
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
	}

	QVERIFY2( report.contains( QStringLiteral( "Refused an oversized" ) ),
		qPrintable( QStringLiteral( "the declared size was not checked; captured: [%1]" ).arg( report ) ) );
	QCOMPARE( probe, QStringLiteral( "old" ) );  // the valid project stands
	QCOMPARE( readText( target ), oldText );
}
void DataFileSaveIntegrityTest::unreadableNewFileIsPreservedNotDeleted() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString temp = target + QStringLiteral( ".new" );

	DataFile first( DataFile::Type::SongProject );
	first.content().setAttribute( "s5probe", "old" );
	QVERIFY( first.writeFile( target ) );
	const QString oldText = readText( target );

	// A complete, recognised document: the content the user asked to save.
	QVERIFY( writeText( temp, QStringLiteral( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<zene-project version=\"40\" type=\"song\"><head/>"
		"<song s5probe=\"pending\"><trackcontainer type=\"song\"/></song></zene-project>\n" ) ) );
	QVERIFY( QFile::setPermissions( temp, QFileDevice::WriteOwner ) );

	// Root reads a file with no read permission, so the refused open this test
	// needs cannot happen there; say so instead of passing without a check.
	QFile unlocked( temp );
	if( unlocked.open( QIODevice::ReadOnly ) )
	{
		unlocked.close();
		QVERIFY( QFile::setPermissions( temp, QFileDevice::ReadOwner | QFileDevice::WriteOwner ) );
		QSKIP( "this process can read a file without read permission, so the open cannot be refused" );
	}

	QString report;
	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		report = recorder.messages();
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
	}

	QVERIFY2( !report.contains( QStringLiteral( "Discarded incomplete" ) ),
		qPrintable( QStringLiteral( "an unreadable .new was treated as incomplete; captured: [%1]" ).arg( report ) ) );
	QVERIFY2( report.contains( QStringLiteral( "Could not read" ) ),
		qPrintable( QStringLiteral( "no report line for the unreadable .new; captured: [%1]" ).arg( report ) ) );
	QVERIFY2( QFile::exists( temp ), "the unreadable .new was deleted - the only staged save is gone" );
	QCOMPARE( probe, QStringLiteral( "old" ) );  // not adopted while it cannot be checked
	QCOMPARE( readText( target ), oldText );

	// Readable again: the preserved save is still there and is adopted.
	QVERIFY( QFile::setPermissions( temp, QFileDevice::ReadOwner | QFileDevice::WriteOwner ) );
	DataFile later( target );
	QCOMPARE( later.content().attribute( QStringLiteral( "s5probe" ) ), QStringLiteral( "pending" ) );
	QVERIFY( !QFile::exists( temp ) );
}
void DataFileSaveIntegrityTest::unnamedDataFileLeavesTheWorkingDirectoryTempAlone() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );

	// A truncated temp, the kind recovery discards: someone else's save.
	const QString temp = dir.filePath( QStringLiteral( ".new" ) );
	QVERIFY( writeText( temp, QStringLiteral(
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<zene-project version=\"40\" type=\"song\"><head><s5" ) ) );

	WorkingDirectory cwd( dir.path() );
	QVERIFY2( cwd.moved(), "could not enter the temporary working directory" );

	QString report;
	{
		MessageRecorder recorder;
		DataFile unnamed{ QString() };
		report = recorder.messages();
	}

	QVERIFY2( QFile::exists( temp ),
		"an unnamed DataFile discarded the working directory's .new temp" );
	QVERIFY2( !report.contains( QStringLiteral( "Discarded incomplete" ) ),
		qPrintable( QStringLiteral( "an unnamed DataFile reported a discard; captured: [%1]" )
			.arg( report ) ) );
}
void DataFileSaveIntegrityTest::failedBackupCopyKeepsThePreviousBackup() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "saved.mmp" ) );
	const QString bak = target + QStringLiteral( ".bak" );
	const QString bakTemp = bak + QStringLiteral( ".new" );
	const QString temp = target + QStringLiteral( ".new" );
	QVERIFY( writeText( target, QStringLiteral( "current" ) ) );
	QVERIFY( writeText( bak, QStringLiteral( "older generation" ) ) );
	// Occupy the staging name so the new backup cannot be produced there.
	QVERIFY( occupy( bakTemp ) );

	DataFile dataFile( DataFile::Type::SongProject );

	QTest::ignoreMessage( QtWarningMsg,
		QRegularExpression( QStringLiteral( "will not be kept as a backup" ) ) );
	QTest::ignoreMessage( QtWarningMsg,
		QRegularExpression( QStringLiteral( "Nothing has been discarded" ) ) );

	const bool saved = dataFile.writeFile( target );

	// Checked before the result, so a failure names the destroyed backup
	// rather than only reporting that writeFile() returned the wrong value.
	QCOMPARE( readText( bak ), QStringLiteral( "older generation" ) );
	QVERIFY2( !saved, "a save whose backup could not be staged was reported as a success" );
	QCOMPARE( readText( target ), QStringLiteral( "current" ) );
	QVERIFY2( QFile::exists( temp ),
		"the refused save did not keep the written project for recovery" );
}
void DataFileSaveIntegrityTest::successfulBackupReplacesTheOlderBackupAtomically() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "saved.mmp" ) );
	const QString bak = target + QStringLiteral( ".bak" );
	const QString bakTemp = bak + QStringLiteral( ".new" );
	QVERIFY( writeText( target, QStringLiteral( "current" ) ) );
	QVERIFY( writeText( bak, QStringLiteral( "older generation" ) ) );

	DataFile dataFile( DataFile::Type::SongProject );
	QVERIFY2( dataFile.writeFile( target ),
		"an ordinary save with a replaceable backup failed" );

	QCOMPARE( readText( bak ), QStringLiteral( "current" ) );
	QVERIFY2( !QFileInfo::exists( bakTemp ),
		"the staged backup was left behind after a successful save" );
}

namespace
{

//! A complete recognised song document whose song element carries \a probe.
QString pendingSong( const QString& probe )
{
	return QStringLiteral( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<zene-project version=\"40\" type=\"song\"><head/>"
		"<song s5probe=\"%1\"><trackcontainer type=\"song\"/></song>"
		"</zene-project>\n" ).arg( probe );
}

//! Sets \a path's modification time to \a secondsSinceEpoch, so the recovery's
//! conflict test runs on an injected order instead of on wall-clock timing.
bool setModifiedTime( const QString& path, qint64 secondsSinceEpoch )
{
	QFile file( path );
	if( !file.open( QIODevice::ReadWrite ) )
	{
		return false;
	}
	return file.setFileTime( QDateTime::fromSecsSinceEpoch( secondsSinceEpoch ),
		QFileDevice::FileModificationTime );
}

} // namespace

void DataFileSaveIntegrityTest::olderPendingSaveDoesNotReplaceANewerProject() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString temp = target + QStringLiteral( ".new" );

	DataFile first( DataFile::Type::SongProject );
	first.content().setAttribute( "s5probe", "newer" );
	QVERIFY( first.writeFile( target ) );
	const QString newerText = readText( target );

	// A complete temp a save left behind. The canonical file was then updated
	// separately, which the injected times state: the canonical is NEWER.
	QVERIFY( writeText( temp, pendingSong( QStringLiteral( "stale" ) ) ) );
	QVERIFY( setModifiedTime( temp, 1000000 ) );
	QVERIFY( setModifiedTime( target, 2000000 ) );

	QString report;
	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		report = recorder.messages();
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
	}

	QVERIFY2( report.contains( QStringLiteral( "older than the project" ) ),
		qPrintable( QStringLiteral( "no conflict report for a stale .new; captured: [%1]" ).arg( report ) ) );
	QVERIFY2( QFile::exists( temp ), "the stale .new was consumed over the newer project" );
	QCOMPARE( probe, QStringLiteral( "newer" ) );
	QCOMPARE( readText( target ), newerText );
}
void DataFileSaveIntegrityTest::pendingSaveOfAnotherDocumentTypeIsNotAdopted() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString temp = target + QStringLiteral( ".new" );

	DataFile first( DataFile::Type::SongProject );
	first.content().setAttribute( "s5probe", "project" );
	QVERIFY( first.writeFile( target ) );
	const QString projectText = readText( target );

	// Complete and recognised, but a TEMPLATE - not this project's save.
	QVERIFY( writeText( temp, QStringLiteral(
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<zene-project version=\"40\" type=\"songtemplate\"><head/>"
		"<songtemplate s5probe=\"template\"/></zene-project>\n" ) ) );
	// Newer than the project, so only the type check can refuse it.
	QVERIFY( setModifiedTime( target, 1000000 ) );
	QVERIFY( setModifiedTime( temp, 2000000 ) );

	QString report;
	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		report = recorder.messages();
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
	}

	QVERIFY2( report.contains( QStringLiteral( "is not this project's save" ) ),
		qPrintable( QStringLiteral( "no type-mismatch report; captured: [%1]" ).arg( report ) ) );
	QVERIFY2( QFile::exists( temp ), "the other-type .new was consumed over the project" );
	QCOMPARE( probe, QStringLiteral( "project" ) );
	QCOMPARE( readText( target ), projectText );
}
void DataFileSaveIntegrityTest::adoptionKeepsThePreviousProjectAsABackup() {
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

	// The interrupted save's complete temp, as a real crash leaves it.
	DataFile second( DataFile::Type::SongProject );
	second.content().setAttribute( "s5probe", "new" );
	QVERIFY( !saveWithFault( second, target, QStringLiteral( "after-write" ) ) );
	QVERIFY( QFile::exists( temp ) );
	QVERIFY( setModifiedTime( target, 1000000 ) );
	QVERIFY( setModifiedTime( temp, 2000000 ) );

	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
		QVERIFY2( recorder.messages().contains( QStringLiteral( "Recovered interrupted save" ) ),
			qPrintable( QStringLiteral( "adoption printed no report; captured: [%1]" )
				.arg( recorder.messages() ) ) );
	}

	QCOMPARE( probe, QStringLiteral( "new" ) );
	QVERIFY2( !QFile::exists( temp ), "the adopted .new was not consumed" );
	// A completed save would have left the previous version here before the
	// rename; recovery must not be the one path that destroys it.
	QVERIFY2( QFile::exists( bak ), "adoption destroyed the previous project with no backup" );
	QCOMPARE( readText( bak ), oldText );
}
void DataFileSaveIntegrityTest::adoptionIsRefusedWhenTheBackupCannotBeMade() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString temp = target + QStringLiteral( ".new" );

	DataFile first( DataFile::Type::SongProject );
	first.content().setAttribute( "s5probe", "old" );
	QVERIFY( first.writeFile( target ) );
	const QString oldText = readText( target );

	QVERIFY( writeText( temp, pendingSong( QStringLiteral( "pending" ) ) ) );
	QVERIFY( setModifiedTime( target, 1000000 ) );
	QVERIFY( setModifiedTime( temp, 2000000 ) );
	// Occupy the backup staging name, as failedBackupCopyKeepsThePreviousBackup
	// does for a save, so no backup can be produced.
	QVERIFY( occupy( target + QStringLiteral( ".bak.new" ) ) );

	QString report;
	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		report = recorder.messages();
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
	}

	QVERIFY2( report.contains( QStringLiteral( "could not back up" ) ),
		qPrintable( QStringLiteral( "no backup-failure report; captured: [%1]" ).arg( report ) ) );
	QVERIFY2( QFile::exists( temp ), "the .new was consumed although its predecessor was not preserved" );
	QCOMPARE( probe, QStringLiteral( "old" ) );
	QCOMPARE( readText( target ), oldText );
}
void DataFileSaveIntegrityTest::adoptionHonoursTheDisableBackupConfiguration() {
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
	QVERIFY( !QFileInfo::exists( bak ) );

	QVERIFY( writeText( temp, pendingSong( QStringLiteral( "new" ) ) ) );
	QVERIFY( setModifiedTime( target, 1000000 ) );
	QVERIFY( setModifiedTime( temp, 2000000 ) );

	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "1" ) );
	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
	}
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	QCOMPARE( probe, QStringLiteral( "new" ) );
	QVERIFY2( !QFile::exists( temp ), "the adopted .new was not consumed" );
	QVERIFY2( !QFileInfo::exists( bak ),
		"recovery wrote a backup although app/disablebackup is set" );
}
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

	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
		const QString report = recorder.messages();
		QVERIFY2( report.contains( QStringLiteral( "is in progress in another instance" ) ),
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

		DataFile dataFile( DataFile::Type::SongProject );
		QTest::ignoreMessage( QtWarningMsg,
			QRegularExpression( QStringLiteral(
				"is being saved by another instance of Zene Studio" ) ) );

		QVERIFY2( !dataFile.writeFile( target ),
			"a save that another instance is running was reported as a success" );

		// A refused save discards nothing and publishes nothing: no second temp
		// written over the writer's, the project and the older backup intact.
		QCOMPARE( readText( target ), previous );
		QCOMPARE( readText( target + QStringLiteral( ".bak" ) ), olderBackup );
		QVERIFY2( !QFileInfo::exists( target + QStringLiteral( ".new" ) ),
			"the refused save left a second temp beside the writer's" );
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
