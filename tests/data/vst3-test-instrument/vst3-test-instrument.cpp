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
 * - LATENCY (MX-157, BUGS 11.34): an instance created while the environment
 *   variable VST3_TEST_INSTRUMENT_LATENCY names a frame count is LATENT. It
 *   reports that count from getLatencySamples() and really delays its output
 *   by it (a fixed ring per channel), so a host's delay compensation can be
 *   measured on rendered audio. When process() sees the variable name a new
 *   count, it switches to it and tells the host with
 *   restartComponent(kLatencyChanged). An instance created without the variable
 *   is exactly the fixture it always was. (Reading the environment in process()
 *   is a test-fixture shortcut, not something a real plug-in would do there.)
 * - FAILED PREPARE: while the environment variable
 *   VST3_TEST_INSTRUMENT_FAIL_PREPARE is set, setupProcessing() refuses the
 *   setup. That is the subject of the host's failed-prepare path: the host
 *   turns it into a failed prepare() and a warning, and a host that has no
 *   retry policy asks again on every poll tick. The variable is read per
 *   setupProcessing() call, so a test can switch it on and off around the
 *   moment it needs.
 */

#include <cstdio>
#include <cstdlib>

#include <cstring>

#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include "pluginterfaces/base/fstrdefs.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"

namespace Steinberg {
namespace Vst {
namespace ZeneTestInstrument {

//------------------------------------------------------------------------
// A stable, documented class UID. Generated once for this fixture and never
// reused: a host that stores it must resolve the same class across versions.
#if defined(ZENE_TEST_INSTRUMENT_MONO)
static const FUID kTestInstrumentUID (0x5A1E57B1, 0x9C4D42E7, 0xB0C3F81A, 0x6D2E4A90);
constexpr SpeakerArrangement kOutputArrangement = SpeakerArr::kMono;
#define ZENE_TEST_INSTRUMENT_CLASS_NAME "Zene VST3 Mono Test Instrument"
#else
static const FUID kTestInstrumentUID (0x5A1E57B0, 0x9C4D42E7, 0xB0C3F81A, 0x6D2E4A90);
constexpr SpeakerArrangement kOutputArrangement = SpeakerArr::kStereo;
#define ZENE_TEST_INSTRUMENT_CLASS_NAME "Zene VST3 Test Instrument"
#endif

// One automatable parameter, so the fixture exercises the parameter surface a
// real instrument has, plus one READ-ONLY parameter (a meter): a host must not
// present it as an editable control.
enum
{
	kLevelParam = 100,
	kMeterParam = 101
};

static const float kDefaultLevel = 0.5f;
static const float kTestStateMagic = static_cast<float> (0x5A4E5431); // 'ZNT1' (explicit: MSVC C4305)
static const int32 kNumKeys = 128;
// The largest latency a latent instance takes, and the size of its delay rings.
static const int32 kMaxLatency = 16384;
static const char* const kLatencyEnv = "VST3_TEST_INSTRUMENT_LATENCY";
// The environment that makes this fixture REFUSE to be prepared (setupProcessing
// fails), so a host can be shown a plug-in whose preparation keeps failing.
static const char* const kFailPrepareEnv = "VST3_TEST_INSTRUMENT_FAIL_PREPARE";

// The count the environment names now, one short of the ring (a whole-ring delay would read the frame just
// written).
static int32 envLatency ()
{
	const char* frames = std::getenv (kLatencyEnv);
	const long value = frames != nullptr ? std::atol (frames) : 0;
	if (value <= 0)
	{
		return 0;
	}
	return value >= kMaxLatency ? kMaxLatency - 1 : static_cast<int32> (value);
}

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
	tresult PLUGIN_API setupProcessing (ProcessSetup& setup) SMTG_OVERRIDE;
	tresult PLUGIN_API setProcessing (TBool state) SMTG_OVERRIDE;
	tresult PLUGIN_API process (ProcessData& data) SMTG_OVERRIDE;
	uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE { return static_cast<uint32> (mLatency); }

