/*
 * NamTanh.h - vectorised tanh for the NAM inference engine
 *
 * The reference implementation (NeuralAmpModelerCore) is built with -Ofast,
 * which lets GCC vectorise ActivationTanh::apply() into glibc libmvec calls
 * (_ZGVbN4v_tanhf, 4-wide). Our Release build uses -O3, which leaves the
 * scalar std::tanh loop unvectorised; the stage profile shows the activation
 * is >45% of the engine's CPU time and is the whole gap against the reference.
 *
 * This header computes tanh with the same libmvec kernels. The kernels are
 * resolved once via dlopen() on first use; the engine calls warmUp() from
 * NamModel::loadFromFile() (worker thread) so the audio thread only ever reads
 * an already-initialised function pointer. If libmvec is unavailable the
 * scalar fallback is used, byte-for-byte the pre-optimisation behaviour.
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
 */

#ifndef LMMS_NAM_TANH_H
#define LMMS_NAM_TANH_H

#include <cmath>

// NAM_TANH_SCALAR forces the pre-optimisation scalar loop. Used by the
// reference-match A/B and as the fallback on platforms without libmvec.
#if defined(__x86_64__) && defined(__linux__) && (defined(__GNUC__) || defined(__clang__)) && !defined(NAM_TANH_SCALAR)
#define NAM_TANH_HAVE_LIBMVEC 1
#include <dlfcn.h>
#include <immintrin.h>
#endif

namespace lmms::nam
{

namespace tanhdetail
{

#ifdef NAM_TANH_HAVE_LIBMVEC

using Tanh4Fn = __m128 (*)(__m128);  ///< _ZGVbN4v_tanhf (SSE2, 4-wide)
using Tanh8Fn = __m256 (*)(__m256);  ///< _ZGVdN8v_tanhf (AVX2+FMA, 8-wide)

/// libmvec entry points, resolved once. Immutable after construction.
struct Kernels
{
	Tanh4Fn v4 = nullptr;
	Tanh8Fn v8 = nullptr;

	Kernels() noexcept
	{
		__builtin_cpu_init();
		void* lib = dlopen("libmvec.so.1", RTLD_LAZY | RTLD_LOCAL);
		if (lib == nullptr) { lib = dlopen("libmvec.so.6", RTLD_LAZY | RTLD_LOCAL); }
		if (lib == nullptr) { return; }
		v4 = reinterpret_cast<Tanh4Fn>(dlsym(lib, "_ZGVbN4v_tanhf"));
		if (__builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma"))
		{
			v8 = reinterpret_cast<Tanh8Fn>(dlsym(lib, "_ZGVdN8v_tanhf"));
		}
		// The handle is intentionally kept for the process lifetime: the kernels
		// must stay mapped while the audio thread calls them.
	}
};

inline const Kernels& kernels() noexcept
{
	static const Kernels k;
	return k;
}

/// Resolves the libmvec kernels if that has not happened yet. Called from
/// NamModel::loadFromFile() (worker thread) so no dlopen() can ever run on the
/// audio thread.
inline void warmUp() noexcept
{
	(void)kernels();
}

__attribute__((target("avx2,fma"))) inline int apply8(float* p, int n, Tanh8Fn fn) noexcept
{
	int i = 0;
	for (; i + 8 <= n; i += 8)
	{
		_mm256_storeu_ps(p + i, fn(_mm256_loadu_ps(p + i)));
	}
	return i;
}

inline int apply4(float* p, int n, Tanh4Fn fn) noexcept
{
	int i = 0;
	for (; i + 4 <= n; i += 4)
	{
		_mm_storeu_ps(p + i, fn(_mm_loadu_ps(p + i)));
	}
	return i;
}

#else  // !NAM_TANH_HAVE_LIBMVEC

/// No-op: the scalar fallback needs no kernel resolution.
inline void warmUp() noexcept
{
}

#endif  // NAM_TANH_HAVE_LIBMVEC

}  // namespace tanhdetail

/// tanh, in place, over n contiguous floats. Element order is unchanged from a
/// scalar std::tanh loop; only the implementation of each element differs
/// (libmvec kernels are <=2 ulp from glibc scalar tanhf, exactly as the
/// reference implementation's own vectorised activation).
inline void tanhInPlace(float* data, int n) noexcept
{
	int done = 0;
#ifdef NAM_TANH_HAVE_LIBMVEC
	const tanhdetail::Kernels& k = tanhdetail::kernels();
	if (k.v8 != nullptr)
	{
		done = tanhdetail::apply8(data, n, k.v8);
	}
	else if (k.v4 != nullptr)
	{
		done = tanhdetail::apply4(data, n, k.v4);
	}
#endif
	for (; done < n; ++done)
	{
		data[done] = std::tanh(data[done]);
	}
}

}  // namespace lmms::nam

#endif  // LMMS_NAM_TANH_H
