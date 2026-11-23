/*
 * Lv2UridMap.cpp - Lv2UridMap implementation
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


#include "Lv2UridMap.h"

#ifdef LMMS_HAVE_LV2

#include <cstring>

#include "RealtimeScope.h"

namespace lmms
{


static LV2_URID staticMap(LV2_URID_Map_Handle handle, const char* uri)
{
	auto map = static_cast<UridMap*>(handle);
	return map->map(uri);
}

static const char* staticUnmap(LV2_URID_Unmap_Handle handle, LV2_URID urid)
{
	auto map = static_cast<UridMap*>(handle);
	return map->unmap(urid);
}

std::uint32_t UridMap::rtHash(const char* uri) noexcept
{
	// FNV-1a, the usual small non-cryptographic choice: no allocation, no lock.
	std::uint32_t hash = 2166136261u;
	for (const auto* c = reinterpret_cast<const unsigned char*>(uri); *c != 0; ++c)
	{
		hash ^= *c;
		hash *= 16777619u;
	}
	// 0 is the empty-slot marker.
	return hash != 0u ? hash : 1u;
}

LV2_URID UridMap::mapRt(const char* uri) const noexcept
{
	const std::uint32_t hash = rtHash(uri);
	const std::size_t home = hash % RtCapacity;
	for (std::size_t probe = 0; probe < RtCapacity; ++probe)
	{
		const RtEntry& entry = m_rtEntries[(home + probe) % RtCapacity];
		const std::uint32_t slotHash = entry.hash.load(std::memory_order_acquire);
		if (slotHash == 0u)
		{
			// Insertions only ever fill the first empty slot on the probe path and
			// are never removed, so an empty slot means the URI is not published.
			return 0;
		}
		if (slotHash == hash && std::strcmp(entry.uri, uri) == 0)
		{
			// urid was stored before hash's release store.
			return entry.urid.load(std::memory_order_relaxed);
		}
	}
	return 0;
}

void UridMap::publishRt(const char* uri, LV2_URID urid) noexcept
{
	if (urid == 0u) { return; }

	if (urid <= RtCapacity)
	{
		m_rtUris[urid - 1].store(uri, std::memory_order_release);
	}

	if (m_rtSize >= RtCapacity) { return; }

	const std::uint32_t hash = rtHash(uri);
	const std::size_t home = hash % RtCapacity;
	for (std::size_t probe = 0; probe < RtCapacity; ++probe)
	{
		RtEntry& entry = m_rtEntries[(home + probe) % RtCapacity];
		if (entry.hash.load(std::memory_order_relaxed) != 0u) { continue; }
		entry.uri = uri;
		entry.urid.store(urid, std::memory_order_relaxed);
		// Publishing the hash last makes the uri and urid above visible to mapRt().
		entry.hash.store(hash, std::memory_order_release);
		++m_rtSize;
		return;
	}
}

UridMap::UridMap()
	: m_rtEntries(std::make_unique<RtEntry[]>(RtCapacity))
	, m_rtUris(std::make_unique<std::atomic<const char*>[]>(RtCapacity))
{
	for (std::size_t i = 0; i < RtCapacity; ++i)
	{
		m_rtUris[i].store(nullptr, std::memory_order_relaxed);
	}
	m_mapFeature.handle = static_cast<LV2_URID_Map_Handle>(this);
	m_mapFeature.map = staticMap;
	m_unmapFeature.handle = static_cast<LV2_URID_Unmap_Handle>(this);
	m_unmapFeature.unmap = staticUnmap;
}

LV2_URID UridMap::map(const char *uri)
{
	if (uri == nullptr) { return 0u; }

	// A plug-in's run() may call this. The contract (lv2/urid/urid.h) allows the map
	// to be slow, but this host keeps its audio-thread callers off the mutex and off
	// the heap: a known URI is read from the lock-free mirror, and a URI first seen
	// here is left for the control thread to register instead of allocating now.
	if (RealtimeScope::isActive()) { return mapRt(uri); }

	LV2_URID result = 0u;

	// the Lv2 docs say that 0 should be returned in any case
	// where creating an ID for the given URI fails
	try
	{
		// TODO:
		// when using C++14, we can get around any string allocation
		// in the case the URI is already inside the map:
		// * use `m_map.find(uri)` instead of `m_map.find(uriStr)`
		// * to avoid temporary string construction in the `find` call, create
		//   m_map like this:
		//   std::unordered_map<std::string, LV2_URID,
		//     std::hash<std::string>, std::equal<>> m_map;
		// * move the try block inside the case where the URI is not in the map
		const std::string uriStr = uri;

		std::lock_guard<std::mutex> guard (m_MapMutex);

		auto itr = m_map.find(uriStr);
		if (itr == m_map.end())
		{
			// 1 is the first free URID
			const auto index = static_cast<LV2_URID>(1u + m_unMap.size());
			auto pr = m_map.emplace(std::move(uriStr), index);
			if (pr.second)
			{
				m_unMap.emplace_back(pr.first->first.c_str());
				publishRt(pr.first->first.c_str(), index);
				result = index;
			}
		}
		else { result = itr->second; }
	}
	catch(...) { /* result variable is already 0 */ }

	return result;
}

const char *UridMap::unmap(LV2_URID urid)
{
	if (RealtimeScope::isActive())
	{
		// The mirror's URI pointers are the canonical map's stable key storage.
		if (urid == 0u || urid > RtCapacity) { return nullptr; }
		return m_rtUris[urid - 1].load(std::memory_order_acquire);
	}

	std::size_t idx = static_cast<std::size_t>(urid) - 1;

	std::lock_guard<std::mutex> guard (m_MapMutex);
	return (idx < m_unMap.size()) ? m_unMap[idx] : nullptr;
}


} // namespace lmms

#endif // LMMS_HAVE_LV2

