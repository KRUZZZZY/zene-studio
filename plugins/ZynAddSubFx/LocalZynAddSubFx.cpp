/*
 * LocalZynAddSubFx.cpp - local implementation of ZynAddSubFx plugin
 *
 * Copyright (c) 2009-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "LocalZynAddSubFx.h"

#include <cassert>
#include <algorithm>
#include <ctime>

#include "lmmsconfig.h"

#ifdef LMMS_BUILD_WIN32
#	include <wchar.h>
#	include "IoHelper.h"
#else
#	include <unistd.h>
#endif

#include "MidiEvent.h"

#include <Nio/NulEngine.h>
#include <Misc/Master.h>
#include <Misc/Part.h>
#include <Misc/Util.h>

// Global variable in zynaddsubfx/src/globals.h
SYNTH_T* synth = nullptr;


namespace lmms
{


int LocalZynAddSubFx::s_instanceCount = 0;


LocalZynAddSubFx::LocalZynAddSubFx() :
	m_master( nullptr ),
	m_ioEngine( nullptr )
{
	if( s_instanceCount == 0 )
	{
		initConfig();

		synth = new SYNTH_T;
		synth->oscilsize = config.cfg.OscilSize;
		synth->alias();

		srand( time( nullptr ) );

		denormalkillbuf = new float[synth->buffersize];
		for( int i = 0; i < synth->buffersize; ++i )
		{
			denormalkillbuf[i] = (RND-0.5)*1e-16;
		}
	}

	++s_instanceCount;

	m_ioEngine = new NulEngine;

	m_master = new Master();
	m_master->swaplr = false;
}




LocalZynAddSubFx::~LocalZynAddSubFx()
{
	delete m_master;
	delete m_ioEngine;

	if( --s_instanceCount == 0 )
	{
		delete[] denormalkillbuf;
	}
}




void LocalZynAddSubFx::initConfig()
{
	config.init();

	config.cfg.GzipCompression = 0;
}




void LocalZynAddSubFx::setSampleRate( int sampleRate )
{
	synth->samplerate = sampleRate;
	synth->alias();
}




void LocalZynAddSubFx::setBufferSize( int bufferSize )
{
	// A period has at least one frame. A non-positive size is refused and the last valid
	// one stays in force: the synth divides by its buffer size when it starts a note, so
	// a zero there would put infinities into the voices, and a negative transport size
	// would silence every period.
	if (bufferSize < 1) { return; }

	m_transportSize = bufferSize;
	// Zyn renders in blocks of its own. A block that is smaller than the period is
	// what lets a scheduled MIDI event take effect inside the period it was scheduled
	// for: the event is applied while the period is rendered, and the block that
	// follows it carries the event's voice.
	synth->buffersize = std::min( SynthBlockSize, bufferSize );
	synth->alias();
}




void LocalZynAddSubFx::saveXML( const std::string & _filename )
{
	char * name = strdup( _filename.c_str() );
	m_master->saveXML( name );
	free( name );
}




void LocalZynAddSubFx::loadXML( const std::string & _filename )
{
	char * f = strdup( _filename.c_str() );

	{
		const auto lock = std::lock_guard{m_master->mutex};
		m_master->defaults();
		m_master->loadXML( f );
	}

	m_master->applyparameters();

#ifdef LMMS_BUILD_WIN32
	_wunlink(toWString(_filename).get());
#else
	unlink( f );
#endif

	free( f );
}




void LocalZynAddSubFx::loadPreset( const std::string & _filename, int _part )
{
	char * f = strdup( _filename.c_str() );

	{
		const auto lock = std::lock_guard{m_master->mutex};
		m_master->part[_part]->defaultsinstrument();
		m_master->part[_part]->loadXMLinstrument( f );
	}

	m_master->applyparameters();

	free( f );
}




void LocalZynAddSubFx::setPresetDir( const std::string & _dir )
{
	m_presetsDir = _dir;
	for (auto& bankRootDir : config.cfg.bankRootDirList)
	{
		if (bankRootDir.empty())
		{
			bankRootDir = m_presetsDir;
			break;
		}
		else if (bankRootDir == m_presetsDir)
		{
			break;
		}
	}
}




void LocalZynAddSubFx::setLmmsWorkingDir( const std::string & _dir )
{
	if( config.workingDir != nullptr )
	{
		free( config.workingDir );
	}
	config.workingDir = strdup( _dir.c_str() );

	initConfig();
}



void LocalZynAddSubFx::setPitchWheelBendRange( int semitones )
{
	for (const auto& part : m_master->part)
	{
		part->ctl.setpitchwheelbendrange(semitones * 100);
	}
}



void LocalZynAddSubFx::processMidiEvent( const MidiEvent& event, f_cnt_t offset )
{
	if (offset > 0)
	{
		// The event was scheduled for a frame inside the period that is rendered next:
		// it waits there, and the render applies it at that frame.
		if (m_scheduledCount < m_scheduled.size())
		{
			ScheduledEvent scheduled;
			scheduled.type = static_cast<std::uint8_t>( event.type() );
			scheduled.channel = static_cast<std::int8_t>( event.channel() );
			scheduled.param1 = static_cast<std::int16_t>( event.param( 0 ) );
			scheduled.param2 = static_cast<std::int16_t>( event.param( 1 ) );
			scheduled.offset = static_cast<std::uint32_t>( offset );
			m_scheduled[m_scheduledCount++] = scheduled;
			return;
		}
		// The list is full: the event is applied at once rather than lost.
	}

	applyMidiEvent( event );
}




void LocalZynAddSubFx::applyMidiEvent( const MidiEvent& event )
{
	switch( event.type() )
	{
		case MidiNoteOn:
			if( event.velocity() > 0 )
			{
				if( event.key() < 0 || event.key() > MidiMaxKey )
				{
					break;
				}
				if( m_runningNotes[event.key()] > 0 )
				{
					m_master->noteOff( event.channel(), event.key() );
				}
				++m_runningNotes[event.key()];
				m_master->noteOn( event.channel(), event.key(), event.velocity() );
				break;
			}
		case MidiNoteOff:
		{
			if( event.key() < 0 || event.key() > MidiMaxKey )
			{
				break;
			}
			const auto runningNotes = m_runningNotes[event.key()];
			if( runningNotes == 1 )
			{
				m_master->noteOff( event.channel(), event.key() );
			}
			m_runningNotes[event.key()] = std::max( 0, runningNotes - 1 );
			break;
		}
		case MidiPitchBend:
			m_master->setController( event.channel(), C_pitchwheel, event.pitchBend()-8192 );
			break;
		case MidiControlChange:
			m_master->setController( event.channel(), event.controllerNumber(), event.controllerValue() );
			break;
		default:
			break;
	}
}




void LocalZynAddSubFx::process(PlanarBufferView<float, 2> out)
{
	// The synth writes exactly the frames it is asked for, straight into the caller's
	// planes, so a period that is longer than the one the host announced would write
	// past them. The shipped configuration defines NDEBUG (RelWithDebInfo), so the
	// assert alone is not a guard: this makes such a period render silence instead of
	// overrunning the plane.
	if (out.frames() > static_cast<f_cnt_t>(m_transportSize))
	{
		assert(out.frames() <= static_cast<f_cnt_t>(m_transportSize));
		std::fill_n(out.bufferPtr<0>(), out.frames(), 0.f);
		std::fill_n(out.bufferPtr<1>(), out.frames(), 0.f);
		return;
	}

	// The events that were scheduled for this period are applied while it is rendered:
	// the render is split at each event's frame, so the event takes effect in the period
	// it was scheduled for. Zyn renders in blocks of SynthBlockSize, so the event's
	// effect starts at the first block boundary at or after its frame.
	//
	// Zyn generates the block that starts at a boundary when the render REACHES that
	// boundary: the block that starts at frame f is generated by the render call that
	// ends at f. An event that sits exactly on a boundary therefore has to be applied
	// before that call - one frame earlier - or the block that starts at its frame is
	// generated without it and the event lands in the block after it.
	f_cnt_t rendered = 0;
	for (std::size_t i = 0; i < m_scheduledCount; ++i)
	{
		const auto frame = std::clamp<f_cnt_t>(m_scheduled[i].offset, rendered, out.frames());
		auto split = frame;
		if (split > rendered && split % static_cast<f_cnt_t>(SynthBlockSize) == 0) { --split; }
		if (split > rendered)
		{
			renderFrames(out, rendered, split);
			rendered = split;
		}
		applyScheduledEvent(m_scheduled[i]);
	}
	m_scheduledCount = 0;

	renderFrames(out, rendered, out.frames());
}




void LocalZynAddSubFx::applyScheduledEvent( const ScheduledEvent& event )
{
	applyMidiEvent( MidiEvent( static_cast<MidiEventTypes>(event.type), event.channel,
		event.param1, event.param2 ) );
}




void LocalZynAddSubFx::renderFrames( PlanarBufferView<float, 2> out, f_cnt_t from, f_cnt_t to )
{
	const auto frames = to - from;
	if (frames <= 0) { return; }

	m_master->GetAudioOutSamples( static_cast<std::size_t>(frames), synth->samplerate,
		out.bufferPtr<0>() + from, out.bufferPtr<1>() + from );
}


} // namespace lmms
