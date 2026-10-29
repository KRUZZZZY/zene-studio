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
 * The factory holds a SECOND class, TEST_INSTRUMENT_MULTIOUT_ID (0.5.0 MX-114):
 * the same generator with two stereo output ports - "Out" (main) and "Aux" -
 * the subject of the multi-output instrument commands (plugin.output_*). Its
 * Aux port carries the same constant level scaled by TEST_INSTRUMENT_AUX_SCALE,
 * so the two buses are told apart by their amplitude. The first class is
 * unchanged: one output port, the same descriptor, the same process().
 *
 * LATENCY (MX-157, BUGS 11.34): an instance created while the environment
 * variable CLAP_TEST_INSTRUMENT_LATENCY names a frame count is LATENT. It
 * implements clap.latency, reports that count, and really delays its output by
 * it (a ring of the per-frame level), so a host's delay compensation can be
 * measured on rendered audio. The count is read at activate(), the only time
 * CLAP lets a latency change: when process() sees the variable name a new
 * count it asks for a restart (clap_host.request_restart), and the activate()
 * that follows reads it and says so through clap_host_latency.changed(). An
 * instance created without the variable is exactly the plug-in it always was.
 * (Reading the environment in process() is a test-fixture shortcut, not
 * something a real plug-in would do on its audio thread.)
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
#define TEST_INSTRUMENT_STATE_VERSION 2u
/* How long a note takes to fade after its note-off, in frames. */
#define TEST_INSTRUMENT_RELEASE 256u
/* The level one sounding voice writes, before the gain parameter. */
#define TEST_INSTRUMENT_LEVEL 0.25

/* The multi-output class (MX-114): the same generator with a second stereo port. */
#define TEST_INSTRUMENT_MULTIOUT_ID "org.lmms.test.clap-instrument-multiout"
#define TEST_INSTRUMENT_MULTIOUT_NAME "LMMS CLAP Test Instrument (multi-out)"
/* The Aux port's level, relative to the main port's. */
#define TEST_INSTRUMENT_AUX_SCALE 0.5
/* The largest latency a latent instance takes, and the size of its delay ring.
 * 32768 since 2026-10-19 (Q050-169): the over-capacity case the limitations page documents
 * (a latency above LatencyCompensation::MaxFrames, 16384) needs a fixture that can really
 * report one, so the clamp can be measured rather than asserted. The ring is allocated only
 * for a latent instance, so the extra 64 KB is per latent fixture, not per module. */
#define TEST_INSTRUMENT_MAX_LATENCY 32768u
#define TEST_INSTRUMENT_LATENCY_ENV "CLAP_TEST_INSTRUMENT_LATENCY"
/* CLAP_TEST_INSTRUMENT_LAYOUT: set (to any value) and the single-output class reports a
   SECOND stereo output port, i.e. its audio-port layout changes while the host runs. During
   activate() (the !active_state callback), the instance checks that the host supports a list
   rescan, requests CLAP_AUDIO_PORTS_RESCAN_LIST, and asks for a restart - the subject of the
   instrument view's stale-channel-count case. Unset, the class is unchanged. */
#define TEST_INSTRUMENT_LAYOUT_ENV "CLAP_TEST_INSTRUMENT_LAYOUT"

enum
{
	PARAM_GAIN = 1,
	NOTE_PORT_ID = 1,
	AUDIO_PORT_ID = 1,
	AUX_AUDIO_PORT_ID = 2,
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
	uint32_t max_block;           /* largest frames_count since activate() */
	uint32_t oversized_calls;     /* oversized calls since activate() */
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
	/* version 2 (engine audit E11): the note expressions received */
	uint32_t expression_count;
	int32_t last_expression_id;
	uint32_t last_expression_milli; /* value * 1000 */
	int32_t last_expression_key;
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
	uint32_t expression_count;
	int32_t last_expression_id;
	uint32_t last_expression_milli;
	int32_t last_expression_key;
	/* the sounding voice */
	int sounding;          /* a note-on has arrived and no note-off yet */
	uint32_t release_left; /* frames left of the release, 0 when silent */
	/* 0 for the original class; 1 for TEST_INSTRUMENT_MULTIOUT_ID */
	int multiout;
	/* The output-port count this instance's clap.audio-ports currently reports (see
	   TEST_INSTRUMENT_LAYOUT_ENV); activate() re-reads the variable and announces a change. */
	uint32_t layout_ports;
	/* the latency (see LATENCY above); all 0 for an instance that is not latent */
	int latent;               /* created while TEST_INSTRUMENT_LATENCY_ENV was set */
	uint32_t latency;         /* frames, read at activate() */
	int restart_requested;    /* a new count was seen; the restart has not come yet */
	float* delay_ring;        /* TEST_INSTRUMENT_MAX_LATENCY levels, allocated at activate() */
	uint32_t ring_pos;
} instrument_plugin_t;

