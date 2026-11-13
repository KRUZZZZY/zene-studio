/*
 * vst2-test-effect.c - the in-tree VST2 test effect (Q050-481.2).
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*
 * A deterministic VST2 effect that exists only to be hosted through Carla by
 * the product's VST2 effect path (plugins/CarlaVst2Effect). Like the test
 * instrument beside it (tests/data/vst2-test-instrument, whose clean-room ABI
 * header it shares - no Steinberg header), the SAME source builds as a native
 * .so and, with winegcc, as a Windows PE .dll that Carla can only reach through
 * its Wine bridge. Never installed.
 *
 * What it does, so a test can see each host feature from the outside:
 *  - every output frame is (left, right) = (2 * Left * inL, 2 * Right * inR):
 *    its two parameters, 0.5 (unity) by default;
 *  - it counts the blocks in which it heard a non-silent input. The count is
 *    NOT a parameter: it travels only in the plug-in's chunk, so a host that
 *    kept the parameters but lost the chunk is caught;
 *  - its editor records the parent window it was handed and reports a fixed
 *    240x120 rectangle; it draws nothing. Whether it is open is reported in the
 *    chunk (editor=1), never restored from it;
 *  - with ZENE_VST2_FIXTURE_STALL_MS set when it is instantiated, every process
 *    call sleeps that long: a stand-in for a slow plug-in or a stalled bridge.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 199309L  /* nanosleep */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "zene_vst2_abi.h"

enum
{
	ParamLeft = 0,
	ParamRight = 1,
	ParamCount = 2,
	EditorWidth = 240,
	EditorHeight = 120
};

typedef struct
{
	ZvAEffect effect;
	ZvHostCallback host;
	float params[ParamCount];
	int heard;
	void* editorParent;
	ZvRect rect;
	char chunk[128];
	long stallMs;
} Instance;

static Instance* instanceOf(ZvAEffect* effect) { return (Instance*)effect->object; }

static void copyText(void* destination, const char* text, size_t capacity)
{
	char* out = (char*)destination;
	size_t i = 0;
	for (; text[i] != '\0' && i + 1 < capacity; ++i) { out[i] = text[i]; }
	out[i] = '\0';
}

