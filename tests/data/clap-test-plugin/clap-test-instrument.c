/*
 * clap-test-instrument.c - a minimal, complete CLAP INSTRUMENT (generator)
 *
 * Built from the pinned CLAP 1.2.10 headers (MIT) as the test subject for the
 * CLAP instrument path of the LMMS native host (plugins/ClapEffect +
 * plugins/ClapInstrument, feature row 79 / board task #669). It is a real CLAP
 * plug-in, and its shape is a GENERATOR's:
 *
 *   clap.note-ports   one note INPUT port, CLAP dialect preferred
 *   clap.audio-ports  NO audio input port, one stereo output port
 *   clap.params       one parameter ("Gain")
 *   clap.state        its counters and the gain, so a test can read back what
 *                     the host actually delivered
 *
 * It is deliberately NOT a musical synthesiser: while a voice is sounding it
 * writes a CONSTANT level (`gain * 0.25`), so a test can assert the exact
 * amplitude of "this note reached the plug-in and produced audio" instead of
 * inferring it from a waveform. What is under test is the note path - the
 * note ports, the input event list, the frame offset, the release - not the
 * synthesis. Note-off starts a linear release over TEST_INSTRUMENT_RELEASE
 * frames; note-choke silences immediately.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <clap/clap.h>

#define TEST_INSTRUMENT_ID "org.lmms.test.clap-instrument"
#define TEST_INSTRUMENT_NAME "LMMS CLAP Test Instrument"
#define TEST_INSTRUMENT_MAGIC 0x4C4D494Eu /* "LMIN" */
/* Bump when the state layout below changes; the host stores the bytes
   opaquely. The test mirrors this struct and static_asserts its size. */
#define TEST_INSTRUMENT_STATE_VERSION 1u
/* How long a note takes to fade after its note-off, in frames. */
#define TEST_INSTRUMENT_RELEASE 256u
/* The level one sounding voice writes, before the gain parameter. */
#define TEST_INSTRUMENT_LEVEL 0.25

enum
{
	PARAM_GAIN = 1,
	NOTE_PORT_ID = 1,
	AUDIO_PORT_ID = 1,
};

/* The plug-in's own account of what it was asked to do, saved through
   clap.state so the test reads it rather than guessing from the audio. Only
   8-byte and 4-byte members, the doubles first, so the layout has no padding
   and the test's mirror of it is byte-exact. */
typedef struct instrument_state
{
	double frames_processed;   /* frames written since activate() */
	double gain;

	uint32_t magic;
	uint32_t version;
	uint32_t declared_max_frames; /* what activate() was told */
	uint32_t blocks;              /* process() calls since activate() */
	uint32_t max_block;           /* largest frames_count seen */
	uint32_t oversized_calls;     /* calls with frames_count > declared_max_frames */
	uint32_t event_count;         /* every event in every input list */
	uint32_t param_event_count;   /* CLAP_EVENT_PARAM_VALUE events */
	uint32_t note_on_count;
	uint32_t note_off_count;
	uint32_t choke_count;
	uint32_t ignored_event_count; /* events that are neither of the above */
	uint32_t active_voices;       /* voices sounding at the last process() */
	uint32_t release_left;        /* frames left of the release, 0 when silent */
	uint32_t last_velocity_milli; /* last note-on velocity * 1000 */
	int32_t last_key;
	int32_t last_channel;
	int32_t last_port_index;      /* clap_event_note_t::port_index as received */
	int32_t first_note_time;      /* header.time of the first note-on, -1 if none */
	int32_t last_note_time;       /* header.time of the most recent note */
} instrument_state_t;

typedef struct instrument_plugin
{
	clap_plugin_t plugin;
	const clap_host_t* host;
	const clap_plugin_params_t* params;
	double gain;
	double frames_processed;
	uint32_t declared_max_frames;
	uint32_t blocks;
	uint32_t max_block;
	uint32_t oversized_calls;
	uint32_t event_count;
	uint32_t param_event_count;
	uint32_t note_on_count;
	uint32_t note_off_count;
	uint32_t choke_count;
	uint32_t ignored_event_count;
	uint32_t active_voices;
	uint32_t last_velocity_milli;
	int32_t last_key;
	int32_t last_channel;
	int32_t last_port_index;
	int32_t first_note_time;
	int32_t last_note_time;
	/* the sounding voice */
	int sounding;          /* a note-on has arrived and no note-off yet */
	uint32_t release_left; /* frames left of the release, 0 when silent */
} instrument_plugin_t;

