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

void DataFileSaveIntegrityTest::blockedRenameIsRefusedAndReported() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "saved.mmp" ) );
	const QString bak = target + QStringLiteral( ".bak" );
	const QString temp = target + QStringLiteral( ".new" );
	QVERIFY( occupy( target ) );
	QVERIFY( occupy( bak ) );

	DataFile dataFile( DataFile::Type::SongProject );

	// The failure has to be visible where the user is: a warning on the
	// log/stderr path when there is no GUI (showError's core-only branch).
	// The backup step fails here too, and is reported first.
	QTest::ignoreMessage( QtWarningMsg,
		QRegularExpression( QStringLiteral( "will not be kept as a backup" ) ) );
	QTest::ignoreMessage( QtWarningMsg,
		QRegularExpression( QStringLiteral( "Nothing has been discarded" ) ) );

	QVERIFY2( !dataFile.writeFile( target ),
		"a save whose every rename the filesystem refused was reported as a success" );

	// State consistency: the destination is exactly what it was before the
	// save (nothing was half-replaced), and the written project is still on
	// disk at the path the message names, because nothing was discarded.
	QVERIFY( QFileInfo( target ).isDir() );
	QVERIFY2( readText( target + QStringLiteral( "/occupied" ) )
			== QStringLiteral( "keep out" ),
		"the refused save modified the destination" );
	QVERIFY2( QFile::exists( temp ),
		"the refused save did not keep the written project for recovery" );
	// The root element is `zene-project` since the rename's layer 3 made the
	// format read-both/write-new (post-alpha/rename-complete, merged in train 3B).
	// This assertion is not vacuous: it failed against `lmms-project` on exactly
	// the tree where it was changed.
	QVERIFY( readText( temp ).contains( QStringLiteral( "<zene-project" ) ) );
}
void DataFileSaveIntegrityTest::uncheckedRenameDanceReportedSuccess() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );

	const QString target = dir.filePath( QStringLiteral( "saved.mmp" ) );
	const QString bak = target + QStringLiteral( ".bak" );
	const QString temp = target + QStringLiteral( ".new" );
	QVERIFY( occupy( target ) );
	QVERIFY( occupy( bak ) );
	QVERIFY( writeText( temp, QStringLiteral( "<lmms-project/>" ) ) );

	// git show post-alpha/integration:src/core/DataFile.cpp, the three
	// lines that ended writeFile() upstream.
	const auto uncheckedRenameDance = []( const QString& current,
		const QString& temporary, const QString& backup )
	{
		QFile::remove( backup );
		QFile::rename( current, backup );
		QFile::rename( temporary, current );
		return true; // the defect: unconditional, whatever happened above
	};

	QVERIFY2( uncheckedRenameDance( target, temp, bak ),
		"control: the pre-fix sequence did not report success - this fixture no longer reproduces D3" );
	QVERIFY2( QFileInfo( target ).isDir(),
		"control: the destination was replaced, so the fixture no longer reproduces D3" );
	QVERIFY2( QFile::exists( temp ),
		"control: the project never reached the destination, which is the silent loss D3 is about" );
}
void DataFileSaveIntegrityTest::backupFailureAlsoRefusesTheSave() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "saved.mmp" ) );
	QVERIFY( writeText( target, QStringLiteral( "the previous version" ) ) );
	// a non-empty directory: the move-aside cannot succeed
	QVERIFY( occupy( target + QStringLiteral( ".bak" ) ) );

	DataFile dataFile( DataFile::Type::SongProject );
	QTest::ignoreMessage( QtWarningMsg,
		QRegularExpression( QStringLiteral( "will not be kept as a backup" ) ) );
	QTest::ignoreMessage( QtWarningMsg,
		QRegularExpression( QStringLiteral( "Nothing has been discarded" ) ) );

	QVERIFY2( !dataFile.writeFile( target ),
		"the save reported success although the project never reached the file" );
	QCOMPARE( readText( target ), QStringLiteral( "the previous version" ) );
	QVERIFY2( QFile::exists( target + QStringLiteral( ".new" ) ),
		"the refused save did not keep the written project for recovery" );
}
void DataFileSaveIntegrityTest::disableBackupRemovalFailureIsRefused() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "1" ) );

	const QString target = dir.filePath( QStringLiteral( "saved.mmp" ) );
	QVERIFY( occupy( target ) );

	DataFile dataFile( DataFile::Type::SongProject );
	QTest::ignoreMessage( QtWarningMsg,
		QRegularExpression( QStringLiteral( "Nothing has been discarded" ) ) );

	QVERIFY2( !dataFile.writeFile( target ),
		"a save that could not clear the destination reported success" );
	QVERIFY( QFileInfo( target ).isDir() );
	QVERIFY( readText( target + QStringLiteral( "/occupied" ) )
			== QStringLiteral( "keep out" ) );

	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );
}
void DataFileSaveIntegrityTest::ordinarySaveStillWritesAndKeepsABackup() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "saved.mmp" ) );

	DataFile first( DataFile::Type::SongProject );
	QVERIFY2( first.writeFile( target ), "an ordinary save was refused" );
	const QString firstText = readText( target );
	QVERIFY( firstText.contains( QStringLiteral( "<zene-project" ) ) );
	// nothing existed to back up yet
	QVERIFY( !QFile::exists( target + QStringLiteral( ".bak" ) ) );

	DataFile second( DataFile::Type::SongProject );
	QVERIFY( second.writeFile( target ) );
	QCOMPARE( readText( target + QStringLiteral( ".bak" ) ), firstText );
	QVERIFY( readText( target ).contains( QStringLiteral( "<zene-project" ) ) );
}
void DataFileSaveIntegrityTest::unremovableBackupDoesNotBlockANewProject() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "fresh.mmp" ) );
	QVERIFY( occupy( target + QStringLiteral( ".bak" ) ) );

	DataFile dataFile( DataFile::Type::SongProject );
	QVERIFY2( dataFile.writeFile( target ),
		"a stale .bak that could not be removed blocked saving a new project" );
	QVERIFY( readText( target ).contains( QStringLiteral( "<zene-project" ) ) );
}
void DataFileSaveIntegrityTest::moveBasedBackupOpensACanonicalGap() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString bak = target + QStringLiteral( ".bak" );
	const QString temp = target + QStringLiteral( ".new" );

	const QString oldDoc = QStringLiteral(
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<zene-project version=\"40\" type=\"song\"></zene-project>\n" );
	const QString newDoc = QStringLiteral(
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<zene-project version=\"40\" type=\"song\"><head s5=\"new\"/></zene-project>\n" );
	QVERIFY( writeText( target, oldDoc ) );
	QVERIFY( writeText( temp, newDoc ) );

	// the old dance, step by step:
	QVERIFY( QFile::rename( target, bak ) );	// MOVE current -> .bak: gap opens
	const bool canonicalAbsentMidFlight = !QFile::exists( target );
	QVERIFY( QFile::rename( temp, target ) );	// then .new -> canonical: gap closes

	QVERIFY2( canonicalAbsentMidFlight,
		"control: the move-based sequence did not leave the canonical path empty, "
		"so it no longer reproduces the gap S5 closes" );
	QVERIFY( QFile::exists( target ) );
}
void DataFileSaveIntegrityTest::saveStoppedMidFlightKeepsCanonicalComplete() {
	// A leftover from any earlier run must not stop THIS test's saves.
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString temp = target + QStringLiteral( ".new" );
	const QString bak = target + QStringLiteral( ".bak" );

	for( const QString& point : { QStringLiteral( "after-write" ),
			QStringLiteral( "after-backup" ), QStringLiteral( "after-rename" ) } )
	{
		// Reset: a known-complete "old" document at the canonical path.
		DataFile first( DataFile::Type::SongProject );
		first.content().setAttribute( "s5probe", "old" );
		QVERIFY2( first.writeFile( target ),
			qPrintable( QStringLiteral( "could not seed the canonical file at %1" )
				.arg( point ) ) );
		const QString oldText = readText( target );
		QVERIFY( oldText.contains( QStringLiteral( "</zene-project>" ) ) );

		// Attempt a DIFFERENT save with the fault armed; saveWithFault()
		// disarms before any assertion below can return early.
		DataFile second( DataFile::Type::SongProject );
		second.content().setAttribute( "s5probe", "new" );
		const bool saved = saveWithFault( second, target, point );

		QVERIFY2( !saved, qPrintable( QStringLiteral(
			"the save was not interrupted at %1" ).arg( point ) ) );

		// THE invariant: at every instant the canonical path holds a
		// complete file - one of the two known-good documents, never absent
		// and never truncated.
		QVERIFY2( QFile::exists( target ), qPrintable( QStringLiteral(
			"the canonical path vanished when the save stopped at %1" ).arg( point ) ) );
		const QString now = readText( target );
		QVERIFY2( now.contains( QStringLiteral( "<zene-project" ) )
				&& now.contains( QStringLiteral( "</zene-project>" ) ),
			qPrintable( QStringLiteral( "the canonical file was incomplete at %1" )
				.arg( point ) ) );

		if( point == QLatin1String( "after-rename" ) )
		{
			// The atomic step landed: canonical is the NEW complete file.
			QVERIFY2( now.contains( QStringLiteral( "s5probe=\"new\"" ) ),
				"after the rename the canonical path did not hold the new save" );
		}
		else
		{
			// Before the rename: canonical is still the untouched OLD file
			// (under the pre-fix move it would be ABSENT here - see the
			// control above), and the new content waits in .new.
			QCOMPARE( now, oldText );
			QVERIFY2( QFile::exists( temp ),
				qPrintable( QStringLiteral( "the staged .new went missing at %1" )
					.arg( point ) ) );
			QVERIFY( readText( temp ).contains( QStringLiteral( "s5probe=\"new\"" ) ) );
			if( point == QLatin1String( "after-backup" ) )
			{
				QVERIFY( QFile::exists( bak ) );
				QCOMPARE( readText( bak ), oldText );
			}
		}
	}
}
void DataFileSaveIntegrityTest::orphanedNewFileIsAdoptedWithReport() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	ConfigManager::inst()->setValue( QStringLiteral( "app" ),
		QStringLiteral( "disablebackup" ), QStringLiteral( "0" ) );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString temp = target + QStringLiteral( ".new" );

	// canonical holds a complete "old" save
	DataFile first( DataFile::Type::SongProject );
	first.content().setAttribute( "s5probe", "old" );
	QVERIFY( first.writeFile( target ) );
	const QString oldText = readText( target );

	// Leave a COMPLETE .new the way a real interruption does: fault the
	// save right after the temp is written, so the orphan is byte-for-byte
	// what an interrupted save leaves behind.
	DataFile second( DataFile::Type::SongProject );
	second.content().setAttribute( "s5probe", "new" );
	QVERIFY( !saveWithFault( second, target, QStringLiteral( "after-write" ) ) );
	QVERIFY( QFile::exists( temp ) );
	QVERIFY( readText( temp ).contains( QStringLiteral( "s5probe=\"new\"" ) ) );
	QCOMPARE( readText( target ), oldText );	// canonical untouched by the crash

	// Opening the file adopts the orphan. Capture the report inside the
	// scope so the handler is restored before any QVERIFY can run.
	QString report;
	QString adoptedProbe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		report = recorder.messages();
		adoptedProbe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
	}

	QVERIFY2( report.contains( QStringLiteral( "Recovered interrupted save" ) ),
		qPrintable( QStringLiteral( "adoption printed no report line; captured: [%1]" )
			.arg( report ) ) );
	// Round-trip: the interrupted save's content is now the file's content...
	QCOMPARE( adoptedProbe, QStringLiteral( "new" ) );
	QVERIFY( readText( target ).contains( QStringLiteral( "s5probe=\"new\"" ) ) );
	// ...and the orphan that used to sit there forever is consumed.
	QVERIFY2( !QFile::exists( temp ), "the adopted .new was not consumed" );
}
void DataFileSaveIntegrityTest::saveFaultIsDisarmedWhenTheSaveDoesNotStop() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	DataFile dataFile( DataFile::Type::SongProject );
	dataFile.content().setAttribute( "s5probe", "unstopped" );

	const bool saved = saveWithFault( dataFile, target, QStringLiteral( "no-such-point" ) );
	QVERIFY2( qgetenv( "ZENE_TEST_SAVE_FAULT" ).isEmpty(),
		"the save-fault seam was left armed after the save" );
	QVERIFY2( saved, "a seam name writeFile() does not check stopped the save" );
	QVERIFY( readText( target ).contains( QStringLiteral( "s5probe=\"unstopped\"" ) ) );
}
void DataFileSaveIntegrityTest::truncatedNewFileIsDiscardedWithReport() {
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

	// A .new cut off mid-tag: well-formed enough to exist, not complete.
	QVERIFY( writeText( temp, QStringLiteral(
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<zene-project version=\"40\" type=\"song\"><head><s5" ) ) );

	QString report;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		report = recorder.messages();
	}

	QVERIFY2( report.contains( QStringLiteral( "Discarded incomplete" ) ),
		qPrintable( QStringLiteral( "discard printed no report line; captured: [%1]" )
			.arg( report ) ) );
	QVERIFY2( !QFile::exists( temp ), "the truncated .new was not discarded" );
	// The canonical file is untouched by the discard.
	QCOMPARE( readText( target ), oldText );
}
void DataFileSaveIntegrityTest::unrelatedXmlNewFileIsNotAdopted() {
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );

	const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
	const QString temp = target + QStringLiteral( ".new" );

	DataFile first( DataFile::Type::SongProject );
	first.content().setAttribute( "s5probe", "old" );
	QVERIFY( first.writeFile( target ) );
	const QString oldText = readText( target );

	// Complete, well-formed XML - of something that is not a project.
	QVERIFY( writeText( temp, QStringLiteral( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<not-a-project probe=\"unrelated\"/>\n" ) ) );

	QString report;
	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		report = recorder.messages();
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
	}

	QVERIFY2( report.contains( QStringLiteral( "Discarded incomplete" ) ),
		qPrintable( QStringLiteral( "an unrelated .new was adopted; captured: [%1]" ).arg( report ) ) );
	QVERIFY2( !QFile::exists( temp ), "the unrelated .new was not discarded" );
	// The valid project is still the project.
	QCOMPARE( probe, QStringLiteral( "old" ) );
	QCOMPARE( readText( target ), oldText );
}
void DataFileSaveIntegrityTest::newFileWithContentNestedUnderAnotherElementIsNotAdopted() {
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
		"<wrapper><song s5probe=\"nested\"/></wrapper></zene-project>\n" ) ) );

	QString report;
	QString probe;
	{
		MessageRecorder recorder;
		DataFile loaded( target );
		report = recorder.messages();
		probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
	}

	QVERIFY2( report.contains( QStringLiteral( "Discarded incomplete" ) ),
		qPrintable( QStringLiteral( "a misnested .new was adopted; captured: [%1]" ).arg( report ) ) );
	QVERIFY2( !QFile::exists( temp ), "the misnested .new was not discarded" );
	QCOMPARE( probe, QStringLiteral( "old" ) );
	QCOMPARE( readText( target ), oldText );
}
