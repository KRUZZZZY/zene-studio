/*
 * chunk-contract-check.c - the ZNV2 chunk contract of the VST2 test instrument
 * (Q050-481.1), exercised directly (ci-d14f7617bae6160d).
 *
 * The hosted macos-x86_64 build failed on this fixture's feature-test macro, not on its
 * behaviour, so the chunk paths are checked here as well: the written text, the parse,
 * the 128-byte read guard and the rejection of malformed input. Carla only ever hands a
 * host well-formed chunks, so the boundary and invalid branches are unreachable through
 * CarlaVst2HostingTest and are covered here instead.
 *
 * This program is test-only and never installed.
 */

#include "vst2-test-instrument.c"

#include <stdio.h>

static int failures = 0;

static void check(int condition, const char* expression)
{
	if (!condition) { printf("FAIL %s\n", expression); ++failures; }
}

int main(void)
{
	Instance* self = (Instance*)calloc(1, sizeof(Instance));
	check(self != NULL, "calloc self");
	if (self == NULL) { return 1; }
	self->params[ParamLevel] = 0.5f;
	self->params[ParamTone] = 0.25f;
	self->noteOns = 7;

	void* data = NULL;
	const intptr_t size = writeChunk(self, &data);
	check(size > 0, "writeChunk returns a size");
	check(strcmp((const char*)data, "ZNV2 level=500000 tone=250000 noteons=7 editor=0") == 0, "ZNV2 text");

	Instance* restored = (Instance*)calloc(1, sizeof(Instance));
	check(restored != NULL, "calloc restored");
	if (restored == NULL) { free(self); return 1; }
	check(readChunk(restored, data, size) == 1, "round-trip accepted");
	check(restored->params[ParamLevel] == self->params[ParamLevel], "level round-trip");
	check(restored->params[ParamTone] == self->params[ParamTone], "tone round-trip");
	check(restored->noteOns == 7, "noteons round-trip");

	/* invalid input: null data, non-positive size, malformed text */
	check(readChunk(restored, NULL, size) == 0, "null data rejected");
	check(readChunk(restored, data, 0) == 0, "zero size rejected");
	check(readChunk(restored, "GARBAGE", 7) == 0, "malformed text rejected");

	/* boundary: the read guard rejects size == sizeof(text) (128) */
	check(readChunk(restored, data, (intptr_t)sizeof(self->chunk)) == 0, "128-byte size rejected");

	/* boundary: 127 bytes holding a valid chunk are accepted */
	char boundary[127];
	memcpy(boundary, data, sizeof(boundary) - 1);
	boundary[sizeof(boundary) - 1] = '\0';
	check(readChunk(restored, boundary, (intptr_t)sizeof(boundary)) == 1, "127-byte size accepted");
	check(restored->noteOns == 7, "127-byte chunk parsed");

	free(self);
	free(restored);
	if (failures != 0) { printf("%d failure(s)\n", failures); return 1; }
	printf("chunk contract OK\n");
	return 0;
}
