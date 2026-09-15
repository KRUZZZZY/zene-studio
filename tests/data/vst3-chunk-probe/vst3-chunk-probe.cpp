/*
 * vst3-chunk-probe.cpp - a purpose-built VST3 EFFECT that witnesses the host's
 *                        chunking contract
 *
 * SPDX-License-Identifier: MIT
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * CODE-4: "the plugin hosts process in chunks instead of truncating or
 * overrunning". The defect it names was in the HOST, not in any plug-in:
 * Vst3Host.cpp sized `silence` and `scratchOutput` to the prepared block and
 * then handed the plug-in `numSamples = frames` for whatever the caller asked
 * for - one process() call, no clamp - so a request larger than the prepared
 * block read and wrote past both scratch buffers; and ClapHost.cpp clamped the
 * request to the prepared block and returned true, leaving the tail of the
 * caller's buffers untouched.
 *
 * A test for that needs a plug-in that can SAY what it was asked for. There is
 * no third-party VST3 plug-in installable on this machine and none is needed:
 * this fixture is the in-tree MIT subject, the VST3 twin of
 * tests/data/clap-test-plugin/clap-test-gain.c (which carries the same
 * witnesses for the CLAP host). No LMMS code, no third-party identity, built
 * from the pinned MIT VST3 SDK checkout the product's own host is built from;
 * it is NEVER installed.
 *
 * WHAT IT DOES
 * ------------
 * - It is a two-in / two-out audio effect (Vst::PlugType::kFx, so the host
 *   classifies it as an effect and not an instrument) with one continuous,
 *   automatable parameter, Gain, whose plain value is the linear gain factor.
 *   process() is a plain per-sample multiply, so a test can assert exact sample
 *   values and detect a frame the host never asked it to write.
 * - It WITNESSES the call sequence. Every process() call records its own
 *   numSamples in a fixed ring, counts the calls and the frames, tracks the
 *   largest call, and counts any call that asked for MORE samples than
 *   setupProcessing() declared (setup.maxSamplesPerBlock) - which is exactly
 *   the over-run the host is not allowed to commit. All of it travels in the
 *   component state, so a host-level test reads the plug-in's own account
 *   through saveState() and needs no extra host API and no symbol lookup.
 * - The counters reset in setActive(true), like a real plug-in's DSP state:
 *   a test measures one activation.
 *
 * The state layout is mirrored, with a static_assert on its size, in
 * tests/src/plugins/Vst3ChunkProbeTest.cpp.
 */

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include "pluginterfaces/base/fstrdefs.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <cstring>

namespace Steinberg {
namespace Vst {
namespace ZeneChunkProbe {

//------------------------------------------------------------------------
// A stable, documented class UID. Generated once for this fixture and never
// reused: a host that stores it must resolve the same class across versions.
static const FUID kChunkProbeUID (0x7C4E1A55, 0x2B8F4D31, 0xA5D90E64, 0x13F7C2B8);

// One automatable parameter. The witness counters travel in the state, not in
// the parameter surface: a parameter is a control the user automates, and
// adding fake ones would change what the host shows.
enum
{
	kGainParam = 100
};

static const float kDefaultGain = 1.0f;

//! The state blob, mirrored in the test. All members are 4 bytes wide, so the
//! layout has no padding and both sides can memcpy it.
static const int32 kStateMagic = 0x43484E4B; // 'CHNK'
static const int32 kStateVersion = 1;
static const int32 kMaxRecordedBlocks = 16;

struct ProbeState
{
	int32 magic;
	int32 version;
	float gain;
	int32 declaredMaxBlock;  //!< what setupProcessing() declared
	int32 blocks;            //!< process() calls since setActive(true)
	int32 framesProcessed;   //!< frames those calls asked for, in total
	int32 maxBlock;          //!< largest numSamples seen
	int32 oversizedCalls;    //!< calls with numSamples > declaredMaxBlock
	int32 recordedCount;     //!< entries of `recorded` that are valid
	int32 recorded[kMaxRecordedBlocks];
};

//------------------------------------------------------------------------
class ChunkProbe : public SingleComponentEffect
{
public:
	ChunkProbe ();

