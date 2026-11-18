/*
 * ReverbSC.cpp - A native reverb based on an algorithm by Sean Costello
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

#include "ReverbSC.h"

#include "embed.h"
#include "lmms_math.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT reverbsc_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"ReverbSC",
	QT_TRANSLATE_NOOP( "PluginBrowser", "Reverb algorithm by Sean Costello" ),
	"Paul Batchelor",
	0x0123,
	Plugin::Type::Effect,
	new PixmapLoader("zene-plugin-logo"),
	nullptr,
	nullptr,
} ;

}

ReverbSCEffect::ReverbSCEffect( Model* parent, const Descriptor::SubPluginFeatures::Key* key ) :
	AudioPlugin(&reverbsc_plugin_descriptor, parent, key),
	m_reverbSCControls( this )
{
	// The DSP state is created before the effect is used. A state that could
	// not be created leaves the effect bypassed.
	DspState* state = createState(Engine::audioEngine()->outputSampleRate());
	if (state != nullptr)
	{
		m_activeState.store(state, std::memory_order_release);
	}
}

ReverbSCEffect::~ReverbSCEffect()
{
	// The effect is torn down with the engine stopped, so the states the
	// audio thread may still hold are released here.
	destroyState(m_activeState.exchange(nullptr, std::memory_order_acq_rel));
	destroyState(m_pendingState.exchange(nullptr, std::memory_order_acq_rel));
	reclaimRetiredStates();
}

ReverbSCEffect::DspState* ReverbSCEffect::createState( sample_rate_t sampleRate )
{
	auto* state = new DspState();

	if (sp_create(&state->sp) != SP_OK)
	{
		// The soundpipe context could not be allocated: there is no state.
		destroyState(state);
		return nullptr;
	}
	state->sp->sr = sampleRate;

	if (sp_revsc_create(&state->revsc) != SP_OK || state->revsc == nullptr)
	{
		// The reverb state could not be created: there is no state to
		// initialise, so the effect stays bypassed.
		destroyState(state);
		return nullptr;
	}
	if (sp_revsc_init(state->sp, state->revsc) != SP_OK)
	{
		// The state could not be initialised: drop it and stay bypassed.
		destroyState(state);
		return nullptr;
	}

	if (sp_dcblock_create_and_init_pair(state->sp, state->dcblk, 1) == SP_NOT_OK)
	{
		destroyState(state);
		return nullptr;
	}

	return state;
}

void ReverbSCEffect::destroyState( DspState* state )
{
	if (state == nullptr)
	{
		return;
	}
	if (state->revsc != nullptr)
	{
		sp_revsc_destroy(&state->revsc);
	}
	sp_dcblock_destroy(&state->dcblk[0]);
	sp_dcblock_destroy(&state->dcblk[1]);
	if (state->sp != nullptr)
	{
		sp_destroy(&state->sp);
	}
	delete state;
}

void ReverbSCEffect::reclaimRetiredStates()
{
	for (auto& slot : m_retiredStates)
	{
		DspState* state = slot.exchange(nullptr, std::memory_order_acq_rel);
		destroyState(state);
	}
}

ProcessStatus ReverbSCEffect::processImpl(InterleavedBufferView<float, 2> inOut)
{
	// A sample-rate change prepared a new state off the audio thread. Take it
	// over here, at the audio boundary, and hand the state we leave behind to
	// the main thread: the audio thread neither allocates nor frees.
	if (DspState* pending = m_pendingState.exchange(nullptr, std::memory_order_acq_rel))
	{
		DspState* previous = m_activeState.exchange(pending, std::memory_order_acq_rel);
		if (previous != nullptr)
		{
			const auto index = m_retiredIndex.fetch_add(1, std::memory_order_relaxed) % 2;
			m_retiredStates[index].store(previous, std::memory_order_release);
		}
	}

	DspState* state = m_activeState.load(std::memory_order_acquire);
	if (state == nullptr || state->revsc == nullptr ||
		state->dcblk[0] == nullptr || state->dcblk[1] == nullptr)
	{
		// Allocation failure leaves the effect bypassed and the input untouched.
		return ProcessStatus::ContinueIfNotQuiet;
	}

	const float d = dryLevel();
	const float w = wetLevel();

	SPFLOAT tmpL, tmpR;
	SPFLOAT dcblkL, dcblkR;

	ValueBuffer * inGainBuf = m_reverbSCControls.m_inputGainModel.valueBuffer();
	ValueBuffer * sizeBuf = m_reverbSCControls.m_sizeModel.valueBuffer();
	ValueBuffer * colorBuf = m_reverbSCControls.m_colorModel.valueBuffer();
	ValueBuffer * outGainBuf = m_reverbSCControls.m_outputGainModel.valueBuffer();

	for (f_cnt_t f = 0; f < inOut.frames(); ++f)
	{
		float* frame = inOut.framePtr(f);
		auto s = std::array{frame[0], frame[1]};

		const auto inGain = fastPow10f<SPFLOAT>(
			(inGainBuf ? inGainBuf->values()[f] : m_reverbSCControls.m_inputGainModel.value()) / 20.f);
		const auto outGain = fastPow10f<SPFLOAT>(
			(outGainBuf ? outGainBuf->values()[f] : m_reverbSCControls.m_outputGainModel.value()) / 20.f);

		s[0] *= inGain;
		s[1] *= inGain;
		state->revsc->feedback = (SPFLOAT)(sizeBuf ?
			sizeBuf->values()[f]
			: m_reverbSCControls.m_sizeModel.value());

		state->revsc->lpfreq = (SPFLOAT)(colorBuf ?
			colorBuf->values()[f]
			: m_reverbSCControls.m_colorModel.value());


		sp_revsc_compute(state->sp, state->revsc, &s[0], &s[1], &tmpL, &tmpR);
		sp_dcblock_compute(state->sp, state->dcblk[0], &tmpL, &dcblkL);
		sp_dcblock_compute(state->sp, state->dcblk[1], &tmpR, &dcblkR);
		frame[0] = d * frame[0] + w * dcblkL * outGain;
		frame[1] = d * frame[1] + w * dcblkR * outGain;
	}

	return ProcessStatus::ContinueIfNotQuiet;
}

void ReverbSCEffect::changeSampleRate()
{
	// The replacement state is prepared here, off the audio thread, with its
	// own soundpipe context for the new sample rate. The state the audio
	// thread may be using is NOT touched: it is released later, after the
	// audio thread has swapped to the new state.
	DspState* replacement = createState(Engine::audioEngine()->outputSampleRate());
	if (replacement == nullptr)
	{
		// The new state could not be created: the effect keeps the state it
		// has and stays as it was.
		return;
	}

	// Release the states the audio thread has left behind. This happens before
	// the new state is published, so the audio thread can retire at most one
	// state per published replacement.
	reclaimRetiredStates();

	// Hand the new state to the audio thread, which takes it over at its next
	// block boundary. A state the audio thread never took over is ours.
	DspState* stale = m_pendingState.exchange(replacement, std::memory_order_acq_rel);
	if (stale != nullptr)
	{
		destroyState(stale);
	}
}

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model* parent, void* data )
{
	return new ReverbSCEffect(
		parent,
		static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data)
	);
}

}


} // namespace lmms
