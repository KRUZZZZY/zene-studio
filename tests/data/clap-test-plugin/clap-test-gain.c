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
/* Bump when the state layout below changes; the host stores the bytes opaquely. */
#define TEST_PLUGIN_STATE_VERSION 3u
/* How many per-call block sizes the fixture records, for the chunking test
   (ClapHostTest::testChunkedProcessing). The recorded sequence is what the
   host actually asked the plug-in to process, so a test can assert the exact
   chunk shape rather than infer it from the audio. */
#define TEST_PLUGIN_MAX_RECORDED_BLOCKS 16

enum
{
	PARAM_GAIN = 1,
	PARAM_BYPASS = 2,
	/* W6: the list a CLAP_PARAM_RESCAN_ALL brings in (CLAP_TEST_GAIN_RESCAN=all): Gain, Drive, Tone - the
	   bypass switch gone, two parameters new. */
	PARAM_DRIVE = 3,
	PARAM_TONE = 4,
};

typedef struct gain_state
{
	uint32_t magic;
	uint32_t version;
	uint32_t declared_max_frames; /* what activate() was told */
	uint32_t blocks;              /* process() calls since activate() */
	uint32_t max_block;           /* largest frames_count seen */
	uint32_t oversized_calls;     /* calls with frames_count > declared_max_frames */
	uint32_t recorded_count;
	uint32_t recorded[TEST_PLUGIN_MAX_RECORDED_BLOCKS];
	double gain;
	double bypass;
	double frames_processed;
	uint32_t host_flushes; /* params.flush() calls that arrived outside process() - a host answering a
	                          request_flush while the plug-in was active */
} gain_state_t;

typedef struct gain_plugin
{
	clap_plugin_t plugin;
	uint32_t host_flushes;
	int in_process;         /* set for the duration of process(), so flush() can tell the two apart */
	int return_pending;     /* CLAP_TEST_GAIN_REQUEST_FLUSH: the next process() returns the changed value */
	const clap_host_t* host;
	const clap_plugin_params_t* params;
	double gain;
	double bypass;
	double drive;
	double tone;
	/* W6, CLAP_TEST_GAIN_RESCAN: "info" renames Gain to Level (CLAP_PARAM_RESCAN_INFO); "all" asks for a restart
	   and, deactivated, swaps in the extended list (CLAP_PARAM_RESCAN_ALL) - the order CLAP prescribes. */
	bool renamed;
	bool extend_on_deactivate;
	bool extended;
	double frames_processed;
	uint32_t declared_max_frames;
	uint32_t blocks;
	uint32_t max_block;
	uint32_t oversized_calls;
	uint32_t recorded_count;
	uint32_t recorded[TEST_PLUGIN_MAX_RECORDED_BLOCKS];
	/* R4.1: a timer registered through the host's clap.timer-support, as an editor's
	   repaint timer is, and the ticks it has delivered. */
	clap_id timer_id;
	bool timer_registered;
	uint32_t timer_ticks;
	/* R4.3: an embedded editor - sized 400x300, never smaller than 100x80. Q050-490: on X11, win32
	   or cocoa, the api it was created for in gui_api. */
	bool gui_created;
	const char* gui_api;
	bool gui_visible;
	uint64_t gui_parent;
	uint32_t gui_width;
	uint32_t gui_height;
	bool gui_scale_finite;
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
	const gain_plugin_t* self = (const gain_plugin_t*)plugin->plugin_data;
	return self->extended ? 3 : 2;
}

static void gain_param_info(clap_param_info_t* info, clap_id id, const char* name, double max, double def)
{
	info->id = id;
	strncpy(info->name, name, CLAP_NAME_SIZE - 1);
	info->min_value = 0.0;
	info->max_value = max;
	info->default_value = def;
	info->flags = CLAP_PARAM_IS_AUTOMATABLE;
	/* CLAP_TEST_GAIN_BAD_RANGE: Gain reports an inverted range (min > max), which a host must survive
	   (std::clamp needs ordered bounds) instead of storing verbatim. */
	if (id == PARAM_GAIN && getenv("CLAP_TEST_GAIN_BAD_RANGE") != NULL)
	{
		info->min_value = 1.0;
		info->max_value = 0.0;
		info->default_value = 0.5;
	}
}

