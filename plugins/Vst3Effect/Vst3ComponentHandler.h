/*
 * Vst3ComponentHandler.h - the IComponentHandler a hosted VST3 plug-in reports edits to
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

#ifndef LMMS_VST3_COMPONENT_HANDLER_H
#define LMMS_VST3_COMPONENT_HANDLER_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "pluginterfaces/vst/ivsteditcontroller.h"

namespace lmms::vst3
{

//! The SDK's names are brought in here only, so including this header leaks none of them.
namespace handler
{

using namespace Steinberg;
using namespace Steinberg::Vst;

// Moved out of Vst3Host.cpp (0.5.0 L01) so that file stays inside its file-length ratchet while
// the host gains its latency and output-event wiring.
/**
 * Minimal IComponentHandler. performEdit() only mirrors the plug-in's own
 * parameter changes into the lock free parameter array of the host, using a
 * binary search over the pre-sorted parameter index: no allocation, no lock.
 */
class Vst3ComponentHandler : public IComponentHandler
{
public:
	// `release()` deletes through `this`, so the destructor must be virtual to
	// keep the polymorphic delete well defined (-Wdelete-non-virtual-dtor).
	virtual ~Vst3ComponentHandler() = default;

	Vst3ComponentHandler(std::atomic<float>* values,
			const std::vector<std::pair<std::uint32_t, std::size_t>>* indexById) :
		m_values{values},
		m_indexById{indexById}
	{
	}

	tresult PLUGIN_API beginEdit(ParamID) SMTG_OVERRIDE { return kResultOk; }

	tresult PLUGIN_API performEdit(ParamID id, ParamValue valueNormalized) SMTG_OVERRIDE
	{
		if (m_values == nullptr || m_indexById == nullptr) { return kResultOk; }  // detached: the host is gone
		const auto it = std::lower_bound(m_indexById->begin(), m_indexById->end(),
			std::make_pair(static_cast<std::uint32_t>(id), std::size_t{0}));
		if (it != m_indexById->end() && it->first == static_cast<std::uint32_t>(id))
		{
			m_values[it->second].store(static_cast<float>(valueNormalized),
				std::memory_order_relaxed);
		}
		return kResultOk;
	}

	tresult PLUGIN_API endEdit(ParamID) SMTG_OVERRIDE { return kResultOk; }

	//! BUGS 11.34: a plug-in whose latency changed says so here (UI thread, per the VST3 SDK);
	//! the host re-reads it and the owning chain re-aligns its paths.
	tresult PLUGIN_API restartComponent(int32 flags) SMTG_OVERRIDE
	{
		if ((flags & kLatencyChanged) != 0 && m_onLatencyChanged) { m_onLatencyChanged(); }
		return kResultOk;
	}

	//! GUI thread. The handler owns its callback (the plug-in may keep the handler alive past the
	//! host's own teardown, which clears this first: review L01-1 #5).
	void setOnLatencyChanged(std::function<void()> callback) { m_onLatencyChanged = std::move(callback); }

	//! GUI thread, at the host's teardown: forget the host's parameter array and index and its callback, so a
	//! plug-in that keeps the handler past them edits nothing freed (review r4 L20 #11).
	void detach()
	{
		m_values = nullptr;
		m_indexById = nullptr;
		m_onLatencyChanged = nullptr;
	}

	//! The two interfaces this object implements. A plug-in that asks the handler it was given
	//! for IComponentHandler (or for the FUnknown base) gets them, with the reference count
	//! incremented for the reference it receives; anything else is kNoInterface.
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) SMTG_OVERRIDE
	{
		if (obj == nullptr) { return kInvalidArgument; }
		if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid)
			|| FUnknownPrivate::iidEqual(iid, FUnknown::iid))
		{
			*obj = static_cast<IComponentHandler*>(this);
			addRef();
			return kResultOk;
		}
		*obj = nullptr;
		return kNoInterface;
	}

	uint32 PLUGIN_API addRef() SMTG_OVERRIDE { return ++m_refCount; }

	uint32 PLUGIN_API release() SMTG_OVERRIDE
	{
		const auto count = --m_refCount;
		if (count == 0) { delete this; }
		return count;
	}

private:
	std::atomic<float>* m_values;
	const std::vector<std::pair<std::uint32_t, std::size_t>>* m_indexById;
	std::function<void()> m_onLatencyChanged;
	std::atomic<uint32> m_refCount{1};
};

} // namespace handler

using handler::Vst3ComponentHandler;

} // namespace lmms::vst3

#endif // LMMS_VST3_COMPONENT_HANDLER_H
