/*
 * LocalZynAddSubFx.h - local implementation of ZynAddSubFx plugin
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

#ifndef LOCAL_ZYNADDSUBFX_H
#define LOCAL_ZYNADDSUBFX_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "AudioBufferView.h"
#include "LmmsTypes.h"
#include "Note.h"

class Master;
class NulEngine;

namespace lmms
{

class MidiEvent;


class LocalZynAddSubFx
{
public:
	LocalZynAddSubFx();
	~LocalZynAddSubFx();

	void initConfig();

	void setSampleRate( int _sampleRate );
	void setBufferSize( int _bufferSize );

	void saveXML( const std::string & _filename );
	void loadXML( const std::string & _filename );

	void loadPreset( const std::string & _filename, int _part = 0 );

	void setPresetDir( const std::string & _dir );
	void setLmmsWorkingDir( const std::string & _dir );

	void setPitchWheelBendRange( int semitones );

	//! The event is applied at `offset` frames into the period that is rendered next.
	//! An offset of 0 applies the event at once, the way the callers that have no
	//! position for it use it.
	void processMidiEvent( const MidiEvent& event, f_cnt_t offset = 0 );

	void process(PlanarBufferView<float, 2> out);

	//! The granularity Zyn renders in: the synth computes its voices in blocks of this
	//! many frames. A block that is smaller than the period is what lets a scheduled
	//! MIDI event take effect inside the period it was scheduled for.
	static constexpr int SynthBlockSize = 64;

	inline Master * master()
	{
		return m_master;
	}


protected:
	static int s_instanceCount;

	std::string m_presetsDir;

	std::array<int, NumKeys> m_runningNotes = {};
	Master * m_master;
	NulEngine* m_ioEngine;

} ;


} // namespace lmms

#endif