/* The extended list's entries after Gain (W6). */
static bool gain_params_get_extended_info(uint32_t index, clap_param_info_t* info)
{
	if (index == 1) { gain_param_info(info, PARAM_DRIVE, "Drive", 10.0, 2.0); return true; }
	if (index == 2) { gain_param_info(info, PARAM_TONE, "Tone", 1.0, 0.5); return true; }
	return false;
}

static bool gain_params_get_info(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info)
{
	const gain_plugin_t* self = (const gain_plugin_t*)plugin->plugin_data;
	memset(info, 0, sizeof(*info));
	if (index == 0)
	{
		gain_param_info(info, PARAM_GAIN, self->renamed ? "Level" : "Gain", 1.0, 1.0);
		return true;
	}
	if (self->extended) { return gain_params_get_extended_info(index, info); }
	switch (index)
	{
	case 1:
		info->id = PARAM_BYPASS;
		strncpy(info->name, "Bypass", CLAP_NAME_SIZE - 1);
		info->min_value = 0.0;
		info->max_value = 1.0;
		info->default_value = 0.0;
		info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
		/* CLAP_TEST_GAIN_READONLY: Bypass is CLAP_PARAM_IS_READONLY, so a host must not offer an
		   editable knob for it (its writes are ignored). */
		if (getenv("CLAP_TEST_GAIN_READONLY") != NULL) { info->flags |= CLAP_PARAM_IS_READONLY; }
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
		/* CLAP_TEST_GAIN_NAN_VALUE: the plug-in REPORTS a non-finite value. std::clamp does not
		   sanitize NaN, so a host that clamps and stores it forwards NaN to the plug-in's own
		   processing instead of a number in the parameter's range. */
		if (getenv("CLAP_TEST_GAIN_NAN_VALUE") != NULL) { *value = (double)NAN; return true; }
		*value = self->gain;
		return true;
	case PARAM_BYPASS:
		*value = self->bypass;
		return true;
	case PARAM_DRIVE:
		*value = self->drive;
		return true;
	case PARAM_TONE:
		*value = self->tone;
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
	if (!self->in_process) { self->host_flushes += 1; }
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
		case PARAM_DRIVE:
			self->drive = event->value;
			break;
		case PARAM_TONE:
			self->tone = event->value;
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

/* CLAP_TEST_GAIN_LAYOUT selects a port layout for the routing tests (ClapHostTest). Without it the
   fixture keeps its one stereo input and one stereo output, the main port at index 0 of each.
     sidechain-first   inputs: 0 = side-chain (not main), 1 = main; one stereo output
     unreadable-input  inputs: 0 reports no info (get() fails), 1 = main; one stereo output
     zero-output       one stereo input; outputs: 0 = zero-channel, 1 = main stereo
   Each process() copies the main input to the main output (scaled by the gain), and when the ports do
   not match the expected indices it writes nothing, so a misrouted buffer reads back as silence. */
static const char* gain_layout(void) { return getenv("CLAP_TEST_GAIN_LAYOUT"); }

static uint32_t gain_main_index(bool is_input)
{
	const char* layout = gain_layout();
	if (layout == NULL) { return 0; }
	if (strcmp(layout, "zero-output") == 0) { return is_input ? 0 : 1; }
	return is_input ? 1 : 0;
}

static uint32_t gain_audio_ports_count(const clap_plugin_t* plugin, bool is_input)
{
	(void)plugin;
	const char* layout = gain_layout();
	if (layout == NULL) { return 1; }
	const bool zero = strcmp(layout, "zero-output") == 0;
	return is_input ? (zero ? 1 : 2) : (zero ? 2 : 1);
}

static bool gain_audio_ports_get(const clap_plugin_t* plugin, uint32_t index, bool is_input,
	clap_audio_port_info_t* info)
{
	(void)plugin;
	const char* layout = gain_layout();
	if (index >= gain_audio_ports_count(plugin, is_input)) { return false; }
	if (layout != NULL && is_input && index == 0 && strcmp(layout, "unreadable-input") == 0) { return false; }
	memset(info, 0, sizeof(*info));
	const bool empty = layout != NULL && !is_input && index == 0 && strcmp(layout, "zero-output") == 0;
	info->id = (is_input ? 0u : 100u) + index;
	if (layout == NULL)
	{
		strncpy(info->name, is_input ? "Input" : "Output", sizeof(info->name) - 1);
	}
	else
	{
		strncpy(info->name, empty ? "Empty" : (is_input ? (index == gain_main_index(true) ? "Main" : "Side-chain")
			: "Output"), sizeof(info->name) - 1);
	}
	info->flags = index == gain_main_index(is_input) ? CLAP_AUDIO_PORT_IS_MAIN : 0;
	info->channel_count = empty ? 0 : 2;
	info->port_type = empty ? NULL : CLAP_PORT_STEREO;
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
	state.version = TEST_PLUGIN_STATE_VERSION;
	state.declared_max_frames = self->declared_max_frames;
	state.blocks = self->blocks;
	state.max_block = self->max_block;
	state.oversized_calls = self->oversized_calls;
	state.recorded_count = self->recorded_count;
	memcpy(state.recorded, self->recorded, sizeof(state.recorded));
	state.gain = self->gain;
	state.bypass = self->bypass;
	state.frames_processed = self->frames_processed;
	state.host_flushes = self->host_flushes;
	return stream->write(stream, &state, sizeof(state)) == (int64_t)sizeof(state);
}

static bool gain_state_load(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	gain_state_t state;
	memset(&state, 0, sizeof(state));
	if (stream->read(stream, &state, sizeof(state)) != (int64_t)sizeof(state)) { return false; }
	if (state.magic != TEST_PLUGIN_MAGIC) { return false; }
	if (state.version != TEST_PLUGIN_STATE_VERSION) { return false; }
	self->gain = state.gain;
	self->bypass = state.bypass;
	self->frames_processed = state.frames_processed;
	self->host_flushes = state.host_flushes;
	/* The counters are part of the saved state, so restoring a state restores
	   what the fixture reports as well: two saves of one state round-trip
	   byte-wise (ClapHostTest::testStateRoundTrip). */
	self->declared_max_frames = state.declared_max_frames;
	self->blocks = state.blocks;
	self->max_block = state.max_block;
	self->oversized_calls = state.oversized_calls;
	self->recorded_count =
		state.recorded_count <= TEST_PLUGIN_MAX_RECORDED_BLOCKS ? state.recorded_count
																: TEST_PLUGIN_MAX_RECORDED_BLOCKS;
	memcpy(self->recorded, state.recorded, sizeof(self->recorded));
	return true;
}

static const clap_plugin_state_t s_state = {
	.save = gain_state_save,
	.load = gain_state_load,
};

/* --------------------------------------------------------------- latency -- */

/* 0 unless CLAP_TEST_GAIN_LATENCY names a frame count: the host's PDC wiring (BUGS 11.34) is
 * proven against a plug-in that declares a known latency, and every other test keeps 0. */
static uint32_t gain_latency_get(const clap_plugin_t* plugin)
{
	(void)plugin;
	const char* frames = getenv("CLAP_TEST_GAIN_LATENCY");
	return frames != NULL ? (uint32_t)strtoul(frames, NULL, 10) : 0u;
}

/* clap.tail: 0 unless CLAP_TEST_GAIN_TAIL names a frame count (engine audit E6). */
static uint32_t gain_tail_get(const clap_plugin_t* plugin)
{
	(void)plugin;
	const char* frames = getenv("CLAP_TEST_GAIN_TAIL");
	return frames != NULL ? (uint32_t)strtoul(frames, NULL, 10) : 0u;
}

static const clap_plugin_tail_t s_tail = {
	.get = gain_tail_get,
};

static const clap_plugin_latency_t s_latency = {
	.get = gain_latency_get,
};

/* --------------------------------------------------------------- plugin --- */

static bool gain_init(const clap_plugin_t* plugin)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	/* R4.1: a host without clap.timer-support is still a valid host - the timer is optional. */
	const clap_host_timer_support_t* timers =
		(const clap_host_timer_support_t*)self->host->get_extension(self->host, CLAP_EXT_TIMER_SUPPORT);
	self->timer_registered = timers != NULL && timers->register_timer(self->host, 10, &self->timer_id);
	return true;
}

static void gain_destroy(const clap_plugin_t* plugin)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	const clap_host_timer_support_t* timers =
		(const clap_host_timer_support_t*)self->host->get_extension(self->host, CLAP_EXT_TIMER_SUPPORT);
	if (self->timer_registered && timers != NULL) { timers->unregister_timer(self->host, self->timer_id); }
	free(plugin->plugin_data);
}

