/*
 * vst3-test-instrument.cpp - a minimal VST3 INSTRUMENT, as an in-tree test subject
 *
 * SPDX-License-Identifier: MIT
 *
 * WHY THIS FILE EXISTS
 * -------------------
 * The VST3 host in this tree (plugins/Vst3Effect) hosts effects. Instrument
 * hosting needs a MIDI event path that does not exist yet: ProcessData::
 * inputEvents is never assigned (plugins/Vst3Effect/Vst3Host.cpp:573-581), so
 * no instrument can be driven even if one were installed. There is also no
 * third-party VST3 instrument on this machine and no way to install one
 * without sudo.
 *
 * This file is the missing subject: a real VST3 module that declares itself
 * as an instrument (Vst::PlugType::kInstrumentSynth, not an effect category),
 * declares one kEvent input bus, and - the point of the exercise - READS note
 * events out of ProcessData::inputEvents and renders audio in response. When
 * the host lane wires the event bus up, this fixture is what proves it.
 *
 * It mirrors tests/data/clap-test-plugin/clap-test-gain.c for the CLAP host:
 * a purpose-built, deterministic, in-tree plug-in that exists so the host can
 * be exercised without a third-party binary.
 *
 * DESIGN NOTES
 * ------------
 * - Built on the pinned MIT-licensed VST3 SDK (cmake/modules/Vst3Sdk.cmake,
 *   tag v3.8.1_build_84). It is derived from Vst::SingleComponentEffect, the
 *   SDK's own base class for a plug-in whose processor and edit controller are
 *   one component. That exercises the host's single-component branch
 *   (Vst3Host.cpp:295-299) as well.
 * - The synthesiser renders a CONSTANT level for as long as any note is held,
 *   instead of a real waveform. A host-level test can then assert exact sample
 *   values - "the first half of the block is non-zero and the second half is
 *   exactly zero after the note-off at sampleOffset N/2" - with no dependence
 *   on DSP coincidence.
 * - The process path is real-time safe: no allocation, no locking, no
 *   unbounded growth. All state is fixed-size members and the only input is
 *   the host's event list and its audio buffers.
 */

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include "pluginterfaces/base/fstrdefs.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"

namespace Steinberg {
namespace Vst {
namespace ZeneTestInstrument {

//------------------------------------------------------------------------
// A stable, documented class UID. Generated once for this fixture and never
// reused: a host that stores it must resolve the same class across versions.
static const FUID kTestInstrumentUID (0x5A1E57B0, 0x9C4D42E7, 0xB0C3F81A, 0x6D2E4A90);

// One automatable parameter, so the fixture exercises the parameter surface a
// real instrument has.
enum
{
	kLevelParam = 100
};

static const float kDefaultLevel = 0.5f;
static const float kTestStateMagic = 0x5A4E5431; // 'ZNT1'
static const int32 kNumKeys = 128;

//------------------------------------------------------------------------
/** Minimal polyphonic-ish note gate: tracks which keys are down. */
class TestInstrument : public SingleComponentEffect
{
public:
	TestInstrument ();

	//--- called by the factory macro ------------------------------------
	static FUnknown* createInstance (void* /*context*/)
	{
		return static_cast<IAudioProcessor*> (new TestInstrument ());
	}

	//--- IPluginBase ----------------------------------------------------
	tresult PLUGIN_API initialize (FUnknown* context) SMTG_OVERRIDE;
	tresult PLUGIN_API terminate () SMTG_OVERRIDE;