/* A named array, not a compound literal: MSVC's C compiler rejects (type){...} (C4576 under /WX). */
static const char* const s_instrument_features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
	CLAP_PLUGIN_FEATURE_SYNTHESIZER, NULL};

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
	.features = s_instrument_features,
};

static const clap_plugin_descriptor_t s_descriptor_multiout = {
	.clap_version = CLAP_VERSION_INIT,
	.id = TEST_INSTRUMENT_MULTIOUT_ID,
	.name = TEST_INSTRUMENT_MULTIOUT_NAME,
	.vendor = "LMMS contributors",
	.url = "https://lmms.io",
	.manual_url = "https://lmms.io",
	.support_url = "https://lmms.io",
	.version = "1.0.0",
	.description = "Multi-output generator test plug-in for the LMMS native CLAP instrument host",
	.features = s_instrument_features,
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

/* A GENERATOR: no input port at all, one main stereo output - or two when
   TEST_INSTRUMENT_LAYOUT_ENV asked for a runtime layout change (see the define). */
static uint32_t instrument_output_ports(void)
{
	return getenv(TEST_INSTRUMENT_LAYOUT_ENV) != NULL ? 2u : 1u;
}

static uint32_t instrument_audio_ports_count(const clap_plugin_t* plugin, bool is_input)
{
	(void)plugin;
	return is_input ? 0 : instrument_output_ports();
}

static bool instrument_audio_ports_get(const clap_plugin_t* plugin, uint32_t index,
	bool is_input, clap_audio_port_info_t* info)
{
	(void)plugin;
	if (is_input || index >= instrument_output_ports()) { return false; }
	memset(info, 0, sizeof(*info));
	info->id = index == 0 ? AUDIO_PORT_ID : AUX_AUDIO_PORT_ID;
	strncpy(info->name, index == 0 ? "Out" : "Aux", CLAP_NAME_SIZE - 1);
	info->flags = index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
	info->channel_count = 2;
	info->port_type = CLAP_PORT_STEREO;
	info->in_place_pair = CLAP_INVALID_ID;
	return true;
}

static const clap_plugin_audio_ports_t s_audio_ports = {
	.count = instrument_audio_ports_count,
	.get = instrument_audio_ports_get,
};

/* The multi-output class: no input port, the main stereo output and a second
   stereo output ("Aux") - an instrument with one extra output bus. */
static uint32_t multiout_audio_ports_count(const clap_plugin_t* plugin, bool is_input)
{
	(void)plugin;
	return is_input ? 0 : 2;
}

static bool multiout_audio_ports_get(const clap_plugin_t* plugin, uint32_t index,
	bool is_input, clap_audio_port_info_t* info)
{
	if (is_input || index > 1) { return false; }
	if (index == 0) { return instrument_audio_ports_get(plugin, 0, false, info); }
	memset(info, 0, sizeof(*info));
	info->id = AUX_AUDIO_PORT_ID;
	strncpy(info->name, "Aux", CLAP_NAME_SIZE - 1);
	info->flags = 0;
	info->channel_count = 2;
	info->port_type = CLAP_PORT_STEREO;
	info->in_place_pair = CLAP_INVALID_ID;
	return true;
}

static const clap_plugin_audio_ports_t s_audio_ports_multiout = {
	.count = multiout_audio_ports_count,
	.get = multiout_audio_ports_get,
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
	state.expression_count = self->expression_count;
	state.last_expression_id = self->last_expression_id;
	state.last_expression_milli = self->last_expression_milli;
	state.last_expression_key = self->last_expression_key;
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
	if (!isfinite(state.gain) || state.gain < 0.0 || state.gain > 1.0)
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

/* --------------------------------------------------------------- latency -- */

/* The count the environment names now, bounded to the ring. */
static uint32_t instrument_env_latency(void)
{
	const char* frames = getenv(TEST_INSTRUMENT_LATENCY_ENV);
	const long value = frames != NULL ? atol(frames) : 0;
	if (value <= 0) { return 0; }
	/* one short of the ring: a delay of the whole ring would read the frame just written */
	return value >= (long)TEST_INSTRUMENT_MAX_LATENCY ? TEST_INSTRUMENT_MAX_LATENCY - 1u : (uint32_t)value;
}

static uint32_t instrument_latency_get(const clap_plugin_t* plugin)
{
	const instrument_plugin_t* self = (const instrument_plugin_t*)plugin->plugin_data;
	return self->latency;
}

static const clap_plugin_latency_t s_latency = {
	.get = instrument_latency_get,
};

/* One frame of the level, delayed by the latency (a latent instance only). */
static double instrument_delayed(instrument_plugin_t* self, double level)
{
	self->delay_ring[self->ring_pos] = (float)level;
	const uint32_t read = (self->ring_pos + TEST_INSTRUMENT_MAX_LATENCY - self->latency) % TEST_INSTRUMENT_MAX_LATENCY;
	self->ring_pos = (self->ring_pos + 1) % TEST_INSTRUMENT_MAX_LATENCY;
	return self->delay_ring[read];
}

/* A latent instance asks for a restart when the environment names a new count. */
static void instrument_follow_latency(instrument_plugin_t* self)
{
	if (!self->latent || self->restart_requested || instrument_env_latency() == self->latency) { return; }
	self->restart_requested = 1;
	self->host->request_restart(self->host);
}

/* The output-port count the environment names now differs from what this instance reports:
   announce it the CLAP way. rescan() is a main-thread call (activate() is one); request_restart()
   is the restart an active plug-in must ask for before the host re-reads its layout. */
static void instrument_follow_layout(instrument_plugin_t* self)
{
	if (self->multiout) { return; }
	const uint32_t ports = instrument_output_ports();
	if (ports == self->layout_ports) { return; }
	const clap_host_audio_ports_t* host_ports =
		(const clap_host_audio_ports_t*)self->host->get_extension(self->host, CLAP_EXT_AUDIO_PORTS);
	if (!host_ports || !host_ports->is_rescan_flag_supported || !host_ports->rescan ||
		!host_ports->is_rescan_flag_supported(self->host, CLAP_AUDIO_PORTS_RESCAN_LIST)) { return; }
	self->layout_ports = ports;
	host_ports->rescan(self->host, CLAP_AUDIO_PORTS_RESCAN_LIST);
	self->host->request_restart(self->host);
}

/* The level on every output port: the multi-output class's Aux port at its own level. */
static void instrument_write_level(const instrument_plugin_t* self, const clap_process_t* process,
	double level, uint32_t frames)
{
	for (uint32_t p = 0; p < process->audio_outputs_count; ++p)
	{
		const clap_audio_buffer_t* buffer = &process->audio_outputs[p];
		if (!buffer->data32) { continue; }
		/* the multi-output class's Aux port: the same voice, at its own level */
		const double port_level = (self->multiout && p > 0) ? level * TEST_INSTRUMENT_AUX_SCALE : level;
		for (uint32_t c = 0; c < buffer->channel_count; ++c)
		{
			float* channel = buffer->data32[c];
			if (!channel) { continue; }
			for (uint32_t f = 0; f < frames; ++f) { channel[f] = (float)port_level; }
		}
	}
}

/* A latent instance: the same level, written a frame at a time through its delay ring. */
static void instrument_write_delayed(instrument_plugin_t* self, const clap_process_t* process,
	double level, uint32_t frames)
{
	for (uint32_t f = 0; f < frames; ++f)
	{
		const double delayed = instrument_delayed(self, level);
		for (uint32_t p = 0; p < process->audio_outputs_count; ++p)
		{
			const clap_audio_buffer_t* buffer = &process->audio_outputs[p];
			const double port_level = (self->multiout && p > 0) ? delayed * TEST_INSTRUMENT_AUX_SCALE : delayed;
			for (uint32_t c = 0; buffer->data32 && c < buffer->channel_count; ++c)
			{
				if (buffer->data32[c]) { buffer->data32[c][f] = (float)port_level; }
			}
		}
	}
}

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
			if (!header)
			{
				++self->ignored_event_count;
				continue;
			}
			++self->event_count;
			if (header->space_id != CLAP_CORE_EVENT_SPACE_ID)
			{
				++self->ignored_event_count;
				continue;
			}
			switch (header->type)
			{
			case CLAP_EVENT_NOTE_ON:
			case CLAP_EVENT_NOTE_OFF:
			case CLAP_EVENT_NOTE_CHOKE:
				instrument_note_event(self, (const clap_event_note_t*)header);
				break;
			case CLAP_EVENT_NOTE_EXPRESSION:
			{
				const clap_event_note_expression_t* event = (const clap_event_note_expression_t*)header;
				++self->expression_count;
				self->last_expression_id = (int32_t)event->expression_id;
				self->last_expression_milli = (uint32_t)(event->value * 1000.0 + 0.5);
				self->last_expression_key = (int32_t)event->key;
				break;
			}
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

	/* --- a latent instance asks for a restart when its count changes ----- */
	instrument_follow_latency(self);

	/* --- the audio: a constant level per sounding voice ------------------ */
	const double level = self->gain * TEST_INSTRUMENT_LEVEL * (double)self->active_voices;
	if (self->latency > 0) { instrument_write_delayed(self, process, level, frames); }
	else { instrument_write_level(self, process, level, frames); }

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
	instrument_plugin_t* self = (instrument_plugin_t*)plugin->plugin_data;
	free(self->delay_ring);
	free(self);
}

/* A latent instance reads its count here - the one place CLAP lets it change - and says so. */
static bool instrument_activate_latency(instrument_plugin_t* self)
{
	const uint32_t latency = instrument_env_latency();
	self->restart_requested = 0;
	free(self->delay_ring);
	self->delay_ring = (float*)calloc(TEST_INSTRUMENT_MAX_LATENCY, sizeof(float));
	self->ring_pos = 0;
	if (!self->delay_ring) { return false; }
	if (latency != self->latency)
	{
		self->latency = latency;
		const clap_host_latency_t* host_latency =
			(const clap_host_latency_t*)self->host->get_extension(self->host, CLAP_EXT_LATENCY);
		if (host_latency && host_latency->changed) { host_latency->changed(self->host); }
	}
	return true;
}

static bool instrument_activate(const clap_plugin_t* plugin, double sample_rate,
	uint32_t min_frames_count, uint32_t max_frames_count)
{
	(void)sample_rate;
	(void)min_frames_count;
	instrument_plugin_t* self = (instrument_plugin_t*)plugin->plugin_data;
	self->declared_max_frames = max_frames_count;
	self->frames_processed = 0.0;
	self->blocks = 0;
	self->max_block = 0;
	self->oversized_calls = 0;
	instrument_follow_layout(self);
	return self->latent ? instrument_activate_latency(self) : true;
}

static void instrument_deactivate(const clap_plugin_t* plugin)
{
	instrument_plugin_t* self = (instrument_plugin_t*)plugin->plugin_data;
	free(self->delay_ring);
	self->delay_ring = NULL;
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
	const instrument_plugin_t* self = (const instrument_plugin_t*)plugin->plugin_data;
	if (!strcmp(id, CLAP_EXT_PARAMS)) { return &s_params; }
	if (!strcmp(id, CLAP_EXT_NOTE_PORTS)) { return &s_note_ports; }
	if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) { return self->multiout ? &s_audio_ports_multiout : &s_audio_ports; }
	if (!strcmp(id, CLAP_EXT_STATE)) { return &s_state; }
	if (!strcmp(id, CLAP_EXT_LATENCY) && self->latent) { return &s_latency; }
	return NULL;
}

static void instrument_on_main_thread(const clap_plugin_t* plugin)
{
	(void)plugin;
}

static const clap_plugin_t* instrument_create(const clap_host_t* host, int multiout)
{
	instrument_plugin_t* self = (instrument_plugin_t*)calloc(1, sizeof(*self));
	if (!self) { return NULL; }

	self->host = host;
	self->multiout = multiout;
	self->layout_ports = multiout ? 2u : instrument_output_ports();
	self->latent = getenv(TEST_INSTRUMENT_LATENCY_ENV) != NULL;
	self->gain = 1.0;
	self->first_note_time = -1;
	self->last_note_time = -1;
	self->last_expression_id = -1;
	self->last_expression_key = -2;
	self->last_key = -1;
	self->last_channel = -1;
	self->last_port_index = -1;

	self->plugin.desc = multiout ? &s_descriptor_multiout : &s_descriptor;
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
	return 2;
}

static const clap_plugin_descriptor_t* instrument_factory_get_plugin_descriptor(
	const clap_plugin_factory_t* factory, uint32_t index)
{
	(void)factory;
	if (index == 0) { return &s_descriptor; }
	return index == 1 ? &s_descriptor_multiout : NULL;
}

static const clap_plugin_t* instrument_factory_create_plugin(
	const clap_plugin_factory_t* factory, const clap_host_t* host, const char* plugin_id)
{
	(void)factory;
	if (!clap_version_is_compatible(host->clap_version)) { return NULL; }
	if (!plugin_id) { return NULL; }
	if (!strcmp(plugin_id, TEST_INSTRUMENT_ID)) { return instrument_create(host, 0); }
	if (!strcmp(plugin_id, TEST_INSTRUMENT_MULTIOUT_ID)) { return instrument_create(host, 1); }
	return NULL;
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
