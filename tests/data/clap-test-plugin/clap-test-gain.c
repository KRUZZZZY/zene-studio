/*
 * clap-test-gain.c - a minimal, complete CLAP stereo gain/utility effect
 *
 * Built from the pinned CLAP 1.2.10 headers (MIT) as the test subject for the
 * LMMS native CLAP host (plugins/ClapEffect). It is a real CLAP plug-in: it
 * implements clap.params, clap.audio-ports and clap.state, plus clap.latency,
 * and it applies parameter changes from the host's event list in process().
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <clap/clap.h>

#define TEST_PLUGIN_ID "org.lmms.test.clap-gain"
#define TEST_PLUGIN_MAGIC 0x4C4D4741u /* "LMGA" */

enum
{
	PARAM_GAIN = 1,
	PARAM_BYPASS = 2,
};

typedef struct gain_state
{
	uint32_t magic;
	double gain;
	double bypass;
	double frames_processed;
} gain_state_t;

typedef struct gain_plugin
{
	clap_plugin_t plugin;
	const clap_host_t* host;
	const clap_plugin_params_t* params;
	double gain;
	double bypass;
	double frames_processed;
} gain_plugin_t;

static const clap_plugin_descriptor_t s_descriptor = {
	.clap_version = CLAP_VERSION_INIT,
	.id = TEST_PLUGIN_ID,
	.name = "LMMS CLAP Test Gain",
	.vendor = "LMMS contributors",
	.url = "https://lmms.io",
	.manual_url = "https://lmms.io",
	.support_url = "https://lmms.io",
	.version = "1.0.0",
	.description = "Stereo gain test plug-in for the LMMS native CLAP host",
	.features = (const char* const[]){CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, NULL},
};

/* ---------------------------------------------------------------- params -- */

static uint32_t gain_params_count(const clap_plugin_t* plugin)
{
	(void)plugin;
	return 2;
}

static bool gain_params_get_info(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info)
{
	(void)plugin;
	memset(info, 0, sizeof(*info));
	switch (index)
	{
	case 0:
		info->id = PARAM_GAIN;
		strncpy(info->name, "Gain", CLAP_NAME_SIZE - 1);
		info->min_value = 0.0;
		info->max_value = 1.0;
		info->default_value = 1.0;
		info->flags = CLAP_PARAM_IS_AUTOMATABLE;
		return true;
	case 1:
		info->id = PARAM_BYPASS;
		strncpy(info->name, "Bypass", CLAP_NAME_SIZE - 1);
		info->min_value = 0.0;
		info->max_value = 1.0;
		info->default_value = 0.0;
		info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
		return true;
	default:
		return false;
	}
}

static bool gain_params_get_value(const clap_plugin_t* plugin, clap_id param_id, double* value)
{
	const gain_plugin_t* self = (const gain_plugin_t*)plugin->plugin_data;
	switch (param_id)
	{
	case PARAM_GAIN:
		*value = self->gain;
		return true;
	case PARAM_BYPASS:
		*value = self->bypass;
		return true;
	default:
		return false;
	}
}

static bool gain_params_value_to_text(const clap_plugin_t* plugin, clap_id param_id, double value,
	char* out_buffer, uint32_t out_buffer_capacity)
{
	(void)plugin;
	(void)param_id;
	snprintf(out_buffer, out_buffer_capacity, "%.2f", value);
	return true;
}

static bool gain_params_text_to_value(const clap_plugin_t* plugin, clap_id param_id,
	const char* text, double* value)
{
	(void)plugin;
	(void)param_id;
	*value = atof(text);
	return true;
}

static void gain_params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in_events,
	const clap_output_events_t* out_events)
{
	(void)out_events;
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	const uint32_t size = in_events->size(in_events);
	for (uint32_t i = 0; i < size; ++i)
	{
		const clap_event_header_t* header = in_events->get(in_events, i);
		if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID) { continue; }
		if (header->type != CLAP_EVENT_PARAM_VALUE) { continue; }
		const clap_event_param_value_t* event = (const clap_event_param_value_t*)header;
		switch (event->param_id)
		{
		case PARAM_GAIN:
			self->gain = event->value;
			break;
		case PARAM_BYPASS:
			self->bypass = event->value;
			break;
		default:
			break;
		}
	}
}

