/*
 * lv2-test-instrument.c - a minimal LV2 INSTRUMENT that reports a latency
 *
 * The LV2 subject of InstrumentLatencyPdcTest (MX-157, BUGS 11.34), the twin of
 * tests/data/clap-test-plugin/clap-test-instrument.c and
 * tests/data/vst3-test-instrument/vst3-test-instrument.cpp for the LV2 host
 * (plugins/Lv2Instrument). It is a real LV2 plug-in - a bundle with a manifest,
 * loaded through lilv - and a GENERATOR:
 *
 *   port 0  atom:Sequence input carrying midi:MidiEvent
 *   port 1  audio output (left)
 *   port 2  audio output (right)
 *   port 3  control output, lv2:reportsLatency: the latency, in frames
 *
 * While a key is held it writes a CONSTANT level (TEST_LEVEL) on both outputs,
 * sample-accurately from the event's frame, so a test reads exact values.
 *
 * LATENCY. An instance created while the environment variable
 * LV2_TEST_INSTRUMENT_LATENCY names a frame count is LATENT: it writes that
 * count to its latency port and really delays its output by it (a ring of the
 * per-frame level). LV2 lets a plug-in change its latency at any time - the
 * port is the report - so a latent instance re-reads the variable at every
 * run(). (Reading the environment in run() is a test-fixture shortcut, not
 * something a real plug-in would do on its audio thread.) An instance created
 * without the variable reports 0 and is not delayed.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/midi/midi.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>

#define TEST_URI "urn:zene:test:lv2-instrument"
#define TEST_LEVEL 0.25f
/* The largest latency a latent instance takes, and the size of its ring. */
#define TEST_MAX_LATENCY 16384u
#define TEST_LATENCY_ENV "LV2_TEST_INSTRUMENT_LATENCY"
/* The state this plug-in saves (a string), the key it accepts only as that string, and the key it
 * REFUSES: its restore returns an error status for any state that names it. */
#define TEST_ACCEPTED_KEY "urn:zene:test:accepted"
#define TEST_REFUSED_KEY "urn:zene:test:refuse"
#define TEST_PATH_KEY "urn:zene:test:path"

enum
{
	PORT_MIDI = 0,
	PORT_OUT_LEFT = 1,
	PORT_OUT_RIGHT = 2,
	PORT_LATENCY = 3,
};

typedef struct test_instrument
{
	const LV2_Atom_Sequence* midi;
	float* out[2];
	float* latency_port;
	LV2_URID midi_event;
	LV2_URID accepted_key;
	LV2_URID refused_key;
	LV2_URID path_key;
	LV2_URID string_type;
	LV2_URID path_type;
	int latent;
	uint32_t latency;
	int held;          /* keys down */
	uint32_t ring_pos;
	float ring[TEST_MAX_LATENCY];
} test_instrument_t;

/* The count the environment names now, one short of the ring (a whole-ring delay reads the frame just written). */
static uint32_t env_latency(void)
{
	const char* frames = getenv(TEST_LATENCY_ENV);
	const long value = frames != NULL ? atol(frames) : 0;
	if (value <= 0) { return 0; }
	return value >= (long)TEST_MAX_LATENCY ? TEST_MAX_LATENCY - 1u : (uint32_t)value;
}

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path,
	const LV2_Feature* const* features)
{
	(void)descriptor;
	(void)rate;
	(void)bundle_path;
	const LV2_URID_Map* map = NULL;
	for (int i = 0; features != NULL && features[i] != NULL; ++i)
	{
		if (!strcmp(features[i]->URI, LV2_URID__map)) { map = (const LV2_URID_Map*)features[i]->data; }
	}
	if (map == NULL) { return NULL; }
	test_instrument_t* self = (test_instrument_t*)calloc(1, sizeof(*self));
	if (self == NULL) { return NULL; }
	self->midi_event = map->map(map->handle, LV2_MIDI__MidiEvent);
	self->accepted_key = map->map(map->handle, TEST_ACCEPTED_KEY);
	self->refused_key = map->map(map->handle, TEST_REFUSED_KEY);
	self->path_key = map->map(map->handle, TEST_PATH_KEY);
	self->string_type = map->map(map->handle, LV2_ATOM__String);
	self->path_type = map->map(map->handle, LV2_ATOM__Path);
	self->latent = getenv(TEST_LATENCY_ENV) != NULL;
	self->latency = self->latent ? env_latency() : 0;
	return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data)
{
	test_instrument_t* self = (test_instrument_t*)instance;
	switch (port)
	{
	case PORT_MIDI: self->midi = (const LV2_Atom_Sequence*)data; break;
	case PORT_OUT_LEFT: self->out[0] = (float*)data; break;
	case PORT_OUT_RIGHT: self->out[1] = (float*)data; break;
	case PORT_LATENCY: self->latency_port = (float*)data; break;
	default: break;
	}
}

static void activate(LV2_Handle instance)
{
	test_instrument_t* self = (test_instrument_t*)instance;
	self->held = 0;
	self->ring_pos = 0;
	memset(self->ring, 0, sizeof(self->ring));
}

