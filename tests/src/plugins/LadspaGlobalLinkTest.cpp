/*
 * LadspaGlobalLinkTest.cpp - the LADSPA global stereo link over a 65536-control plugin
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <QtTest>

#include "Engine.h"
#include "LadspaControls.h"

using namespace lmms;

namespace
{

//! What the global stereo link did to the ports of the first processor.
struct LinkWalk
{
	int calls = 0;
	int lastPort = -1;
	int linkedCalls = 0;
	int unlinkedCalls = 0;
};



void onLinkPort( int _port, bool _linked, void * _user )
{
	auto * walk = static_cast<LinkWalk*>( _user );
	walk->calls++;
	walk->lastPort = _port;
	if( _linked ) { walk->linkedCalls++; } else { walk->unlinkedCalls++; }
}

} // namespace


class LadspaGlobalLinkTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init( true );
	}

	void cleanupTestCase()
	{
		LadspaControls::setLinkPortVisitor( nullptr, nullptr );
		Engine::destroy();
	}

	// A LADSPA plugin with 65536 control ports kept the control total in a 16-bit
	// ch_cnt_t, so the total read back as 0: the effect view hid the controls of the
	// plugin and the global stereo link walked over none of them.
	void theControlTotalOfASixtyFiveThousandControlPluginIsNotTruncated()
	{
		LadspaControls controls( 2, 65536 );

		QCOMPARE( controls.controlCount(), 65536 );
	}

	// The global stereo link has to cover every port of the first processor, which is
	// the control total divided by the processor count.
	void globalStereoLinkCoversEveryPortOfASixtyFiveThousandControlPlugin()
	{
		LinkWalk walk;
		LadspaControls::setLinkPortVisitor( &onLinkPort, &walk );

		// Two processors with 65536 control ports between them: 32768 ports each.
		LadspaControls controls( 2, 65536 );

		controls.setGlobalLinkState( false );
		QCOMPARE( walk.unlinkedCalls, 32768 );
		QCOMPARE( walk.lastPort, 32767 );

		walk.calls = 0;
		walk.lastPort = -1;
		controls.setGlobalLinkState( true );
		QCOMPARE( walk.linkedCalls, 32768 );
		QCOMPARE( walk.lastPort, 32767 );

		QCOMPARE( controls.controlCount(), 65536 );

		LadspaControls::setLinkPortVisitor( nullptr, nullptr );
	}

	// A plugin with fewer controls than the 16-bit limit keeps its count, and its
	// global link covers exactly the ports of the first processor.
	void globalStereoLinkCoversThePortsOfAPluginBelowTheSixteenBitLimit()
	{
		LinkWalk walk;
		LadspaControls::setLinkPortVisitor( &onLinkPort, &walk );

		LadspaControls controls( 2, 100 );

		QCOMPARE( controls.controlCount(), 100 );

		controls.setGlobalLinkState( false );
		QCOMPARE( walk.unlinkedCalls, 50 );
		QCOMPARE( walk.lastPort, 49 );

		LadspaControls::setLinkPortVisitor( nullptr, nullptr );
	}

	// A plugin whose channel count does not divide the host's, such as a three-input
	// plugin on a stereo host, has no processors. It has no ports to walk, so the global
	// stereo link must change nothing and must not divide by the zero processor count.
	void globalStereoLinkOfAPluginWithoutProcessorsWalksNoPortsAndDoesNotDivideByZero()
	{
		LinkWalk walk;
		LadspaControls::setLinkPortVisitor( &onLinkPort, &walk );

		LadspaControls controls( 0, 4 );

		controls.setGlobalLinkState( false );
		controls.setGlobalLinkState( true );
		QCOMPARE( walk.calls, 0 );

		LadspaControls::setLinkPortVisitor( nullptr, nullptr );
	}

	// A plugin without processors may also report no controls, so the global stereo link
	// must be safe with zero processors and zero controls as well.
	void globalStereoLinkOfAPluginWithoutProcessorsOrControlsDoesNotDivideByZero()
	{
		LinkWalk walk;
		LadspaControls::setLinkPortVisitor( &onLinkPort, &walk );

		LadspaControls controls( 0, 0 );

		controls.setGlobalLinkState( false );
		controls.setGlobalLinkState( true );
		QCOMPARE( walk.calls, 0 );

		LadspaControls::setLinkPortVisitor( nullptr, nullptr );
	}
};

QTEST_GUILESS_MAIN(LadspaGlobalLinkTest)

#include "LadspaGlobalLinkTest.moc"