static const clap_plugin_descriptor_t s_descriptor = {
	.clap_version = CLAP_VERSION_INIT,
	.id = TEST_INSTRUMENT_ID,
	.name = TEST_INSTRUMENT_NAME,
	.vendor = "LMMS contributors",
	.url = "https://lmms.io",
	.manual_url = "https://lmms.io",
	.support_url = "https://lmms.io",
	.version = "1.0.0",
	.description = "Generator test plug-in for the LMMS native CLAP instrument host",
	.features = (const char* const[]){CLAP_PLUGIN_FEATURE_INSTRUMENT,
		CLAP_PLUGIN_FEATURE_SYNTHESIZER, NULL},
};

/* ---------------------------------------------------------------- params -- */

static uint32_t instrument_params_count(const clap_plugin_t* plugin)
{
	(void)plugin;
	return 1;
}

static bool instrument_params_get_info(const clap_plugin_t* plugin, uint32_t index,
	clap_param_info_t* info)
{
	(void)plugin;
	if (index != 0) { return false; }
	memset(info, 0, sizeof(*info));
	info->id = PARAM_GAIN;
	strncpy(info->name, "Gain", CLAP_NAME_SIZE - 1);
	info->min_value = 0.0;
	info->max_value = 1.0;
	info->default_value = 1.0;
	info->flags = CLAP_PARAM_IS_AUTOMATABLE;
	return true;
}

static bool instrument_params_get_value(const clap_plugin_t* plugin, clap_id param_id,
	double* value)
{
	const instrument_plugin_t* self = (const instrument_plugin_t*)plugin->plugin_data;
	if (param_id != PARAM_GAIN) { return false; }
	*value = self->gain;
	return true;
}

static bool instrument_params_value_to_text(const clap_plugin_t* plugin, clap_id param_id,
	double value, char* out_buffer, uint32_t out_buffer_capacity)
{
	(void)plugin;
	(void)param_id;
	snprintf(out_buffer, out_buffer_capacity, "%.2f", value);
	return true;
}

static bool instrument_params_text_to_value(const clap_plugin_t* plugin, clap_id param_id,
	const char* text, double* value)
{
	(void)plugin;
	(void)param_id;
	*value = atof(text);
	return true;
}

static void instrument_params_flush(const clap_plugin_t* plugin,
	const clap_input_events_t* in_events, const clap_output_events_t* out_events)
{
	(void)out_events;
	instrument_plugin_t* self = (instrument_plugin_t*)plugin->plugin_data;
	const uint32_t size = in_events->size(in_events);
	for (uint32_t i = 0; i < size; ++i)
	{
		const clap_event_header_t* header = in_events->get(in_events, i);
		if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID) { continue; }
		if (header->type != CLAP_EVENT_PARAM_VALUE) { continue; }
		const clap_event_param_value_t* event = (const clap_event_param_value_t*)header;
		if (event->param_id == PARAM_GAIN) { self->gain = event->value; }
	}
}

static const clap_plugin_params_t s_params = {
	.count = instrument_params_count,
	.get_info = instrument_params_get_info,
	.get_value = instrument_params_get_value,
	.value_to_text = instrument_params_value_to_text,
	.text_to_value = instrument_params_text_to_value,
	.flush = instrument_params_flush,
};

/* ------------------------------------------------------------ note ports -- */

/* One note INPUT port, CLAP dialect preferred. This is the extension the
   instrument host reads to decide whether track MIDI is worth queueing, and
   the port INDEX is what every clap_event_note_t it sends carries back. */
static uint32_t instrument_note_ports_count(const clap_plugin_t* plugin, bool is_input)
{
	(void)plugin;
	return is_input ? 1 : 0;
}

