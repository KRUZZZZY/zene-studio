/*
 * Lv2UridMap.cpp - Lv2UridMap class
 *
 * Copyright (c) 2019 Johannes Lorenz <j.git$$$lorenz-ho.me, $$$=@>
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

#ifndef LMMS_LV2_URID_MAP_H
#define LMMS_LV2_URID_MAP_H

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_LV2

#include <lv2/urid/urid.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex> // protects the canonical map and the mirror's publication; RT reads the mirror without it
#include <string>
#include <unordered_map>
#include <vector>


namespace lmms
{


/**
 * Complete implementation of the Lv2 Urid Map extension
 *
 * A plug-in may call map()/unmap() from its run() (cluster new-p17-82), and the LV2
 * specification warns that the map is "not necessarily very fast or RT-safe". The
 * audio-thread path therefore reads a lock-free mirror of the mappings: a known URI
 * resolves without the mutex and without allocating, and a URI first seen on the
 * audio thread is not registered there at all (map() returns 0). Registration is
 * done by the control thread, under m_MapMutex, and published into the mirror.
 */
class UridMap
{
	//! Slots in the lock-free mirror. Reaching it would need more distinct URIs than
	//! any real session maps; beyond it, RT lookups return 0 but the canonical map
	//! (and so every control-thread caller) keeps working unbounded.
	static constexpr std::size_t RtCapacity = 16384;

	//! One open-addressed slot of the RT mirror; hash == 0 means empty. uri points at
	//! the canonical map's key, whose storage is stable for the map's lifetime.
	struct RtEntry
	{
		std::atomic<std::uint32_t> hash{0};
		std::atomic<LV2_URID> urid{0};
		const char* uri = nullptr;
	};

	std::unordered_map<std::string, LV2_URID> m_map;
	std::vector<const char*> m_unMap;

	//! mutex for both m_map and m_unMap, and for publishing into the mirror
	//! the URID map is global, which is why a mutex is required here
	std::mutex m_MapMutex;

	//! lock-free mirror read by the audio thread; allocated once in the constructor
	std::unique_ptr<RtEntry[]> m_rtEntries;
	//! urid - 1 -> URI, also read by the audio thread (for unmap())
	std::unique_ptr<std::atomic<const char*>[]> m_rtUris;
	//! published mirror entries; guarded by m_MapMutex
	std::size_t m_rtSize = 0;

	LV2_URID_Map m_mapFeature;
	LV2_URID_Unmap m_unmapFeature;

	//! FNV-1a hash for the mirror, never 0 (0 marks an empty slot)
	static std::uint32_t rtHash(const char* uri) noexcept;
	//! Lock-free mirror lookup; 0 when the URI is not published there
	LV2_URID mapRt(const char* uri) const noexcept;
	//! Publish a canonical mapping into the mirror; call with m_MapMutex held
	void publishRt(const char* uri, LV2_URID urid) noexcept;

public:
	//! constructor; will set up the features
	UridMap();

	//! map feature function
	LV2_URID map(const char* uri);
	//! unmap feature function
	const char* unmap(LV2_URID urid);

	// access the features
	LV2_URID_Map* mapFeature() { return &m_mapFeature; }
	LV2_URID_Unmap* unmapFeature() { return &m_unmapFeature; }
};


} // namespace lmms

#endif // LMMS_HAVE_LV2

#endif // LMMS_LV2_URID_MAP_H
