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

} // namespace

#if defined(_WIN32)
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
	return index == 0 ? &s_descriptor : nullptr;
}