static bool instrument_note_ports_get(const clap_plugin_t* plugin, uint32_t index,
	bool is_input, clap_note_port_info_t* info)
{
	(void)plugin;
	if (!is_input || index != 0) { return false; }
	memset(info, 0, sizeof(*info));
	info->id = NOTE_PORT_ID;
	info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
	info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
	strncpy(info->name, "Notes", CLAP_NAME_SIZE - 1);
	return true;
}

static const clap_plugin_note_ports_t s_note_ports = {
	.count = instrument_note_ports_count,
	.get = instrument_note_ports_get,
};

/* ----------------------------------------------------------- audio ports -- */

/* A GENERATOR: no input port at all, one main stereo output. */
static uint32_t instrument_audio_ports_count(const clap_plugin_t* plugin, bool is_input)
{
	(void)plugin;
	return is_input ? 0 : 1;
}

static bool instrument_audio_ports_get(const clap_plugin_t* plugin, uint32_t index,
	bool is_input, clap_audio_port_info_t* info)
{
	(void)plugin;
	if (is_input || index != 0) { return false; }
	memset(info, 0, sizeof(*info));
	info->id = AUDIO_PORT_ID;
	strncpy(info->name, "Out", CLAP_NAME_SIZE - 1);
	info->flags = CLAP_AUDIO_PORT_IS_MAIN;
	info->channel_count = 2;
	info->port_type = CLAP_PORT_STEREO;
	info->in_place_pair = CLAP_INVALID_ID;
	return true;
}

static const clap_plugin_audio_ports_t s_audio_ports = {
	.count = instrument_audio_ports_count,
	.get = instrument_audio_ports_get,
};

/* ----------------------------------------------------------------- state -- */

static bool instrument_state_save(const clap_plugin_t* plugin,
	const clap_ostream_t* stream)
{
	const instrument_plugin_t* self = (const instrument_plugin_t*)plugin->plugin_data;
	instrument_state_t state;
	memset(&state, 0, sizeof(state));
	state.frames_processed = self->frames_processed;
	state.gain = self->gain;
	state.magic = TEST_INSTRUMENT_MAGIC;
	state.version = TEST_INSTRUMENT_STATE_VERSION;
	state.declared_max_frames = self->declared_max_frames;
	state.blocks = self->blocks;
	state.max_block = self->max_block;
	state.oversized_calls = self->oversized_calls;
	state.event_count = self->event_count;
	state.param_event_count = self->param_event_count;
	state.note_on_count = self->note_on_count;
	state.note_off_count = self->note_off_count;
	state.choke_count = self->choke_count;
	state.ignored_event_count = self->ignored_event_count;
	state.active_voices = self->active_voices;
	state.release_left = self->release_left;
	state.last_velocity_milli = self->last_velocity_milli;
	state.last_key = self->last_key;
	state.last_channel = self->last_channel;
	state.last_port_index = self->last_port_index;
	state.first_note_time = self->first_note_time;
	state.last_note_time = self->last_note_time;
	return stream->write(stream, &state, (int64_t)sizeof(state)) == (int64_t)sizeof(state);
}

static bool instrument_state_load(const clap_plugin_t* plugin,
	const clap_istream_t* stream)
{
	instrument_plugin_t* self = (instrument_plugin_t*)plugin->plugin_data;
	instrument_state_t state;
	if (stream->read(stream, &state, (int64_t)sizeof(state)) != (int64_t)sizeof(state))
	{
		return false;
	}
	if (state.magic != TEST_INSTRUMENT_MAGIC || state.version != TEST_INSTRUMENT_STATE_VERSION)
	{
		return false;
	}
	/* Only the value a user set is restored: the counters describe what this
	   instance has been asked to process and belong to the host's session. */
	self->gain = state.gain;
	return true;
}

static const clap_plugin_state_t s_state = {
	.save = instrument_state_save,
	.load = instrument_state_load,
};

/* --------------------------------------------------------------- process -- */

