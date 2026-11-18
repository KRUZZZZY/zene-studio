/*
 * ConvolutionFftw.cpp - the scope in which FFTW's planner API may be used
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

#include "ConvolutionFftw.h"

#include <atomic>

#include "AudioEngine.h"
#include "Engine.h"

namespace lmms
{

namespace
{

//! The FFTW planner calls that ran inside a scope, and the ones that did not - the second count
//! is a planner call that could race the audio thread's fftwf_execute, so it must stay at zero.
std::atomic<std::uint64_t> s_guardedCalls{0};
std::atomic<std::uint64_t> s_unguardedCalls{0};
//! How many scopes this thread holds.
thread_local int s_depth = 0;

} // namespace

ConvolutionFftwScope::ConvolutionFftwScope()
{
	// A nested scope is free: the guard is already held, and the outermost scope releases it.
	if (s_depth == 0)
	{
		// The engine is gone only at shutdown, when no audio thread runs any more.
		AudioEngine* const engine = Engine::audioEngine();
		if (engine != nullptr)
		{
			// The render thread holds this guard for every period it renders, so taking it
			// waits for the period in flight to end and keeps the next one out.
			engine->requestChangeInModel();
			m_engine = engine;
		}
	}
	++s_depth;
}

ConvolutionFftwScope::~ConvolutionFftwScope()
{
	--s_depth;
	if (m_engine != nullptr) { m_engine->doneChangeInModel(); }
}

bool ConvolutionFftwScope::held() noexcept
{
	return s_depth > 0;
}

void ConvolutionFftwScope::notePlannerCall() noexcept
{
	if (held()) { s_guardedCalls.fetch_add(1, std::memory_order_relaxed); }
	else { s_unguardedCalls.fetch_add(1, std::memory_order_relaxed); }
}

std::uint64_t ConvolutionFftwScope::guardedPlannerCalls() noexcept
{
	return s_guardedCalls.load(std::memory_order_relaxed);
}

std::uint64_t ConvolutionFftwScope::unguardedPlannerCalls() noexcept
{
	return s_unguardedCalls.load(std::memory_order_relaxed);
}

} // namespace lmms