static const clap_plugin_params_t s_params = {
	.count = gain_params_count,
	.get_info = gain_params_get_info,
	.get_value = gain_params_get_value,
	.value_to_text = gain_params_value_to_text,
	.text_to_value = gain_params_text_to_value,
	.flush = gain_params_flush,
};

/* ----------------------------------------------------------- audio ports -- */

static uint32_t gain_audio_ports_count(const clap_plugin_t* plugin, bool is_input)
{
	(void)plugin;
	return 1;
	(void)is_input;
}

static bool gain_audio_ports_get(const clap_plugin_t* plugin, uint32_t index, bool is_input,
	clap_audio_port_info_t* info)
{
	(void)plugin;
	if (index != 0) { return false; }
	memset(info, 0, sizeof(*info));
	info->id = is_input ? 0 : 1;
	strncpy(info->name, is_input ? "Input" : "Output", sizeof(info->name) - 1);
	info->flags = CLAP_AUDIO_PORT_IS_MAIN;
	info->channel_count = 2;
	info->port_type = CLAP_PORT_STEREO;
	info->in_place_pair = CLAP_INVALID_ID;
	return true;
}

static const clap_plugin_audio_ports_t s_audio_ports = {
	.count = gain_audio_ports_count,
	.get = gain_audio_ports_get,
};

/* ----------------------------------------------------------------- state -- */

static bool gain_state_save(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
	const gain_plugin_t* self = (const gain_plugin_t*)plugin->plugin_data;
	gain_state_t state;
	/* zero the struct first: the padding bytes are unspecified otherwise, which
	   would make two saves of the same musical state differ byte-wise */
	memset(&state, 0, sizeof(state));
	state.magic = TEST_PLUGIN_MAGIC;
	state.gain = self->gain;
	state.bypass = self->bypass;
	state.frames_processed = self->frames_processed;
	return stream->write(stream, &state, sizeof(state)) == (int64_t)sizeof(state);
}

static bool gain_state_load(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	gain_state_t state = {};
	if (stream->read(stream, &state, sizeof(state)) != (int64_t)sizeof(state)) { return false; }
	if (state.magic != TEST_PLUGIN_MAGIC) { return false; }
	self->gain = state.gain;
	self->bypass = state.bypass;
	self->frames_processed = state.frames_processed;
	return true;
}

static const clap_plugin_state_t s_state = {
	.save = gain_state_save,
	.load = gain_state_load,
};

/* --------------------------------------------------------------- latency -- */

static uint32_t gain_latency_get(const clap_plugin_t* plugin)
{
	(void)plugin;
	return 0;
}

static const clap_plugin_latency_t s_latency = {
	.get = gain_latency_get,
};

/* --------------------------------------------------------------- plugin --- */

static bool gain_init(const clap_plugin_t* plugin)
{
	(void)plugin;
	return true;
}

static void gain_destroy(const clap_plugin_t* plugin)
{
	free(plugin->plugin_data);
}

static bool gain_activate(const clap_plugin_t* plugin, double sample_rate, uint32_t min_frames_count,
	uint32_t max_frames_count)
{
	(void)plugin;
	(void)sample_rate;
	(void)min_frames_count;
	(void)max_frames_count;
	return true;
}

static void gain_deactivate(const clap_plugin_t* plugin) { (void)plugin; }

static bool gain_start_processing(const clap_plugin_t* plugin)
{
	(void)plugin;
	return true;
}

static void gain_stop_processing(const clap_plugin_t* plugin) { (void)plugin; }

static void gain_reset(const clap_plugin_t* plugin)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	self->frames_processed = 0.0;
}

