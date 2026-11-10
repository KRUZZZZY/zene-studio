/*
 * vst2-test-instrument.c - the in-tree VST2 test instrument (Q050-481.1).
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
 * A deterministic VST2 instrument that exists only to be hosted through Carla
 * by the product's VST2 path (plugins/CarlaVst2). The SAME source builds twice:
 * a native Linux .so (the in-process path) and, with winegcc, a Windows PE .dll
 * that Carla can only reach through its Wine bridge. Never installed.
 *
 * What it does, so a test can see each host feature from the outside:
 *  - while at least one key is held, every output frame is
 *    (left, right) = (Level, Tone) - the two parameters - and silence otherwise;
 *    note-ons and note-offs take effect at their sample offset in the block;
 *  - it counts the note-ons it has received. The count is NOT a parameter: it
 *    travels only in the plug-in's chunk (effGetChunk/effSetChunk), so a host
 *    that kept the parameters but lost the chunk is caught;
 *  - its editor records the parent window it was handed and reports a fixed
 *    240x120 rectangle; it draws nothing. Whether it is open is reported in the
 *    chunk (editor=1), never restored from it;
 *  - with ZENE_VST2_FIXTURE_STALL_MS set when it is instantiated, every process
 *    call sleeps that long: a stand-in for a slow plug-in or a stalled bridge,
 *    so a test can show the host's audio thread does not wait for it.
 */

/* nanosleep is POSIX.1b (199309L). Darwin's default feature level already exposes it, and
 * defining _POSIX_C_SOURCE there selects the strict POSIX level, which hides the C99 snprintf
 * declaration (macos-x86_64 run 42805458633: "call to undeclared library function 'snprintf'").
 */
#if !defined(_WIN32) && !defined(__APPLE__)
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
	ParamLevel = 0,
	ParamTone = 1,
	ParamCount = 2,
	MaxPendingEvents = 64,
	EditorWidth = 240,
	EditorHeight = 120
};

typedef struct
{
	int32_t frame;
	int on;
} PendingEvent;

typedef struct
{
	ZvAEffect effect;
	ZvHostCallback host;
	float params[ParamCount];
	int heldKeys;
	int noteOns;
	PendingEvent pending[MaxPendingEvents];
	int pendingCount;
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

static void queueMidi(Instance* self, const ZvMidiEvent* event)
{
	const int status = (unsigned char)event->midiData[0] & 0xF0;
	const int velocity = (unsigned char)event->midiData[2];
	int on = -1;
	if (status == 0x90 && velocity > 0) { on = 1; }
	else if (status == 0x80 || (status == 0x90 && velocity == 0)) { on = 0; }
	if (on < 0 || self->pendingCount >= MaxPendingEvents) { return; }
	self->pending[self->pendingCount].frame = event->deltaFrames;
	self->pending[self->pendingCount].on = on;
	++self->pendingCount;
}

static void applyEvent(Instance* self, const PendingEvent* event)
{
	if (event->on)
	{
		++self->heldKeys;
		++self->noteOns;
	}
	else if (self->heldKeys > 0) { --self->heldKeys; }
}

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
	stall(self->stallMs);
	int next = 0;
	(void)inputs;
	for (int32_t f = 0; f < frames; ++f)
	{
		while (next < self->pendingCount && self->pending[next].frame <= f) { applyEvent(self, &self->pending[next++]); }
		outputs[0][f] = self->heldKeys > 0 ? self->params[ParamLevel] : 0.0f;
		outputs[1][f] = self->heldKeys > 0 ? self->params[ParamTone] : 0.0f;
	}
	while (next < self->pendingCount) { applyEvent(self, &self->pending[next++]); }
	self->pendingCount = 0;
}

static void ZENE_VST_CALL setParameter(ZvAEffect* effect, int32_t index, float value)
{
	if (index >= 0 && index < ParamCount) { instanceOf(effect)->params[index] = clampUnit(value); }
}

static float ZENE_VST_CALL getParameter(ZvAEffect* effect, int32_t index)
{
	return index >= 0 && index < ParamCount ? instanceOf(effect)->params[index] : 0.0f;
}