	//--- called by the factory macro ------------------------------------
	static FUnknown* createInstance (void* /*context*/)
	{
		return static_cast<IAudioProcessor*> (new ChunkProbe ());
	}

	//--- IPluginBase ----------------------------------------------------
	tresult PLUGIN_API initialize (FUnknown* context) SMTG_OVERRIDE;
	tresult PLUGIN_API terminate () SMTG_OVERRIDE;

	//--- IComponent -----------------------------------------------------
	tresult PLUGIN_API setActive (TBool state) SMTG_OVERRIDE;
	tresult PLUGIN_API setState (IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API getState (IBStream* state) SMTG_OVERRIDE;

	//--- IAudioProcessor ------------------------------------------------
	tresult PLUGIN_API setupProcessing (ProcessSetup& setup) SMTG_OVERRIDE;
	tresult PLUGIN_API setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
	                                       SpeakerArrangement* outputs,
	                                       int32 numOuts) SMTG_OVERRIDE;
	tresult PLUGIN_API canProcessSampleSize (int32 symbolicSampleSize) SMTG_OVERRIDE;
	tresult PLUGIN_API setProcessing (TBool state) SMTG_OVERRIDE;
	tresult PLUGIN_API process (ProcessData& data) SMTG_OVERRIDE;

	//--- IEditController (renamed to *EditorState by SingleComponentEffect)
	tresult PLUGIN_API setEditorState (IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API getEditorState (IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API setParamNormalized (ParamID tag, ParamValue value) SMTG_OVERRIDE;

private:
	//! Fold one parameter change from the host's queue into \c mGain.
	void applyParameterChanges (IParameterChanges& changes);
	//! The witnesses as the state blob (mirrored in Vst3ChunkProbeTest.cpp).
	ProbeState snapshot () const;
	//! Restore the witnesses from a state blob.
	void applyState (const ProbeState& state);

	float mGain;
	bool mProcessing;
	int32 mDeclaredMaxBlock;
	int32 mBlocks;
	int32 mFramesProcessed;
	int32 mMaxBlock;
	int32 mOversizedCalls;
	int32 mRecordedCount;
	int32 mRecorded[kMaxRecordedBlocks];
};

//------------------------------------------------------------------------
ChunkProbe::ChunkProbe ()
: mGain (kDefaultGain)
, mProcessing (false)
, mDeclaredMaxBlock (0)
, mBlocks (0)
, mFramesProcessed (0)
, mMaxBlock (0)
, mOversizedCalls (0)
, mRecordedCount (0)
{
	for (int32 i = 0; i < kMaxRecordedBlocks; ++i)
	{
		mRecorded[i] = 0;
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API ChunkProbe::initialize (FUnknown* context)
{
	tresult result = SingleComponentEffect::initialize (context);
	if (result != kResultOk)
	{
		return result;
	}

	// An effect: one stereo input, one stereo output, no event bus.
	addAudioInput (STR16 ("Audio In"), SpeakerArr::kStereo);
	addAudioOutput (STR16 ("Audio Out"), SpeakerArr::kStereo);

	parameters.addParameter (STR16 ("Gain"), nullptr, /*flags*/ 0, kDefaultGain,
	                         ParameterInfo::kCanAutomate, kGainParam);
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API ChunkProbe::terminate ()
{
	return SingleComponentEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API ChunkProbe::setupProcessing (ProcessSetup& setup)
{
	// The block size the host promises never to exceed. process() reports any
	// call that does exceed it, which is what makes an over-run observable
	// from outside the host.
	mDeclaredMaxBlock = setup.maxSamplesPerBlock;
	return SingleComponentEffect::setupProcessing (setup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API ChunkProbe::setActive (TBool state)
{
	// Not the audio thread: the host calls this from prepare()/release().
	if (state)
	{
		mBlocks = 0;
		mFramesProcessed = 0;
		mMaxBlock = 0;
		mOversizedCalls = 0;
		mRecordedCount = 0;
		for (int32 i = 0; i < kMaxRecordedBlocks; ++i)
		{
			mRecorded[i] = 0;
		}
	}
	return SingleComponentEffect::setActive (state);
}

//------------------------------------------------------------------------
tresult PLUGIN_API ChunkProbe::setParamNormalized (ParamID tag, ParamValue value)
{
	tresult result = SingleComponentEffect::setParamNormalized (tag, value);
	if (result == kResultOk && tag == kGainParam)
	{
		mGain = static_cast<float> (value);
	}
	return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API ChunkProbe::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                   SpeakerArrangement* outputs,
                                                   int32 numOuts)
{
	// Only the one arrangement the fixture declares: stereo in, stereo out.
	if (numIns == 1 && numOuts == 1 && inputs[0] == SpeakerArr::kStereo &&
	    outputs[0] == SpeakerArr::kStereo)
	{
		return SingleComponentEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
	}
	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API ChunkProbe::canProcessSampleSize (int32 symbolicSampleSize)
{
	return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API ChunkProbe::setProcessing (TBool state)
{
	mProcessing = state != 0;
	return kResultOk;
}

//------------------------------------------------------------------------
void ChunkProbe::applyParameterChanges (IParameterChanges& changes)
{
	const int32 count = changes.getParameterCount ();
	for (int32 i = 0; i < count; ++i)
	{
		IParamValueQueue* queue = changes.getParameterData (i);
		if (queue == nullptr || queue->getParameterId () != kGainParam)
		{
			continue;
		}
		const int32 points = queue->getPointCount ();
		if (points <= 0)
		{
			continue;
		}
		// The value that applies to this block is the last point's: every
		// point is a time-0 point of the chunk the host delivered it with.
		ParamValue value = 0.0;
		int32 sampleOffset = 0;
		if (queue->getPoint (points - 1, sampleOffset, value) == kResultOk)
		{
			mGain = static_cast<float> (value);
		}
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API ChunkProbe::process (ProcessData& data)
{
	// Witness first: the call happened, whatever the plug-in then does with it.
	if (data.numSamples > 0)
	{
		++mBlocks;
		mFramesProcessed += data.numSamples;
		if (data.numSamples > mMaxBlock)
		{
			mMaxBlock = data.numSamples;
		}
		if (mDeclaredMaxBlock != 0 && data.numSamples > mDeclaredMaxBlock)
		{
			++mOversizedCalls;
		}
		if (mRecordedCount < kMaxRecordedBlocks)
		{
			mRecorded[mRecordedCount] = data.numSamples;
			++mRecordedCount;
		}
	}

	if (data.numSamples <= 0 || data.symbolicSampleSize != kSample32)
	{
		return kResultOk;
	}
	if (data.inputParameterChanges != nullptr)
	{
		applyParameterChanges (*data.inputParameterChanges);
	}

	const int32 numSamples = data.numSamples;
	const Sample32 gain = static_cast<Sample32> (mGain);
	const int32 inputCount = data.inputs != nullptr ? data.numInputs : 0;
	const int32 outputCount = data.outputs != nullptr ? data.numOutputs : 0;

	for (int32 bus = 0; bus < outputCount; ++bus)
	{
		AudioBusBuffers& outBus = data.outputs[bus];
		if (outBus.channelBuffers32 == nullptr)
		{
			continue;
		}
		const AudioBusBuffers* inBus =
		    (bus < inputCount && data.inputs[bus].channelBuffers32 != nullptr)
		        ? &data.inputs[bus]
		        : nullptr;
		for (int32 channel = 0; channel < outBus.numChannels; ++channel)
		{
			Sample32* dst = outBus.channelBuffers32[channel];
			if (dst == nullptr)
			{
				continue;
			}
			// A channel the host has no input for is silence, and the fixture
			// writes it as silence: every frame of every channel the host
			// asked for is written, so a caller can tell a written frame from
			// one that was left alone.
			const Sample32* src = (inBus != nullptr && channel < inBus->numChannels)
			                          ? inBus->channelBuffers32[channel]
			                          : nullptr;
			for (int32 frame = 0; frame < numSamples; ++frame)
			{
				dst[frame] = src != nullptr ? src[frame] * gain : 0.0f;
			}
		}
		// The fixture always writes its outputs, so report the real state
		// rather than leaving the host's flags.
		outBus.silenceFlags = 0;
	}
	return kResultOk;
}

//------------------------------------------------------------------------
static tresult writeState (const ProbeState& state, IBStream* stream)
{
	if (stream == nullptr)
	{
		return kInvalidArgument;
	}
	int32 written = 0;
	if (stream->write (const_cast<ProbeState*> (&state), sizeof (ProbeState), &written) != kResultOk ||
	    written != static_cast<int32> (sizeof (ProbeState)))
	{
		return kResultFalse;
	}
	return kResultOk;
}

static tresult readState (ProbeState* state, IBStream* stream)
{
	if (stream == nullptr || state == nullptr)
	{
		return kInvalidArgument;
	}
	int32 read = 0;
	if (stream->read (state, sizeof (ProbeState), &read) != kResultOk ||
	    read != static_cast<int32> (sizeof (ProbeState)))
	{
		return kResultFalse;
	}
	if (state->magic != kStateMagic || state->version != kStateVersion)
	{
		return kResultFalse;
	}
	return kResultOk;
}

//! The component's and the editor's state carry the same blob: the host saves
//! and restores both, so the fixture answers both calls.
ProbeState ChunkProbe::snapshot () const
{
	ProbeState state;
	std::memset (&state, 0, sizeof (state));
	state.magic = kStateMagic;
	state.version = kStateVersion;
	state.gain = mGain;
	state.declaredMaxBlock = mDeclaredMaxBlock;
	state.blocks = mBlocks;
	state.framesProcessed = mFramesProcessed;
	state.maxBlock = mMaxBlock;
	state.oversizedCalls = mOversizedCalls;
	state.recordedCount = mRecordedCount;
	for (int32 i = 0; i < kMaxRecordedBlocks; ++i)
	{
		state.recorded[i] = mRecorded[i];
	}
	return state;
}

void ChunkProbe::applyState (const ProbeState& state)
{
	mGain = state.gain;
	// Loading a state restores what the fixture reports, so two saves of one
	// state round-trip byte-wise.
	mDeclaredMaxBlock = state.declaredMaxBlock;
	mBlocks = state.blocks;
	mFramesProcessed = state.framesProcessed;
	mMaxBlock = state.maxBlock;
	mOversizedCalls = state.oversizedCalls;
	mRecordedCount = state.recordedCount <= kMaxRecordedBlocks ? state.recordedCount
	                                                          : kMaxRecordedBlocks;
	for (int32 i = 0; i < kMaxRecordedBlocks; ++i)
	{
		mRecorded[i] = state.recorded[i];
	}
}

tresult PLUGIN_API ChunkProbe::setState (IBStream* stream)
{
	ProbeState state;
	if (readState (&state, stream) != kResultOk)
	{
		return kResultFalse;
	}
	applyState (state);
	return kResultOk;
}

tresult PLUGIN_API ChunkProbe::getState (IBStream* stream)
{
	const ProbeState state = snapshot ();
	return writeState (state, stream);
}

tresult PLUGIN_API ChunkProbe::setEditorState (IBStream* stream)
{
	return setState (stream);
}

tresult PLUGIN_API ChunkProbe::getEditorState (IBStream* stream)
{
	return getState (stream);
}

//------------------------------------------------------------------------
} // namespace ZeneChunkProbe
} // namespace Vst
} // namespace Steinberg

//------------------------------------------------------------------------
// The standard VST3 module factory. The category is kVstAudioEffectClass and
// the subcategory Vst::PlugType::kFx, so the host reads this as an effect
// (plugins/Vst3Effect/Vst3Host.cpp classifies an instrument by the
// subcategory string) and gives it the effect path.
BEGIN_FACTORY_DEF ("Zene Studio test fixture", "https://github.com/KRUZZZZY/zene-studio",
                   "mailto:test@example.invalid")

DEF_CLASS2 (INLINE_UID_FROM_FUID (Steinberg::Vst::ZeneChunkProbe::kChunkProbeUID),
            PClassInfo::kManyInstances,
            kVstAudioEffectClass,
            "Zene VST3 Chunk Probe",
            Steinberg::Vst::kDistributable,
            Steinberg::Vst::PlugType::kFx,
            "1.0.0",
            kVstVersionString,
            Steinberg::Vst::ZeneChunkProbe::ChunkProbe::createInstance)

END_FACTORY