	//--- IEditController (renamed to *EditorState by SingleComponentEffect)
	tresult PLUGIN_API setEditorState (IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API getEditorState (IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API setParamNormalized (ParamID tag, ParamValue value) SMTG_OVERRIDE;

	//--- R4.2: the editor the host embeds --------------------------------
	IPlugView* PLUGIN_API createView (FIDString name) SMTG_OVERRIDE;

	//--- non-interface helpers (also used by the probe) -----------------
	/** Number of keys currently down. */
	int32 activeNoteCount () const { return mActiveNotes; }

private:
	/** Fold one host event into the note gate. No allocation. */
	void applyEvent (const Event& event);

	/** Write one constant value over [from, to) on every output channel. */
	void renderSegment (ProcessData& data, int32 from, int32 to);

	/** A latent instance: follow a new count, then delay the block by it. No allocation. */
	void applyLatency (ProcessData& data);
	/** A latent instance: take the count the environment names now; tell the host when it moved. */
	void followLatency ();
	/** Delay the first two channels of @a outBus by mLatency, through the rings. */
	void delayChannels (AudioBusBuffers& outBus, int32 numSamples);

	float mLevel;
	bool mGate[kNumKeys];
	int32 mActiveNotes;
	bool mProcessing;
	bool mLatent;
	int32 mLatency;
	int32 mRingPos;
	float mRing[2][kMaxLatency];
};

//------------------------------------------------------------------------
TestInstrument::TestInstrument ()
: mLevel (kDefaultLevel)
, mActiveNotes (0)
, mProcessing (false)
, mLatent (std::getenv (kLatencyEnv) != nullptr)
, mLatency (mLatent ? envLatency () : 0)
, mRingPos (0)
{
	for (int32 i = 0; i < kNumKeys; ++i)
	{
		mGate[i] = false;
	}
	std::memset (mRing, 0, sizeof (mRing));
}

//------------------------------------------------------------------------
void TestInstrument::applyLatency (ProcessData& data)
{
	if (!mLatent)
	{
		return;
	}
	followLatency ();
	if (mLatency == 0 || data.outputs == nullptr || data.numOutputs < 1)
	{
		return;
	}
	delayChannels (data.outputs[0], data.numSamples);
}

//------------------------------------------------------------------------
void TestInstrument::followLatency ()
{
	const int32 wanted = envLatency ();
	if (wanted == mLatency)
	{
		return;
	}
	mLatency = wanted;
	if (componentHandler != nullptr)
	{
		componentHandler->restartComponent (kLatencyChanged);
	}
}

//------------------------------------------------------------------------
void TestInstrument::delayChannels (AudioBusBuffers& outBus, int32 numSamples)
{
	const int32 channels = outBus.numChannels < 2 ? outBus.numChannels : 2;
	for (int32 s = 0; s < numSamples; ++s)
	{
		const int32 read = (mRingPos + kMaxLatency - mLatency) % kMaxLatency;
		for (int32 channel = 0; channel < channels; ++channel)
		{
			Sample32* dst = outBus.channelBuffers32 ? outBus.channelBuffers32[channel] : nullptr;
			if (dst == nullptr)
			{
				continue;
			}
			mRing[channel][mRingPos] = dst[s];
			dst[s] = mRing[channel][read];
		}
		mRingPos = (mRingPos + 1) % kMaxLatency;
	}
	outBus.silenceFlags = 0;
}

//------------------------------------------------------------------------
tresult PLUGIN_API TestInstrument::initialize (FUnknown* context)
{
	tresult result = SingleComponentEffect::initialize (context);
	if (result != kResultOk)
	{
		return result;
	}

	// An instrument: NO audio input, one audio output, one event input.
	// An effect would add an audio input here; this asymmetry is exactly what
	// the host has to handle and what the probe asserts.
	addAudioOutput (STR16 ("Audio Output"), kOutputArrangement);
	addEventInput (STR16 ("Event Input"), 1);

	parameters.addParameter (STR16 ("Level"), nullptr, /*flags*/ 0, kDefaultLevel,
	                         ParameterInfo::kCanAutomate, kLevelParam);
	// A visible read-only parameter: the plug-in owns its value, so a host must
	// not offer it as an editable knob. SDK argument order is
	// (title, units, stepCount, default, flags, tag).
	parameters.addParameter (STR16 ("Meter"), nullptr, /*stepCount*/ 0, 0.0,
	                         ParameterInfo::kIsReadOnly, kMeterParam);
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
	// Only the one arrangement this fixture variant declares.
	if (numIns == 0 && numOuts == 1 && outputs[0] == kOutputArrangement)
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
tresult PLUGIN_API TestInstrument::setupProcessing (ProcessSetup& setup)
{
	// A plug-in that REFUSES to be set up: the environment names it. This is
	// the subject of the host's failed-prepare path (Vst3Instrument's re-prepare
	// retry policy), so it has to be a real setupProcessing() failure - the
	// host turns it into a failed prepare() and a warning.
	if (std::getenv (kFailPrepareEnv) != nullptr)
	{
		return kResultFalse;
	}
	return SingleComponentEffect::setupProcessing (setup);
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
	applyLatency (data);

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
// R4.2: the fixture's EDITOR - what a real plug-in's view does with its host, minus the
// drawing: it embeds in an X11 window (an HWND or an NSView too, Q050-490), resizes, and on attach asks the frame for
// Linux::IRunLoop and registers a 10 ms timer (a repaint timer), unregistering it on
// The two Linux interfaces' UIDs, spelled here: the SDK defines their iids only in its Linux build
// (commoniids.cpp, #if SMTG_OS_LINUX), so naming Linux::...::iid failed to link elsewhere.
static const TUID kTimerHandlerIid = INLINE_UID (0x10BDD94F, 0x41424774, 0x821FAD8F, 0xECA72CA9);
static const TUID kRunLoopIid = INLINE_UID (0x18C35366, 0x97764F1A, 0x9C5B8385, 0x7A871389);

// removal. The host's side is plugins/Vst3Effect/Vst3EditorSession.cpp.
class RepaintTimer : public Linux::ITimerHandler
{
public:
	virtual ~RepaintTimer () = default;
	void PLUGIN_API onTimer () SMTG_OVERRIDE { ++ticks; }
	tresult PLUGIN_API queryInterface (const TUID iid, void** obj) SMTG_OVERRIDE
	{
		if (FUnknownPrivate::iidEqual (iid, kTimerHandlerIid) || FUnknownPrivate::iidEqual (iid, FUnknown::iid))
		{
			*obj = this;
			addRef ();
			return kResultOk;
		}
		*obj = nullptr;
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef () SMTG_OVERRIDE { return ++refCount; }
	uint32 PLUGIN_API release () SMTG_OVERRIDE
	{
		const uint32 count = --refCount;
		if (count == 0) delete this;
		return count;
	}
	uint32 ticks {0};

private:
	uint32 refCount {1};
};

// 0.5.0 MX-094: the editor also scales itself, as a real scalable editor does through
// IPlugViewContentScaleSupport - its size follows the factor and it asks the host for that size
// through the frame. VST3_TEST_EDITOR_NO_CONTENT_SCALE set in the environment withholds the
// interface, so the host's other path (it sizes the window itself) can be exercised too.
class TestEditorView : public CPluginView, public IPlugViewContentScaleSupport
{
public:
	TestEditorView () : CPluginView (&initialSize ()) {}
	~TestEditorView () override { if (timer) timer->release (); }

	tresult PLUGIN_API setContentScaleFactor (ScaleFactor factor) SMTG_OVERRIDE
	{
		if (factor <= 0.f) return kInvalidArgument;
		ViewRect scaled (0, 0, static_cast<int32> (initialSize ().getWidth () * factor + 0.5f),
		                 static_cast<int32> (initialSize ().getHeight () * factor + 0.5f));
		if (plugFrame) return plugFrame->resizeView (this, &scaled);
		setRect (scaled);
		return kResultTrue;
	}
	tresult PLUGIN_API queryInterface (const TUID iid, void** obj) SMTG_OVERRIDE
	{
		if (std::getenv ("VST3_TEST_EDITOR_NO_CONTENT_SCALE") == nullptr &&
		    FUnknownPrivate::iidEqual (iid, IPlugViewContentScaleSupport::iid))
		{
			addRef ();
			*obj = static_cast<IPlugViewContentScaleSupport*> (this);
			return kResultOk;
		}
		return CPluginView::queryInterface (iid, obj);
	}
	REFCOUNT_METHODS (CPluginView)

	// Q050-490: X11, HWND and NSView, as a real cross-platform editor offers; VST3_TEST_EDITOR_PLATFORM
	// names the only one it takes instead, so the host's refusal of the others can be exercised.
	tresult PLUGIN_API isPlatformTypeSupported (FIDString type) SMTG_OVERRIDE
	{
		if (type == nullptr) return kResultFalse;
		if (const char* only = std::getenv ("VST3_TEST_EDITOR_PLATFORM"))
			return strcmp (type, only) == 0 ? kResultTrue : kResultFalse;
		return strcmp (type, kPlatformTypeX11EmbedWindowID) == 0 || strcmp (type, kPlatformTypeHWND) == 0 ||
		               strcmp (type, kPlatformTypeNSView) == 0
		           ? kResultTrue
		           : kResultFalse;
	}
	tresult PLUGIN_API canResize () SMTG_OVERRIDE { return kResultTrue; }
	tresult PLUGIN_API checkSizeConstraint (ViewRect* rect) SMTG_OVERRIDE
	{
		// Never smaller than 100 x 80: a real editor has a minimum, and the host must honour it.
		if (rect->getWidth () < 100) rect->right = rect->left + 100;
		if (rect->getHeight () < 80) rect->bottom = rect->top + 80;
		return kResultTrue;
	}
	tresult PLUGIN_API attached (void* parent, FIDString type) SMTG_OVERRIDE
	{
		// Attached with the type the host asked about, or not at all.
		if (parent == nullptr || isPlatformTypeSupported (type) != kResultTrue) return kResultFalse;
		const tresult attachedResult = CPluginView::attached (parent, type);
		// Q050-490: VST3_TEST_EDITOR_REQUEST_RESIZE="<w>x<h>" - the editor asks the host for a new size
		// through the frame once attached, the way a plug-in's own resize handle does.
		if (const char* request = std::getenv ("VST3_TEST_EDITOR_REQUEST_RESIZE"))
		{
			int width = 0;
			int height = 0;
			if (plugFrame && std::sscanf (request, "%dx%d", &width, &height) == 2)
			{
				ViewRect wanted (0, 0, width, height);
				plugFrame->resizeView (this, &wanted);
			}
		}
		Linux::IRunLoop* loop = nullptr;
		if (plugFrame && plugFrame->queryInterface (kRunLoopIid, reinterpret_cast<void**> (&loop)) == kResultOk)
		{
			runLoop = loop;
			timer = new RepaintTimer;
			runLoop->registerTimer (timer, 10);
		}
		return attachedResult;
	}
	tresult PLUGIN_API removed () SMTG_OVERRIDE
	{
		if (runLoop)
		{
			runLoop->unregisterTimer (timer);
			runLoop->release ();
			runLoop = nullptr;
		}
		return CPluginView::removed ();
	}

private:
	//! ViewRect is not a literal type, so the initial size is a function-local static.
	static const ViewRect& initialSize ()
	{
		static const ViewRect size (0, 0, 400, 300);
		return size;
	}
	Linux::IRunLoop* runLoop {nullptr};
	RepaintTimer* timer {nullptr};
};

IPlugView* PLUGIN_API TestInstrument::createView (FIDString name)
{
	if (name != nullptr && strcmp (name, ViewType::kEditor) == 0) return new TestEditorView;
	return nullptr;
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
            ZENE_TEST_INSTRUMENT_CLASS_NAME,
            Steinberg::Vst::kDistributable,
            Steinberg::Vst::PlugType::kInstrumentSynth,
            "1.0.0",
            kVstVersionString,
            Steinberg::Vst::ZeneTestInstrument::TestInstrument::createInstance)

END_FACTORY
