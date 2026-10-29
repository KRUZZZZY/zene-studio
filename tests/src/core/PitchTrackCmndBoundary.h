/*
 * PitchTrackCmndBoundary.h - the YIN lag refinement never reads past its CMND vector
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
/*! Included by AudioAnalysisTest.cpp, whose operator new honours the tail switches below. Cluster new-p10-291. */

#ifndef LMMS_PITCH_TRACK_CMND_BOUNDARY_H
#define LMMS_PITCH_TRACK_CMND_BOUNDARY_H

#include <QtTest>

#include <cmath>
#include <cstddef>
#include <vector>

#include "PitchTrack.h"

//! While set, the test binary's allocator gives each allocation AllocationTail extra bytes of g_tailByte past its
//! requested size. A read past a vector then sees a known value instead of whatever the heap held.
inline thread_local bool g_fillAllocationTail = false;
inline thread_local unsigned char g_tailByte = 0;
constexpr std::size_t AllocationTail = 16;

//! A sine of period 564 frames: YIN's dip first falls under the threshold at lag 510 (maxLag - 2) and the lag after
//! it is lower still, so the refinement must stop at 510 and read CMND[511] - never CMND[512], past the vector.
//! The answer must not depend on the bytes past the vector: 0xFF reads as NaN, 0x00 as 0.f.
inline void checkPitchNeverReadsPastCmnd()
{
	std::vector<float> wave(1024);
	for (std::size_t i = 0; i < wave.size(); ++i)
	{
		wave[i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * static_cast<double>(i) / 564.0));
	}
	const auto pitchWithTail = [&wave](unsigned char byte)
	{
		g_tailByte = byte;
		g_fillAllocationTail = true;
		const float hz = lmms::pitchtrack::pitchAt(wave.data(), wave.size(), 0, 1024, 11025);
		g_fillAllocationTail = false;
		return hz;
	};
	const float nanTail = pitchWithTail(0xFF);
	const float zeroTail = pitchWithTail(0x00);
	QVERIFY(std::isfinite(zeroTail) && zeroTail > 0.f);
	QVERIFY2(nanTail == zeroTail, "pitchAt depends on the bytes past its CMND vector");
}

#endif // LMMS_PITCH_TRACK_CMND_BOUNDARY_H