static float clampUnit(float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

static void stall(long ms)
{
	if (ms <= 0) { return; }
#ifdef _WIN32
	Sleep((DWORD)ms);
#else
	struct timespec pause;
	pause.tv_sec = ms / 1000;
	pause.tv_nsec = (ms % 1000) * 1000000L;
	nanosleep(&pause, NULL);
#endif
}

static void ZENE_VST_CALL processReplacing(ZvAEffect* effect, float** inputs, float** outputs, int32_t frames)
{
	Instance* self = instanceOf(effect);
	const float left = 2.0f * self->params[ParamLeft];
	const float right = 2.0f * self->params[ParamRight];
	int heard = 0;
	stall(self->stallMs);
	for (int32_t f = 0; f < frames; ++f)
	{
		const float inL = inputs[0][f];
		const float inR = inputs[1][f];
		if (inL != 0.0f || inR != 0.0f) { heard = 1; }
		outputs[0][f] = inL * left;
		outputs[1][f] = inR * right;
	}
	self->heard += heard;
}

static void ZENE_VST_CALL setParameter(ZvAEffect* effect, int32_t index, float value)
{
	if (index >= 0 && index < ParamCount) { instanceOf(effect)->params[index] = clampUnit(value); }
}

static float ZENE_VST_CALL getParameter(ZvAEffect* effect, int32_t index)
{
	return index >= 0 && index < ParamCount ? instanceOf(effect)->params[index] : 0.0f;
}

//! "ZNE2 left=<micro> right=<micro> heard=<n> editor=<0|1>": integers, so no locale reads a comma.
static intptr_t writeChunk(Instance* self, void** data)
{
	if (data == NULL) { return 0; }
	snprintf(self->chunk, sizeof(self->chunk), "ZNE2 left=%ld right=%ld heard=%d editor=%d",
		(long)(self->params[ParamLeft] * 1000000.0f + 0.5f), (long)(self->params[ParamRight] * 1000000.0f + 0.5f),
		self->heard, self->editorParent != NULL ? 1 : 0);
	*data = self->chunk;
	return (intptr_t)strlen(self->chunk) + 1;
}

static intptr_t readChunk(Instance* self, const void* data, intptr_t size)
{
	char text[128];
	long left = 0;
	long right = 0;
	int heard = 0;
	if (data == NULL || size <= 0 || size >= (intptr_t)sizeof(text)) { return 0; }
	memcpy(text, data, (size_t)size);
	text[size] = '\0';
	if (sscanf(text, "ZNE2 left=%ld right=%ld heard=%d", &left, &right, &heard) != 3) { return 0; }  /* editor= is not state */
	self->params[ParamLeft] = clampUnit((float)left / 1000000.0f);
	self->params[ParamRight] = clampUnit((float)right / 1000000.0f);
	self->heard = heard;
	return 1;
}

static const char* const ParamNames[ParamCount] = {"Left", "Right"};

static int describeParameter(Instance* self, int32_t opcode, int32_t index, void* ptr)
{
	const int valid = index >= 0 && index < ParamCount;
	switch (opcode)
	{
	case zvGetParamName:
		if (valid) { copyText(ptr, ParamNames[index], 8); }
		return 1;
	case zvGetParamLabel: copyText(ptr, "", 8); return 1;
	case zvGetParamDisplay:
		if (valid) { snprintf((char*)ptr, 8, "%.3f", (double)self->params[index]); }
		return 1;
	default: return 0;
	}
}

static intptr_t describe(int32_t opcode, void* ptr)
{
	switch (opcode)
	{
	case zvGetEffectName: copyText(ptr, "Zene VST2 Test Effect", 32); return 1;
	case zvGetProductString: copyText(ptr, "Zene VST2 Test Effect", 64); return 1;
	case zvGetVendorString: copyText(ptr, "Zene Studio Tests", 64); return 1;
	case zvGetVendorVersion: return 1000;
	case zvGetVstVersion: return 2400;
	case zvGetPlugCategory: return zvPlugCategoryEffect;
	default: return 0;
	}
}

static intptr_t editor(Instance* self, int32_t opcode, void* ptr)
{
	switch (opcode)
	{
	case zvEditGetRect:
		self->rect.top = 0;
		self->rect.left = 0;
		self->rect.bottom = EditorHeight;
		self->rect.right = EditorWidth;
		if (ptr != NULL) { *(ZvRect**)ptr = &self->rect; }
		return 1;
	case zvEditOpen:
		self->editorParent = ptr;
		return ptr != NULL ? 1 : 0;
	case zvEditClose:
		self->editorParent = NULL;
		return 1;
	default: return 0;
	}
}

static intptr_t ZENE_VST_CALL dispatcher(ZvAEffect* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr,
	float opt)
{
	Instance* self = instanceOf(effect);
	(void)opt;
	if (opcode == zvClose)
	{
		free(self);
		return 1;
	}
	if (opcode == zvEditGetRect || opcode == zvEditOpen || opcode == zvEditClose) { return editor(self, opcode, ptr); }
	switch (opcode)
	{
	case zvGetChunk: return writeChunk(self, (void**)ptr);
	case zvSetChunk: return readChunk(self, ptr, value);
	default: return describeParameter(self, opcode, index, ptr) ? 0 : describe(opcode, ptr);
	}
}

ZENE_VST_EXPORT ZvAEffect* VSTPluginMain(ZvHostCallback host);

ZENE_VST_EXPORT ZvAEffect* VSTPluginMain(ZvHostCallback host)
{
	if (host == NULL || host(NULL, zvMasterVersion, 0, 0, NULL, 0.0f) == 0) { return NULL; }
	Instance* self = (Instance*)calloc(1, sizeof(Instance));
	if (self == NULL) { return NULL; }
	self->host = host;
	self->stallMs = getenv("ZENE_VST2_FIXTURE_STALL_MS") != NULL ? atol(getenv("ZENE_VST2_FIXTURE_STALL_MS")) : 0;
	self->params[ParamLeft] = 0.5f;
	self->params[ParamRight] = 0.5f;
	self->effect.magic = zvMagic;
	self->effect.dispatcher = dispatcher;
	self->effect.setParameter = setParameter;
	self->effect.getParameter = getParameter;
	self->effect.processReplacing = processReplacing;
	self->effect.numPrograms = 0;
	self->effect.numParams = ParamCount;
	self->effect.numInputs = 2;
	self->effect.numOutputs = 2;
	self->effect.flags = zvFlagsHasEditor | zvFlagsCanReplacing | zvFlagsProgramChunks;
	self->effect.ioRatio = 1.0f;
	self->effect.object = self;
	self->effect.uniqueID = ZENE_VST_FOURCC('Z', 'n', 'E', '2');
	self->effect.version = 1000;
	return &self->effect;
}