	//--- IComponent -----------------------------------------------------
	tresult PLUGIN_API setActive (TBool state) SMTG_OVERRIDE;
	tresult PLUGIN_API setState (IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API getState (IBStream* state) SMTG_OVERRIDE;

	//--- IAudioProcessor ------------------------------------------------
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

	//--- non-interface helpers (also used by the probe) -----------------
	/** Number of keys currently down. */
	int32 activeNoteCount () const { return mActiveNotes; }

private:
	/** Fold one host event into the note gate. No allocation. */
	void applyEvent (const Event& event);

	/** Write one constant value over [from, to) on every output channel. */
	void renderSegment (ProcessData& data, int32 from, int32 to);

	float mLevel;
	bool mGate[kNumKeys];
	int32 mActiveNotes;
	bool mProcessing;
};

//------------------------------------------------------------------------
TestInstrument::TestInstrument ()
: mLevel (kDefaultLevel)
, mActiveNotes (0)
, mProcessing (false)
{
	for (int32 i = 0; i < kNumKeys; ++i)
	{
		mGate[i] = false;
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API TestInstrument::initialize (FUnknown* context)
{
	tresult result = SingleComponentEffect::initialize (context);
	if (result != kResultOk)
	{
		return result;
	}

	// An instrument: NO audio input, one stereo audio output, one event input.
	// An effect would add an audio input here; this asymmetry is exactly what
	// the host has to handle and what the probe asserts.
	addAudioOutput (STR16 ("Audio Output"), SpeakerArr::kStereo);
	addEventInput (STR16 ("Event Input"), 1);

	parameters.addParameter (STR16 ("Level"), nullptr, /*flags*/ 0, kDefaultLevel,
	                         ParameterInfo::kCanAutomate, kLevelParam);
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API TestInstrument::terminate ()
{
	return SingleComponentEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API TestInstrument::setActive (TBool state)
{
	// Not the audio thread: the host calls this from prepare()/release().
	if (!state)
	{
		for (int32 i = 0; i < kNumKeys; ++i)
		{
			mGate[i] = false;
		}
		mActiveNotes = 0;
	}
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API TestInstrument::setParamNormalized (ParamID tag, ParamValue value)
{
	tresult result = SingleComponentEffect::setParamNormalized (tag, value);
	if (result == kResultOk && tag == kLevelParam)
	{
		mLevel = static_cast<float> (value);
	}
	return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API TestInstrument::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                       SpeakerArrangement* outputs, int32 numOuts)
{
	// Only the one arrangement the fixture declares: no audio input, one
	// stereo output.
	if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo)
	{
		return SingleComponentEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
	}
	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API TestInstrument::canProcessSampleSize (int32 symbolicSampleSize)
{
	return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API TestInstrument::setProcessing (TBool state)
{
	mProcessing = state != 0;
	return kResultOk;
}

//------------------------------------------------------------------------
void TestInstrument::applyEvent (const Event& event)
{
	switch (event.type)
	{
		case Event::kNoteOnEvent:
		{
			const int32 key = event.noteOn.pitch & (kNumKeys - 1);
			// A note-on with zero velocity is the MIDI idiom for note-off.
			const bool down = event.noteOn.velocity > 0.0f;
			if (down && !mGate[key])
			{
				mGate[key] = true;
				++mActiveNotes;
			}
			else if (!down && mGate[key])
			{
				mGate[key] = false;
				--mActiveNotes;
			}
			break;
		}
		case Event::kNoteOffEvent:
		{
			const int32 key = event.noteOff.pitch & (kNumKeys - 1);
			if (mGate[key])
			{
				mGate[key] = false;
				--mActiveNotes;
			}
			break;
		}
		default:
			// All other event types are ignored by this fixture.
			break;
	}
}

//------------------------------------------------------------------------
void TestInstrument::renderSegment (ProcessData& data, int32 from, int32 to)
{
	if (data.outputs == nullptr || data.numOutputs < 1)
	{
		return;
	}
	AudioBusBuffers& outBus = data.outputs[0];
	if (from >= to || outBus.numChannels <= 0)
	{
		return;
	}

	// Constant level while at least one key is down, exact zero otherwise.
	const Sample32 value = mActiveNotes > 0 ? static_cast<Sample32> (mLevel) : 0.0f;

	for (int32 channel = 0; channel < outBus.numChannels; ++channel)
	{
		Sample32* dst = outBus.channelBuffers32 ? outBus.channelBuffers32[channel] : nullptr;
		if (dst == nullptr)
		{
			continue;
		}
		for (int32 s = from; s < to; ++s)
		{
			dst[s] = value;
		}
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API TestInstrument::process (ProcessData& data)
{
	if (data.numSamples <= 0 || data.symbolicSampleSize != kSample32)
	{
		return kResultOk;
	}

	const int32 numSamples = data.numSamples;
	const int32 numEvents = data.inputEvents != nullptr ? data.inputEvents->getEventCount () : 0;

	// Walk the block in segments delimited by event offsets, so a note-off at
	// sampleOffset N/2 silences the block from N/2 onwards and not a sample
	// earlier or later. Each event is fetched exactly once; no allocation.
	int32 position = 0;
	for (int32 i = 0; i < numEvents; ++i)
	{
		Event event;
		if (data.inputEvents->getEvent (i, event) != kResultOk)
		{
			continue;
		}

		int32 offset = event.sampleOffset;
		if (offset < 0)
		{
			offset = 0;
		}
		else if (offset > numSamples)
		{
			offset = numSamples;
		}

		if (offset > position)
		{
			renderSegment (data, position, offset);
			position = offset;
		}
		applyEvent (event);
	}
	if (position < numSamples)
	{
		renderSegment (data, position, numSamples);
	}

	// We always write the output buffers, so nothing is left silent by
	// accident: report the real state rather than leaving the host's flags.
	if (data.outputs != nullptr && data.numOutputs >= 1)
	{
		const int32 channels = data.outputs[0].numChannels;
		if (mActiveNotes > 0)
		{
			data.outputs[0].silenceFlags = 0;
		}
		else
		{
			const uint64 allSilent =
			    channels >= 64 ? ~static_cast<uint64> (0) : ((static_cast<uint64> (1) << channels) - 1);
			data.outputs[0].silenceFlags = allSilent;
		}
	}

	return kResultOk;
}

//------------------------------------------------------------------------
// Component state and edit-controller state both carry the same two fields.
// Kept separate on purpose: the host saves and restores BOTH
// (Vst3Host.cpp:448-500), so the fixture has to answer both calls.
static tresult writeState (float level, IBStream* state)
{
	if (state == nullptr)
	{
		return kInvalidArgument;
	}
	const float magic = kTestStateMagic;
	int32 written = 0;
	if (state->write (const_cast<float*> (&magic), sizeof (float), &written) != kResultOk ||
	    written != static_cast<int32> (sizeof (float)))
	{
		return kResultFalse;
	}
	if (state->write (&level, sizeof (float), &written) != kResultOk ||
	    written != static_cast<int32> (sizeof (float)))
	{
		return kResultFalse;
	}
	return kResultOk;
}

static tresult readState (float* level, IBStream* state)
{
	if (state == nullptr || level == nullptr)
	{
		return kInvalidArgument;
	}
	float magic = 0.0f;
	int32 read = 0;
	if (state->read (&magic, sizeof (float), &read) != kResultOk ||
	    read != static_cast<int32> (sizeof (float)) || magic != kTestStateMagic)
	{
		return kResultFalse;
	}
	if (state->read (level, sizeof (float), &read) != kResultOk ||
	    read != static_cast<int32> (sizeof (float)))
	{
		return kResultFalse;
	}
	return kResultOk;
}

tresult PLUGIN_API TestInstrument::setState (IBStream* state)
{
	return readState (&mLevel, state);
}

tresult PLUGIN_API TestInstrument::getState (IBStream* state)
{
	return writeState (mLevel, state);
}

tresult PLUGIN_API TestInstrument::setEditorState (IBStream* state)
{
	return readState (&mLevel, state);
}

tresult PLUGIN_API TestInstrument::getEditorState (IBStream* state)
{
	return writeState (mLevel, state);
}

//------------------------------------------------------------------------
} // namespace ZeneTestInstrument
} // namespace Vst
} // namespace Steinberg

//------------------------------------------------------------------------
// The standard VST3 module factory.
//
// The category is kVstAudioEffectClass (every VST3 component registers under
// it, instruments included) and the SUBCATEGORY is Vst::PlugType::
// kInstrumentSynth - "Instrument|Synth". That subcategory string is what the
// host reads to tell an instrument from an effect
// (plugins/Vst3Effect/Vst3Host.cpp:244, :304), so this is the declaration that
// makes the fixture an instrument rather than an effect.
BEGIN_FACTORY_DEF ("Zene Studio test fixture", "https://github.com/KRUZZZZY/zene-studio",
                   "mailto:test@example.invalid")

DEF_CLASS2 (INLINE_UID_FROM_FUID (Steinberg::Vst::ZeneTestInstrument::kTestInstrumentUID),
            PClassInfo::kManyInstances,
            kVstAudioEffectClass,
            "Zene VST3 Test Instrument",
            Steinberg::Vst::kDistributable,
            Steinberg::Vst::PlugType::kInstrumentSynth,
            "1.0.0",
            kVstVersionString,
            Steinberg::Vst::ZeneTestInstrument::TestInstrument::createInstance)

END_FACTORY
