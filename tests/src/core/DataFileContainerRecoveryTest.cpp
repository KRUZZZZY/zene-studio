/*
 * DataFileContainerRecoveryTest.cpp - a pending save whose READ fails (the plain
 * document's first read, or a v2 container's second read) is kept and reported,
 * and adopted once a read succeeds; malformed, truncated and oversized saves keep
 * their existing outcomes.
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

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "DataFile.h"
#include "ProjectContainer.h"

using namespace lmms;

namespace
{

QString readText( const QString& path )
{
	QFile file( path );
	if( !file.open( QIODevice::ReadOnly ) )
	{
		return QString();
	}
	return QString::fromUtf8( file.readAll() );
}

QByteArray readBytes( const QString& path )
{
	QFile file( path );
	if( !file.open( QIODevice::ReadOnly ) )
	{
		return QByteArray();
	}
	return file.readAll();
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

//! A plain (legacy XML) document of a recognised project whose song element
//! carries \a probe - a complete pending save that is not a container.
QByteArray plainPending( const QString& probe )
{
	return QStringLiteral( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<zene-project version=\"40\" type=\"song\"><head/>"
		"<song s5probe=\"%1\"><trackcontainer type=\"song\"/></song></zene-project>\n" )
		.arg( probe ).toUtf8();
}

//! Writes a v2 container of a recognised project whose song element carries
//! \a probe. The container writer needs the document's <z:index>, and the index
//! names every child of the song element as a section, so the song element holds
//! a trackcontainer the way a real song does. The document is then written the
//! way a save writes it.
bool writePendingContainer( const QString& path, const QString& probe, QString* error )
{
	DataFile document( DataFile::Type::SongProject );
	document.content().setAttribute( QStringLiteral( "s5probe" ), probe );
	QDomElement tracks = document.createElement( QStringLiteral( "trackcontainer" ) );
	tracks.setAttribute( QStringLiteral( "type" ), QStringLiteral( "song" ) );
	document.content().appendChild( tracks );
	document.setDocumentIndexEnabled( true );
	QString xml;
	QTextStream stream( &xml );
	document.write( stream );
	stream.flush();
	return projectcontainer::writeDocumentContainer( path, xml.toUtf8(), QStringLiteral( "song" ), error );
}


/*! \brief Captures the Qt log lines emitted while it is alive, so a test can
 *  ASSERT that a report line was actually printed rather than merely tolerate
 *  it. The handler does not chain to the previous one, so the report a recovery
 *  emits cannot itself fail the run. The destructor restores the previous handler
 *  first, so no QVERIFY below it can be swallowed.
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

} // namespace


class DataFileContainerRecoveryTest : public QObject
{
	Q_OBJECT
private slots:
	//! A v2 container's temp is read twice: DataFile reads its bytes, then the
	//! container reader reads the file again to reassemble the document. Here the
	//! second read fails while the file still opens. The reader used to take the
	//! empty result for a malformed container, the recovery found the file
	//! openable and deleted the only staged copy. A read error must keep it.
	void unreadableContainerNewFileIsPreservedNotDeleted()
	{
		qunsetenv( "ZENE_TEST_SAVE_FAULT" );
		qunsetenv( "ZENE_TEST_CONTAINER_READ_FAULT" );
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
		const QString temp = target + QStringLiteral( ".new" );

		DataFile first( DataFile::Type::SongProject );
		first.content().setAttribute( "s5probe", "old" );
		QVERIFY( first.writeFile( target ) );
		const QString oldText = readText( target );

		// A complete v2 container of a recognised project: the save the user asked for.
		QString error;
		QVERIFY2( writePendingContainer( temp, QStringLiteral( "pending" ), &error ), qPrintable( error ) );
		QVERIFY( projectcontainer::isContainer( readBytes( temp ) ) );

		QString report;
		QString probe;
		{
			MessageRecorder recorder;
			qputenv( "ZENE_TEST_CONTAINER_READ_FAULT", "1" );
			DataFile loaded( target );
			qunsetenv( "ZENE_TEST_CONTAINER_READ_FAULT" );
			report = recorder.messages();
			probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
		}

		QVERIFY2( !report.contains( QStringLiteral( "Discarded incomplete" ) ),
			qPrintable( QStringLiteral( "a .new whose read failed was treated as incomplete; captured: [%1]" ).arg( report ) ) );
		QVERIFY2( report.contains( QStringLiteral( "Could not read" ) ),
			qPrintable( QStringLiteral( "no report line for the unreadable container; captured: [%1]" ).arg( report ) ) );
		QVERIFY2( QFile::exists( temp ), "the .new was deleted after its container read failed" );
		QCOMPARE( probe, QStringLiteral( "old" ) );  // not adopted while it cannot be checked
		QCOMPARE( readText( target ), oldText );

		// Readable again: the preserved container is adopted.
		DataFile later( target );
		QCOMPARE( later.content().attribute( QStringLiteral( "s5probe" ) ), QStringLiteral( "pending" ) );
		QVERIFY( !QFile::exists( temp ) );
	}

	//! A container cut short is malformed content, not an I/O failure: its reader
	//! refuses it on the directory it cannot find, so the temp is still discarded
	//! with the ordinary report. Keeping unreadable temps must not keep these.
	void truncatedContainerNewFileIsDiscardedWithReport()
	{
		qunsetenv( "ZENE_TEST_SAVE_FAULT" );
		qunsetenv( "ZENE_TEST_CONTAINER_READ_FAULT" );
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
		const QString temp = target + QStringLiteral( ".new" );

		DataFile first( DataFile::Type::SongProject );
		first.content().setAttribute( "s5probe", "old" );
		QVERIFY( first.writeFile( target ) );
		const QString oldText = readText( target );

		QString error;
		QVERIFY2( writePendingContainer( temp, QStringLiteral( "pending" ), &error ), qPrintable( error ) );
		const QByteArray whole = readBytes( temp );
		QVERIFY( projectcontainer::isContainer( whole ) );
		// Cut in half: the signature survives, the central directory does not.
		QVERIFY( writeBytes( temp, whole.left( whole.size() / 2 ) ) );

		QString report;
		QString probe;
		{
			MessageRecorder recorder;
			DataFile loaded( target );
			report = recorder.messages();
			probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
		}

		QVERIFY2( report.contains( QStringLiteral( "Discarded incomplete" ) ),
			qPrintable( QStringLiteral( "a truncated container was not discarded; captured: [%1]" ).arg( report ) ) );
		QVERIFY2( !QFile::exists( temp ), "the truncated container .new was not discarded" );
		QCOMPARE( probe, QStringLiteral( "old" ) );
		QCOMPARE( readText( target ), oldText );
	}

	//! The FIRST read of a pending save can fail too: DataFile opens the .new and
	//! its read gives back nothing. A file whose read failed was never checked, so
	//! the recovery must keep it, and adopt it once a read succeeds. Without the
	//! fix the empty read is taken for a truncated save and the .new is deleted.
	void unreadablePendingFileIsPreservedNotDeleted()
	{
		qunsetenv( "ZENE_TEST_SAVE_FAULT" );
		qunsetenv( "ZENE_TEST_PENDING_READ_FAULT" );
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
		const QString temp = target + QStringLiteral( ".new" );

		DataFile first( DataFile::Type::SongProject );
		first.content().setAttribute( "s5probe", "old" );
		QVERIFY( first.writeFile( target ) );
		const QString oldText = readText( target );
		QVERIFY( writeBytes( temp, plainPending( QStringLiteral( "pending" ) ) ) );

		QString report;
		QString probe;
		{
			MessageRecorder recorder;
			qputenv( "ZENE_TEST_PENDING_READ_FAULT", "1" );
			DataFile loaded( target );
			qunsetenv( "ZENE_TEST_PENDING_READ_FAULT" );
			report = recorder.messages();
			probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
		}

		QVERIFY2( !report.contains( QStringLiteral( "Discarded incomplete" ) ),
			qPrintable( QStringLiteral( "a .new whose first read failed was treated as incomplete; captured: [%1]" ).arg( report ) ) );
		QVERIFY2( report.contains( QStringLiteral( "Could not read" ) ),
			qPrintable( QStringLiteral( "no report line for the unreadable pending file; captured: [%1]" ).arg( report ) ) );
		QVERIFY2( QFile::exists( temp ), "the .new was deleted after its first read failed" );
		QCOMPARE( probe, QStringLiteral( "old" ) );  // the canonical project stands
		QCOMPARE( readText( target ), oldText );

		// Readable again: the preserved save is adopted.
		DataFile later( target );
		QCOMPARE( later.content().attribute( QStringLiteral( "s5probe" ) ), QStringLiteral( "pending" ) );
		QVERIFY( !QFile::exists( temp ) );
	}

	//! A container's second read can fail on every attempt. Each recovery must keep
	//! the .new and say so; the first readable attempt then adopts it. Nothing is
	//! lost between attempts, however many there are.
	void repeatedContainerReadFailuresKeepTheNewFileUntilAReadableAttempt()
	{
		qunsetenv( "ZENE_TEST_SAVE_FAULT" );
		qunsetenv( "ZENE_TEST_CONTAINER_READ_FAULT" );
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
		const QString temp = target + QStringLiteral( ".new" );

		DataFile first( DataFile::Type::SongProject );
		first.content().setAttribute( "s5probe", "old" );
		QVERIFY( first.writeFile( target ) );
		const QString oldText = readText( target );

		QString error;
		QVERIFY2( writePendingContainer( temp, QStringLiteral( "pending" ), &error ), qPrintable( error ) );

		QStringList reports;
		QStringList probes;
		for( int attempt = 0; attempt < 3; ++attempt )
		{
			MessageRecorder recorder;
			qputenv( "ZENE_TEST_CONTAINER_READ_FAULT", "1" );
			DataFile loaded( target );
			qunsetenv( "ZENE_TEST_CONTAINER_READ_FAULT" );
			reports.append( recorder.messages() );
			probes.append( loaded.content().attribute( QStringLiteral( "s5probe" ) ) );
		}

		for( const QString& report : reports )
		{
			QVERIFY2( !report.contains( QStringLiteral( "Discarded incomplete" ) ),
				qPrintable( QStringLiteral( "a failed attempt discarded the .new; captured: [%1]" ).arg( report ) ) );
			QVERIFY2( report.contains( QStringLiteral( "Could not read" ) ),
				qPrintable( QStringLiteral( "a failed attempt gave no report; captured: [%1]" ).arg( report ) ) );
		}
		for( const QString& probe : probes )
		{
			QCOMPARE( probe, QStringLiteral( "old" ) );
		}
		QVERIFY2( QFile::exists( temp ), "the .new was lost across the failed attempts" );
		QCOMPARE( readText( target ), oldText );

		// The first readable attempt adopts it.
		DataFile later( target );
		QCOMPARE( later.content().attribute( QStringLiteral( "s5probe" ) ), QStringLiteral( "pending" ) );
		QVERIFY( !QFile::exists( temp ) );
	}

	//! Control: an oversized container is refused by its own limit, reported as
	//! such, and kept - the same behaviour as before the read fix. Only the
	//! document's size is measured; the read is not involved.
	void oversizedContainerNewFileIsRefusedAndKept()
	{
		qunsetenv( "ZENE_TEST_SAVE_FAULT" );
		qunsetenv( "ZENE_TEST_CONTAINER_READ_FAULT" );
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString target = dir.filePath( QStringLiteral( "song.mmp" ) );
		const QString temp = target + QStringLiteral( ".new" );

		DataFile first( DataFile::Type::SongProject );
		first.content().setAttribute( "s5probe", "old" );
		QVERIFY( first.writeFile( target ) );
		const QString oldText = readText( target );

		QString error;
		QVERIFY2( writePendingContainer( temp, QStringLiteral( "pending" ), &error ), qPrintable( error ) );

		QString report;
		QString probe;
		{
			MessageRecorder recorder;
			qputenv( "ZENE_TEST_MAX_RECOVERED_BYTES", "64" );
			DataFile loaded( target );
			qunsetenv( "ZENE_TEST_MAX_RECOVERED_BYTES" );
			report = recorder.messages();
			probe = loaded.content().attribute( QStringLiteral( "s5probe" ) );
		}

		QVERIFY2( report.contains( QStringLiteral( "Refused an oversized" ) ),
			qPrintable( QStringLiteral( "the oversized container was not refused by its limit; captured: [%1]" ).arg( report ) ) );
		QVERIFY2( QFile::exists( temp ), "the oversized container .new was deleted" );
		QCOMPARE( probe, QStringLiteral( "old" ) );
		QCOMPARE( readText( target ), oldText );
	}
};

QTEST_GUILESS_MAIN( DataFileContainerRecoveryTest )
#include "DataFileContainerRecoveryTest.moc"
