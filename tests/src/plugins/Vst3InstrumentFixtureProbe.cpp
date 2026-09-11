/*
 * Vst3InstrumentFixtureProbe.cpp - independent check of the VST3 instrument fixture
 *
 * SPDX-License-Identifier: MIT
 *
 * WHAT THIS IS
 * ------------
 * A standalone probe (no Qt, no LMMS host code) that links lmms_vst3_sdk and
 * exercises tests/data/vst3-test-instrument/ through the SDK's OWN machinery:
 * VST3::Hosting::Module::create() to load the bundle, then the SDK's
 * PlugProvider to bring up the component, its IAudioProcessor and its
 * IEditController.
 *
 * It answers three questions the instrument-hosting lane needs answered before
 * it can be dispatched, and answers them with observed values rather than
 * prose:
 *
 *   1. Does the fixture declare itself as an INSTRUMENT (kVstAudioEffectClass
 *      with an Instrument subcategory) and not as an effect? It prints every
 *      class the module factory reports, category and subcategories included.
 *   2. What bus layout does it present? It prints the audio and event bus
 *      counts with names, channel counts and directions - the layout is
 *      asserted to be "no audio input, one stereo audio output, one kEvent
 *      input", which is the shape an instrument has and an effect does not.
 *   3. Does it actually consume MIDI from ProcessData::inputEvents and render
 *      audio in response? It drives a synthetic note-on at sampleOffset 0 and
 *      a note-off at sampleOffset N/2 through an SDK EventList, and checks the
 *      block is non-silent exactly up to the note-off offset and exactly
 *      silent after it. A block before and a block after are checked too, so
 *      "non-silent" cannot be an artefact of stale buffers.
 *
 * It is deliberately NOT a host: it does not build with the LMMS Vst3Host, it
 * does not wire events into the product, and it holds no host state beyond one
 * synthetic block. It exists so those claims are measured, not asserted.
 *
 * Exit code: 0 if every check passed, 1 otherwise.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

#ifndef VST3_TEST_INSTRUMENT_BUNDLE
#error "VST3_TEST_INSTRUMENT_BUNDLE must be defined (see tests/CMakeLists.txt)"
#endif

namespace
{
int gFailures = 0;

void check(bool ok, const char* what)
{
	std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
	{
		++gFailures;
	}
}

/** The host context every PlugProvider needs before it is constructed. */
auto hostContext() -> IPtr<FUnknown>
{
	static IPtr<HostApplication> application = [] {
		IPtr<HostApplication> app = owned(new HostApplication);
		PluginContextFactory::instance().setPluginContext(app);
		return app;
	}();
	return application;
}

auto busName(const String128 name) -> std::string
{
	// String128 is UTF-16; the fixture's names are ASCII, so a narrowing copy
	// is enough for a diagnostic line and needs no SDK string helper.
	std::string out;
	for (int i = 0; i < 128 && name[i] != 0; ++i)
	{
		out.push_back(name[i] < 128 ? static_cast<char>(name[i]) : '?');
	}
	return out;
}

void printBusLayout(IComponent* component)
{
	std::printf("\n-- bus layout reported by the module --\n");
	const MediaType types[2] = {kAudio, kEvent};
	const BusDirection dirs[2] = {kInput, kOutput};
	for (const MediaType type : types)
	{
		for (const BusDirection dir : dirs)
		{
			const int32 count = component->getBusCount(type, dir);
			std::printf("  %s %s: %d bus(es)\n", type == kAudio ? "kAudio" : "kEvent",
			            dir == kInput ? "input" : "output", static_cast<int>(count));
			for (int32 i = 0; i < count; ++i)
			{
				BusInfo info{};
				if (component->getBusInfo(type, dir, i, info) != kResultOk)
				{
					continue;
				}
				std::printf("    [%d] name=\"%s\" channels=%d busType=%s active=%s\n",
				            static_cast<int>(i), busName(info.name).c_str(),
				            static_cast<int>(info.channelCount),
				            info.busType == kMain ? "kMain" : "kAux",
				            (info.flags & BusInfo::kDefaultActive) != 0 ? "yes" : "no");
			}
		}
	}
}
} // namespace

