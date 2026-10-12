/* A minimal LADSPA fixture for the Ports dialog's sample-rate hint display. */

#include <ladspa.h>

namespace
{

const LADSPA_PortDescriptor portDescriptors[] = {
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL
};

const char* const portNames[] = {
	"Bound-derived default",
	"Fixed default"
};

const LADSPA_PortRangeHint rangeHints[] = {
	{
		LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE
			| LADSPA_HINT_SAMPLE_RATE | LADSPA_HINT_DEFAULT_MIDDLE,
		0.25f,
		0.75f
	},
	{
		LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE
			| LADSPA_HINT_SAMPLE_RATE | LADSPA_HINT_DEFAULT_440,
		0.001f,
		0.02f
	}
};

const LADSPA_Descriptor descriptor = {
	1234567,
	"sample_rate_display_defaults",
	0,
	"Sample-rate display defaults",
	"Zene Studio tests",
	"None",
	2,
	portDescriptors,
	portNames,
	rangeHints,
	nullptr
};

} // namespace

#if defined(_WIN32)
#define LADSPA_FIXTURE_EXPORT __declspec(dllexport)
#else
#define LADSPA_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" LADSPA_FIXTURE_EXPORT const LADSPA_Descriptor* ladspa_descriptor(unsigned long index)
{
	return index == 0 ? &descriptor : nullptr;
}
