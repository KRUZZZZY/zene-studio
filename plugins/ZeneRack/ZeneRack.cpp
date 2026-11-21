/*
 * ZeneRack.cpp - the Zene Rack VST3 effect: one documented, ordered chain
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * WHAT THIS IS. The first stage of the Zene Rack product plug-in (Q050-267.1):
 * an installable VST3 audio effect that loads ONE documented chain and processes
 * it in a fixed order. The chain is the three Zene built-in effects whose DSP
 * the Rack carries itself, so the bundle does not depend on Zene's executable
 * or its plug-in module loader:
 *
 *   1. Amplifier   - linear gain (the Amplifier effect's wet gain)
 *   2. BassBooster - the FastBassBoost recurrence used by BassBooster
 *                    (frequency 100 * sampleRate / 44100, ratio 2.0)
 *   3. WaveShaper  - the Rack's documented subset: input drive then a forced
 *                    hard clip at -1..+1 (WaveShaper's Input gain and Clip
 *                    sections; the wave table and Output gain are not used)
 *
 * The order is part of the product contract: stage 1 runs before stage 2 and
 * stage 2 before stage 3 for every sample. The chain is reported three ways
 * the host can read without a Zene API: parameter names prefixed 1/2/3, the
 * read-only Chain parameter's string value, and the IUnitInfo program list
 * (and again in the saved component state).
 *
 * Stage 2 (host-state recall) and stage 3 (standalone project bridge) build on
 * this file; nothing here has to be undone for them.
 */

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include <atomic>
#include <cstdio>

namespace lmms::vst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;

//! A stable class id. Generated once for the Rack and never reused: a host
//! that stores it must resolve the same plug-in across versions.
static const FUID kZeneRackUID (0x5A6E6552, 0x61636B31, 0x9F2B4D67, 0x0C8E1A35);

//! The one documented chain, in processing order. Kept short so the program
//! name fits String128; the state and the read-only parameter carry the same
//! string.
static const char kChainDescriptor[] = "Amplifier > BassBooster > WaveShaper";

enum : ParamID
{
	kChainParam = 1000,
	kGainParam = 1001,
	kBassParam = 1002,
	kDriveParam = 1003,
	kProgramListId = 2000
};

enum : UnitID
{
	kChainUnitId = 3000
};

//! Plain ranges. They are also what the host shows in the parameter list.
static constexpr float kGainDefault = 1.0f;
static constexpr float kGainMin = 0.0f;
static constexpr float kGainMax = 4.0f;
static constexpr float kBassDefault = 1.0f;
static constexpr float kBassMin = 0.0f;
static constexpr float kBassMax = 4.0f;
static constexpr float kDriveDefault = 1.0f;
static constexpr float kDriveMin = 0.0f;
static constexpr float kDriveMax = 8.0f;

//! BassBooster's documented constants: its default frequency and ratio. The
//! frequency is scaled by the sample rate exactly as BassBoosterEffect does
//! (outputSampleRate / 44100).
static constexpr double kBassFrequency = 100.0;
static constexpr double kBassRatio = 2.0;

static constexpr int32 kStateVersion = 1;
static constexpr int32 kStateBufferSize = 256;

//! One state line per parameter. Fixed text, no locale-sensitive formatting
//! beyond %f, written off the audio thread only. Reading it back is stage 2.
static const char kStateMagic[] = "ZENERACK-STATE";

static_assert (std::atomic<float>::is_always_lock_free,
               "the Rack's parameter shadows must stay lock-free on every host platform");

//------------------------------------------------------------------------
//! The three ordered stages. Each helper is called once per sample per channel,
//! in declaration order by process(); none of them allocates or locks.
inline float amplifierStage (float sample, float gain)
{
	return sample * gain;
}

inline float bassBoosterStage (float sample, double& cap, float gain, double frequency)
{
	// BassBoosterEffect's DspEffectLibrary::FastBassBoost, unchanged:
	//   cap = (in + cap * f) / (f + 1)
	//   out = (in + cap * ratio) * gain
	const double safeFrequency = frequency < 10.0 ? 10.0 : frequency;
	const double gain1 = 1.0 / (safeFrequency + 1.0);
	cap = (static_cast<double> (sample) + cap * safeFrequency) * gain1;
	return static_cast<float> ((static_cast<double> (sample) + cap * kBassRatio) * static_cast<double> (gain));
}

