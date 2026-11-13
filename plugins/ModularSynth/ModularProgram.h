/*
 * ModularProgram.h - Q050-059: a modular graph compiled for the audio thread, and the voice that plays it
 *
 * compile() (control thread) turns a valid Graph and its parameter models into a Program: fixed-size arrays in
 * processing order, the wiring as indices, nothing to look up or allocate. A Voice lives in a NotePlayHandle's own
 * plug-in storage; render() reads the Program and the models and writes samples - no allocation, no lock, no wait,
 * no signal.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_MODULAR_PROGRAM_H
#define LMMS_MODULAR_PROGRAM_H

#include <array>
#include <cstdint>
#include <memory>

#include "ModularGraph.h"
#include "LmmsTypes.h"

namespace lmms
{

class FloatModel;
class SampleFrame;

namespace modular
{

struct ProgramModule
{
	ModuleKind kind = ModuleKind::Output;
	std::array<const FloatModel*, MaxParams> params{};
	//! For each input port, the processing-order indices of the modules wired into it.
	std::array<std::array<std::int8_t, MaxModules>, MaxPorts> sources{};
	std::array<std::int8_t, MaxPorts> sourceCount{};
};

struct Program
{
	std::array<ProgramModule, MaxModules> modules{};
	int count = 0;
	int output = -1;			//!< processing-order index of the output module
	bool outputEnveloped = false;	//!< an envelope drives the output's level, so it shapes the release itself
	std::uint64_t serial = 0;	//!< tells a voice the graph it was playing has been replaced
};

/*! Compiles @a graph (which must be valid) with @a params[i][p] the model of parameter p of graph module i.
 *  Control thread. */
std::unique_ptr<Program> compile(const Graph& graph,
	const std::array<std::array<const FloatModel*, MaxParams>, MaxModules>& params);

//! The longest release among the program's envelopes, in milliseconds. Any thread; reads models only.
float longestReleaseMs(const Program& program);

//! One note's state: phases, envelope stages and filter memories, per module. Trivially copyable, no heap.
struct Voice
{
	std::uint64_t serial = 0;
	std::array<std::array<float, 4>, MaxModules> state{};
};

/*! Renders frames [@a from, @a to) of one note into @a out (both channels written), the note's base frequency
 *  @a frequency at @a sampleRate. Frames at or after @a releaseAt are in the note's release. Audio thread. */
void render(const Program& program, Voice& voice, float frequency, float sampleRate,
	SampleFrame* out, f_cnt_t from, f_cnt_t to, f_cnt_t releaseAt) noexcept;

} // namespace modular

} // namespace lmms

#endif // LMMS_MODULAR_PROGRAM_H
