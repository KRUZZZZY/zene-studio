/*
 * ClapHost.cpp - in-process CLAP host for LMMS
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

//! The parameter and state path of the CLAP host: the parameter getters and
//! setters (clap.params, mirrored into the lock-free slots process() publishes)
//! and the state blob (clap.state). Split out of ClapHost.cpp beside the note
//! path, so the host core file stays under the file-length ratchet.

#include "ClapHost.h"
#include "ClapHostInternals.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <clap/clap.h>

#include <QByteArray>

namespace lmms::clap
{

auto HostedPlugin::paramIndex(std::uint32_t id) const -> int { return m_impl->indexOfParam(id); }

auto HostedPlugin::paramPlain(std::uint32_t id) const -> double
{
	const auto& impl = *m_impl;
	const auto index = impl.indexOfParam(id);
	return index >= 0 ? impl.paramSlots[index].value.load(std::memory_order_relaxed) : 0.0;
}

auto HostedPlugin::paramNormalized(std::uint32_t id) const -> float
{
	const auto& impl = *m_impl;
	const auto index = impl.indexOfParam(id);
	if (index < 0) { return 0.0f; }
	return normalize(impl.params[index], impl.paramSlots[index].value.load(std::memory_order_relaxed));
}

auto HostedPlugin::paramDisplayValue(std::uint32_t id, double plainValue) const -> QString
{
	const auto& impl = *m_impl;
	if (impl.paramsExt)
	{
		char text[CLAP_NAME_SIZE] = {};
		if (impl.paramsExt->value_to_text(impl.plugin, id, plainValue, text, sizeof(text)))
		{
			return QString::fromUtf8(text);
		}
	}
	return QString::number(plainValue, 'g', 6);
}

void HostedPlugin::setParamPlain(std::uint32_t id, double value)
{
	auto& impl = *m_impl;
	const auto index = impl.indexOfParam(id);
	if (index < 0 || impl.params[index].readOnly) { return; }
	auto& descriptor = impl.params[index];
	const auto clamped = std::clamp(value, descriptor.minValue, descriptor.maxValue);
	auto& slot = impl.paramSlots[index];
	if (slot.value.load(std::memory_order_relaxed) == clamped) { return; }
	slot.value.store(clamped, std::memory_order_relaxed);
	slot.revision.fetch_add(1, std::memory_order_relaxed);
}

void HostedPlugin::setParamNormalized(std::uint32_t id, float normalized)
{
	auto& impl = *m_impl;
	const auto index = impl.indexOfParam(id);
	if (index < 0) { return; }
	const auto& descriptor = impl.params[index];
	const auto range = descriptor.maxValue - descriptor.minValue;
	setParamPlain(id, range > 0.0 ? descriptor.minValue + normalized * range : descriptor.minValue);
}

auto HostedPlugin::saveState(QByteArray* state) const -> bool
{
	const auto& impl = *m_impl;
	if (!state) { return false; }
	state->clear();
	if (!impl.stateExt) { return false; }

	clap_ostream_t stream{};
	stream.ctx = state;
	stream.write = [](const clap_ostream_t* self, const void* buffer, std::uint64_t size) -> std::int64_t {
		auto* out = static_cast<QByteArray*>(self->ctx);
		out->append(static_cast<const char*>(buffer), static_cast<qsizetype>(size));
		return static_cast<std::int64_t>(size);
	};
	return impl.stateExt->save(impl.plugin, &stream);
}

auto HostedPlugin::loadState(const QByteArray& state) -> bool
{
	auto& impl = *m_impl;
	if (!impl.stateExt) { return false; }

	struct Reader
	{
		const QByteArray* data;
		qsizetype offset;
	};
	Reader reader{&state, 0};
	clap_istream_t stream{};
	stream.ctx = &reader;
	stream.read = [](const clap_istream_t* self, void* buffer, std::uint64_t size) -> std::int64_t {
		auto* reader = static_cast<Reader*>(self->ctx);
		const auto available = reader->data->size() - reader->offset;
		if (available <= 0) { return 0; }
		const auto count = std::min<std::uint64_t>(size, static_cast<std::uint64_t>(available));
		std::memcpy(buffer, reader->data->constData() + reader->offset, static_cast<std::size_t>(count));
		reader->offset += static_cast<qsizetype>(count);
		return static_cast<std::int64_t>(count);
	};
	if (!impl.stateExt->load(impl.plugin, &stream)) { return false; }

	// The plug-in restored its own values.
	impl.mirrorParamValues();
	return true;
}

auto HostedPlugin::serviceMainThread() -> bool
{
	auto& impl = *m_impl;
	if (impl.plugin == nullptr) { return false; }
	// The plug-in asked for a main-thread call (clap_host.request_callback): it gets it now.
	if (impl.callbackRequested.exchange(false, std::memory_order_relaxed) && impl.plugin->on_main_thread)
	{
		impl.plugin->on_main_thread(impl.plugin);
	}
	// A rescan of values or text re-reads them; a rescan of the list itself takes a new
	// processing setup (the CLAP rule: the plug-in is deactivated for it).
	const auto flags = impl.rescanFlags.exchange(0, std::memory_order_relaxed);
	if ((flags & (CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_INFO)) != 0) { impl.mirrorParamValues(); }
	if ((flags & CLAP_PARAM_RESCAN_ALL) != 0) { impl.needsReprepare.store(true, std::memory_order_relaxed); }
	// A flush asked for while the plug-in is inactive is run here. While it is active the
	// request stays raised until process() delivers the parameters, which clears it (review
	// L01-1 #9: it used to be consumed here and lost when the chain was asleep).
	if (!impl.prepared && impl.flushRequested.exchange(false, std::memory_order_relaxed)
		&& impl.paramsExt != nullptr && impl.paramsExt->flush != nullptr)
	{
		static const clap_input_events_t empty{nullptr, [](const clap_input_events_t*) -> std::uint32_t { return 0; },
			[](const clap_input_events_t*, std::uint32_t) -> const clap_event_header_t* { return nullptr; }};
		impl.paramsExt->flush(impl.plugin, &empty, &impl.outEvents);
	}
	return impl.stateDirty.exchange(false, std::memory_order_relaxed);
}

} // namespace lmms::clap