inline float waveShaperStage (float sample, float drive)
{
	// WaveShaperControls defaults: Input gain, then Clip enabled at -1..+1.
	const float driven = sample * drive;
	return driven < -1.0f ? -1.0f : (driven > 1.0f ? 1.0f : driven);
}

//------------------------------------------------------------------------
//! VST3 treats 0 on a side as "leave that side unchanged"; a present side of
//! this stereo-only effect must be one stereo bus.
inline bool stereoOrUnchanged (const SpeakerArrangement* arrangements, int32 count)
{
	return count == 0
	       || (count == 1 && arrangements != nullptr && arrangements[0] == SpeakerArr::kStereo);
}

//------------------------------------------------------------------------
class ZeneRack : public SingleComponentEffect
{
public:
	ZeneRack ();

	//--- called by the factory macro ------------------------------------
	static FUnknown* createInstance (void* /*context*/)
	{
		return static_cast<IAudioProcessor*> (new ZeneRack ());
	}

	//--- IPluginBase ----------------------------------------------------
	tresult PLUGIN_API initialize (FUnknown* context) SMTG_OVERRIDE;

	//--- IComponent -----------------------------------------------------
	tresult PLUGIN_API setActive (TBool state) SMTG_OVERRIDE;
	tresult PLUGIN_API getState (IBStream* state) SMTG_OVERRIDE;

	//--- IAudioProcessor ------------------------------------------------
	tresult PLUGIN_API setupProcessing (ProcessSetup& setup) SMTG_OVERRIDE;
	tresult PLUGIN_API setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
	                                       SpeakerArrangement* outputs, int32 numOuts) SMTG_OVERRIDE;
	tresult PLUGIN_API canProcessSampleSize (int32 symbolicSampleSize) SMTG_OVERRIDE;
	tresult PLUGIN_API setProcessing (TBool state) SMTG_OVERRIDE;
	tresult PLUGIN_API process (ProcessData& data) SMTG_OVERRIDE;

	//--- IEditController ------------------------------------------------
	tresult PLUGIN_API setParamNormalized (ParamID tag, ParamValue value) SMTG_OVERRIDE;
	tresult PLUGIN_API getParamStringByValue (ParamID tag, ParamValue valueNormalized,
	                                          String128 string) SMTG_OVERRIDE;
	tresult PLUGIN_API getEditorState (IBStream* state) SMTG_OVERRIDE { return getState (state); }

private:
	//! Fold the host's last parameter point per tag into the atomic shadows.
	void applyParameterChanges (IParameterChanges& changes);
	//! Write one bus channel through the documented stages, in order.
	void processChannel (Sample32* destination, const Sample32* source, int32 frames,
	                     int32 channel, float gain, float bassGain, float drive,
	                     double bassFrequency);
	//! Walk one output bus and its matching input bus.
	void processBus (AudioBusBuffers& outputBus, const AudioBusBuffers* inputBus,
	                 int32 frames, float gain, float bassGain, float drive,
	                 double bassFrequency);
	//! Walk all output buses in the processed block.
	void processAllBuses (ProcessData& data, float gain, float bassGain, float drive,
	                      double bassFrequency);
	//! The input bus matching \a bus, or nullptr when the host supplied none.
	const AudioBusBuffers* inputBusFor (const ProcessData& data, int32 bus) const;
	//! True when every stage parameter exists (set up by initialize()).
	bool parametersReady () const;
	//! Read one stage's current plain value from its atomic shadow.
	float plainValue (RangeParameter* parameter, const std::atomic<float>& normalized) const;
	//! Write the documented text state.
	tresult writeState (IBStream* stream) const;

	RangeParameter* mGain = nullptr;
	RangeParameter* mBass = nullptr;
	RangeParameter* mDrive = nullptr;

	std::atomic<float> mGainNormalized;
	std::atomic<float> mBassNormalized;
	std::atomic<float> mDriveNormalized;

	double mSampleRate = 44100.0;
	double mBassCap[2] = {0.0, 0.0};
};

