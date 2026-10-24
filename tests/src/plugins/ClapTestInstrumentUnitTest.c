/*
 * Direct regression tests for the in-tree CLAP instrument fixture.
 *
 * The fixture is included here so the tests can call its static callbacks
 * with state and events that the production host does not generate.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

int main(int argc, char** argv)
{
	if (argc != 2 || strcmp(argv[1], "state") != 0)
	{
		fprintf(stderr, "usage: %s state\n", argv[0]);
		return 2;
	}
	return test_state_gain_validation();
}