/* Frames [from, to) of the level, through the ring when the instance is latent. */
static void render(test_instrument_t* self, uint32_t from, uint32_t to)
{
	for (uint32_t f = from; f < to; ++f)
	{
		float value = self->held > 0 ? TEST_LEVEL : 0.0f;
		if (self->latency > 0)
		{
			self->ring[self->ring_pos] = value;
			value = self->ring[(self->ring_pos + TEST_MAX_LATENCY - self->latency) % TEST_MAX_LATENCY];
			self->ring_pos = (self->ring_pos + 1) % TEST_MAX_LATENCY;
		}
		if (self->out[0] != NULL) { self->out[0][f] = value; }
		if (self->out[1] != NULL) { self->out[1][f] = value; }
	}
}

static void note(test_instrument_t* self, const uint8_t* msg)
{
	switch (lv2_midi_message_type(msg))
	{
	case LV2_MIDI_MSG_NOTE_ON:
		/* a note-on with velocity 0 is a note-off */
		if (msg[2] > 0) { ++self->held; }
		else if (self->held > 0) { --self->held; }
		break;
	case LV2_MIDI_MSG_NOTE_OFF:
		if (self->held > 0) { --self->held; }
		break;
	default:
		break;
	}
}

static void run(LV2_Handle instance, uint32_t frames)
{
	test_instrument_t* self = (test_instrument_t*)instance;
	if (self->latent) { self->latency = env_latency(); }
	uint32_t position = 0;
	if (self->midi != NULL)
	{
		LV2_ATOM_SEQUENCE_FOREACH(self->midi, event)
		{
			if (event->body.type != self->midi_event) { continue; }
			uint32_t at = event->time.frames < 0 ? 0u : (uint32_t)event->time.frames;
			if (at > frames) { at = frames; }
			if (at > position)
			{
				render(self, position, at);
				position = at;
			}
			note(self, (const uint8_t*)(event + 1));
		}
	}
	render(self, position, frames);
	if (self->latency_port != NULL) { *self->latency_port = (float)self->latency; }
}

static void deactivate(LV2_Handle instance)
{
	(void)instance;
}

static void cleanup(LV2_Handle instance)
{
	free(instance);
}

/* LV2_State_Interface: the plug-in saves one string property; restore accepts it only as the string it
 * saved and returns an error status for a state that names the refused key. */
static LV2_State_Status save(LV2_Handle instance, LV2_State_Store_Function store, LV2_State_Handle handle,
	uint32_t flags, const LV2_Feature* const* features)
{
	const test_instrument_t* self = (const test_instrument_t*)instance;
	static const char value[] = "1";
	(void)flags;
	(void)features;
	return store(handle, self->accepted_key, value, sizeof(value), self->string_type,
		LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
}

static LV2_State_Status restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve, LV2_State_Handle handle,
	uint32_t flags, const LV2_Feature* const* features)
{
	const test_instrument_t* self = (const test_instrument_t*)instance;
	size_t size = 0;
	uint32_t type = 0;
	uint32_t value_flags = 0;
	const LV2_State_Map_Path* map_path = NULL;
	const LV2_State_Free_Path* free_path = NULL;
	(void)flags;
	for (int i = 0; features != NULL && features[i] != NULL; ++i)
	{
		if (!strcmp(features[i]->URI, LV2_STATE__mapPath)) { map_path = (const LV2_State_Map_Path*)features[i]->data; }
		if (!strcmp(features[i]->URI, LV2_STATE__freePath)) { free_path = (const LV2_State_Free_Path*)features[i]->data; }
	}
	/* A path property needs both host path features, and the mapped result must be freed through freePath. */
	const void* path = retrieve(handle, self->path_key, &size, &type, &value_flags);
	if (path != NULL)
	{
		if (type != self->path_type) { return LV2_STATE_ERR_BAD_TYPE; }
		if (map_path == NULL || map_path->absolute_path == NULL || free_path == NULL || free_path->free_path == NULL)
		{
			return LV2_STATE_ERR_NO_FEATURE;
		}
		char* mapped_path = map_path->absolute_path(map_path->handle, (const char*)path);
		if (mapped_path == NULL) { return LV2_STATE_ERR_UNKNOWN; }
		free_path->free_path(free_path->handle, mapped_path);
	}
	/* A state that names the refused key is syntactically valid, and this plug-in refuses it. */
	if (retrieve(handle, self->refused_key, &size, &type, &value_flags) != NULL) { return LV2_STATE_ERR_UNKNOWN; }
	/* The accepted key must arrive as the string it was saved as; a decode that loses it fails here. */
	const void* accepted = retrieve(handle, self->accepted_key, &size, &type, &value_flags);
	if (accepted != NULL && (type != self->string_type || size == 0 || ((const char*)accepted)[0] != '1'))
	{
		return LV2_STATE_ERR_BAD_TYPE;
	}
	return LV2_STATE_SUCCESS;
}

static const LV2_State_Interface s_state = {
	save,
	restore,
};

static const void* extension_data(const char* uri)
{
	return strcmp(uri, LV2_STATE__interface) == 0 ? &s_state : NULL;
}

static const LV2_Descriptor s_descriptor = {
	TEST_URI,
	instantiate,
	connect_port,
	activate,
	run,
	deactivate,
	cleanup,
	extension_data,
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
	return index == 0 ? &s_descriptor : NULL;
}