static clap_process_status gain_process(const clap_plugin_t* plugin, const clap_process_t* process)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;

	/* The host delivers parameter changes through the event list. */
	self->params->flush(plugin, process->in_events, process->out_events);

	const double gain = self->bypass >= 0.5 ? 1.0 : self->gain;
	const uint32_t frames = process->frames_count;
	const uint32_t input_count = process->audio_inputs_count;
	for (uint32_t p = 0; p < process->audio_outputs_count; ++p)
	{
		const clap_audio_buffer_t* out = &process->audio_outputs[p];
		const clap_audio_buffer_t* in = p < input_count ? &process->audio_inputs[p] : NULL;
		for (uint32_t c = 0; c < out->channel_count; ++c)
		{
			float* dst = out->data32[c];
			if (!dst) { continue; }
			const float* src = (in && c < in->channel_count) ? in->data32[c] : NULL;
			for (uint32_t f = 0; f < frames; ++f)
			{
				dst[f] = src ? (float)(src[f] * gain) : 0.0f;
			}
		}
	}
	self->frames_processed += frames;
	return CLAP_PROCESS_CONTINUE;
}

static const void* gain_get_extension(const clap_plugin_t* plugin, const char* id)
{
	(void)plugin;
	if (strcmp(id, CLAP_EXT_PARAMS) == 0) { return &s_params; }
	if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) { return &s_audio_ports; }
	if (strcmp(id, CLAP_EXT_STATE) == 0) { return &s_state; }
	if (strcmp(id, CLAP_EXT_LATENCY) == 0) { return &s_latency; }
	return NULL;
}

static void gain_on_main_thread(const clap_plugin_t* plugin) { (void)plugin; }

/* -------------------------------------------------------------- factory --- */

static uint32_t gain_factory_get_plugin_count(const clap_plugin_factory_t* factory)
{
	(void)factory;
	return 1;
}

static const clap_plugin_descriptor_t* gain_factory_get_plugin_descriptor(
	const clap_plugin_factory_t* factory, uint32_t index)
{
	(void)factory;
	return index == 0 ? &s_descriptor : NULL;
}

static const clap_plugin_t* gain_factory_create_plugin(const clap_plugin_factory_t* factory,
	const clap_host_t* host, const char* plugin_id)
{
	(void)factory;
	if (!plugin_id || strcmp(plugin_id, TEST_PLUGIN_ID) != 0) { return NULL; }
	gain_plugin_t* self = (gain_plugin_t*)calloc(1, sizeof(gain_plugin_t));
	if (!self) { return NULL; }
	self->plugin.desc = &s_descriptor;
	self->plugin.plugin_data = self;
	self->plugin.init = gain_init;
	self->plugin.destroy = gain_destroy;
	self->plugin.activate = gain_activate;
	self->plugin.deactivate = gain_deactivate;
	self->plugin.start_processing = gain_start_processing;
	self->plugin.stop_processing = gain_stop_processing;
	self->plugin.reset = gain_reset;
	self->plugin.process = gain_process;
	self->plugin.get_extension = gain_get_extension;
	self->plugin.on_main_thread = gain_on_main_thread;
	self->host = host;
	self->params = &s_params;
	self->gain = 1.0;
	self->bypass = 0.0;
	return &self->plugin;
}

static const clap_plugin_factory_t s_factory = {
	.get_plugin_count = gain_factory_get_plugin_count,
	.get_plugin_descriptor = gain_factory_get_plugin_descriptor,
	.create_plugin = gain_factory_create_plugin,
};

/* ---------------------------------------------------------------- entry --- */

static bool gain_entry_init(const char* plugin_path)
{
	(void)plugin_path;
	return true;
}

static void gain_entry_deinit(void) {}

static const void* gain_entry_get_factory(const char* factory_id)
{
	return strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &s_factory : NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
	.clap_version = CLAP_VERSION_INIT,
	.init = gain_entry_init,
	.deinit = gain_entry_deinit,
	.get_factory = gain_entry_get_factory,
};
