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

} // namespace

extern "C" LADSPA_TEST_EXPORT const LADSPA_Descriptor* ladspa_descriptor(unsigned long index)
{
	if (index == 0) { return &stereoDescriptor; }
	if (index == 1) { return &asymmetricDescriptor; }
	return nullptr;
}