//! "ZNV2 level=<micro> tone=<micro> noteons=<n> editor=<0|1>": integers, so no locale reads a comma. The
//! editor flag is reported, never restored: it lets a test see that the host opened and closed this
//! plug-in's own editor.
static intptr_t writeChunk(Instance* self, void** data)
{
	if (data == NULL) { return 0; }
	snprintf(self->chunk, sizeof(self->chunk), "ZNV2 level=%ld tone=%ld noteons=%d editor=%d",
		(long)(self->params[ParamLevel] * 1000000.0f + 0.5f), (long)(self->params[ParamTone] * 1000000.0f + 0.5f),
		self->noteOns, self->editorParent != NULL ? 1 : 0);
	*data = self->chunk;
	return (intptr_t)strlen(self->chunk) + 1;
}

static intptr_t readChunk(Instance* self, const void* data, intptr_t size)
{
	char text[128];
	long level = 0;
	long tone = 0;
	int noteOns = 0;
	if (data == NULL || size <= 0 || size >= (intptr_t)sizeof(text)) { return 0; }
	memcpy(text, data, (size_t)size);
	text[size] = '\0';
	if (sscanf(text, "ZNV2 level=%ld tone=%ld noteons=%d", &level, &tone, &noteOns) != 3) { return 0; }  /* editor= is not state */
	self->params[ParamLevel] = clampUnit((float)level / 1000000.0f);
	self->params[ParamTone] = clampUnit((float)tone / 1000000.0f);
	self->noteOns = noteOns;
	return 1;
}

static const char* const ParamNames[ParamCount] = {"Level", "Tone"};

//! The parameter strings: 1 when @a opcode was one of them.
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

static intptr_t canDo(const char* what)
{
	if (what == NULL) { return 0; }
	return strcmp(what, "receiveVstEvents") == 0 || strcmp(what, "receiveVstMidiEvent") == 0 ? 1 : 0;
}

//! Who the plug-in is, what it is and what it can do.
static intptr_t describe(int32_t opcode, void* ptr)
{
	switch (opcode)
	{
	case zvGetEffectName: copyText(ptr, "Zene VST2 Test Instrument", 32); return 1;
	case zvGetProductString: copyText(ptr, "Zene VST2 Test Instrument", 64); return 1;
	case zvGetVendorString: copyText(ptr, "Zene Studio Tests", 64); return 1;
	case zvGetVendorVersion: return 1000;
	case zvGetVstVersion: return 2400;
	case zvGetPlugCategory: return zvPlugCategorySynth;
	case zvCanDo: return canDo((const char*)ptr);
	default: return 0;
	}
}

//! The editor: it records the parent window it was handed and reports a fixed rectangle.
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

static intptr_t processEvents(Instance* self, const ZvEvents* events)
{
	for (int32_t i = 0; events != NULL && i < events->numEvents; ++i)
	{
		if (events->events[i] != NULL && events->events[i]->type == zvMidiType) { queueMidi(self, events->events[i]); }
	}
	return 1;
}

static int isEditorOpcode(int32_t opcode)
{
	return opcode == zvEditGetRect || opcode == zvEditOpen || opcode == zvEditClose;
}

static intptr_t mainsChanged(Instance* self, intptr_t on)
{
	if (on == 0)
	{
		self->heldKeys = 0;
		self->pendingCount = 0;
	}
	return 0;
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
	if (isEditorOpcode(opcode)) { return editor(self, opcode, ptr); }
	switch (opcode)
	{
	case zvProcessEvents: return processEvents(self, (const ZvEvents*)ptr);
	case zvGetChunk: return writeChunk(self, (void**)ptr);
	case zvSetChunk: return readChunk(self, ptr, value);
	case zvMainsChanged: return mainsChanged(self, value);
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
	self->params[ParamLevel] = 0.5f;
	self->params[ParamTone] = 0.25f;
	self->effect.magic = zvMagic;
	self->effect.dispatcher = dispatcher;
	self->effect.setParameter = setParameter;
	self->effect.getParameter = getParameter;
	self->effect.processReplacing = processReplacing;
	self->effect.numPrograms = 0;
	self->effect.numParams = ParamCount;
	self->effect.numInputs = 0;
	self->effect.numOutputs = 2;
	self->effect.flags = zvFlagsHasEditor | zvFlagsCanReplacing | zvFlagsProgramChunks | zvFlagsIsSynth;
	self->effect.ioRatio = 1.0f;
	self->effect.object = self;
	self->effect.uniqueID = ZENE_VST_FOURCC('Z', 'n', 'V', '2');
	self->effect.version = 1000;
	return &self->effect;
}