static void gain_on_timer(const clap_plugin_t* plugin, clap_id timer_id)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	if (timer_id == self->timer_id) { ++self->timer_ticks; }
}

/* ------------------------------------------------------------------ gui --- */

/* Q050-490: x11, win32 and cocoa, embedded, as a real cross-platform editor offers;
   CLAP_TEST_GAIN_GUI_API names the only api it takes instead, so the host's refusal can be exercised. */
static bool gui_is_api_supported(const clap_plugin_t* plugin, const char* api, bool is_floating)
{
	(void)plugin;
	if (api == NULL || is_floating) { return false; }
	const char* only = getenv("CLAP_TEST_GAIN_GUI_API");
	if (only != NULL) { return strcmp(api, only) == 0; }
	return strcmp(api, CLAP_WINDOW_API_X11) == 0 || strcmp(api, CLAP_WINDOW_API_WIN32) == 0
		|| strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

static bool gui_get_preferred_api(const clap_plugin_t* plugin, const char** api, bool* is_floating)
{
	(void)plugin;
#if defined(_WIN32)
	*api = CLAP_WINDOW_API_WIN32;
#elif defined(__APPLE__)
	*api = CLAP_WINDOW_API_COCOA;
#else
	*api = CLAP_WINDOW_API_X11;
#endif
	*is_floating = false;
	return true;
}

static bool gui_create(const clap_plugin_t* plugin, const char* api, bool is_floating)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	if (!gui_is_api_supported(plugin, api, is_floating) || self->gui_created) { return false; }
	self->gui_created = true;
	self->gui_api = strcmp(api, CLAP_WINDOW_API_WIN32) == 0 ? CLAP_WINDOW_API_WIN32
		: strcmp(api, CLAP_WINDOW_API_COCOA) == 0 ? CLAP_WINDOW_API_COCOA : CLAP_WINDOW_API_X11;
	/* No set_scale is a scale of 1 (a cocoa host never calls it). A test can require the host to
	   call set_scale with one exact factor before it accepts the parent. */
	self->gui_scale_finite = getenv("CLAP_TEST_GAIN_EXPECT_SCALE") == NULL;
	self->gui_width = 400;
	self->gui_height = 300;
	return true;
}

