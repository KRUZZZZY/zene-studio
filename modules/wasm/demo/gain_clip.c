/*
 * gain_clip.c - reference LMMS WASM DSP module (G4)
 *
 * Copyright (c) 2026 LMMS WASM DSP sandbox contributors
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ABI (specs/SPEC-wasm-sandbox.md section 3), frozen at v0:
 *
 *   process(in_ptr, out_ptr, frames, sample_rate) -> i32
 *     in_ptr / out_ptr are OFFSETS into this module's linear memory, not host
 *     pointers. Each plane holds `frames` f32 samples (planar). Return 0 on
 *     success.
 *
 *   import "env" "host_get_param" (i32) -> f32
 *   import "env" "host_log" (i32 ptr, i32 len) -> ()
 *
 * DSP: one-pole gain from host_get_param(0) followed by a soft clip
 * (x / (1 + |x|)). The module is channel-count agnostic: it processes exactly
 * one plane per process() call, so the host may call it once per channel.
 *
 * Build (no Rust/C wasm toolchain is installed by default; zig cc bundles
 * clang + wasm-ld):
 *
 *   zig cc -target wasm32-freestanding -O2 -nostdlib \
 *       -Wl,--no-entry -o gain_clip.wasm gain_clip.c
 */

#define WASM_EXPORT(name) __attribute__((export_name(#name)))

extern float host_get_param(int index)
	__attribute__((import_module("env"), import_name("host_get_param")));
extern void host_log(const char* ptr, int len)
	__attribute__((import_module("env"), import_name("host_log")));

static int g_logged_first_block = 0;

WASM_EXPORT(process)
int process(int in_ptr, int out_ptr, int frames, float sample_rate)
{
	const float* in = (const float*)in_ptr;
	float* out = (float*)out_ptr;
	const float gain = host_get_param(0);

	for (int i = 0; i < frames; ++i)
	{
		const float x = in[i] * gain;
		// soft clip: bounded, monotonic, no branches on the hot path
		out[i] = x / (1.0f + (x < 0.0f ? -x : x));
	}

	if (!g_logged_first_block)
	{
		static const char message[] = "gain_clip: first block processed";
		host_log(message, (int)(sizeof(message) - 1));
		g_logged_first_block = 1;
	}

	(void)sample_rate;
	return 0;
}
