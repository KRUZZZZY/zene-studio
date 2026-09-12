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

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "DataFile.h"

using namespace lmms;

namespace
{

bool writeText( const QString& path, const QString& text )
{
	QFile file( path );
	if( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
	{
		return false;
	}
	return file.write( text.toUtf8() ) >= 0;
}


QString readText( const QString& path )
{
	QFile file( path );
	if( !file.open( QIODevice::ReadOnly ) )
	{
		return QString();
	}
	return QString::fromUtf8( file.readAll() );
}


//! Occupy a path with a non-empty directory, so renaming anything onto it is
//! refused by the OS. This stands in for the destination a real user has that
//! cannot be replaced - a file held open by another process, a foreign-owned
//! or read-only file, a protected path - because the refusal writeFile() has
//! to survive is the same whatever the cause, and unlike a permission bit it
//! behaves identically for every user the test runs as.
bool occupy( const QString& path )
{
	if( !QDir().mkdir( path ) )
	{
		return false;
	}
	return writeText( path + QStringLiteral( "/occupied" ), QStringLiteral( "keep out" ) );
}

} // namespace


class DataFileSaveIntegrityTest : public QObject
{
	Q_OBJECT
private slots:
	//! The deliverable: a save the filesystem refuses to complete must say so
	//! and must not leave the document claiming a clean save.
	void blockedRenameIsRefusedAndReported()
	{
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

	//! Inverted control. The pre-fix tail of DataFile::writeFile discarded
	//! every rename result and returned true unconditionally; this is that
	//! sequence, transcribed, run on the same fixture. It must report success
	//! on a save where nothing was written - the silent success D3 describes -
	//! so if a platform ever allows these renames the control fails instead of
	//! letting the test above pass vacuously.
	void uncheckedRenameDanceReportedSuccess()
	{
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

	//! Qt's QFile::rename never overwrites an existing file (measured: renaming
	//! a file onto an existing file returns false and leaves both alone). So a
	//! save whose move-aside failed cannot land either - the final rename finds
	//! the destination still occupied and is refused. Upstream returned true
	//! anyway: the same silent loss through a second door. Both failures are
	//! reported here, the save is refused, and the previous project is kept.
	void backupFailureAlsoRefusesTheSave()
	{
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

	//! The same refusal in the disablebackup configuration, which takes the
	//! other branch of the checked sequence.
	void disableBackupRemovalFailureIsRefused()
	{
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

	//! Behaviour preservation: an ordinary save still writes the project and
	//! still moves the previous version aside to <name>.bak.
	void ordinarySaveStillWritesAndKeepsABackup()
	{
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

	//! A stale <name>.bak that cannot be removed must not block saving a
	//! project to a path that has none yet: only the destination decides.
	void unremovableBackupDoesNotBlockANewProject()
	{
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
};

QTEST_GUILESS_MAIN( DataFileSaveIntegrityTest )
#include "DataFileSaveIntegrityTest.moc"