static void gui_destroy(const clap_plugin_t* plugin)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	self->gui_created = false;
	self->gui_visible = false;
	self->gui_parent = 0;
}

static bool gui_set_scale(const clap_plugin_t* plugin, double scale)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	/* Q050-490: "cocoa uses logical size, don't call set_scale" (clap/ext/gui.h). A host that does
	   anyway is refused the parent, as a non-finite scale is. */
	if (self->gui_api == CLAP_WINDOW_API_COCOA)
	{
		self->gui_scale_finite = false;
		return false;
	}
	const char* expected = getenv("CLAP_TEST_GAIN_EXPECT_SCALE");
	self->gui_scale_finite = isfinite(scale) && (expected == NULL || scale == strtod(expected, NULL));
	return true;
}

static bool gui_get_size(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
	const gain_plugin_t* self = (const gain_plugin_t*)plugin->plugin_data;
	if (!self->gui_created) { return false; }
	*width = self->gui_width;
	*height = self->gui_height;
	return true;
}

static bool gui_can_resize(const clap_plugin_t* plugin) { (void)plugin; return true; }

static bool gui_get_resize_hints(const clap_plugin_t* plugin, clap_gui_resize_hints_t* hints)
{
	(void)plugin;
	hints->can_resize_horizontally = true;
	hints->can_resize_vertically = true;
	hints->preserve_aspect_ratio = false;
	return true;
}

static bool gui_adjust_size(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
	(void)plugin;
	if (*width < 100) { *width = 100; }
	if (*height < 80) { *height = 80; }
	return true;
}

static bool gui_set_size(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	if (!self->gui_created || width < 100 || height < 80) { return false; }
	self->gui_width = width;
	self->gui_height = height;
	return true;
}