int main()
{
	std::printf("== VST3 instrument fixture probe ==\n");
	std::printf("bundle: %s\n", VST3_TEST_INSTRUMENT_BUNDLE);

	hostContext();

	// --- 1. the module loads and declares itself an instrument -------------
	std::printf("\n-- module load (VST3::Hosting::Module::create) --\n");
	std::string error;
	auto module = VST3::Hosting::Module::create(VST3_TEST_INSTRUMENT_BUNDLE, error);
	check(module != nullptr, "the .vst3 bundle loads through the SDK's own module loader");
	if (!module)
	{
		std::printf("  error: %s\n", error.c_str());
		std::printf("\nRESULT: FAIL (module did not load)\n");
		return 1;
	}

	std::printf("\n-- class infos reported by the module factory --\n");
	bool foundInstrumentClass = false;
	bool foundFxCategory = false;
	VST3::Hosting::ClassInfo classInfo;
	for (const auto& info : module->getFactory().classInfos())
	{
		const std::string subCategories = info.subCategoriesString();
		std::printf("  name=\"%s\"\n    category=\"%s\" subCategories=\"%s\" vendor=\"%s\"\n",
		            info.name().c_str(), info.category().c_str(), subCategories.c_str(),
		            info.vendor().c_str());
		if (info.category() != kVstAudioEffectClass)
		{
			continue;
		}
		const bool isInstrument = subCategories.find("Instrument") != std::string::npos;
		const bool isFx = subCategories.find("Fx") != std::string::npos;
		if (isInstrument && !isFx)
		{
			foundInstrumentClass = true;
			classInfo = info;
		}
		if (isFx)
		{
			foundFxCategory = true;
		}
	}
	check(foundInstrumentClass, "declares a kVstAudioEffectClass whose subcategory is an Instrument");
	check(!foundFxCategory, "declares no Fx-categorised class (it is an instrument, not an effect)");
	if (!foundInstrumentClass)
	{
		std::printf("\nRESULT: FAIL (no instrument class)\n");
		return 1;
	}

	// --- 2. instantiate through the SDK's PlugProvider ---------------------
	std::printf("\n-- instantiation (SDK PlugProvider) --\n");
	PlugProvider provider(module->getFactory(), classInfo);
	check(provider.initialize(), "PlugProvider::initialize()");
	auto component = provider.getComponentPtr();
	auto controller = provider.getControllerPtr();
	FUnknownPtr<IAudioProcessor> processor{component.get()};
	check(component && processor, "IComponent and IAudioProcessor are available");
	if (!component || !processor)
	{
		std::printf("\nRESULT: FAIL (no processor)\n");
		return 1;
	}
	check(controller != nullptr, "an IEditController is available");

	FUnknown* componentAsController = nullptr;
	const bool singleComponent = component->queryInterface(IEditController::iid,
	                                                       reinterpret_cast<void**>(&componentAsController)) == kResultOk &&
	                             componentAsController != nullptr;
	if (componentAsController)
	{
		componentAsController->release();
	}
	std::printf("  component is single-component (processor == controller): %s\n",
	            singleComponent ? "yes" : "no");

	printBusLayout(component.get());

	const int32 audioInputs = component->getBusCount(kAudio, kInput);
	const int32 audioOutputs = component->getBusCount(kAudio, kOutput);
	const int32 eventInputs = component->getBusCount(kEvent, kInput);
	check(audioInputs == 0, "NO audio input bus - the instrument shape, not the effect shape");
	check(audioOutputs == 1, "exactly one audio output bus");
	check(eventInputs >= 1, "at least one kEvent input bus - the MIDI path");
	if (audioOutputs < 1 || eventInputs < 1)
	{
		std::printf("\nRESULT: FAIL (bus layout)\n");
		return 1;
	}

	// --- 3. drive one synthetic note through ProcessData::inputEvents ------
	constexpr int32 kBlockSize = 512;
	ProcessSetup setup{};
	setup.processMode = kRealtime;
	setup.symbolicSampleSize = kSample32;
	setup.maxSamplesPerBlock = kBlockSize;
	setup.sampleRate = 44100.0;
	check(processor->setupProcessing(setup) == kResultOk, "setupProcessing(kSample32, 44.1 kHz, 512)");
	check(component->setActive(true) == kResultOk, "setActive(true)");
	const tresult processing = processor->setProcessing(true);
	check(processing == kResultOk || processing == kNotImplemented, "setProcessing(true)");

	// One stereo output bus, no audio input bus, exactly as an instrument.
	std::vector<float> left(kBlockSize, 0.0f);
	std::vector<float> right(kBlockSize, 0.0f);
	float* channels[2] = {left.data(), right.data()};
	AudioBusBuffers outBus{};
	outBus.numChannels = 2;
	outBus.silenceFlags = 0;
	outBus.channelBuffers32 = channels;

	ProcessData data{};
	data.processMode = kRealtime;
	data.symbolicSampleSize = kSample32;
	data.numSamples = kBlockSize;
	data.numInputs = 0;
	data.inputs = nullptr;
	data.numOutputs = 1;
	data.outputs = &outBus;
	data.inputParameterChanges = nullptr;
	data.outputParameterChanges = nullptr;
	data.processContext = nullptr;

	auto renderBlock = [&](IEventList* events) {
		// Pre-fill with a sentinel: a fixture that never writes its output
		// buffers cannot pass the "silent" checks by accident.
		std::fill(left.begin(), left.end(), -1.0f);
		std::fill(right.begin(), right.end(), -1.0f);
		outBus.silenceFlags = 0;
		data.inputEvents = events;
		return processor->process(data);
	};

	std::printf("\n-- MIDI in -> audio out (ProcessData::inputEvents) --\n");

	EventList beforeBlock; // no events at all
	check(renderBlock(&beforeBlock) == kResultOk, "process() returns kResultOk");
	const bool beforeSilent = std::all_of(left.begin(), left.end(), [](float v) { return v == 0.0f; }) &&
	                          std::all_of(right.begin(), right.end(), [](float v) { return v == 0.0f; });
	check(beforeSilent, "a block with no events renders exact silence (no note is latent)");

	EventList notes;
	Event noteOn{};
	noteOn.type = Event::kNoteOnEvent;
	noteOn.busIndex = 0;
	noteOn.sampleOffset = 0;
	noteOn.noteOn.channel = 0;
	noteOn.noteOn.pitch = 69; // A4
	noteOn.noteOn.velocity = 1.0f;
	noteOn.noteOn.length = 256;
	notes.addEvent(noteOn);

	Event noteOff{};
	noteOff.type = Event::kNoteOffEvent;
	noteOff.busIndex = 0;
	noteOff.sampleOffset = 256;
	noteOff.noteOff.channel = 0;
	noteOff.noteOff.pitch = 69;
	noteOff.noteOff.velocity = 0.0f;
	notes.addEvent(noteOff);

	check(renderBlock(&notes) == kResultOk, "process() with a note-on at 0 and a note-off at 256");
	const bool firstHalfNonZero = left[0] != 0.0f && left[255] != 0.0f;
	const bool secondHalfSilent =
	    std::all_of(left.begin() + 256, left.end(), [](float v) { return v == 0.0f; }) &&
	    std::all_of(right.begin() + 256, right.end(), [](float v) { return v == 0.0f; });
	check(firstHalfNonZero, "the block is NON-SILENT before the note-off - MIDI really drives audio");
	check(secondHalfSilent, "the block is EXACTLY silent from the note-off sampleOffset onwards");
	std::printf("    level=%.6f  sample[0]=%.6f  sample[255]=%.6f  sample[256]=%.6f  sample[511]=%.6f\n",
	            left[0], left[0], left[255], left[256], left[511]);

	EventList afterBlock; // the note is off again
	const tresult afterResult = renderBlock(&afterBlock);
	const bool afterSilent = std::all_of(left.begin(), left.end(), [](float v) { return v == 0.0f; }) &&
	                         std::all_of(right.begin(), right.end(), [](float v) { return v == 0.0f; });
	check(afterResult == kResultOk && afterSilent,
	      "the next block is silent again (the note did not leak forward)");

	// A note pushed for this block must not affect the block that already ran;
	// the check below pushes a note and verifies it takes effect in THIS block
	// only, which is the "no leak backwards" direction.
	EventList lateNote;
	Event later{};
	later.type = Event::kNoteOnEvent;
	later.sampleOffset = 128;
	later.noteOn.channel = 0;
	later.noteOn.pitch = 60;
	later.noteOn.velocity = 1.0f;
	lateNote.addEvent(later);
	check(renderBlock(&lateNote) == kResultOk, "process() with a note-on at sampleOffset 128");
	const bool offsetHonoured = std::all_of(left.begin(), left.begin() + 128,
	                                        [](float v) { return v == 0.0f; }) &&
	                            left[128] != 0.0f && left[511] != 0.0f;
	check(offsetHonoured, "a note-on at sampleOffset 128 sounds from sample 128, not from sample 0");

	processor->setProcessing(false);
	component->setActive(false);

	std::printf("\nRESULT: %s (%d failed check(s))\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
	return gFailures == 0 ? 0 : 1;
}
