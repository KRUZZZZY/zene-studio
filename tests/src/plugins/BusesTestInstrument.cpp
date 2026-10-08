/*
 * BusesTestInstrument.cpp - an instrument with extra output buses, so plugin.output_buses can be measured without a plug-in SDK
 *
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

/*! A TEST SUBJECT, not a product plug-in. The commands plugin.output_buses, plugin.output_bus_route and
 *  plugin.output_buses_create_channels act on an instrument's InstrumentOutputBuses, and the only instruments that
 *  own one are the hosted CLAP, VST3 and LV2 ones - which need an SDK, a built fixture plug-in and (for most of
 *  them) a host module that a build may lack. This module is the smallest instrument that has the model: a main
 *  stereo output and four extra buses -
 *
 *    1 Kick   active from the start
 *    2 Snare  active from the start
 *    3 Aux    inactive until a host asks for it (as a VST3 aux bus is); the enable hook activates it
 *    4 Locked inactive, and the enable hook REFUSES it (a plug-in that will not turn the bus on)
 *
 *  It renders nothing. Never installed: tests/CMakeLists.txt builds it into its own directory, and the test that
 *  needs it points LMMS_PLUGIN_DIR there. */

#include <vector>

#include "Instrument.h"
#include "InstrumentOutputBuses.h"
#include "InstrumentTrack.h"
#include "Plugin.h"
#include "SampleFrame.h"

#include "embed.h"

#if defined(_MSC_VER)
#define PLUGIN_EXPORT __declspec(dllexport)
#else
#define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT buses_test_instrument_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"Buses Test Instrument",
	QT_TRANSLATE_NOOP( "PluginBrowser", "test subject: an instrument with extra output buses" ),
	"Zene Studio test suite",
	0x0100,
	Plugin::Type::Instrument,
	nullptr,
	nullptr,
	nullptr,
};

} // extern "C"

namespace
{

class BusesTestInstrument final : public Instrument
{
public:
	explicit BusesTestInstrument( InstrumentTrack* instrumentTrack ) :
		Instrument( instrumentTrack, &buses_test_instrument_plugin_descriptor )
	{
		m_layout = {
			{ QStringLiteral( "Main" ), 2, 0 },
			{ QStringLiteral( "Kick" ), 2, 2 },
			{ QStringLiteral( "Snare" ), 2, 4 },
			{ QStringLiteral( "Aux" ), 2, -1 },
			{ QStringLiteral( "Locked" ), 2, -1 },
		};
		m_buses.setLayout( m_layout );
		m_buses.setEnableHook( [this]( int bus ) { return enableBus( bus ); } );
	}

	auto outputBuses() -> InstrumentOutputBuses* override { return &m_buses; }

	QString nodeName() const override { return QStringLiteral( "busestestinstrument" ); }
	void saveSettings( QDomDocument& doc, QDomElement& parent ) override { m_buses.saveSettings( doc, parent ); }
	void loadSettings( const QDomElement& parent ) override { m_buses.loadSettings( parent ); }

	gui::PluginView* instantiateView( QWidget* ) override { return nullptr; }

protected:
	void playNoteImpl( NotePlayHandle*, std::span<SampleFrame> ) override {}

private:
	//! The host's activation of an inactive bus: Aux turns on (and the layout is reported again), Locked never does.
	bool enableBus( int bus )
	{
		if( bus != 3 ) { return false; }
		m_layout[3].firstChannel = 6;
		m_buses.setLayout( m_layout );
		return true;
	}

	std::vector<InstrumentOutputBus> m_layout;
	InstrumentOutputBuses m_buses;
};

} // namespace

extern "C"
{

PLUGIN_EXPORT Plugin* lmms_plugin_main( Model* parent, void* )
{
	return new BusesTestInstrument( static_cast<InstrumentTrack*>( parent ) );
}

} // extern "C"

} // namespace lmms