static bool gui_set_parent(const clap_plugin_t* plugin, const clap_window_t* window)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	if (!self->gui_created || !self->gui_scale_finite || window == NULL || window->api == NULL ||
		strcmp(window->api, self->gui_api) != 0) { return false; }
	/* The handle is read from the union member of the api the editor was created for. */
	if (self->gui_api == CLAP_WINDOW_API_WIN32) { self->gui_parent = (uint64_t)(uintptr_t)window->win32; }
	else if (self->gui_api == CLAP_WINDOW_API_COCOA) { self->gui_parent = (uint64_t)(uintptr_t)window->cocoa; }
	else { self->gui_parent = (uint64_t)window->x11; }
	return self->gui_parent != 0;
}

static bool gui_set_transient(const clap_plugin_t* plugin, const clap_window_t* window)
{
	(void)plugin;
	(void)window;
	return false;
}

static void gui_suggest_title(const clap_plugin_t* plugin, const char* title) { (void)plugin; (void)title; }

static bool gui_show(const clap_plugin_t* plugin)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	self->gui_visible = self->gui_created && self->gui_parent != 0;
	/* CLAP_TEST_GAIN_REQUEST_RESIZE: once shown, the editor asks for a new size from the main thread. */
	if (self->gui_visible && getenv("CLAP_TEST_GAIN_REQUEST_RESIZE") != NULL) { self->host->request_callback(self->host); }
	return self->gui_visible;
}

static bool gui_hide(const clap_plugin_t* plugin)
{
	((gain_plugin_t*)plugin->plugin_data)->gui_visible = false;
	return true;
}

static const clap_plugin_gui_t s_gui = {
	.is_api_supported = gui_is_api_supported,
	.get_preferred_api = gui_get_preferred_api,
	.create = gui_create,
	.destroy = gui_destroy,
	.set_scale = gui_set_scale,
	.get_size = gui_get_size,
	.can_resize = gui_can_resize,
	.get_resize_hints = gui_get_resize_hints,
	.adjust_size = gui_adjust_size,
	.set_size = gui_set_size,
	.set_parent = gui_set_parent,
	.set_transient = gui_set_transient,
	.suggest_title = gui_suggest_title,
	.show = gui_show,
	.hide = gui_hide,
};

static const clap_plugin_timer_support_t s_timer = {
	.on_timer = gain_on_timer,
};

static bool gain_activate(const clap_plugin_t* plugin, double sample_rate, uint32_t min_frames_count,
	uint32_t max_frames_count)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	(void)sample_rate;
	(void)min_frames_count;
	/* The host tells the plug-in the largest block it will ever be asked for.
	   That number is what makes an over-run observable: process() records any
	   call that asks for more than this. */
	self->declared_max_frames = max_frames_count;
	self->blocks = 0;
	self->max_block = 0;
	self->oversized_calls = 0;
	self->recorded_count = 0;
	memset(self->recorded, 0, sizeof(self->recorded));
	/* With CLAP_TEST_GAIN_MAIN_THREAD set, the plug-in asks for a main-thread call, the path the
	   host services in HostedPlugin::serviceMainThread() (engine audit E8). */
	self->host_flushes = 0;
	self->in_process = 0;
	self->return_pending = 0;
	if (getenv("CLAP_TEST_GAIN_MAIN_THREAD") != NULL || getenv("CLAP_TEST_GAIN_RESCAN") != NULL
		|| getenv("CLAP_TEST_GAIN_REQUEST_FLUSH") != NULL || getenv("CLAP_TEST_GAIN_CLOSE_GUI") != NULL)
	{
		self->host->request_callback(self->host);
	}
	return true;
}

static const clap_host_params_t* gain_host_params(const gain_plugin_t* self)
{
	return (const clap_host_params_t*)self->host->get_extension(self->host, CLAP_EXT_PARAMS);
}

/* W6: deactivated, which is where CLAP lets a plug-in change its parameter list: the restart it asked for. */
static void gain_deactivate(const clap_plugin_t* plugin)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	if (!self->extend_on_deactivate) { return; }
	self->extend_on_deactivate = false;
	self->extended = true;
	const clap_host_params_t* params = gain_host_params(self);
	if (params != NULL) { params->rescan(self->host, CLAP_PARAM_RESCAN_ALL); }
}

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
	self->blocks = 0;
	self->max_block = 0;
	self->oversized_calls = 0;
	self->recorded_count = 0;
	memset(self->recorded, 0, sizeof(self->recorded));
}

