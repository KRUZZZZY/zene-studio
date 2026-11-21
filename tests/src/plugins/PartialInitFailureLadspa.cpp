#include <ladspa.h>

#include <cstddef>

namespace
{

unsigned long s_instantiations = 0;
unsigned long s_cleanups = 0;

struct Instance
{
	LADSPA_Data* input = nullptr;
	LADSPA_Data* output = nullptr;
};

LADSPA_Handle instantiate(const LADSPA_Descriptor*, unsigned long)
{
	if (++s_instantiations == 2)
	{
		return nullptr;
	}
	return new Instance;
}

void connectPort(LADSPA_Handle handle, unsigned long port, LADSPA_Data* data)
{
	auto* instance = static_cast<Instance*>(handle);
	(port == 0 ? instance->input : instance->output) = data;
}

void run(LADSPA_Handle, unsigned long)
{
}

void cleanup(LADSPA_Handle handle)
{
	++s_cleanups;
	delete static_cast<Instance*>(handle);
}

const LADSPA_PortDescriptor s_portDescriptors[] = {
	LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
};

const char* s_portNames[] = {"Input", "Output"};

const LADSPA_PortRangeHint s_portHints[] = {{0, 0, 0}, {0, 0, 0}};

const LADSPA_Descriptor s_descriptor = {
	0x7f010001,
	"partial_init_failure",
	LADSPA_PROPERTY_HARD_RT_CAPABLE,
	"Partial initialization failure fixture",
	"Zene Studio tests",
	"None",
	2,
	s_portDescriptors,
	s_portNames,
	s_portHints,
	nullptr,
	instantiate,
	connectPort,
	nullptr,
	run,
	nullptr,
	nullptr,
	nullptr,
	cleanup,
};

// A Source: one audio output and no audio input. Loading it under the effect
// wrapper must be refused before DEFAULT_CHANNELS / inputChannels is computed.
const LADSPA_PortDescriptor s_zeroInputPortDescriptors[] = {
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
};

const char* s_zeroInputPortNames[] = {"Output"};

const LADSPA_PortRangeHint s_zeroInputPortHints[] = {{0, 0, 0}};

const LADSPA_Descriptor s_zeroInputDescriptor = {
	0x7f010002,
	"zero_input_source",
	LADSPA_PROPERTY_HARD_RT_CAPABLE,
	"Zero-input source fixture",
	"Zene Studio tests",
	"None",
	1,
	s_zeroInputPortDescriptors,
	s_zeroInputPortNames,
	s_zeroInputPortHints,
	nullptr,
	instantiate,
	connectPort,
	nullptr,
	run,
	nullptr,
	nullptr,
	nullptr,
	cleanup,
};

} // namespace

#if defined(_MSC_VER)
// ladspa.h declares ladspa_descriptor without dllexport, and MSVC rejects a later definition
// that adds it (C2375); the CMake target exports the symbol with /EXPORT instead.
#define LADSPA_TEST_EXPORT
#elif defined(_WIN32)
#define LADSPA_TEST_EXPORT __declspec(dllexport)
#else
#define LADSPA_TEST_EXPORT __attribute__((visibility("default")))
#endif

extern "C" LADSPA_TEST_EXPORT void partial_init_failure_reset()
{
	s_instantiations = 0;
	s_cleanups = 0;
}

extern "C" LADSPA_TEST_EXPORT unsigned long partial_init_failure_instantiations()
{
	return s_instantiations;
}

extern "C" LADSPA_TEST_EXPORT unsigned long partial_init_failure_cleanups()
{
	return s_cleanups;
}

extern "C" LADSPA_TEST_EXPORT const LADSPA_Descriptor* ladspa_descriptor(unsigned long index)
{
	if (index == 0) { return &s_descriptor; }
	if (index == 1) { return &s_zeroInputDescriptor; }
	return nullptr;
}
