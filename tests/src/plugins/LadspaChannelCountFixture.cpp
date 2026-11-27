/*
 * LadspaChannelCountFixture.cpp - deterministic LADSPA channel-count fixture
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "ladspa.h"

#if defined(_MSC_VER)
// ladspa.h declares ladspa_descriptor without dllexport, and MSVC rejects a later definition
// that adds it (C2375); the CMake target exports the symbol with /EXPORT instead.
#define LADSPA_TEST_EXPORT
#elif defined(_WIN32)
#define LADSPA_TEST_EXPORT __declspec(dllexport)
#else
#define LADSPA_TEST_EXPORT __attribute__((visibility("default")))
#endif

namespace
{

const LADSPA_PortDescriptor stereoPorts[]{
	LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL};
const char* const stereoPortNames[]{"Left", "Right", "Left", "Right", "Input gain"};
const LADSPA_PortRangeHint stereoHints[5]{};
const LADSPA_Descriptor stereoDescriptor{
	170021UL, "left_right_stereo", 0, "Left Right Stereo Fixture", "", "None",
	5, stereoPorts, stereoPortNames, stereoHints, nullptr};

const LADSPA_PortDescriptor asymmetricPorts[]{
	LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO};
const char* const asymmetricPortNames[]{"Input", "Left", "Right"};
const LADSPA_PortRangeHint asymmetricHints[3]{};
const LADSPA_Descriptor asymmetricDescriptor{
	170022UL, "input_left_right", 0, "Asymmetric Channel Fixture", "", "None",
	3, asymmetricPorts, asymmetricPortNames, asymmetricHints, nullptr};

// A mono-in/stereo-out transfer. The wrapper makes DEFAULT_CHANNELS / 1 = 2
// processors and maps every audio port named with "OUT" onto the next mixer
// channel, so this layout asks for four channels in a two-channel bus. The
// plugin is INPLACE_BROKEN so each output gets its own port buffer and the
// out-of-range writes can be observed without buffer sharing.
struct MonoInStereoOutInstance
{
	LADSPA_Data* input = nullptr;
	LADSPA_Data* left = nullptr;
	LADSPA_Data* right = nullptr;
};

LADSPA_Handle monoInStereoOutInstantiate(const LADSPA_Descriptor*, unsigned long)
{
	return new MonoInStereoOutInstance;
}

void monoInStereoOutConnectPort(LADSPA_Handle handle, unsigned long port, LADSPA_Data* data)
{
	auto* instance = static_cast<MonoInStereoOutInstance*>(handle);
	switch (port)
	{
	case 0: instance->input = data; break;
	case 1: instance->left = data; break;
	case 2: instance->right = data; break;
	default: break;
	}
}

void monoInStereoOutRun(LADSPA_Handle handle, unsigned long sampleCount)
{
	auto* instance = static_cast<MonoInStereoOutInstance*>(handle);
	for (unsigned long sample = 0; sample < sampleCount; ++sample)
	{
		const LADSPA_Data in = instance->input == nullptr ? 0.0f : instance->input[sample];
		instance->left[sample] = in + 1.0f;
		instance->right[sample] = in + 2.0f;
	}
}

void monoInStereoOutCleanup(LADSPA_Handle handle)
{
	delete static_cast<MonoInStereoOutInstance*>(handle);
}

const LADSPA_PortDescriptor monoInStereoOutPorts[]{
	LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO};
const char* const monoInStereoOutPortNames[]{"Input", "Output Left", "Output Right"};
const LADSPA_PortRangeHint monoInStereoOutHints[3]{};
const LADSPA_Descriptor monoInStereoOutDescriptor{
	170023UL,
	"mono_in_stereo_out",
	LADSPA_PROPERTY_HARD_RT_CAPABLE | LADSPA_PROPERTY_INPLACE_BROKEN,
	"Mono In Stereo Out Fixture",
	"Zene Studio tests",
	"None",
	3,
	monoInStereoOutPorts,
	monoInStereoOutPortNames,
	monoInStereoOutHints,
	nullptr,
	monoInStereoOutInstantiate,
	monoInStereoOutConnectPort,
	nullptr,
	monoInStereoOutRun,
	nullptr,
	nullptr,
	nullptr,
	monoInStereoOutCleanup};

// A valid four-in/two-out transfer. The wrapper splits the effect bus over the
// plugin's audio inputs (DEFAULT_CHANNELS / inputChannels processors), so four
// inputs make 2 / 4 = 0 processors: a layout that fits nowhere on the stereo bus.
struct QuadInStereoOutInstance
{
	LADSPA_Data* inputs[4] = {nullptr, nullptr, nullptr, nullptr};
	LADSPA_Data* left = nullptr;
	LADSPA_Data* right = nullptr;
};

LADSPA_Handle quadInStereoOutInstantiate(const LADSPA_Descriptor*, unsigned long)
{
	return new QuadInStereoOutInstance;
}

void quadInStereoOutConnectPort(LADSPA_Handle handle, unsigned long port, LADSPA_Data* data)
{
	auto* instance = static_cast<QuadInStereoOutInstance*>(handle);
	if (port < 4) { instance->inputs[port] = data; }
	else if (port == 4) { instance->left = data; }
	else if (port == 5) { instance->right = data; }
}

void quadInStereoOutRun(LADSPA_Handle handle, unsigned long sampleCount)
{
	auto* instance = static_cast<QuadInStereoOutInstance*>(handle);
	for (unsigned long sample = 0; sample < sampleCount; ++sample)
	{
		LADSPA_Data sum = 0.0f;
		for (LADSPA_Data* input : instance->inputs)
		{
			sum += input == nullptr ? 0.0f : input[sample];
		}
		instance->left[sample] = sum;
		instance->right[sample] = sum;
	}
}

void quadInStereoOutCleanup(LADSPA_Handle handle)
{
	delete static_cast<QuadInStereoOutInstance*>(handle);
}

const LADSPA_PortDescriptor quadInStereoOutPorts[]{
	LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO};
const char* const quadInStereoOutPortNames[]{
	"Input 1", "Input 2", "Input 3", "Input 4", "Output Left", "Output Right"};
const LADSPA_PortRangeHint quadInStereoOutHints[6]{};
const LADSPA_Descriptor quadInStereoOutDescriptor{
	170024UL,
	"quad_in_stereo_out",
	LADSPA_PROPERTY_HARD_RT_CAPABLE,
	"Quad In Stereo Out Fixture",
	"Zene Studio tests",
	"None",
	6,
	quadInStereoOutPorts,
	quadInStereoOutPortNames,
	quadInStereoOutHints,
	nullptr,
	quadInStereoOutInstantiate,
	quadInStereoOutConnectPort,
	nullptr,
	quadInStereoOutRun,
	nullptr,
	nullptr,
	nullptr,
	quadInStereoOutCleanup};

} // namespace

extern "C" LADSPA_TEST_EXPORT const LADSPA_Descriptor* ladspa_descriptor(unsigned long index)
{
	if (index == 0) { return &stereoDescriptor; }
	if (index == 1) { return &asymmetricDescriptor; }
	if (index == 2) { return &monoInStereoOutDescriptor; }
	if (index == 3) { return &quadInStereoOutDescriptor; }
	return nullptr;
}
