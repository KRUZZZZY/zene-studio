/*
 * LadspaPortCountFixture.cpp - synthetic descriptors for LadspaManagerTest
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <ladspa.h>

#include <array>
#include <climits>
#include <cstdint>
#include <limits>

namespace
{

constexpr unsigned long PortTotal = 65536UL;

auto makePorts(LADSPA_PortDescriptor _port) -> std::array<LADSPA_PortDescriptor, PortTotal>
{
	std::array<LADSPA_PortDescriptor, PortTotal> ports{};
	ports.fill(_port);
	return ports;
}

auto makeNames(const char* _name) -> std::array<const char*, PortTotal>
{
	std::array<const char*, PortTotal> names{};
	names.fill(_name);
	return names;
}

const auto ControlPorts = makePorts(LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL);
const auto ControlNames = makeNames("Control");
const auto AudioPorts = makePorts(LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO);
const auto AudioNames = makeNames("IN");

auto descriptor(unsigned long _id, const char* _label, const char* _name,
		unsigned long _portCount, const LADSPA_PortDescriptor* _ports,
		const char* const* _names) -> LADSPA_Descriptor
{
	LADSPA_Descriptor result{};
	result.UniqueID = _id;
	result.Label = _label;
	result.Name = _name;
	result.Maker = "Zene test fixture";
	result.Copyright = "None";
	result.PortCount = _portCount;
	result.PortDescriptors = _ports;
	result.PortNames = _names;
	return result;
}

const LADSPA_Descriptor ControlDescriptor = descriptor(91001,
	"zene_test_control_65536", "Control ports at 65536", PortTotal,
	ControlPorts.data(), ControlNames.data());
const LADSPA_Descriptor AudioDescriptor = descriptor(91002,
	"zene_test_audio_65536", "Audio ports at 65536", PortTotal,
	AudioPorts.data(), AudioNames.data());

#if ULONG_MAX > UINT32_MAX
const LADSPA_Descriptor OverflowDescriptor = descriptor(91003,
	"zene_test_port_count_overflow", "Port count beyond host width",
	static_cast<unsigned long>(std::numeric_limits<std::uint32_t>::max()) + 1UL,
	nullptr, nullptr);
#endif

} // namespace

#if defined(_WIN32)
#define LADSPA_TEST_EXPORT __declspec(dllexport)
#else
#define LADSPA_TEST_EXPORT __attribute__((visibility("default")))
#endif

extern "C" LADSPA_TEST_EXPORT const LADSPA_Descriptor* ladspa_descriptor(unsigned long _index)
{
	switch (_index)
	{
	case 0: return &ControlDescriptor;
	case 1: return &AudioDescriptor;
#if ULONG_MAX > UINT32_MAX
	case 2: return &OverflowDescriptor;
#endif
	default: return nullptr;
	}
}
