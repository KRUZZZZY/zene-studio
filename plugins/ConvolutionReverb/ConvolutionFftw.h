/*
 * ConvolutionFftw.h - the scope in which FFTW's planner API may be used
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

#ifndef LMMS_CONVOLUTION_FFTW_H
#define LMMS_CONVOLUTION_FFTW_H

#include <cstdint>

namespace lmms
{

class AudioEngine;

/*! The scope in which FFTW's planner API may be used.
 *
 *  FFTW's planner API - creating a plan, destroying a plan, allocating and freeing the buffers a
 *  plan works on - is not thread-safe: it may not run while any thread executes a plan. Only
 *  fftwf_execute is thread-safe, and only for different plans (FFTW's manual, "Thread safety").
 *  The audio thread executes the convolution plans once per block, so a plan may only be created
 *  and destroyed while the audio thread is quiescent - otherwise the planner's own state is
 *  touched while an execute runs on it.
 *
 *  The engine's model-change guard is what quiesces the audio thread: the render thread holds
 *  that guard for every period it renders (AudioEngine::renderNextPeriod()), so a thread that
 *  holds the guard has the whole engine to itself. This scope takes the guard for its lifetime,
 *  so every FFTW planner call made inside it cannot overlap an audio thread's fftwf_execute.
 *
 *  The scope is re-entrant: a nested scope costs nothing (the guard is already held), and the
 *  guard is released when the outermost scope ends. It is a control-thread scope - the audio
 *  thread never takes it, it only executes plans. */
class ConvolutionFftwScope
{
public:
	ConvolutionFftwScope();
	~ConvolutionFftwScope();
	ConvolutionFftwScope(const ConvolutionFftwScope&) = delete;
	ConvolutionFftwScope& operator=(const ConvolutionFftwScope&) = delete;

	//! Whether this thread holds a scope right now.
	static bool held() noexcept;

	/*! Records one FFTW planner call (a plan created or destroyed, a buffer allocated or freed)
	 *  and whether a scope was held for it. The unguarded count is the number of planner calls
	 *  that could race the audio thread's fftwf_execute: it must stay at zero. */
	static void notePlannerCall() noexcept;

	//! How many planner calls ran inside a scope.
	static std::uint64_t guardedPlannerCalls() noexcept;
	//! How many ran outside one.
	static std::uint64_t unguardedPlannerCalls() noexcept;

private:
	//! The engine whose guard this scope holds - null when there was none to take.
	AudioEngine* m_engine = nullptr;
};

} // namespace lmms

#endif // LMMS_CONVOLUTION_FFTW_H
