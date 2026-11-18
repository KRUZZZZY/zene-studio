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
#include "XmlDepthGuard.h"

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

bool writeBytes( const QString& path, const QByteArray& bytes )
{
	QFile file( path );
	if( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
	{
		return false;
	}
	return file.write( bytes ) == bytes.size();
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

//! Arms the save-fault seam for exactly one save and disarms it before
//! returning, whatever the save did. Callers assert on the result only after
//! this returns: a failed QVERIFY returns from the test function, so a disarm
//! written after an assertion would be skipped and the seam would stay armed.
bool saveWithFault( DataFile& dataFile, const QString& target, const QString& point )
{
	qputenv( "ZENE_TEST_SAVE_FAULT", point.toUtf8() );
	const bool saved = dataFile.writeFile( target );
	qunsetenv( "ZENE_TEST_SAVE_FAULT" );
	return saved;
}


/*! \brief Captures the Qt log lines emitted while it is alive, so a test can
 *  ASSERT that a report line was actually printed rather than merely tolerate
 *  it - QTest::ignoreMessage suppresses, it does not prove presence.
 *
 *  The handler deliberately does NOT chain to the previous one: while this is
 *  installed the messages are held here and never reach QTest, so the report a
 *  recovery emits cannot itself fail the run (and this stays correct whether
 *  or not a stray warning would). The destructor restores the previous handler
 *  FIRST, so no QVERIFY below it can be swallowed.
 */
class MessageRecorder
{
public:
	MessageRecorder()
	{
		s_messages.clear();
		m_previous = qInstallMessageHandler( &MessageRecorder::handle );
	}
	~MessageRecorder() { qInstallMessageHandler( m_previous ); }

	MessageRecorder( const MessageRecorder& ) = delete;
	MessageRecorder& operator=( const MessageRecorder& ) = delete;

	QString messages() const { return s_messages.join( QLatin1Char( '\n' ) ); }

private:
	static void handle( QtMsgType, const QMessageLogContext&, const QString& message )
	{
		s_messages.append( message );
	}

	static inline QStringList s_messages;
	QtMessageHandler m_previous{ nullptr };
};

//! Enters \a path as the working directory for one test and puts the previous
//! one back when the test returns - including through a failed QVERIFY, which
//! returns early and would otherwise leave the rest of the run in the temp dir.
class WorkingDirectory
{
public:
	explicit WorkingDirectory( const QString& path )
		: m_previous( QDir::currentPath() ), m_moved( QDir::setCurrent( path ) )
	{
	}
	~WorkingDirectory() { QDir::setCurrent( m_previous ); }

	WorkingDirectory( const WorkingDirectory& ) = delete;
	WorkingDirectory& operator=( const WorkingDirectory& ) = delete;

	bool moved() const { return m_moved; }

private:
	QString m_previous;
	bool m_moved;
};

} // namespace


class DataFileSaveIntegrityTest : public QObject
{
	Q_OBJECT
private slots:
	//! The deliverable: a save the filesystem refuses to complete must say so
	//! and must not leave the document claiming a clean save.
	void blockedRenameIsRefusedAndReported();

	//! Inverted control. The pre-fix tail of DataFile::writeFile discarded
	//! every rename result and returned true unconditionally; this is that
	//! sequence, transcribed, run on the same fixture. It must report success
	//! on a save where nothing was written - the silent success D3 describes -
	//! so if a platform ever allows these renames the control fails instead of
	//! letting the test above pass vacuously.
	void uncheckedRenameDanceReportedSuccess();

	//! Qt's QFile::rename never overwrites an existing file (measured: renaming
	//! a file onto an existing file returns false and leaves both alone). So a
	//! save whose move-aside failed cannot land either - the final rename finds
	//! the destination still occupied and is refused. Upstream returned true
	//! anyway: the same silent loss through a second door. Both failures are
	//! reported here, the save is refused, and the previous project is kept.
	void backupFailureAlsoRefusesTheSave();

	//! The same refusal in the disablebackup configuration, which takes the
	//! other branch of the checked sequence.
	void disableBackupRemovalFailureIsRefused();

	//! Behaviour preservation: an ordinary save still writes the project and
	//! still moves the previous version aside to <name>.bak.
	void ordinarySaveStillWritesAndKeepsABackup();

	//! A stale <name>.bak that cannot be removed must not block saving a
	//! project to a path that has none yet: only the destination decides.
	void unremovableBackupDoesNotBlockANewProject();

	//! ARCH-4 S5, SPEC-ARCH-4 1.8 part 1. The gap the fix closes, transcribed:
	//! the pre-fix sequence MOVED the canonical file to <name>.bak and then
	//! renamed <name>.new into place, so between those two calls the canonical
	//! path held NO file at all. This inverted control proves the fixture would
	//! catch that absence - which is what makes the fault-injection test below
	//! (canonical present at the same point) non-vacuous. If this control ever
	//! finds the path populated mid-flight, QFile has changed and the pair no
	//! longer measures the gap.
	void moveBasedBackupOpensACanonicalGap();

	//! ARCH-4 S5, SPEC-ARCH-4 1.8 part 1. Kill a save at each point of its
	//! finalisation and prove the canonical path ALWAYS holds a complete file.
	//! Before the atomic rename it is still the untouched old document; after
	//! it, it is the new one - never absent, never half-written. The seam is a
	//! test-only env var writeFile() checks at each named step, stopping there
	//! as a crash would (false, no cleanup) so the on-disk state mid-flight is
	//! observable; unset in production it costs one qgetenv per save.
	void saveStoppedMidFlightKeepsCanonicalComplete();

	//! ARCH-4 S5, SPEC-ARCH-4 1.8 part 2. A crash after writing <name>.new but
	//! before the final rename used to orphan that temp forever. The next open
	//! must ADOPT a complete .new - it is the newest intended content - over
	//! the canonical path, and say so with a report line. The proof is the
	//! round-trip: the interrupted save's content becomes the file's content,
	//! the orphan is consumed, and the report was actually printed (captured,
	//! not merely tolerated).
	void orphanedNewFileIsAdoptedWithReport();

	//! The seam is disarmed on every exit of saveWithFault(), including a save
	//! that does not stop where the seam asks. A name writeFile() does not check
	//! lets the save complete, so the result is success and the seam must still
	//! be clear when the assertions below run - a failed assertion returns from
	//! the test and would otherwise leave the seam armed for later tests.
	void saveFaultIsDisarmedWhenTheSaveDoesNotStop();

	//! ARCH-4 S5, SPEC-ARCH-4 1.8 part 2. The other half: a .new truncated
	//! mid-write must be DISCARDED with its own report line, not adopted - and
	//! adopting garbage would trade one corrupt file for another. The canonical
	//! file is left exactly as it was.
	void truncatedNewFileIsDiscardedWithReport();

	//! A .new that PARSES is not yet a document: <not-a-project/> parses and has
	//! a root element, but it is not a Zene or legacy LMMS document at all. The
	//! recovery renames the temp over the project, so adopting it would replace
	//! a perfectly good project with unrelated XML. Only a recognized root, a
	//! recognized type and the content element that type names may be adopted.
	void unrelatedXmlNewFileIsNotAdopted();

	//! A .new whose root, type and head are all recognised, but whose content
	//! element sits under an unrelated wrapper rather than directly under the
	//! root, is not a document this program writes. Recovery used to search
	//! every descendant for the content element, so it adopted this temp over
	//! the valid project and the load then selected the nested song. The content
	//! element must be the root's own child, so this temp is discarded and the
	//! project stands.
	void newFileWithContentNestedUnderAnotherElementIsNotAdopted();

	//! A direct content child is not enough: the loader selects the FIRST element
	//! of the content name in document order, so a temp with a nested song that
	//! precedes its direct song would be adopted, and the load would then take
	//! the nested one. Recovery must accept only a temp whose selected content
	//! element is the root's own child, so this temp is discarded as well.
	void newFileWithNestedContentBeforeDirectContentIsNotAdopted();

	//! The recovery parses the temp before the guarded project load does, so a
	//! deeply nested - but well-formed and complete - .new used to be adopted
	//! with no depth guard at all. Both parses now carry the same limit, so such
	//! a temp is discarded with the ordinary report and the project stands.
	void deeplyNestedNewFileIsRefusedBeforeParsing();

	//! A compressed temp carries its uncompressed size in a 4-byte header, and
	//! qUncompress allocates EXACTLY what that header claims before it looks at
	//! a single compressed byte. A few-byte .new claiming a gigabyte was
	//! therefore allocated on open. The claim is now checked against the
	//! recovery limit first, and the refusal says so - the temp is left in place
	//! rather than destroyed, since it was never validated.
	void newFileDeclaringAnEnormousSizeIsRefusedBeforeDecompressing();

	//! A complete pending save whose read permission was removed - the directory
	//! stays writable - is an I/O failure, not a truncated temp. The open was
	//! refused, and the recovery used to read that refusal as an incomplete
	//! document and delete the only staged copy. It must now be left in place,
	//! reported, and adopted once it can be read again.
	void unreadableNewFileIsPreservedNotDeleted();

	//! Cluster new-p23-77 (BUG 2). A DataFile built from an EMPTY name has no file
	//! to recover, but recoverPendingWrite("") derived "./.new" from it and
	//! discarded whatever interrupted-save temp sat in the working directory - a
	//! file that belongs to some other project. An unnamed load must leave that
	//! temp exactly where it is and must not report a recovery it never made.
	void unnamedDataFileLeavesTheWorkingDirectoryTempAlone();
};

#include "DataFileSaveIntegrityTestParts1.h"
#include "DataFileSaveIntegrityTestParts2.h"

QTEST_GUILESS_MAIN( DataFileSaveIntegrityTest )
#include "DataFileSaveIntegrityTest.moc"