/* The routing modes: the main input, at the index the plug-in calls its main, goes to the main output. */
static void gain_route_layout(const clap_process_t* process, double gain)
{
	const uint32_t src = gain_main_index(true);
	const uint32_t dst = gain_main_index(false);
	if (src >= process->audio_inputs_count || dst >= process->audio_outputs_count) { return; }
	const clap_audio_buffer_t* in = &process->audio_inputs[src];
	const clap_audio_buffer_t* out = &process->audio_outputs[dst];
	const uint32_t channels = in->channel_count < out->channel_count ? in->channel_count : out->channel_count;
	for (uint32_t c = 0; c < channels; ++c)
	{
		if (!in->data32[c] || !out->data32[c]) { continue; }
		for (uint32_t f = 0; f < process->frames_count; ++f)
		{
			out->data32[c][f] = (float)(in->data32[c][f] * gain);
		}
	}
}

static clap_process_status gain_process(const clap_plugin_t* plugin, const clap_process_t* process)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	self->in_process = 1;

	/* The host delivers parameter changes through the event list. */
	self->params->flush(plugin, process->in_events, process->out_events);

	/* CLAP_TEST_GAIN_REQUEST_FLUSH: the value the plug-in changed on the main thread goes back to the host
	   through this process() call's output events - the plug-in's own update, not a flush the host makes. */
	if (self->return_pending && process->out_events != NULL)
	{
		clap_event_param_value_t event;
		memset(&event, 0, sizeof(event));
		event.header.size = sizeof(event);
		event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
		event.header.type = CLAP_EVENT_PARAM_VALUE;
		event.param_id = PARAM_GAIN;
		event.value = self->gain;
		process->out_events->try_push(process->out_events, &event.header);
		self->return_pending = 0;
	}

	/* CLAP_TEST_GAIN_PROCESS_ERROR: report a processing error without writing the output block, the way
	   a plug-in that fails partway through leaves the rest of the buffer as the host handed it over. */
	if (getenv("CLAP_TEST_GAIN_PROCESS_ERROR") != NULL)
	{
		self->in_process = 0;
		return CLAP_PROCESS_ERROR;
	}

	const double gain = self->bypass >= 0.5 ? 1.0 : self->gain;
	const uint32_t frames = process->frames_count;
	const uint32_t input_count = process->audio_inputs_count;
	const char* layout = gain_layout();
	if (layout != NULL) { gain_route_layout(process, gain); }
	for (uint32_t p = 0; p < (layout != NULL ? 0u : process->audio_outputs_count); ++p)
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
	/* The chunking witnesses. `oversized_calls` counts calls that asked for
	   more frames than activate() declared - an over-run at the plug-in's
	   boundary; `recorded` is the exact sequence of chunk sizes the host asked
	   for, so a test can assert the shape and not just the total. */
	self->blocks += 1;
	self->frames_processed += frames;
	if (frames > self->max_block) { self->max_block = frames; }
	if (self->declared_max_frames != 0 && frames > self->declared_max_frames)
	{
		self->oversized_calls += 1;
	}
	if (self->recorded_count < TEST_PLUGIN_MAX_RECORDED_BLOCKS)
	{
		self->recorded[self->recorded_count] = frames;
		self->recorded_count += 1;
	}
	self->in_process = 0;
	return CLAP_PROCESS_CONTINUE;
}

static const void* gain_get_extension(const clap_plugin_t* plugin, const char* id)
{
	(void)plugin;
	if (strcmp(id, CLAP_EXT_PARAMS) == 0) { return &s_params; }
	if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) { return &s_audio_ports; }
	if (strcmp(id, CLAP_EXT_STATE) == 0) { return &s_state; }
	if (strcmp(id, CLAP_EXT_LATENCY) == 0) { return &s_latency; }
	if (strcmp(id, CLAP_EXT_TAIL) == 0) { return &s_tail; }
	if (strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) { return &s_timer; }
	if (strcmp(id, CLAP_EXT_GUI) == 0) { return &s_gui; }
	return NULL;
}

