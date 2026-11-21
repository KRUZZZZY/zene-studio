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

// Complete fixture method definitions; included once after the fixture declaration.

void ProjectContainerSaveTest::initTestCase() {
	Engine::init( true );
	ControlRegistry::setReady( true );
}
void ProjectContainerSaveTest::cleanupTestCase() {
	ConfigManager::inst()->deleteValue( QStringLiteral( "app" ), Song::containerV2ConfigAttribute() );
	ControlRegistry::setReady( false );
	Engine::destroy();
}
void ProjectContainerSaveTest::init() {
	// Every slot starts from the default: no global opt-in, no v2 session.
	ConfigManager::inst()->deleteValue( QStringLiteral( "app" ), Song::containerV2ConfigAttribute() );
}
void ProjectContainerSaveTest::ordinarySaveStillWritesTheLegacyBlob() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QString path = dir.filePath( QStringLiteral( "plain.mmpz" ) );

	const ControlResult saved = saveTo( path );
	QVERIFY2( saved.ok, qPrintable( saved.errorMessage ) );
	const QByteArray bytes = readBytes( path );
	QCOMPARE( projectcontainer::shapeOf( bytes ), projectcontainer::Shape::LegacyCompressed );
	QVERIFY( !projectcontainer::isContainer( bytes ) );
	QCOMPARE( saved.result.value( QStringLiteral( "format" ) ).toString(), QStringLiteral( "legacy" ) );
	QVERIFY( !saved.result.contains( QStringLiteral( "format_note" ) ) );
	QCOMPARE( static_cast<qint64>( saved.result.value( QStringLiteral( "bytes" ) ).toDouble() ), static_cast<qint64>( bytes.size() ) );
	QVERIFY( !Engine::getSong()->isContainerV2Project() );

	// The same holds for the plain-XML spelling and for a save by the GUI's own entry point.
	const QString xml = dir.filePath( QStringLiteral( "plain.mmp" ) );
	QVERIFY( saveTo( xml ).ok );
	QCOMPARE( projectcontainer::shapeOf( readBytes( xml ) ), projectcontainer::Shape::PlainXml );
	const QString gui = dir.filePath( QStringLiteral( "gui.mmpz" ) );
	QVERIFY( Engine::getSong()->guiSaveProjectAs( gui ) );
	QCOMPARE( projectcontainer::shapeOf( readBytes( gui ) ), projectcontainer::Shape::LegacyCompressed );
}
void ProjectContainerSaveTest::formatV2WritesAContainerAndOpenAutoDetectsBothFormats() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QStringList before = stateOf();
	const QString legacy = dir.filePath( QStringLiteral( "legacy.mmpz" ) );
	const QString v2 = dir.filePath( QStringLiteral( "v2.mmpz" ) );

	const ControlResult savedLegacy = saveTo( legacy );
	QVERIFY2( savedLegacy.ok, qPrintable( savedLegacy.errorMessage ) );
	const ControlResult savedV2 = saveTo( v2, QStringLiteral( "v2" ) );
	QVERIFY2( savedV2.ok, qPrintable( savedV2.errorMessage ) );

	QCOMPARE( projectcontainer::shapeOf( readBytes( v2 ) ), projectcontainer::Shape::ZipContainer );
	QCOMPARE( savedV2.result.value( QStringLiteral( "format" ) ).toString(), QStringLiteral( "v2" ) );
	// The compatibility note: the larger file and the older builds, said in the reply.
	const QString note = savedV2.result.value( QStringLiteral( "format_note" ) ).toString();
	QVERIFY2( note.contains( QStringLiteral( "larger" ) ) && note.contains( QStringLiteral( "older" ) ), qPrintable( note ) );
	// ... and true: the container is bigger than the blob it replaces.
	QVERIFY2( readBytes( v2 ).size() > readBytes( legacy ).size(),
		qPrintable( QStringLiteral( "v2 %1 bytes vs legacy %2" ).arg( readBytes( v2 ).size() ).arg( readBytes( legacy ).size() ) ) );

	// project.open tells them apart by content and lands on the same project.
	for( const QString& path : { legacy, v2 } )
	{
		Engine::getSong()->clearProject();
		const ControlResult opened = run( QStringLiteral( "project.open" ), {{ QStringLiteral( "path" ), path }} );
		QVERIFY2( opened.ok, qPrintable( opened.errorMessage ) );
		QCOMPARE( stateOf(), before );
	}
}
void ProjectContainerSaveTest::aV2ProjectStaysV2AndLegacyIsTheWayBack() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QString path = dir.filePath( QStringLiteral( "sticky.mmpz" ) );
	QVERIFY( saveTo( path, QStringLiteral( "v2" ) ).ok );
	QVERIFY( Engine::getSong()->isContainerV2Project() );

	Engine::getSong()->clearProject();
	QVERIFY( !Engine::getSong()->isContainerV2Project() );
	QVERIFY( run( QStringLiteral( "project.open" ), {{ QStringLiteral( "path" ), path }} ).ok );
	QVERIFY( Engine::getSong()->isContainerV2Project() );

	const ControlResult again = saveTo( path );
	QVERIFY( again.ok );
	QCOMPARE( again.result.value( QStringLiteral( "format" ) ).toString(), QStringLiteral( "v2" ) );
	QVERIFY( projectcontainer::isContainer( readBytes( path ) ) );

	const ControlResult back = saveTo( path, QStringLiteral( "legacy" ) );
	QVERIFY( back.ok );
	QCOMPARE( projectcontainer::shapeOf( readBytes( path ) ), projectcontainer::Shape::LegacyCompressed );
	QVERIFY( !Engine::getSong()->isContainerV2Project() );
	QCOMPARE( saveTo( path ).result.value( QStringLiteral( "format" ) ).toString(), QStringLiteral( "legacy" ) );
}
void ProjectContainerSaveTest::aContainerIsRefusedForAPathThatIsNotMmpz() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QString path = dir.filePath( QStringLiteral( "not-a-container.mmp" ) );
	const ControlResult refused = saveTo( path, QStringLiteral( "v2" ) );
	QVERIFY( !refused.ok );
	QVERIFY2( refused.errorMessage.contains( QStringLiteral( ".mmpz" ) ), qPrintable( refused.errorMessage ) );
	QVERIFY( !QFile::exists( path ) );
	// DataFile itself honours the request only for a .mmpz name, and says what it wrote.
	DataFile file( DataFile::Type::SongProject );
	file.setContainerV2( true );
	QVERIFY( file.writeFile( dir.filePath( QStringLiteral( "asked.mmp" ) ) ) );
	QVERIFY( !file.containerV2Written() );
}
void ProjectContainerSaveTest::theGlobalSettingMakesBareSavesContainersForMmpzOnly() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	// Through the registered settings command, as an agent (and the Settings dialog) sets it.
	const ControlResult set = run( QStringLiteral( "settings.set" ), {{ QStringLiteral( "key" ), QStringLiteral( "app/savecontainerv2" ) },
		{ QStringLiteral( "value" ), QStringLiteral( "1" ) }} );
	QVERIFY2( set.ok, qPrintable( set.errorMessage ) );
	QVERIFY( Song::globalContainerV2() );

	const QString mmpz = dir.filePath( QStringLiteral( "global.mmpz" ) );
	QVERIFY( saveTo( mmpz ).ok );
	QVERIFY( projectcontainer::isContainer( readBytes( mmpz ) ) );
	const QString xml = dir.filePath( QStringLiteral( "global.mmp" ) );
	QVERIFY( saveTo( xml ).ok );
	QCOMPARE( projectcontainer::shapeOf( readBytes( xml ) ), projectcontainer::Shape::PlainXml );
	// An explicit "legacy" beats the setting.
	const QString forced = dir.filePath( QStringLiteral( "forced.mmpz" ) );
	QVERIFY( saveTo( forced, QStringLiteral( "legacy" ) ).ok );
	QCOMPARE( projectcontainer::shapeOf( readBytes( forced ) ), projectcontainer::Shape::LegacyCompressed );

	QVERIFY( run( QStringLiteral( "settings.set" ), {{ QStringLiteral( "key" ), QStringLiteral( "app/savecontainerv2" ) },
		{ QStringLiteral( "value" ), QStringLiteral( "0" ) }} ).ok );
	QVERIFY( !Song::globalContainerV2() );
	Engine::getSong()->clearProject();
	const QString off = dir.filePath( QStringLiteral( "off.mmpz" ) );
	QVERIFY( saveTo( off ).ok );
	QCOMPARE( projectcontainer::shapeOf( readBytes( off ) ), projectcontainer::Shape::LegacyCompressed );
}
void ProjectContainerSaveTest::theSaveAsChoiceDecidesTheFormat() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	Song* song = Engine::getSong();

	song->getSaveOptions().saveAsV2Container.setValue( true );
	song->getSaveOptions().formatChosen = true;
	const QString ticked = dir.filePath( QStringLiteral( "ticked.mmpz" ) );
	QVERIFY( song->guiSaveProjectAs( ticked ) );
	QVERIFY( projectcontainer::isContainer( readBytes( ticked ) ) );
	QVERIFY( song->isContainerV2Project() );
	QCOMPARE( song->lastSavedFormat(), Song::SaveFormat::V2Container );

	// The options are one-shot: the next plain Save keeps the container the project now is.
	QVERIFY( song->guiSaveProject() );
	QVERIFY( projectcontainer::isContainer( readBytes( ticked ) ) );

	song->getSaveOptions().saveAsV2Container.setValue( false );
	song->getSaveOptions().formatChosen = true;
	const QString unticked = dir.filePath( QStringLiteral( "unticked.mmpz" ) );
	QVERIFY( song->guiSaveProjectAs( unticked ) );
	QCOMPARE( projectcontainer::shapeOf( readBytes( unticked ) ), projectcontainer::Shape::LegacyCompressed );
	QVERIFY( !song->isContainerV2Project() );

	song->getSaveOptions().saveAsV2Container.setValue( true );
	song->getSaveOptions().formatChosen = true;
	QVERIFY( !song->guiSaveProjectAs( dir.filePath( QStringLiteral( "wrong.mmp" ) ) ) );
	QVERIFY( song->saveRefusal().contains( QStringLiteral( ".mmpz" ) ) );
	QVERIFY( !QFile::exists( dir.filePath( QStringLiteral( "wrong.mmp" ) ) ) );
}
void ProjectContainerSaveTest::legacyAndV2FixturesLoadAndResaveToTheSameProject() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QStringList expected = stateOf();

	// The fixtures are written by the document's own writer so they carry exactly
	// what a real save carries (index, head, every section), then re-saved.
	const QString legacyFixture = dir.filePath( QStringLiteral( "fixture-legacy.mmpz" ) );
	const QString v2Fixture = dir.filePath( QStringLiteral( "fixture-v2.mmpz" ) );
	QVERIFY( saveTo( legacyFixture, QStringLiteral( "legacy" ) ).ok );
	QVERIFY( saveTo( v2Fixture, QStringLiteral( "v2" ) ).ok );

	// The sections are really separate entries: the skeleton, plus one per section.
	std::vector<std::pair<QString, QByteArray>> entries;
	QString error;
	QVERIFY2( projectcontainer::readContainer( v2Fixture, QStringList(), &entries, &error ), qPrintable( error ) );
	QVERIFY2( entries.size() > 2, "a container with one section is not a container" );
	bool sawSkeleton = false;
	for( const auto& entry : entries ) { sawSkeleton = sawSkeleton || entry.first == projectcontainer::skeletonEntryName(); }
	QVERIFY( sawSkeleton );

	for( const QString& fixture : { legacyFixture, v2Fixture } )
	{
		Engine::getSong()->clearProject();
		QVERIFY( run( QStringLiteral( "project.open" ), {{ QStringLiteral( "path" ), fixture }} ).ok );
		QCOMPARE( stateOf(), expected );

		// Save it again in the format it came in (a bare save), open that, compare again.
		const QString resaved = dir.filePath( QStringLiteral( "resaved-" ) + QFileInfo( fixture ).fileName() );
		QVERIFY( saveTo( resaved ).ok );
		QCOMPARE( projectcontainer::isContainer( readBytes( resaved ) ), fixture == v2Fixture );
		Engine::getSong()->clearProject();
		QVERIFY( run( QStringLiteral( "project.open" ), {{ QStringLiteral( "path" ), resaved }} ).ok );
		QCOMPARE( stateOf(), expected );
	}
}
void ProjectContainerSaveTest::theContainerReassemblesToTheLegacyDocument() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	DataFile document( DataFile::Type::SongProject );
	Engine::getSong()->buildProjectDocument( document );
	document.setDocumentIndexEnabled( true );
	QString xml;
	{
		QTextStream stream( &xml );
		document.write( stream );
	}
	const QString path = dir.filePath( QStringLiteral( "doc.mmpz" ) );
	QString error;
	QVERIFY2( projectcontainer::writeDocumentContainer( path, xml.toUtf8(), QStringLiteral( "song" ), &error ), qPrintable( error ) );
	QByteArray rebuilt;
	QVERIFY2( projectcontainer::readDocumentContainer( path, QStringLiteral( "song" ), &rebuilt, &error ), qPrintable( error ) );
	QCOMPARE( rebuilt, xml.toUtf8() );
}
void ProjectContainerSaveTest::anInterruptedV2SaveIsRecoveredLikeALegacyOne() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QStringList expected = stateOf();
	const QString good = dir.filePath( QStringLiteral( "good.mmpz" ) );
	QVERIFY( saveTo( good, QStringLiteral( "v2" ) ).ok );
	const QByteArray container = readBytes( good );

	// The previous save of the project: one track fewer, in the legacy shape. The
	// interrupted save's complete temp must replace it on the next open.
	const QString path = dir.filePath( QStringLiteral( "interrupted.mmpz" ) );
	Engine::getSong()->clearProject();
	QVERIFY( saveTo( path, QStringLiteral( "legacy" ) ).ok );
	QVERIFY( writeBytes( path + QStringLiteral( ".new" ), container ) );
	QVERIFY( run( QStringLiteral( "project.open" ), {{ QStringLiteral( "path" ), path }} ).ok );
	QCOMPARE( stateOf(), expected );
	QVERIFY( !QFile::exists( path + QStringLiteral( ".new" ) ) );
	QVERIFY( projectcontainer::isContainer( readBytes( path ) ) );

	const QString torn = dir.filePath( QStringLiteral( "torn.mmpz" ) );
	QVERIFY( writeBytes( torn, container ) );
	QVERIFY( writeBytes( torn + QStringLiteral( ".new" ), container.left( container.size() / 2 ) ) );
	Engine::getSong()->clearProject();
	QVERIFY( run( QStringLiteral( "project.open" ), {{ QStringLiteral( "path" ), torn }} ).ok );
	QCOMPARE( stateOf(), expected );
	QVERIFY( !QFile::exists( torn + QStringLiteral( ".new" ) ) );
}
void ProjectContainerSaveTest::anUppercaseSuffixIsRefusedForV2AndNeverMislabelled() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QString upper = dir.filePath( QStringLiteral( "Shout.MMPZ" ) );
	const ControlResult refused = saveTo( upper, QStringLiteral( "v2" ) );
	QVERIFY2( !refused.ok, "an explicit v2 save to an uppercase suffix was accepted" );
	QVERIFY( refused.errorMessage.contains( QStringLiteral( "lowercase" ) ) );
	QVERIFY( QDir( dir.path() ).entryList( QDir::Files ).isEmpty() );

	QVERIFY( run( QStringLiteral( "settings.set" ), {{ QStringLiteral( "key" ), QStringLiteral( "app/savecontainerv2" ) },
		{ QStringLiteral( "value" ), QStringLiteral( "1" ) }} ).ok );
	const ControlResult plain = saveTo( upper );
	QVERIFY2( plain.ok, qPrintable( plain.errorMessage ) );
	// What the reply says is what is on disk, wherever the writer put it.
	const QStringList written = QDir( dir.path() ).entryList( QDir::Files );
	QVERIFY( written.size() == 1 );
	const bool onDiskV2 = projectcontainer::isContainer( readBytes( dir.filePath( written.first() ) ) );
	QCOMPARE( plain.result.value( QStringLiteral( "format" ) ).toString(),
		onDiskV2 ? QStringLiteral( "v2" ) : QStringLiteral( "legacy" ) );
	QVERIFY( !onDiskV2 );
}
void ProjectContainerSaveTest::recoveryMeasuresTheContainersDocumentNotItsFile() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QStringList expected = stateOf();
	const QString good = dir.filePath( QStringLiteral( "good.mmpz" ) );
	QVERIFY( saveTo( good, QStringLiteral( "v2" ) ).ok );
	const QByteArray container = readBytes( good );
	QByteArray document;
	QString error;
	QVERIFY2( projectcontainer::readDocumentContainer( good, QStringLiteral( "song" ), &document, &error ), qPrintable( error ) );
	QVERIFY( container.size() > document.size() );

	// A limit that the document fits under and the file does not.
	const qint64 limit = document.size() + ( container.size() - document.size() ) / 2;
	QVERIFY( container.size() > limit && document.size() <= limit );
	qputenv( "ZENE_TEST_MAX_RECOVERED_BYTES", QByteArray::number( limit ) );

	const QString path = dir.filePath( QStringLiteral( "bound.mmpz" ) );
	Engine::getSong()->clearProject();
	QVERIFY( saveTo( path, QStringLiteral( "legacy" ) ).ok );
	QVERIFY( writeBytes( path + QStringLiteral( ".new" ), container ) );
	QVERIFY( run( QStringLiteral( "project.open" ), {{ QStringLiteral( "path" ), path }} ).ok );
	QVERIFY2( projectcontainer::isContainer( readBytes( path ) ), "a complete container within the document limit was not adopted" );
	QVERIFY( !QFile::exists( path + QStringLiteral( ".new" ) ) );

	// And the bound still bites: a document over the limit is refused, and left in place.
	qputenv( "ZENE_TEST_MAX_RECOVERED_BYTES", QByteArray::number( document.size() - 1 ) );
	const QString over = dir.filePath( QStringLiteral( "over.mmpz" ) );
	Engine::getSong()->clearProject();
	QVERIFY( saveTo( over, QStringLiteral( "legacy" ) ).ok );
	const QByteArray legacyBefore = readBytes( over );
	QVERIFY( writeBytes( over + QStringLiteral( ".new" ), container ) );
	run( QStringLiteral( "project.open" ), {{ QStringLiteral( "path" ), over }} );
	qunsetenv( "ZENE_TEST_MAX_RECOVERED_BYTES" );
	QVERIFY( QFile::exists( over + QStringLiteral( ".new" ) ) );
	QCOMPARE( readBytes( over ), legacyBefore );
}
void ProjectContainerSaveTest::aDamagedContainerIsRefusedAndTheSessionSurvives() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QStringList expected = stateOf();
	const QString good = dir.filePath( QStringLiteral( "good.mmpz" ) );
	QVERIFY( saveTo( good, QStringLiteral( "v2" ) ).ok );
	const QByteArray container = readBytes( good );
	const QString torn = dir.filePath( QStringLiteral( "damaged.mmpz" ) );
	QVERIFY( writeBytes( torn, container.left( container.size() / 2 ) ) );

	const ControlResult opened = run( QStringLiteral( "project.open" ), {{ QStringLiteral( "path" ), torn }} );
	QVERIFY( !opened.ok );
	QCOMPARE( stateOf(), expected );
}
void ProjectContainerSaveTest::theReplacedContainerIsKeptAsARevision() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QString path = dir.filePath( QStringLiteral( "rev.mmpz" ) );
	QVERIFY( saveTo( path, QStringLiteral( "v2" ) ).ok );
	const QByteArray first = readBytes( path );
	const ControlResult second = saveTo( path, QStringLiteral( "legacy" ) );
	QVERIFY( second.ok );
	QVERIFY( second.result.value( QStringLiteral( "revision_kept" ) ).toBool() );
	QCOMPARE( readBytes( path + QStringLiteral( ".rev0" ) ), first );
}
void ProjectContainerSaveTest::undoingAV2ProjectKeepsItsContainerFormat() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QString path = dir.filePath( QStringLiteral( "undo.mmpz" ) );
	QVERIFY( saveTo( path, QStringLiteral( "v2" ) ).ok );
	Song* song = Engine::getSong();
	song->clearProject();
	QVERIFY( run( QStringLiteral( "project.open" ), {{ QStringLiteral( "path" ), path }} ).ok && song->isContainerV2Project() );

	const std::shared_ptr<const QByteArray> before = song->captureUndoSnapshot();
	QVERIFY( before && !before->isEmpty() );
	const QStringList state = stateOf();
	QVERIFY( run( QStringLiteral( "transport.set_tempo" ), {{ QStringLiteral( "bpm" ), 90 }} ).ok );

	QVERIFY( song->restoreUndoSnapshot( *before ) );
	QCOMPARE( stateOf(), state );
	QVERIFY2( song->isContainerV2Project(), "undo must keep the v2 container format" );

	const QString next = dir.filePath( QStringLiteral( "after-undo.mmpz" ) );
	QVERIFY( saveTo( next ).ok );
	QCOMPARE( projectcontainer::shapeOf( readBytes( next ) ), projectcontainer::Shape::ZipContainer );
}
void ProjectContainerSaveTest::undoingALegacyProjectKeepsItsLegacyFormat() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QString path = dir.filePath( QStringLiteral( "legacy-undo.mmpz" ) );
	QVERIFY( saveTo( path ).ok );
	Song* song = Engine::getSong();
	QVERIFY( !song->isContainerV2Project() );

	const std::shared_ptr<const QByteArray> before = song->captureUndoSnapshot();
	QVERIFY( before && !before->isEmpty() );
	QVERIFY( run( QStringLiteral( "transport.set_tempo" ), {{ QStringLiteral( "bpm" ), 90 }} ).ok );
	QVERIFY( song->restoreUndoSnapshot( *before ) );
	QVERIFY( !song->isContainerV2Project() );

	const QString next = dir.filePath( QStringLiteral( "legacy-after-undo.mmpz" ) );
	QVERIFY( saveTo( next ).ok );
	QCOMPARE( projectcontainer::shapeOf( readBytes( next ) ), projectcontainer::Shape::LegacyCompressed );
}
void ProjectContainerSaveTest::aRefusedUndoRestoreKeepsTheContainerFormat() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QString path = dir.filePath( QStringLiteral( "refused-undo.mmpz" ) );
	QVERIFY( saveTo( path, QStringLiteral( "v2" ) ).ok );
	Song* song = Engine::getSong();
	song->clearProject();
	QVERIFY( run( QStringLiteral( "project.open" ), {{ QStringLiteral( "path" ), path }} ).ok );
	const QStringList state = stateOf();

	const QByteArray unreadable = qCompress( QByteArrayLiteral( "<<not a project" ) );
	QVERIFY( !song->restoreUndoSnapshot( unreadable ) );
	QVERIFY( song->isContainerV2Project() );
	QCOMPARE( stateOf(), state );
}
