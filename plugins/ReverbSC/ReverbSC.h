/*
 * RerverbSC.h - Reverb algorithm by Sean Costello
 *
 * Copyright (c) 2017 Paul Batchelor
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


#ifndef REVERBSC_H
#define REVERBSC_H

#include <atomic>

#include "AudioPlugin.h"
#include "ReverbSCControls.h"

extern "C" {
    #include "base.h"
    #include "revsc.h"
    #include "dcblock.h"
}


namespace lmms
{


class ReverbSCEffect : public DefaultEffect
{
public:
	ReverbSCEffect( Model* parent, const Descriptor::SubPluginFeatures::Key* key );
	~ReverbSCEffect() override;

	EffectControls* controls() override
	{
		return &m_reverbSCControls;
	}

	void changeSampleRate();

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;

	//! The effect's DSP state: the soundpipe context, the reverb and the two
	//! DC blockers. A state belongs to exactly one sample rate and is only
	//! ever used by the thread that took it over.
	struct DspState
	{
		sp_data* sp = nullptr;
		sp_revsc* revsc = nullptr;
		sp_dcblock* dcblk[2] = {nullptr, nullptr};
	};

	//! Creates a state for the given sample rate, or null when an allocation failed.
	static DspState* createState( sample_rate_t sampleRate );
	//! Releases a state and everything it owns.
	static void destroyState( DspState* state );
	//! Releases the states the audio thread has left behind.
	void reclaimRetiredStates();

	ReverbSCControls m_reverbSCControls;
	//! The state the audio thread renders with.
	std::atomic<DspState*> m_activeState{nullptr};
	//! A state prepared for a sample-rate change, waiting for the audio boundary.
	std::atomic<DspState*> m_pendingState{nullptr};
	//! The states the audio thread has swapped away from; the main thread releases them.
	std::atomic<DspState*> m_retiredStates[2];
	//! Rotates over the retired states.
	std::atomic<unsigned> m_retiredIndex{0};
	friend class ReverbSCControls;
} ;


} // namespace lmms

#endif
