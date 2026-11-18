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
