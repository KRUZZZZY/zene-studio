/*
 * NamProfile.h - stage-level CPU profiling hooks for the WaveNet engine
 *
 * Copyright (c) 2026 AI-KOS Team
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
 * These hooks exist only when NAM_PROFILE_LAYERS is defined (the
 * `nam_profile` target). In the plugin and the normal harness the macros
 * expand to nothing, so the engine code is unchanged.
 */

#ifndef LMMS_NAM_PROFILE_H
#define LMMS_NAM_PROFILE_H

#ifdef NAM_PROFILE_LAYERS

#include <ctime>

namespace lmms::nam
{

/// Stage slots, accumulated in g_namProfUs across the whole run.
enum NamProfSlot
{
	NAM_PROF_CONDITION = 0,  ///< copy input into m_condition
	NAM_PROF_RECHANNEL,      ///< array rechannel copy + GEMM
	NAM_PROF_HEAD_INIT,      ///< head accumulator init (zero or copy)
	NAM_PROF_HISTORY,        ///< layer history shift + append
	NAM_PROF_CONV,           ///< K dilated conv GEMMs
	NAM_PROF_CONV_BIAS,      ///< conv bias broadcast add
	NAM_PROF_MIXIN,          ///< conditioning mixin GEMM
	NAM_PROF_Z_ADD,          ///< z = conv + mixin
	NAM_PROF_TANH,           ///< tanh activation
	NAM_PROF_HEAD_ADD,       ///< head += z skip connection
	NAM_PROF_ONEBYONE,       ///< layer 1x1 GEMM + bias + residual
	NAM_PROF_LAYER_OUT,      ///< array output copy
	NAM_PROF_HEAD_RECHANNEL, ///< head rechannel GEMM + bias
	NAM_PROF_OUTPUT,         ///< head_scale output copy
	NAM_PROF_SLOT_COUNT
};

extern double g_namProfUs[NAM_PROF_SLOT_COUNT];

inline double namProfNowUs() noexcept
{
	timespec t{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return static_cast<double>(t.tv_sec) * 1e6 + static_cast<double>(t.tv_nsec) / 1e3;
}

}  // namespace lmms::nam

// The profiling TU declares `double namT0 = 0.0;` in each timed function.
#define NAM_PROF_TIC() namT0 = ::lmms::nam::namProfNowUs()
#define NAM_PROF_TOC(slot) ::lmms::nam::g_namProfUs[slot] += ::lmms::nam::namProfNowUs() - namT0

#else

#define NAM_PROF_TIC() ((void)0)
#define NAM_PROF_TOC(slot) ((void)0)

#endif  // NAM_PROFILE_LAYERS

#endif  // LMMS_NAM_PROFILE_H
