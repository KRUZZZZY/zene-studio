/*
 * ModulationLayerProjectRoundTripTest.cpp - the modulation layer through the
 * PROJECT path, not the layer's own serialiser: modulators AUTHORED on the
 * control surface (the modulator.* commands), written by `project.save`, the
 * session CLEARED, and the file read back by `project.open`.
 *
 * Why this file exists. Three other files already touch the layer's
 * persistence, and none of them asks this question:
 *
 *   ModulationLayerTest::theLayerPersistsAndReloads
 *       saves and reloads a QDomElement the test built itself - the pure
 *       layer, never a project file and never the command surface.
 *   ProjectOpenIntegrityTest::modulationLayerIsRestoredByOpeningTheFixtureProject
 *       opens tests/data/modulation-layer-fixture.mmp, a fixture checked in by
 *       hand. It proves the READER against a file no writer in this tree
 *       produced, so a drift between what the writer emits and what the
 *       fixture happens to carry is invisible to it.
 *   ModulationLayerTest's `shouldPersist()` cases
 *       prove an empty layer writes NO element, at the layer level.
 *
 * What is missing there, and is the deliverable here: a layer the SURFACE
 * created, written into a real .mmp, and required to come back identical after
 * the session that held it was cleared. That is the path an agent or a user
 * takes (create modulators -> project.save -> new/clear -> project.open), and
 * it is the only one that proves the writer and the reader agree through the
 * commands rather than through a hand-authored document.
 *
 * The assertions are the values this test AUTHORED, one by one - never the
 * file echoed back - so a reader that dropped an attribute, guessed a shape or
 * lost a route fails here. The final XML comparison is what makes "identical"
 * literal: the same document, element for element.
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

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "ModulationLayer.h"
#include "ModulationTestSupport.h"
#include "Song.h"

using namespace modtest;

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


//! The layer as the PROJECT WRITER writes it: a fresh document's root holding
//! exactly the <modulation-layer> element saveSettings produces, serialised
//! through the document. Comparing two of these compares the layer the way the
//! file format sees it. Empty for an empty layer (saveSettings writes nothing).
QString layerXml( const ModulationLayer& layer )
{
	QDomDocument document;
	QDomElement root = document.createElement( QStringLiteral( "song" ) );
	document.appendChild( root );
	if( !layer.saveSettings( document, root ) )
	{
		return QString();
	}
	return document.toString();
}


//! The address a modulator.target_set call takes, on the rack fixture this
//! test shares with ControlModulatorCommandsTest (RackTestSupport.h): the
//! driven chain of channel kChannel, effect 0, by parameter display name.
QJsonObject routeArgs( const QString& modulator, const char* parameter, double depth )
{
	return QJsonObject{{QStringLiteral( "modulator" ), modulator},
		{QStringLiteral( "channel" ), channelIdOf()},
		{QStringLiteral( "chain" ), kDrivenChain},
		{QStringLiteral( "effect" ), 0},
		{QStringLiteral( "parameter" ), QString::fromLatin1( parameter )},
		{QStringLiteral( "depth" ), depth}};
}


/*! One modulator of @a layer against the values this test authored. A failure
 *  records a QTest failure and returns; the test keeps checking the rest, so a
 *  broken round trip reports everything it lost rather than only the first
 *  loss. */
void verifyModulator( const ModulationLayer& layer, int index, const QString& name,
	ModulationShape shape, float rateHz, float phase, bool unipolar )
{
	const Modulator* modulator = layer.modulator( index );
	QVERIFY2( modulator != nullptr,
		qPrintable( QStringLiteral( "no modulator at index %1" ).arg( index ) ) );
	QCOMPARE( modulator->name, name );
	QCOMPARE( modulator->source.shape, shape );
	QCOMPARE( modulator->source.rateHz, rateHz );
	QCOMPARE( modulator->source.phase, phase );
	QCOMPARE( modulator->source.unipolar, unipolar );
	QCOMPARE( modulator->source.active, true );
}

} // namespace


class ModulationLayerProjectRoundTripTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		QString why;
		QVERIFY2( initRackFixture( &why ), qPrintable( why ) );
	}

	void cleanupTestCase() { teardownRackFixture(); }

	//! Every case starts from a layer nobody has edited. NOT
	//! Song::clearProject() - the shared rack fixture (RackTestSupport.h) is
	//! built once, and a case that needs a cleared session clears it itself.
	void init() { resetLayer(); }

	//! THE DELIVERABLE: create modulators on the surface, project.save,
	//! clear the session, project.open - and the layer is identical.
	void createdModulatorsSurviveSaveClearAndReopen()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		QVERIFY2( layer().modulatorCount() == 0,
			"the layer did not start empty, so the round trip below could not prove anything" );

		// 1. CREATE, on the surface an agent has. Two modulators, so the
		//    ordering is part of what comes back.
		const ControlResult wobble = run( QStringLiteral( "modulator.create" ),
			{{QStringLiteral( "name" ), QStringLiteral( "Wobble" )},
				{QStringLiteral( "shape" ), QStringLiteral( "sine" )},
				{QStringLiteral( "rate" ), 2.5}} );
		QVERIFY2( wobble.ok, qPrintable( wobble.errorMessage ) );
		const QString wobbleId = wobble.result.value( QStringLiteral( "modulator" ) ).toString();
		QCOMPARE( wobbleId, QStringLiteral( "modulator-0" ) );
		QVERIFY2( run( QStringLiteral( "modulator.rate_set" ),
			{{QStringLiteral( "modulator" ), wobbleId},
				{QStringLiteral( "phase" ), 0.125},
				{QStringLiteral( "unipolar" ), true}} ).ok,
			"the source edit was refused" );
		QVERIFY2( run( QStringLiteral( "modulator.target_set" ),
			routeArgs( wobbleId, kGainName, 0.75 ) ).ok, "binding Gain was refused" );

		const ControlResult ramp = run( QStringLiteral( "modulator.create" ),
			{{QStringLiteral( "name" ), QStringLiteral( "Ramp" )},
				{QStringLiteral( "shape" ), QStringLiteral( "saw" )},
				{QStringLiteral( "rate" ), 0.25}} );
		QVERIFY2( ramp.ok, qPrintable( ramp.errorMessage ) );
		const QString rampId = ramp.result.value( QStringLiteral( "modulator" ) ).toString();
		QCOMPARE( rampId, QStringLiteral( "modulator-1" ) );
		QVERIFY2( run( QStringLiteral( "modulator.rate_set" ),
			{{QStringLiteral( "modulator" ), rampId}, {QStringLiteral( "phase" ), 0.5}} ).ok,
			"the source edit was refused" );
		QVERIFY2( run( QStringLiteral( "modulator.target_set" ),
			routeArgs( rampId, kPanName, -0.5 ) ).ok, "binding Panning was refused" );

		verifyModulator( layer(), 0, QStringLiteral( "Wobble" ), ModulationShape::Sine, 2.5f,
			0.125f, true );
		verifyModulator( layer(), 1, QStringLiteral( "Ramp" ), ModulationShape::Saw, 0.25f,
			0.5f, false );
		QCOMPARE( layer().modulator( 0 )->routeCount(), 1 );
		QCOMPARE( layer().modulator( 1 )->routeCount(), 1 );

		const QString authored = layerXml( layer() );
		QVERIFY2( !authored.isEmpty(), "an authored layer serialised nothing" );

		// 2. project.save, through the command the socket exposes.
		const QString project = dir.filePath( QStringLiteral( "modulation-round-trip.mmp" ) );
		const ControlResult saved = run( QStringLiteral( "project.save" ),
			{{QStringLiteral( "path" ), project}} );
		QVERIFY2( saved.ok, qPrintable( saved.errorMessage ) );
		const QString savedText = readText( project );
		QVERIFY2( !savedText.isEmpty(), "project.save reported success and wrote no file" );
		QVERIFY2( savedText.contains( QStringLiteral( "<modulation-layer" ) ),
			"the saved project carries no <modulation-layer> element: the layer the surface "
			"authored never reached the file" );

		// 3. CLEAR. The session the modulators lived in is gone, and so is the
		//    layer - otherwise the open below would prove nothing about the file.
		Engine::getSong()->clearProject();
		QCOMPARE( layer().modulatorCount(), 0 );
		QVERIFY2( layerXml( layer() ).isEmpty(), "a cleared project still serialises a layer" );

		// 4. project.open, the command a user's File > Open and an agent take.
		//    loadOnLaunch=false is the path taken when a project is opened while
		//    the application is already running.
		Engine::getSong()->setLoadOnLaunch( false );
		const ControlResult opened = run( QStringLiteral( "project.open" ),
			{{QStringLiteral( "path" ), project}} );
		QVERIFY2( opened.ok, qPrintable( opened.errorMessage ) );

		// 5. IDENTICAL: the authored values, then the whole document.
		QCOMPARE( layer().modulatorCount(), 2 );
		verifyModulator( layer(), 0, QStringLiteral( "Wobble" ), ModulationShape::Sine, 2.5f,
			0.125f, true );
		verifyModulator( layer(), 1, QStringLiteral( "Ramp" ), ModulationShape::Saw, 0.25f,
			0.5f, false );

		QCOMPARE( layer().modulator( 0 )->routeCount(), 1 );
		QCOMPARE( layer().modulator( 0 )->routes[0].channel, kChannel );
		QCOMPARE( layer().modulator( 0 )->routes[0].chain, kDrivenChain );
		QCOMPARE( layer().modulator( 0 )->routes[0].effect, 0 );
		QCOMPARE( layer().modulator( 0 )->routes[0].parameter, QStringLiteral( "Gain" ) );
		QCOMPARE( layer().modulator( 0 )->routes[0].depth, 0.75f );
		QCOMPARE( layer().modulator( 1 )->routeCount(), 1 );
		QCOMPARE( layer().modulator( 1 )->routes[0].parameter, QStringLiteral( "Panning" ) );
		QCOMPARE( layer().modulator( 1 )->routes[0].depth, -0.5f );

		QCOMPARE( layerXml( layer() ), authored );

		// ...and the surface's own view of the reopened layer agrees, so the
		// agreement is not an artefact of reading one accessor twice.
		const ControlResult state = run( QStringLiteral( "modulator.get_state" ) );
		QVERIFY2( state.ok, qPrintable( state.errorMessage ) );
		QCOMPARE( state.result.value( QStringLiteral( "modulator_count" ) ).toInt(), 2 );
		const QJsonArray modulators = state.result.value( QStringLiteral( "modulators" ) ).toArray();
		QCOMPARE( modulators.size(), 2 );
		QCOMPARE( modulators.at( 0 ).toObject().value( QStringLiteral( "name" ) ).toString(),
			QStringLiteral( "Wobble" ) );
		QCOMPARE( modulators.at( 1 ).toObject().value( QStringLiteral( "name" ) ).toString(),
			QStringLiteral( "Ramp" ) );

		evidence( "project-round-trip",
			QStringLiteral( "2 modulators authored on the surface: saved, cleared, reopened "
				"identical (values and document)" ) );
	}

	//! The inverse half, at the project level: a session that never used a
	//! modulator writes no element and does not inherit the previous
	//! project's layer.
	void aProjectWithoutModulatorsNeitherWritesNorInheritsTheTag()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		Engine::getSong()->clearProject();
		QCOMPARE( layer().modulatorCount(), 0 );

		const QString plain = dir.filePath( QStringLiteral( "no-modulation.mmp" ) );
		const ControlResult saved = run( QStringLiteral( "project.save" ),
			{{QStringLiteral( "path" ), plain}} );
		QVERIFY2( saved.ok, qPrintable( saved.errorMessage ) );
		QVERIFY2( !readText( plain ).contains( QStringLiteral( "modulation-layer" ) ),
			"an empty layer wrote an element: a project that never used a modulator must "
			"re-save without one" );

		Engine::getSong()->setLoadOnLaunch( false );
		QVERIFY2( run( QStringLiteral( "project.open" ),
			{{QStringLiteral( "path" ), plain}} ).ok, "project.open refused the plain project" );
		QCOMPARE( layer().modulatorCount(), 0 );
		QVERIFY2( layerXml( layer() ).isEmpty(),
			"opening a project with no element produced a layer" );

		evidence( "empty-layer-project",
			QStringLiteral( "a project with no modulator writes no <modulation-layer> and "
				"reopens empty" ) );
	}
};

QTEST_GUILESS_MAIN( ModulationLayerProjectRoundTripTest )
#include "ModulationLayerProjectRoundTripTest.moc"