//------------------------------------------------------------------------
ZeneRack::ZeneRack ()
: mGainNormalized (0.25f) // kGainDefault in the 0..4 range
, mBassNormalized (0.25f) // kBassDefault in the 0..4 range
, mDriveNormalized (0.125f) // kDriveDefault in the 0..8 range
{
}

//------------------------------------------------------------------------
tresult PLUGIN_API ZeneRack::initialize (FUnknown* context)
{
	const tresult result = SingleComponentEffect::initialize (context);
	if (result != kResultOk)
	{
		return result;
	}

	addAudioInput (STR16 ("Audio In"), SpeakerArr::kStereo);
	addAudioOutput (STR16 ("Audio Out"), SpeakerArr::kStereo);

	// The order of these names is the supported processing order. A host's
	// generic editor shows them in this order; an agent reads the Chain
	// parameter and the program list for the same string.
	parameters.addParameter (STR16 ("Chain"), nullptr, 0, 0.0,
	                         ParameterInfo::kIsReadOnly, kChainParam);
	mGain = static_cast<RangeParameter*> (parameters.addParameter (
		new RangeParameter (STR16 ("1 Amplifier Gain"), kGainParam, STR16 ("x"),
		                    kGainMin, kGainMax, kGainDefault)));
	mBass = static_cast<RangeParameter*> (parameters.addParameter (
		new RangeParameter (STR16 ("2 BassBooster Gain"), kBassParam, STR16 ("x"),
		                    kBassMin, kBassMax, kBassDefault)));
	mDrive = static_cast<RangeParameter*> (parameters.addParameter (
		new RangeParameter (STR16 ("3 WaveShaper Drive"), kDriveParam, STR16 ("x"),
		                    kDriveMin, kDriveMax, kDriveDefault)));

	if (mGain == nullptr || mBass == nullptr || mDrive == nullptr)
	{
		return kResultFalse;
	}

	mGainNormalized.store (static_cast<float> (mGain->toNormalized (kGainDefault)),
	                       std::memory_order_relaxed);
	mBassNormalized.store (static_cast<float> (mBass->toNormalized (kBassDefault)),
	                       std::memory_order_relaxed);
	mDriveNormalized.store (static_cast<float> (mDrive->toNormalized (kDriveDefault)),
	                        std::memory_order_relaxed);

	// The program list is the second report the host can show: one chain, in
	// order, on the plug-in's own preset/unit menu.
	ProgramList* chainList = new ProgramList (STR16 ("Zene Rack chains"),
	                                          kProgramListId, kChainUnitId);
	chainList->addProgram (UString128 (kChainDescriptor));
	addProgramList (chainList);
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API ZeneRack::setActive (TBool state)
{
	if (state)
	{
		// A new activation starts from a clean filter state, so a render is
		// repeatable regardless of what the previous activation left behind.
		mBassCap[0] = 0.0;
		mBassCap[1] = 0.0;
	}
	return SingleComponentEffect::setActive (state);
}

//------------------------------------------------------------------------
tresult PLUGIN_API ZeneRack::setupProcessing (ProcessSetup& setup)
{
	mSampleRate = setup.sampleRate > 0.0 ? setup.sampleRate : 44100.0;
	return SingleComponentEffect::setupProcessing (setup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API ZeneRack::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                 SpeakerArrangement* outputs, int32 numOuts)
{
	// VST3 treats 0 on a side as "leave that side unchanged". The Rack has one
	// stereo input bus and one stereo output bus, so a present side must be
	// that stereo pair; 0/0 is a probe, not a refusal.
	if (numIns < 0 || numOuts < 0)
	{
		return kInvalidArgument;
	}
	if (numIns > 1 || numOuts > 1)
	{
		return kResultFalse;
	}
	if (!stereoOrUnchanged (inputs, numIns) || !stereoOrUnchanged (outputs, numOuts))
	{
		return kResultFalse;
	}
	return SingleComponentEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
}

//------------------------------------------------------------------------
tresult PLUGIN_API ZeneRack::canProcessSampleSize (int32 symbolicSampleSize)
{
	return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API ZeneRack::setProcessing (TBool /*state*/)
{
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API ZeneRack::setParamNormalized (ParamID tag, ParamValue value)
{
	const tresult result = SingleComponentEffect::setParamNormalized (tag, value);
	if (result != kResultOk)
	{
		return result;
	}
	const float clamped = static_cast<float> (
		value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value));
	switch (tag)
	{
	case kGainParam: mGainNormalized.store (clamped, std::memory_order_relaxed); break;
	case kBassParam: mBassNormalized.store (clamped, std::memory_order_relaxed); break;
	case kDriveParam: mDriveNormalized.store (clamped, std::memory_order_relaxed); break;
	default: break;
	}
	return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API ZeneRack::getParamStringByValue (ParamID tag, ParamValue valueNormalized,
                                                    String128 string)
{
	if (tag == kChainParam)
	{
		UString128 (kChainDescriptor).copyTo (string, 128);
		return kResultOk;
	}
	return SingleComponentEffect::getParamStringByValue (tag, valueNormalized, string);
}

//------------------------------------------------------------------------
void ZeneRack::applyParameterChanges (IParameterChanges& changes)
{
	const int32 count = changes.getParameterCount ();
	for (int32 i = 0; i < count; ++i)
	{
		IParamValueQueue* queue = changes.getParameterData (i);
		if (queue == nullptr)
		{
			continue;
		}
		const int32 points = queue->getPointCount ();
		if (points <= 0)
		{
			continue;
		}
		ParamValue value = 0.0;
		int32 sampleOffset = 0;
		if (queue->getPoint (points - 1, sampleOffset, value) != kResultOk)
		{
			continue;
		}
		const float clamped = static_cast<float> (
			value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value));
		switch (queue->getParameterId ())
		{
		case kGainParam: mGainNormalized.store (clamped, std::memory_order_relaxed); break;
		case kBassParam: mBassNormalized.store (clamped, std::memory_order_relaxed); break;
		case kDriveParam: mDriveNormalized.store (clamped, std::memory_order_relaxed); break;
		default: break;
		}
	}
}

//------------------------------------------------------------------------
void ZeneRack::processChannel (Sample32* destination, const Sample32* source, int32 frames,
                               int32 channel, float gain, float bassGain, float drive,
                               double bassFrequency)
{
	const int capIndex = channel < 2 ? channel : 1;
	for (int32 frame = 0; frame < frames; ++frame)
	{
		const float input = source != nullptr ? source[frame] : 0.0f;
		// The documented order: Amplifier, BassBooster, WaveShaper.
		float sample = amplifierStage (input, gain);
		sample = bassBoosterStage (sample, mBassCap[capIndex], bassGain, bassFrequency);
		destination[frame] = waveShaperStage (sample, drive);
	}
}

//------------------------------------------------------------------------
void ZeneRack::processBus (AudioBusBuffers& outputBus, const AudioBusBuffers* inputBus,
                           int32 frames, float gain, float bassGain, float drive,
                           double bassFrequency)
{
	for (int32 channel = 0; channel < outputBus.numChannels; ++channel)
	{
		Sample32* destination = outputBus.channelBuffers32[channel];
		if (destination == nullptr)
		{
			continue;
		}
		const Sample32* source =
		    (inputBus != nullptr && channel < inputBus->numChannels)
		        ? inputBus->channelBuffers32[channel]
		        : nullptr;
		processChannel (destination, source, frames, channel, gain, bassGain, drive, bassFrequency);
	}
	outputBus.silenceFlags = 0;
}

//------------------------------------------------------------------------
bool ZeneRack::parametersReady () const
{
	return mGain != nullptr && mBass != nullptr && mDrive != nullptr;
}

//------------------------------------------------------------------------
float ZeneRack::plainValue (RangeParameter* parameter,
                            const std::atomic<float>& normalized) const
{
	return static_cast<float> (
	    parameter->toPlain (static_cast<ParamValue> (normalized.load (std::memory_order_relaxed))));
}

//------------------------------------------------------------------------
const AudioBusBuffers* ZeneRack::inputBusFor (const ProcessData& data, int32 bus) const
{
	if (data.inputs == nullptr || bus >= data.numInputs)
	{
		return nullptr;
	}
	return data.inputs[bus].channelBuffers32 != nullptr ? &data.inputs[bus] : nullptr;
}

//------------------------------------------------------------------------
void ZeneRack::processAllBuses (ProcessData& data, float gain, float bassGain, float drive,
                                double bassFrequency)
{
	const int32 outputCount = data.outputs != nullptr ? data.numOutputs : 0;
	for (int32 bus = 0; bus < outputCount; ++bus)
	{
		AudioBusBuffers& outputBus = data.outputs[bus];
		if (outputBus.channelBuffers32 == nullptr)
		{
			continue;
		}
		processBus (outputBus, inputBusFor (data, bus), data.numSamples,
		            gain, bassGain, drive, bassFrequency);
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API ZeneRack::process (ProcessData& data)
{
	// Real-time contract: this function reads three atomics per block, walks the
	// caller's buffers and writes them. It does not allocate, free, lock, wait,
	// do I/O or emit a signal; the bass filter state is two doubles owned by
	// the plug-in and reset in setActive().
	if (data.numSamples <= 0 || data.symbolicSampleSize != kSample32)
	{
		return kResultOk;
	}
	if (data.inputParameterChanges != nullptr)
	{
		applyParameterChanges (*data.inputParameterChanges);
	}
	if (!parametersReady ())
	{
		return kResultOk;
	}

	processAllBuses (data,
	                 plainValue (mGain, mGainNormalized),
	                 plainValue (mBass, mBassNormalized),
	                 plainValue (mDrive, mDriveNormalized),
	                 kBassFrequency * mSampleRate / 44100.0);
	return kResultOk;
}

//------------------------------------------------------------------------
tresult ZeneRack::writeState (IBStream* stream) const
{
	if (stream == nullptr)
	{
		return kInvalidArgument;
	}
	if (!parametersReady ())
	{
		return kResultFalse;
	}
	const float gain = static_cast<float> (
		mGain->toPlain (static_cast<ParamValue> (mGainNormalized.load (std::memory_order_relaxed))));
	const float bass = static_cast<float> (
		mBass->toPlain (static_cast<ParamValue> (mBassNormalized.load (std::memory_order_relaxed))));
	const float drive = static_cast<float> (
		mDrive->toPlain (static_cast<ParamValue> (mDriveNormalized.load (std::memory_order_relaxed))));

	char text[kStateBufferSize];
	const int written = std::snprintf (text, sizeof (text),
	                                   "%s %d\nchain=%s\ngain=%f\nbass=%f\ndrive=%f\n",
	                                   kStateMagic, kStateVersion,
	                                   kChainDescriptor, gain, bass, drive);
	if (written <= 0 || written >= static_cast<int> (sizeof (text)))
	{
		return kResultFalse;
	}
	int32 bytes = 0;
	const tresult result = stream->write (text, written, &bytes);
	return result == kResultOk && bytes == written ? kResultOk : kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API ZeneRack::getState (IBStream* stream)
{
	return writeState (stream);
}

//------------------------------------------------------------------------
} // namespace lmms::vst3

//------------------------------------------------------------------------
// The standard VST3 module factory. kVstAudioEffectClass plus PlugType::kFx
// is what an external host reads as an audio effect.
BEGIN_FACTORY_DEF ("Zene Studio", "https://github.com/KRUZZZZY/zene-studio",
                   "mailto:issues@zene.invalid")

DEF_CLASS2 (INLINE_UID_FROM_FUID (lmms::vst3::kZeneRackUID),
            PClassInfo::kManyInstances,
            kVstAudioEffectClass,
            "Zene Rack",
            Steinberg::Vst::kDistributable,
            Steinberg::Vst::PlugType::kFx,
            "1.0.0",
            kVstVersionString,
            lmms::vst3::ZeneRack::createInstance)

END_FACTORY
