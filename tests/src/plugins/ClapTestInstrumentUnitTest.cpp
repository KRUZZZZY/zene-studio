/*
 * Direct regression tests for the in-tree CLAP instrument fixture.
 *
 * The fixture is included here so the tests can call its static callbacks
 * with state and events that the production host does not generate.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// The included fixture's entry symbol is not exported from this unit-test executable.
#define CLAP_EXPORT
#include "clap-test-instrument.c"

#define CHECK(condition, message) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "FAIL: %s\n", message); \
			return 1; \
		} \
	} while (0)

typedef struct test_state_blob
{
	instrument_state_t state;
	int64_t available;
} test_state_blob_t;

typedef struct test_event_list
{
	clap_input_events_t events;
	const clap_event_header_t* entries[2];
	uint32_t count;
} test_event_list_t;

static int64_t test_state_read(const clap_istream_t* stream, void* buffer, uint64_t size)
{
	const test_state_blob_t* blob = (const test_state_blob_t*)stream->ctx;
	const int64_t count = (uint64_t)blob->available < size ? blob->available : (int64_t)size;
	memcpy(buffer, &blob->state, (size_t)count);
	return count;
}

static bool load_test_state(clap_plugin_t* plugin, test_state_blob_t* blob,
	double gain, uint32_t magic, uint32_t version, int64_t available)
{
	memset(&blob->state, 0, sizeof(blob->state));
	blob->state.magic = magic;
	blob->state.version = version;
	blob->state.gain = gain;
	blob->available = available;
	const clap_istream_t stream = {.ctx = blob, .read = test_state_read};
	return instrument_state_load(plugin, &stream);
}

static int test_state_gain_validation(void)
{
	instrument_plugin_t self = {0};
	clap_plugin_t plugin = {0};
	test_state_blob_t blob = {0};
	plugin.plugin_data = &self;

	self.gain = 0.625;
	CHECK(load_test_state(&plugin, &blob, 0.0, TEST_INSTRUMENT_MAGIC,
		TEST_INSTRUMENT_STATE_VERSION, (int64_t)sizeof(blob.state)),
		"valid minimum gain state was rejected");
	CHECK(self.gain == 0.0, "valid minimum gain was not restored");
	CHECK(load_test_state(&plugin, &blob, 1.0, TEST_INSTRUMENT_MAGIC,
		TEST_INSTRUMENT_STATE_VERSION, (int64_t)sizeof(blob.state)),
		"valid maximum gain state was rejected");
	CHECK(self.gain == 1.0, "valid maximum gain was not restored");

	const double invalid_gains[] = {NAN, INFINITY, -INFINITY, -0.01, 1.01};
	for (size_t i = 0; i < sizeof(invalid_gains) / sizeof(invalid_gains[0]); ++i)
	{
		self.gain = 0.625;
		CHECK(!load_test_state(&plugin, &blob, invalid_gains[i], TEST_INSTRUMENT_MAGIC,
			TEST_INSTRUMENT_STATE_VERSION, (int64_t)sizeof(blob.state)),
			"invalid gain state was accepted");
		CHECK(self.gain == 0.625, "rejected state changed the current gain");
	}

	self.gain = 0.625;
	CHECK(!load_test_state(&plugin, &blob, 0.5, TEST_INSTRUMENT_MAGIC,
		TEST_INSTRUMENT_STATE_VERSION, (int64_t)sizeof(blob.state) - 1),
		"truncated state was accepted");
	CHECK(self.gain == 0.625, "truncated state changed the current gain");
	CHECK(!load_test_state(&plugin, &blob, 0.5, TEST_INSTRUMENT_MAGIC + 1,
		TEST_INSTRUMENT_STATE_VERSION, (int64_t)sizeof(blob.state)),
		"bad-magic state was accepted");
	CHECK(self.gain == 0.625, "bad-magic state changed the current gain");
	return 0;
}

static uint32_t test_event_list_size(const clap_input_events_t* events)
{
	const test_event_list_t* list = (const test_event_list_t*)events->ctx;
	return list->count;
}

static const clap_event_header_t* test_event_list_get(const clap_input_events_t* events,
	uint32_t index)
{
	const test_event_list_t* list = (const test_event_list_t*)events->ctx;
	return index < list->count ? list->entries[index] : NULL;
}

static int test_non_core_event_count(void)
{
	instrument_plugin_t self = {0};
	clap_plugin_t plugin = {0};
	clap_event_header_t non_core = {0};
	test_event_list_t list = {0};
	clap_process_t process = {0};
	plugin.plugin_data = &self;

	non_core.size = sizeof(non_core);
	non_core.space_id = CLAP_CORE_EVENT_SPACE_ID + 1;
	non_core.type = 0x7f;
	list.entries[0] = &non_core;
	list.entries[1] = NULL;
	list.count = 2;
	list.events.ctx = &list;
	list.events.size = test_event_list_size;
	list.events.get = test_event_list_get;
	process.in_events = &list.events;

	CHECK(instrument_process(&plugin, &process) == CLAP_PROCESS_CONTINUE,
		"process did not continue after ignored events");
	CHECK(self.event_count == 1, "non-core event was omitted from the total event count");
	CHECK(self.ignored_event_count == 2, "non-core and null entries were not both ignored");
	return 0;
}

static int test_activation_counters_reset(void)
{
	instrument_plugin_t self = {0};
	clap_plugin_t plugin = {0};
	clap_process_t process = {0};
	plugin.plugin_data = &self;
	self.layout_ports = instrument_output_ports();
	self.event_count = 5;
	self.note_on_count = 2;

	CHECK(instrument_activate(&plugin, 48000.0, 1, 64),
		"initial activation failed");
	process.frames_count = 65;
	CHECK(instrument_process(&plugin, &process) == CLAP_PROCESS_CONTINUE,
		"initial process call failed");
	CHECK(self.frames_processed == 65.0 && self.blocks == 1 && self.max_block == 65 &&
		self.oversized_calls == 1,
		"initial activation did not record its processing metrics");

	instrument_deactivate(&plugin);
	CHECK(instrument_activate(&plugin, 48000.0, 1, 128),
		"reactivation failed");
	CHECK(self.declared_max_frames == 128, "reactivation did not update the declared maximum");
	CHECK(self.frames_processed == 0.0 && self.blocks == 0 && self.max_block == 0 &&
		self.oversized_calls == 0,
		"activation-scoped processing metrics survived reactivation");
	CHECK(self.event_count == 5 && self.note_on_count == 2,
		"session event counters were reset during reactivation");
	return 0;
}

int main(int argc, char** argv)
{
	if (argc == 2 && strcmp(argv[1], "state") == 0)
	{
		return test_state_gain_validation();
	}
	if (argc == 2 && strcmp(argv[1], "events") == 0)
	{
		return test_non_core_event_count();
	}
	if (argc == 2 && strcmp(argv[1], "activation") == 0)
	{
		return test_activation_counters_reset();
	}
	else
	{
		fprintf(stderr, "usage: %s state|events|activation\n", argv[0]);
		return 2;
	}
}
