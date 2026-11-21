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

void ProjectContainerSaveTest::undoingAProjectOpenRestoresTheDisplacedContainerFormat() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	Song* song = Engine::getSong();
	// The project to open is a v2 container.
	buildSession();
	const QString opened = dir.filePath( QStringLiteral( "opened.mmpz" ) );
	QVERIFY( saveTo( opened, QStringLiteral( "v2" ) ).ok );
	// The displaced session is legacy with unsaved work, so the open checkpoints it.
	buildSession();
	QVERIFY( run( QStringLiteral( "track.add" ), {{ QStringLiteral( "type" ), QStringLiteral( "sample" ) }, { QStringLiteral( "name" ), QStringLiteral( "Displaced" ) }} ).ok );
	QVERIFY( !song->isContainerV2Project() && song->isModified() );
	const QStringList displaced = stateOf();
	const QString displacedFile = song->projectFileName();

	const ControlResult open = run( QStringLiteral( "project.open" ), {{ QStringLiteral( "path" ), opened }} );
	QVERIFY2( open.ok && open.result.value( QStringLiteral( "checkpoint" ) ).toObject().value( QStringLiteral( "undoable" ) ).toBool(), qPrintable( open.errorMessage ) );
	QVERIFY( song->isContainerV2Project() );
	const QStringList openedState = stateOf();

	QVERIFY( run( QStringLiteral( "control.undo" ) ).ok );
	QCOMPARE( stateOf(), displaced );
	QCOMPARE( song->projectFileName(), displacedFile );
	QVERIFY2( !song->isContainerV2Project(), "undo must restore the displaced legacy format" );
	const QString legacy = dir.filePath( QStringLiteral( "displaced-saved.mmpz" ) );
	QVERIFY( saveTo( legacy ).ok );
	QCOMPARE( projectcontainer::shapeOf( readBytes( legacy ) ), projectcontainer::Shape::LegacyCompressed );

	QVERIFY( run( QStringLiteral( "control.redo" ) ).ok );
	QCOMPARE( stateOf(), openedState );
	QVERIFY2( song->isContainerV2Project(), "redo must restore the opened v2 format" );
	const QString container = dir.filePath( QStringLiteral( "opened-saved.mmpz" ) );
	QVERIFY( saveTo( container ).ok );
	QCOMPARE( projectcontainer::shapeOf( readBytes( container ) ), projectcontainer::Shape::ZipContainer );
}
void ProjectContainerSaveTest::aRefusedProjectOpenRollsBackToTheDisplacedContainerFormat() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	Song* song = Engine::getSong();
	buildSession();
	const QString opened = dir.filePath( QStringLiteral( "opened-refused.mmpz" ) );
	QVERIFY( saveTo( opened, QStringLiteral( "v2" ) ).ok );
	buildSession();
	QVERIFY( run( QStringLiteral( "track.add" ), {{ QStringLiteral( "type" ), QStringLiteral( "sample" ) }, { QStringLiteral( "name" ), QStringLiteral( "Displaced" ) }} ).ok );
	QVERIFY( !song->isContainerV2Project() );
	const QStringList displaced = stateOf();
	const QString displacedFile = song->projectFileName();

	// The budget admits the displaced session's own snapshot (the pre-action checkpoint) but not that snapshot
	// beside the loaded project's redo state, so the open loads, cannot keep its step and rolls back. The
	// 64-byte margin covers the two captures of the same session differing in their compressed size.
	const BudgetGuard budget( song->captureUndoSnapshot()->size() + 64 );
	const ControlResult refused = run( QStringLiteral( "project.open" ), {{ QStringLiteral( "path" ), opened }} );
	QVERIFY2( !refused.ok && refused.errorMessage.contains( QStringLiteral( "its redo state" ) ), qPrintable( refused.errorMessage ) );
	QCOMPARE( stateOf(), displaced );
	QCOMPARE( song->projectFileName(), displacedFile );
	QVERIFY2( !song->isContainerV2Project(), "the rollback must put the displaced legacy format back" );
}
void ProjectContainerSaveTest::aFailedReadOfASavedContainerIsNamedAsARead() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QString path = dir.filePath( QStringLiteral( "read-fault.mmpz" ) );
	QVERIFY( saveTo( path, QStringLiteral( "v2" ) ).ok );
	QVERIFY( projectcontainer::isContainer( readBytes( path ) ) );

	QByteArray document;
	QString error;
	QVERIFY2( projectcontainer::readDocumentContainer( path, QStringLiteral( "song" ),
		&document, &error ), qPrintable( error ) );

	qputenv( "ZENE_TEST_CONTAINER_READ_FAULT", "1" );
	const bool ok = projectcontainer::readDocumentContainer( path, QStringLiteral( "song" ),
		&document, &error );
	qunsetenv( "ZENE_TEST_CONTAINER_READ_FAULT" );
	QVERIFY( !ok );
	QVERIFY2( error.contains( QStringLiteral( "cannot read" ) ), qPrintable( error ) );
}
void ProjectContainerSaveTest::aTruncatedSavedContainerIsRefusedAsMalformed() {
	QTemporaryDir dir;
	QVERIFY( dir.isValid() );
	buildSession();
	const QString path = dir.filePath( QStringLiteral( "cut.mmpz" ) );
	QVERIFY( saveTo( path, QStringLiteral( "v2" ) ).ok );
	const QByteArray whole = readBytes( path );
	QVERIFY( writeBytes( path, whole.left( whole.size() / 2 ) ) );

	QByteArray document;
	QString error;
	QVERIFY( !projectcontainer::readDocumentContainer( path, QStringLiteral( "song" ),
		&document, &error ) );
	QVERIFY2( !error.contains( QStringLiteral( "cannot read" ) ), qPrintable( error ) );
}
