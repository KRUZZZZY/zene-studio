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
	return hash;
}

LV2_URID UridMap::mapRt(const char* uri) const noexcept
{
	const std::uint32_t hash = rtHash(uri);
	// The chain is append-only and nodes are immutable after publication, so the
	// acquire load of the head makes every node in the chain including its uri visible.
	const RtNode* node = m_rtBuckets[hash % RtBucketCount].load(std::memory_order_acquire);
	while (node != nullptr)
	{
		if (node->hash == hash && std::strcmp(node->uri, uri) == 0) { return node->urid; }
		node = node->next;
	}
	return 0;
}

void UridMap::ensureUridChunk(std::size_t chunkIndex)
{
	while (m_uridChunkCount.load(std::memory_order_relaxed) <= chunkIndex)
	{
		auto chunk = std::make_unique<UridChunk>();
		for (auto& slot : chunk->uris) { slot.store(nullptr, std::memory_order_relaxed); }
		UridChunk* raw = chunk.release();
		m_uridTail->next.store(raw, std::memory_order_release);
		m_uridTail = raw;
		m_uridChunkCount.store(m_uridChunkCount.load(std::memory_order_relaxed) + 1,
			std::memory_order_release);
	}
}

void UridMap::publishRt(const char* uri, LV2_URID urid)
{
	if (uri == nullptr || urid == 0u) { return; }

	const std::size_t index = static_cast<std::size_t>(urid) - 1u;
	const std::size_t chunkIndex = index / UridChunkSize;
	ensureUridChunk(chunkIndex);
	UridChunk* chunk = m_uridChunks.get();
	for (std::size_t i = 0; i < chunkIndex; ++i)
	{
		chunk = chunk->next.load(std::memory_order_relaxed);
	}
	// The chunk is already linked, so the slot can be published independently.
	chunk->uris[index % UridChunkSize].store(uri, std::memory_order_release);

	const std::uint32_t hash = rtHash(uri);
	std::atomic<RtNode*>& head = m_rtBuckets[hash % RtBucketCount];
	// Idempotent: a retry after a transient allocation failure must not add a
	// second node (and must not change the URID). The mutex serialises writers.
	for (const RtNode* node = head.load(std::memory_order_relaxed); node != nullptr; node = node->next)
	{
		if (node->urid == urid) { return; }
	}

	auto node = std::make_unique<RtNode>();
	node->hash = hash;
	node->urid = urid;
	node->uri = uri;
	node->next = head.load(std::memory_order_relaxed);
	RtNode* raw = node.release();
	head.store(raw, std::memory_order_release);
}

UridMap::UridMap()
	: m_rtBuckets(std::make_unique<std::atomic<RtNode*>[]>(RtBucketCount))
	, m_uridChunks(std::make_unique<UridChunk>())
	, m_uridTail(m_uridChunks.get())
{
	for (std::size_t i = 0; i < RtBucketCount; ++i)
	{
		m_rtBuckets[i].store(nullptr, std::memory_order_relaxed);
	}
	for (auto& slot : m_uridChunks->uris) { slot.store(nullptr, std::memory_order_relaxed); }
	m_uridChunkCount.store(1, std::memory_order_relaxed);
	m_mapFeature.handle = static_cast<LV2_URID_Map_Handle>(this);
	m_mapFeature.map = staticMap;
	m_unmapFeature.handle = static_cast<LV2_URID_Unmap_Handle>(this);
	m_unmapFeature.unmap = staticUnmap;
}

UridMap::~UridMap()
{
	for (std::size_t i = 0; i < RtBucketCount; ++i)
	{
		RtNode* node = m_rtBuckets[i].load(std::memory_order_relaxed);
		while (node != nullptr)
		{
			RtNode* next = node->next;
			delete node;
			node = next;
		}
	}
	UridChunk* chunk = m_uridChunks.release();
	while (chunk != nullptr)
	{
		UridChunk* next = chunk->next.load(std::memory_order_relaxed);
		delete chunk;
		chunk = next;
	}
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
				result = index;
				// A publication failure (the mirror allocates a node or a chunk) must
				// not lose an ID the canonical map already assigned; the next map()
				// of this URI retries publication below.
				try { publishRt(pr.first->first.c_str(), index); }
				catch (...) { }
			}
		}
		else
		{
			result = itr->second;
			try { publishRt(itr->first.c_str(), result); }
			catch (...) { /* the canonical ID stands */ }
		}
	}
	catch(...) { /* result variable is already 0 */ }

	return result;
}

const char *UridMap::unmap(LV2_URID urid)
{
	if (RealtimeScope::isActive())
	{
		if (urid == 0u) { return nullptr; }
		const std::size_t index = static_cast<std::size_t>(urid) - 1u;
		const std::size_t chunkIndex = index / UridChunkSize;
		// Reject an absurd URID without walking a chain that cannot contain it.
		if (chunkIndex >= m_uridChunkCount.load(std::memory_order_acquire)) { return nullptr; }
		const UridChunk* chunk = m_uridChunks.get();
		for (std::size_t i = 0; i < chunkIndex; ++i)
		{
			chunk = chunk->next.load(std::memory_order_acquire);
			if (chunk == nullptr) { return nullptr; }
		}
		return chunk->uris[index % UridChunkSize].load(std::memory_order_acquire);
	}

	std::size_t idx = static_cast<std::size_t>(urid) - 1;

	std::lock_guard<std::mutex> guard (m_MapMutex);
	return (idx < m_unMap.size()) ? m_unMap[idx] : nullptr;
}


} // namespace lmms

#endif // LMMS_HAVE_LV2

