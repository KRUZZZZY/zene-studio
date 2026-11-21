/*
 * SamplePlayHandlePool.cpp - pooled storage and tryCreate() for SamplePlayHandle
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "SamplePlayHandle.h"

#include <cstddef>
#include <memory>
#include <new>
#include <utility>
#include <vector>

#include "BufferManager.h"
#include "CompSlip.h"
#include "RealtimeFreeList.h"
#include "RealtimeScope.h"
#include "SampleClip.h"

namespace lmms
{

namespace
{

/*! Every block starts with this header and the handle is built right after it. The header is aligned to
 *  max_align_t, so the handle payload is too; the static_asserts below check the handle's alignment and that owned
 *  blocks get the default new alignment. */
struct alignas(std::max_align_t) PoolHeader : RealtimeFreeNode<PoolHeader>
{
	bool pooled = false;
};

static_assert(alignof(SamplePlayHandle) <= alignof(std::max_align_t), "pooled payload alignment");
static_assert(alignof(std::max_align_t) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__, "owned blocks need default new alignment");

constexpr std::size_t PoolBlockBytes = sizeof(PoolHeader) + sizeof(SamplePlayHandle);

void* payloadOf(PoolHeader* header) noexcept
{
	return reinterpret_cast<char*>(header) + sizeof(PoolHeader);
}

PoolHeader* headerOf(void* payload) noexcept
{
	return reinterpret_cast<PoolHeader*>(static_cast<char*>(payload) - sizeof(PoolHeader));
}

//! Free blocks. Pushed by any thread; popped by tryCreate() on the audio thread.
RealtimeFreeList<PoolHeader>& pool() noexcept
{
	static RealtimeFreeList<PoolHeader> freeBlocks;
	return freeBlocks;
}

//! Keeps every pooled block alive. Only the control thread appends here.
std::vector<std::unique_ptr<std::byte[]>>& owners()
{
	static std::vector<std::unique_ptr<std::byte[]>> blocks;
	return blocks;
}

} // namespace

void* SamplePlayHandle::operator new(std::size_t size)
{
	auto* header = static_cast<PoolHeader*>(::operator new(sizeof(PoolHeader) + size));
	::new (static_cast<void*>(header)) PoolHeader();
	return payloadOf(header);
}

void SamplePlayHandle::operator delete(void* p) noexcept
{
	if (!p) { return; }
	PoolHeader* header = headerOf(p);
	if (header->pooled)
	{
		pool().push(header);
		return;
	}
	::operator delete(static_cast<void*>(header));
}

SamplePlayHandle* SamplePlayHandle::tryCreate(SampleClip* clip, const SampleWindow& window) noexcept
{
	PoolHeader* header = pool().pop();
	if (!header) { return nullptr; }

	auto* handle = ::new (payloadOf(header)) SamplePlayHandle(clip, window);
	if (handle->bufferExhausted() || handle->m_compSlipExhausted)
	{
		handle->~SamplePlayHandle();
		pool().push(header);
		return nullptr;
	}
	return handle;
}

void SamplePlayHandle::reserveBlocks(std::size_t blocks)
{
	while (pool().count() < blocks)
	{
		auto storage = std::make_unique<std::byte[]>(PoolBlockBytes);
		auto* header = ::new (static_cast<void*>(storage.get())) PoolHeader();
		header->pooled = true;
		owners().push_back(std::move(storage));
		pool().push(header);
	}
}

void SamplePlayHandle::reserveForClips()
{
	if (RealtimeScope::isActive()) { return; }
	const int live = SampleClip::liveClipCount();
	const std::size_t clips = live > 0 ? static_cast<std::size_t>(live) : std::size_t{0};
	// Target per live clip, 25 percent headroom plus 8. Each live clip can own one play handle (~33.9 KB), one frame
	// block (~2 KB) and one comp-slip bundle (~9.3 KB): about 45 KB per live clip, about 56 KB with the headroom.
	// The frame and slip pools take the same target, so a start is not refused for want of either before a top-up.
	const std::size_t target = clips + clips / 4 + 8;
	reserveBlocks(target);
	BufferManager::reserve(target);
	CompSlipReaders::reserve(target);
}

} // namespace lmms
