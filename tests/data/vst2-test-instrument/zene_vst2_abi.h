/*
 * zene_vst2_abi.h - the few VST2 ABI declarations the in-tree VST2 test
 *                   instrument needs, in plain C (Q050-481.1).
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
 * NOT a Steinberg header (AGENTS.md rule 8). The layout is the clean-room one
 * the tree already carries in include/aeffectx.h (VeSTige, GPLv2+), restated in
 * C so the same fixture source builds as a native .so with the host compiler
 * and as a Windows PE .dll with winegcc, where no C++ runtime exists. Only what
 * the fixture touches is declared; the opcode numbers are the ones VeSTige and
 * Carla's own vestige.h use.
 */

#ifndef ZENE_VST2_ABI_H
#define ZENE_VST2_ABI_H

#include <stdint.h>

#ifdef _WIN32
#define ZENE_VST_CALL __cdecl
#define ZENE_VST_EXPORT __declspec(dllexport)
#else
#define ZENE_VST_CALL
#define ZENE_VST_EXPORT __attribute__((visibility("default")))
#endif

#define ZENE_VST_FOURCC(a, b, c, d) \
	((int32_t)(((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d)))

enum
{
	zvMagic = ZENE_VST_FOURCC('V', 's', 't', 'P'),

	zvFlagsHasEditor = 1,
	zvFlagsCanReplacing = 1 << 4,
	zvFlagsProgramChunks = 1 << 5,
	zvFlagsIsSynth = 1 << 8,

	zvOpen = 0,
	zvClose = 1,
	zvGetProgram = 3,
	zvGetParamLabel = 6,
	zvGetParamDisplay = 7,
	zvGetParamName = 8,
	zvSetSampleRate = 10,
	zvSetBlockSize = 11,
	zvMainsChanged = 12,
	zvEditGetRect = 13,
	zvEditOpen = 14,
	zvEditClose = 15,
	zvGetChunk = 23,
	zvSetChunk = 24,
	zvProcessEvents = 25,
	zvGetPlugCategory = 35,
	zvGetEffectName = 45,
	zvGetVendorString = 47,
	zvGetProductString = 48,
	zvGetVendorVersion = 49,
	zvCanDo = 51,
	zvGetVstVersion = 58,

	zvPlugCategoryEffect = 1,  /* Q050-481.2: the test effect */
	zvPlugCategorySynth = 2,
	zvMidiType = 1,

	zvMasterVersion = 1
};

typedef struct ZvAEffect ZvAEffect;

typedef intptr_t (ZENE_VST_CALL* ZvHostCallback)(ZvAEffect*, int32_t, int32_t, intptr_t, void*, float);

struct ZvAEffect
{
	int32_t magic;
	intptr_t (ZENE_VST_CALL* dispatcher)(ZvAEffect*, int32_t, int32_t, intptr_t, void*, float);
	void (ZENE_VST_CALL* process)(ZvAEffect*, float**, float**, int32_t);
	void (ZENE_VST_CALL* setParameter)(ZvAEffect*, int32_t, float);
	float (ZENE_VST_CALL* getParameter)(ZvAEffect*, int32_t);
	int32_t numPrograms;
	int32_t numParams;
	int32_t numInputs;
	int32_t numOutputs;
	int32_t flags;
	intptr_t reserved1;
	intptr_t reserved2;
	int32_t initialDelay;
	int32_t realQualities;
	int32_t offQualities;
	float ioRatio;
	void* object;
	void* user;
	int32_t uniqueID;
	int32_t version;
	void (ZENE_VST_CALL* processReplacing)(ZvAEffect*, float**, float**, int32_t);
	void (ZENE_VST_CALL* processDoubleReplacing)(ZvAEffect*, double**, double**, int32_t);
	char future[56];
};

typedef struct
{
	int32_t type;
	int32_t byteSize;
	int32_t deltaFrames;
	int32_t flags;
	int32_t noteLength;
	int32_t noteOffset;
	char midiData[4];
	char detune;
	char noteOffVelocity;
	char reserved1;
	char reserved2;
} ZvMidiEvent;

typedef struct
{
	int32_t numEvents;
	intptr_t reserved;
	ZvMidiEvent* events[2];
} ZvEvents;

typedef struct
{
	int16_t top;
	int16_t left;
	int16_t bottom;
	int16_t right;
} ZvRect;

#endif /* ZENE_VST2_ABI_H */
