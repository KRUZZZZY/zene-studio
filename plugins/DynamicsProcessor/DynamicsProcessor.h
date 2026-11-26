/*
 * DynamicsProcessor.h - DynamicsProcessor effect-plugin
 *
 * Copyright (c) 2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
 * Copyright (c) 2006-2008 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */


#ifndef DYNPROC_H
#define DYNPROC_H

#include "AudioPlugin.h"
#include "DynamicsProcessorControls.h"
#include "RmsHelper.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace lmms
{


/*! A prepared pair of RMS windows.

	The control thread builds (and resizes: allocates and frees the buffers) one of these
	outside the processing path and publishes it to the audio thread by pointer. The audio
	thread installs it with one pointer exchange and announces the bank it now runs on, so a
	sample-rate change never allocates or frees memory on the audio thread
	(BUG new-p9-130). */
struct RmsBank
{
	explicit RmsBank( std::size_t window ) :
		m_rms{ RmsHelper{ window }, RmsHelper{ window } }
	{
	}

	//! Control thread only: the only place an RMS window is (re)allocated.
	void resize( std::size_t window )
	{
		m_rms[0].setSize( window );
		m_rms[1].setSize( window );
	}

	std::array<RmsHelper, 2> m_rms;
};


class DynProcEffect : public DefaultEffect
{
public:
	DynProcEffect( Model * _parent,
			const Descriptor::SubPluginFeatures::Key * _key );
	~DynProcEffect() override;

	EffectControls * controls() override
	{
		return( &m_dpControls );
	}

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;
	void processBypassedImpl() override;

	void calcAttack();
	void calcRelease();

	//! Control thread: prepare the window the new rate needs and publish it, then announce
	//! the request. Everything that allocates happens here, never in processImpl().
	void prepareSampleRateChange();
	//! Audio thread: install a prepared bank and announce it as the one in use.
	void adoptRmsBank( RmsBank * bank );

	DynProcControls m_dpControls;

// this member array is needed for peak detection 
	float m_currentPeak[2];
	double m_attCoeff;
	double m_relCoeff;
	
	// The sample-rate request the controls' slot publishes on the control thread and
	// processImpl() consumes on the audio thread. It is an atomic handoff, not a plain
	// bool: the two accesses are ordered by this member alone (BUG new-p9-130).
	std::atomic<bool> m_needsUpdate{false};

	// The banks are owned here for the effect's whole life: the audio path only reads the
	// one it has installed, so neither thread can free a bank the other is using, and the
	// control thread rebuilds a bank it knows is idle (that resize is its allocation site).
	std::vector<std::unique_ptr<RmsBank>> m_banks;
	// The bank the audio path runs on. Written and read by the audio thread alone.
	RmsBank * m_currentBank = nullptr;
	// Audio -> control: the bank the audio path has installed, so the control thread never
	// rebuilds the one in use.
	std::atomic<RmsBank *> m_currentPublished{nullptr};
	// Control -> audio. Exchanged by exactly one side, so a pointer it returns is never in use.
	std::atomic<RmsBank *> m_pendingBank{nullptr};
	// Control thread only: the bank last published and not yet collected by the audio path.
	RmsBank * m_lastPublished = nullptr;

	friend class DynProcControls;

} ;


} // namespace lmms

#endif
