/*
 * SlicerTZoomTest.cpp - regression for cluster new-p10-377: waveform zoom clamp
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

//! plugins/SlicerT/SlicerTWaveform.cpp wheelEvent() clamped the zoom level only
//! at zero: `m_zoomLevel = std::max(0.0f, m_zoomLevel)`. drawEditorWaveform()
//! uses zoom as a scale factor - at 0 the sample rect has zero height and the
//! waveform disappears; scrolling back up leaves it unbounded, producing
//! out-of-range geometry conversions. The fix clamps to [s_minZoom, s_maxZoom].
//!
//! The plugin is built as a module, so its sources are compiled into this test
//! binary (the WasmSandboxTest precedent). The zoom level is a private member
//! whose only other observable is pixel geometry, so the access specifiers are
//! opened for this TU only; they affect neither layout nor name mangling, so
//! the plugin TUs and the link are unaffected.

#include <QtTest>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "Song.h"

#define private public
#define protected public
#include "SlicerT.h"
#include "SlicerTView.h"
#include "SlicerTWaveform.h"
#undef private
#undef protected

class SlicerTZoomTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { lmms::Engine::init(true); }
	void cleanupTestCase() { lmms::Engine::destroy(); }

	// One wheel-up from the default 1.0 is in range: 1 + 120/360 * 0.5 = 7/6.
	// The fix must preserve in-range behaviour exactly, so this passes both
	// before and after the clamp change.
	void inRangeScrollStepIsPreserved()
	{
		using namespace lmms;
		InstrumentTrack track( Engine::getSong() );
		SlicerT slicer( &track );
		QWidget parent;
		::lmms::gui::SlicerTWaveform wf( 400, 300, &slicer, &parent );

		QWheelEvent up( QPointF( 10, 10 ), QPointF( 10, 10 ), QPoint( 0, 0 ), QPoint( 0, 120 ),
			Qt::MouseButtons(), Qt::KeyboardModifiers(), Qt::ScrollPhase(), false );
		wf.wheelEvent( &up );

	 QVERIFY( qAbs( wf.m_zoomLevel - 7.0f / 6.0f ) < 1e-4f );
	}

	// Ten wheel-downs from 1.0: 1 - 10 * (120/360 * 0.5) = -0.5. The old floor
	// clamped to exactly 0.0, giving drawEditorWaveform a zero-height sample
	// rect - the waveform vanished.
	void zoomingOutNeverCollapsesTheWaveform()
	{
		using namespace lmms;
		InstrumentTrack track( Engine::getSong() );
		SlicerT slicer( &track );
		QWidget parent;
		::lmms::gui::SlicerTWaveform wf( 400, 300, &slicer, &parent );

		QWheelEvent down( QPointF( 10, 10 ), QPointF( 10, 10 ), QPoint( 0, 0 ), QPoint( 0, -120 ),
			Qt::MouseButtons(), Qt::KeyboardModifiers(), Qt::ScrollPhase(), false );
		for ( int i = 0; i < 10; ++i )
		{
			wf.wheelEvent( &down );
		}

	 QVERIFY( wf.m_zoomLevel > 0.0f );
	}

	// Same ten downs, then 120 ups: the old code reached 0 + 120 * 1/6 = 20.0
	// with no upper bound, blowing the sample rect far past the widget.
	void zoomingBackUpStaysBounded()
	{
		using namespace lmms;
		InstrumentTrack track( Engine::getSong() );
		SlicerT slicer( &track );
		QWidget parent;
		::lmms::gui::SlicerTWaveform wf( 400, 300, &slicer, &parent );

		QWheelEvent down( QPointF( 10, 10 ), QPointF( 10, 10 ), QPoint( 0, 0 ), QPoint( 0, -120 ),
			Qt::MouseButtons(), Qt::KeyboardModifiers(), Qt::ScrollPhase(), false );
		for ( int i = 0; i < 10; ++i )
		{
			wf.wheelEvent( &down );
		}

		QWheelEvent up( QPointF( 10, 10 ), QPointF( 10, 10 ), QPoint( 0, 0 ), QPoint( 0, 120 ),
			Qt::MouseButtons(), Qt::KeyboardModifiers(), Qt::ScrollPhase(), false );
		for ( int i = 0; i < 120; ++i )
		{
			wf.wheelEvent( &up );
		}

	 QVERIFY( wf.m_zoomLevel <= 15.0f );
	}
};

QTEST_MAIN( SlicerTZoomTest )

#include "SlicerTZoomTest.moc"