static void instrument_note_event(instrument_plugin_t* self,
	const clap_event_note_t* event)
{
	self->last_key = event->key;
	self->last_channel = event->channel;
	self->last_port_index = event->port_index;
	self->last_note_time = (int32_t)event->header.time;
	switch (event->header.type)
	{
	case CLAP_EVENT_NOTE_ON:
		++self->note_on_count;
		self->last_velocity_milli = (uint32_t)(event->velocity * 1000.0 + 0.5);
		self->sounding = 1;
		self->release_left = 0;
		if (self->first_note_time < 0) { self->first_note_time = (int32_t)event->header.time; }
		break;
	case CLAP_EVENT_NOTE_OFF:
		++self->note_off_count;
		if (self->sounding) { self->release_left = TEST_INSTRUMENT_RELEASE; }
		break;
	case CLAP_EVENT_NOTE_CHOKE:
		++self->choke_count;
		/* No release: the voice stops with the event. */
		self->sounding = 0;
		self->release_left = 0;
		break;
	default:
		break;
	}
}

static clap_process_status instrument_process(const clap_plugin_t* plugin,
	const clap_process_t* process)
{
	instrument_plugin_t* self = (instrument_plugin_t*)plugin->plugin_data;
	const uint32_t frames = process->frames_count;

	++self->blocks;
	if (frames > self->max_block) { self->max_block = frames; }
	if (self->declared_max_frames != 0 && frames > self->declared_max_frames)
	{
		++self->oversized_calls;
	}
	self->frames_processed += (double)frames;

	/* --- the input event list: notes first, then everything else --------- */
	if (process->in_events)
	{
		const uint32_t size = process->in_events->size(process->in_events);
		for (uint32_t i = 0; i < size; ++i)
		{
			const clap_event_header_t* header = process->in_events->get(process->in_events, i);
			if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID)
			{
				++self->ignored_event_count;
				continue;
			}
			++self->event_count;
			switch (header->type)
			{
			case CLAP_EVENT_NOTE_ON:
			case CLAP_EVENT_NOTE_OFF:
			case CLAP_EVENT_NOTE_CHOKE:
				instrument_note_event(self, (const clap_event_note_t*)header);
				break;
			case CLAP_EVENT_PARAM_VALUE:
			{
				const clap_event_param_value_t* event = (const clap_event_param_value_t*)header;
				++self->param_event_count;
				if (event->param_id == PARAM_GAIN) { self->gain = event->value; }
				break;
			}
			default:
				++self->ignored_event_count;
				break;
			}
		}
	}
	self->active_voices = (self->sounding || self->release_left > 0) ? 1u : 0u;

	/* --- the audio: a constant level per sounding voice ------------------ */
	const double level = self->gain * TEST_INSTRUMENT_LEVEL * (double)self->active_voices;
	for (uint32_t p = 0; p < process->audio_outputs_count; ++p)
	{
		const clap_audio_buffer_t* buffer = &process->audio_outputs[p];
		if (!buffer->data32) { continue; }
		for (uint32_t c = 0; c < buffer->channel_count; ++c)
		{
			float* channel = buffer->data32[c];
			if (!channel) { continue; }
			for (uint32_t f = 0; f < frames; ++f) { channel[f] = (float)level; }
		}
	}

	/* the release is counted in frames written, so it is sample accurate */
	if (self->release_left > 0)
	{
		self->release_left = self->release_left > frames ? self->release_left - frames : 0;
		if (self->release_left == 0) { self->sounding = 0; }
	}
	return CLAP_PROCESS_CONTINUE;
}

/* ------------------------------------------------------------- lifecycle -- */

static bool instrument_init(const clap_plugin_t* plugin)
{
	(void)plugin;
	return true;
}

static void instrument_destroy(const clap_plugin_t* plugin)
{
	free(plugin->plugin_data);
}

static bool instrument_activate(const clap_plugin_t* plugin, double sample_rate,
	uint32_t min_frames_count, uint32_t max_frames_count)
{
	(void)sample_rate;
	(void)min_frames_count;
	instrument_plugin_t* self = (instrument_plugin_t*)plugin->plugin_data;
	self->declared_max_frames = max_frames_count;
	return true;
}

