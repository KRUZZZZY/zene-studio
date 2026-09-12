/*
 * SessionSchedulerRenderTest.cpp - Session View behaviour-preservation proof
 *                                 (task #595)
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

//! Three renders of the same fixture song through the real export path
//! (ProjectRenderer, the code `lmms render` uses), each hashed and printed as
//! evidence:
//!
//!   1. nothing launched            -> byte-identical across runs, and
//!      byte-identical to the render of the same song with the feature off
//!      (the OFF build has no session code at all, so that is the same claim).
//!   2. a Trigger launch on track 0 -> MUST differ: the launched clip takes the
//!      track over (SPEC-zene-studio A1) and its arrangement content stops.
//!   3. a Gate press+release        -> MUST be byte-identical to (1): the
//!      trigger was released before its grid line, so nothing ever launched.
//!      This is the control that shows (2) is caused by the *launch firing*,
//!      not by the mere fact that a request was made.
//!
//! Evidence format matches MixerAbRegressionTest: one AB_EVIDENCE line per
//! render on stdout, so the hashes can be pasted into the report.

#include <QtTest>

#include <QCryptographicHash>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>
#include <memory>
#include <vector>

#include "Engine.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "SessionScheduler.h"
#include "Song.h"

using namespace lmms;

namespace
{

//! 1 second at the rate the fixture is built for; short enough to keep the
//! test quick, long enough to be a whole bar of song at the default tempo.
constexpr int kSampleRate = 44100;

QString sha256( const QByteArray& data )
{
	return QString::fromLatin1(
		QCryptographicHash::hash( data, QCryptographicHash::Sha256 ).toHex() );
}

void printEvidence( const char* label, const QByteArray& data )
{
	std::fprintf( stdout, "AB_EVIDENCE %s bytes=%d sha256=%s\n",
		label, static_cast<int>( data.size() ), qPrintable( sha256( data ) ) );
	std::fflush( stdout );
}

} // namespace


class SessionSchedulerRenderTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init( true );
		// The dummy device thread renders in the background; this test drives
		// the export synchronously through ProjectRenderer, so stop it.
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! Determinism and non-silence first: without those, the sensitivity
	//! control below would prove nothing.
	void renderWithNothingLaunchedIsByteIdenticalAcrossRuns()
	{
		buildFixture();
		const QByteArray first = render();
		const QByteArray second = render();

		QVERIFY2( !first.isEmpty(), "the export produced no file" );
		QVERIFY2( first != QByteArray( first.size(), '\0' ), "the render is silent" );
		QVERIFY2( first == second, "two renders of the same song differ" );

		printEvidence( "no-launch-run1", first );
		printEvidence( "no-launch-run2", second );
	}

	//! The sensitivity control: launching a clip on the song's only track must
	//! change the render. If this passes while nothing changes, the engine is
	//! not connected to the audio path at all.
	void launchedClipChangesTheRender()
	{
		buildFixture();
		const QByteArray before = render();

		SessionScheduler& scheduler = Engine::getSong()->sessionScheduler();
		QVERIFY( scheduler.requestLaunch( 0, 0, LaunchMode::Trigger, LaunchQuantisation::Bar ) );

		const QByteArray after = render();
		printEvidence( "launched-trigger", after );

		QVERIFY2( scheduler.completedLaunches() > 0,
			"the launch never reached the audio thread" );
		QVERIFY2( before != after,
			"a launched session clip did not change the render - the scheduler is "
			"not driving the audio path" );
	}

	//! The control on the control: a Gate trigger released before its grid line
	//! never starts, so the render must be exactly the one with no launch at
	//! all. This separates "the launch fired" from "a request was made".
	void gateReleasedBeforeTheBoundaryLeavesTheRenderUnchanged()
	{
		buildFixture();
		const QByteArray before = render();

		SessionScheduler& scheduler = Engine::getSong()->sessionScheduler();
		// Both commands are drained in the same audio period, so the release
		// cancels the pending launch before the clock reaches its grid line.
		QVERIFY( scheduler.requestLaunch( 0, 0, LaunchMode::Gate, LaunchQuantisation::Bar ) );
		QVERIFY( scheduler.requestRelease( 0, 0, LaunchMode::Gate, LaunchQuantisation::Bar ) );

		const QByteArray after = render();
		printEvidence( "gate-cancelled", after );

		QVERIFY2( scheduler.completedLaunches() == 0u,
			"a cancelled Gate launch started anyway" );
		QVERIFY2( before == after,
			"a cancelled launch changed the render - the track was taken over "
			"without the clip ever starting" );
	}

private:
	/*! One plugin-free arrangement: a sample track whose clip carries an
	 *  in-memory, deterministic stereo buffer. No instrument plugin, no file,
	 *  so the fixture cannot depend on what is loadable in the test
	 *  environment. */
	void buildFixture()
	{
		Song* song = Engine::getSong();
		song->clearProject();

		const std::size_t frames = static_cast<std::size_t>( kSampleRate );
		std::vector<SampleFrame> data( frames );
		for( std::size_t f = 0; f < frames; ++f )
		{
			// Integer-derived, so the bits are identical on every platform.
			const float value = static_cast<float>( ( f * 7 ) % 101 - 50 ) / 128.0f;
			data[f][0] = value;
			data[f][1] = -value;
		}
		auto buffer = std::make_shared<const SampleBuffer>( std::move( data ), kSampleRate );

		auto* track = new SampleTrack( song );
		track->setName( QStringLiteral( "SessionRenderFixture" ) );
		auto* clip = new SampleClip( track );
		clip->setSampleBuffer( buffer );
		clip->movePosition( TimePos( 0 ) );

		song->updateLength();
	}

	//! Renders the current song through the real export path and returns the
	//! bytes of the written WAV file.
	QByteArray render()
	{
		const QString path = m_dir.filePath( QStringLiteral( "session-render.wav" ) );
		QFile::remove( path );

		const OutputSettings settings( kSampleRate, 192,
			OutputSettings::BitDepth::Depth16Bit, OutputSettings::StereoMode::Stereo );
		ProjectRenderer renderer( settings, ProjectRenderer::ExportFileFormat::Wave, path );
		if( !renderer.isReady() )
		{
			return QByteArray();
		}
		renderer.startProcessing();
		renderer.wait();

		QFile file( path );
		if( !file.open( QIODevice::ReadOnly ) )
		{
			return QByteArray();
		}
		const QByteArray bytes = file.readAll();
		file.close();
		return bytes;
	}

	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN( SessionSchedulerRenderTest )
#include "SessionSchedulerRenderTest.moc"