/* On the main-thread call: change a value behind the host's back, ask it to re-read the values,
   and mark the state dirty - three requests a host must honour. */
/* W6, CLAP_TEST_GAIN_RESCAN: "info" renames a parameter in place; "all" changes the list, which an active plug-in
   may only do after a restart (gain_deactivate). */
static void gain_request_rescan(gain_plugin_t* self, const char* kind)
{
	if (strcmp(kind, "info") == 0)
	{
		self->renamed = true;
		const clap_host_params_t* params = gain_host_params(self);
		if (params != NULL) { params->rescan(self->host, CLAP_PARAM_RESCAN_INFO); }
	}
	else if (strcmp(kind, "all") == 0 && !self->extended)
	{
		self->extend_on_deactivate = true;
		self->host->request_restart(self->host);
	}
}

static void gain_on_main_thread(const clap_plugin_t* plugin)
{
	gain_plugin_t* self = (gain_plugin_t*)plugin->plugin_data;
	/* Q050-490, CLAP_TEST_GAIN_REQUEST_RESIZE="<w>x<h>": the plug-in resizes its OWN editor (a resize
	   handle in the editor) through clap.gui's request_resize; the host grants it with set_size. */
	const char* resize = getenv("CLAP_TEST_GAIN_REQUEST_RESIZE");
	if (resize != NULL && self->gui_created)
	{
		char* end = NULL;
		const uint32_t resize_width = (uint32_t)strtoul(resize, &end, 10);
		const uint32_t resize_height = *end == 'x' ? (uint32_t)strtoul(end + 1, NULL, 10) : 0u;
		const clap_host_gui_t* resizeGui =
			(const clap_host_gui_t*)self->host->get_extension(self->host, CLAP_EXT_GUI);
		if (resizeGui != NULL) { resizeGui->request_resize(self->host, resize_width, resize_height); }
		return;
	}
	/* CLAP_TEST_GAIN_CLOSE_GUI: the plug-in closes its OWN editor from the editor's side, the way a
	   plug-in with a close button does - "hide" leaves the GUI alive, "destroy" takes it down and says
	   so with was_destroyed. The host must reopen it correctly afterwards. */
	const char* closeGui = getenv("CLAP_TEST_GAIN_CLOSE_GUI");
	if (closeGui != NULL && self->gui_created)
	{
		const clap_host_gui_t* hostGui =
			(const clap_host_gui_t*)self->host->get_extension(self->host, CLAP_EXT_GUI);
		if (hostGui != NULL)
		{
			const bool destroyed = strcmp(closeGui, "destroy") == 0;
			if (destroyed) { s_gui.destroy(plugin); }
			else { s_gui.hide(plugin); }
			hostGui->closed(self->host, destroyed);
		}
		return;
	}
	const char* rescan = getenv("CLAP_TEST_GAIN_RESCAN");
	if (rescan != NULL) { gain_request_rescan(self, rescan); }
	/* CLAP_TEST_GAIN_REQUEST_FLUSH: the plug-in changed a value behind the host's back and asks for
	   the flush, while it is active - the host has to answer it on the audio thread. */
	if (getenv("CLAP_TEST_GAIN_REQUEST_FLUSH") != NULL)
	{
		self->gain = 0.75;
		self->return_pending = 1;
		const clap_host_params_t* flushParams =
			(const clap_host_params_t*)self->host->get_extension(self->host, CLAP_EXT_PARAMS);
		if (flushParams != NULL) { flushParams->request_flush(self->host); }
		return;
	}
	if (getenv("CLAP_TEST_GAIN_MAIN_THREAD") == NULL) { return; }
	self->gain = 0.25;
	const clap_host_params_t* params = (const clap_host_params_t*)self->host->get_extension(self->host, CLAP_EXT_PARAMS);
	if (params != NULL) { params->rescan(self->host, CLAP_PARAM_RESCAN_VALUES); }
	const clap_host_state_t* state = (const clap_host_state_t*)self->host->get_extension(self->host, CLAP_EXT_STATE);
	if (state != NULL) { state->mark_dirty(self->host); }
}

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
	self->drive = 2.0;
	self->tone = 0.5;
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