static void instrument_deactivate(const clap_plugin_t* plugin)
{
	(void)plugin;
}

static bool instrument_start_processing(const clap_plugin_t* plugin)
{
	/* A fresh run starts with silence: no voice survives a stop/start. */
	instrument_plugin_t* self = (instrument_plugin_t*)plugin->plugin_data;
	self->sounding = 0;
	self->release_left = 0;
	self->active_voices = 0;
	return true;
}

static void instrument_stop_processing(const clap_plugin_t* plugin)
{
	(void)plugin;
}

static void instrument_reset(const clap_plugin_t* plugin)
{
	instrument_plugin_t* self = (instrument_plugin_t*)plugin->plugin_data;
	self->sounding = 0;
	self->release_left = 0;
	self->active_voices = 0;
}

static const void* instrument_get_extension(const clap_plugin_t* plugin, const char* id)
{
	(void)plugin;
	if (!strcmp(id, CLAP_EXT_PARAMS)) { return &s_params; }
	if (!strcmp(id, CLAP_EXT_NOTE_PORTS)) { return &s_note_ports; }
	if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) { return &s_audio_ports; }
	if (!strcmp(id, CLAP_EXT_STATE)) { return &s_state; }
	return NULL;
}

static void instrument_on_main_thread(const clap_plugin_t* plugin)
{
	(void)plugin;
}

static const clap_plugin_t* instrument_create(const clap_host_t* host)
{
	instrument_plugin_t* self = (instrument_plugin_t*)calloc(1, sizeof(*self));
	if (!self) { return NULL; }

	self->host = host;
	self->gain = 1.0;
	self->first_note_time = -1;
	self->last_note_time = -1;
	self->last_key = -1;
	self->last_channel = -1;
	self->last_port_index = -1;

	self->plugin.desc = &s_descriptor;
	self->plugin.plugin_data = self;
	self->plugin.init = instrument_init;
	self->plugin.destroy = instrument_destroy;
	self->plugin.activate = instrument_activate;
	self->plugin.deactivate = instrument_deactivate;
	self->plugin.start_processing = instrument_start_processing;
	self->plugin.stop_processing = instrument_stop_processing;
	self->plugin.reset = instrument_reset;
	self->plugin.process = instrument_process;
	self->plugin.get_extension = instrument_get_extension;
	self->plugin.on_main_thread = instrument_on_main_thread;
	return &self->plugin;
}

/* ----------------------------------------------------------------- entry -- */

static uint32_t instrument_factory_get_plugin_count(const clap_plugin_factory_t* factory)
{
	(void)factory;
	return 1;
}

static const clap_plugin_descriptor_t* instrument_factory_get_plugin_descriptor(
	const clap_plugin_factory_t* factory, uint32_t index)
{
	(void)factory;
	return index == 0 ? &s_descriptor : NULL;
}

static const clap_plugin_t* instrument_factory_create_plugin(
	const clap_plugin_factory_t* factory, const clap_host_t* host, const char* plugin_id)
{
	(void)factory;
	if (!clap_version_is_compatible(host->clap_version)) { return NULL; }
	if (!plugin_id || strcmp(plugin_id, TEST_INSTRUMENT_ID)) { return NULL; }
	return instrument_create(host);
}

static const clap_plugin_factory_t s_factory = {
	.get_plugin_count = instrument_factory_get_plugin_count,
	.get_plugin_descriptor = instrument_factory_get_plugin_descriptor,
	.create_plugin = instrument_factory_create_plugin,
};

static bool instrument_entry_init(const char* plugin_path)
{
	(void)plugin_path;
	return true;
}

static void instrument_entry_deinit(void)
{
}

static const void* instrument_entry_get_factory(const char* factory_id)
{
	return !strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) ? &s_factory : NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
	.clap_version = CLAP_VERSION_INIT,
	.init = instrument_entry_init,
	.deinit = instrument_entry_deinit,
	.get_factory = instrument_entry_get_factory,
};
