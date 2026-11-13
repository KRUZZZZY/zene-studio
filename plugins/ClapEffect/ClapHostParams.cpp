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

//! The parameter and state path of the CLAP host: the parameter list and its
//! rescans (W6), the parameter getters and setters (clap.params, mirrored into
//! the lock-free slots process() publishes) and the state blob (clap.state).
//! Split out of ClapHost.cpp beside the note path, so the host core file stays
//! under the file-length ratchet.

#include "ClapHost.h"
#include "ClapHostInternals.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

#include <clap/clap.h>

#include <QByteArray>

namespace lmms::clap
{

namespace
{

//! CLAP wants a finite [min_value, max_value] with min <= max, and every host-side clamp/normalization
//! depends on it (std::clamp is undefined behaviour with unordered bounds). A plug-in that reports
//! anything else gets valid bounds here: non-finite ones become 0..1, a reversed pair is swapped, so the
//! range stays as close to the plug-in's own as it can be validly expressed. The SPAN must be finite too:
//! -DBL_MAX..DBL_MAX has two finite endpoints but max - min overflows to +inf, and normalize() then divides
//! by it (half-way normalizes to inf/inf = NaN, every ordinary value to 0), so such a range falls back to
//! 0..1 like a non-finite endpoint does.
void sanitizedRange(const clap_param_info_t& info, ParamDescriptor* descriptor)
{
	if (!std::isfinite(info.min_value) || !std::isfinite(info.max_value))
	{
		descriptor->minValue = 0.0;
		descriptor->maxValue = 1.0;
	}
	else
	{
		descriptor->minValue = std::min(info.min_value, info.max_value);
		descriptor->maxValue = std::max(info.min_value, info.max_value);
		// A span that overflows cannot be divided by. The pair is replaced by the same harmless 0..1 a
		// non-finite bound gets; a finite span (0..DBL_MAX) is left exactly as the plug-in reported it.
		if (!std::isfinite(descriptor->maxValue - descriptor->minValue))
		{
			descriptor->minValue = 0.0;
			descriptor->maxValue = 1.0;
		}
	}
	descriptor->defaultValue
		= std::isfinite(info.default_value) ? info.default_value : descriptor->minValue;
	descriptor->defaultValue
		= std::clamp(descriptor->defaultValue, descriptor->minValue, descriptor->maxValue);
}

auto descriptorOf(const clap_param_info_t& info) -> ParamDescriptor
{
	ParamDescriptor descriptor;
	descriptor.id = info.id;
	descriptor.title = QString::fromUtf8(info.name);
	descriptor.module = QString::fromUtf8(info.module);
	sanitizedRange(info, &descriptor);
	descriptor.stepped = (info.flags & CLAP_PARAM_IS_STEPPED) != 0;
	descriptor.periodic = (info.flags & CLAP_PARAM_IS_PERIODIC) != 0;
	descriptor.readOnly = (info.flags & CLAP_PARAM_IS_READONLY) != 0;
	descriptor.bypass = (info.flags & CLAP_PARAM_IS_BYPASS) != 0;
	descriptor.automatable = (info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0;
	descriptor.hidden = (info.flags & CLAP_PARAM_IS_HIDDEN) != 0;
	descriptor.requiresProcess = (info.flags & CLAP_PARAM_REQUIRES_PROCESS) != 0;
	descriptor.modulatablePerNote = (info.flags & CLAP_PARAM_IS_MODULATABLE) != 0
		&& (info.flags & (CLAP_PARAM_IS_MODULATABLE_PER_KEY | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID)) != 0;
	descriptor.defaultNormalized = normalize(descriptor, descriptor.defaultValue);
	return descriptor;
}

//! True when @a fresh changes none of what the audio thread reads of @a held - the id, the range, the read-only
//! flag - nor what CLAP lets change only with a full rescan (RESCAN_ALL: the stepped and bypass flags too).
auto sameShape(const ParamDescriptor& held, const ParamDescriptor& fresh) -> bool
{
	return held.id == fresh.id && held.minValue == fresh.minValue && held.maxValue == fresh.maxValue
		&& held.stepped == fresh.stepped && held.readOnly == fresh.readOnly && held.bypass == fresh.bypass;
}

} // namespace

void HostedPlugin::Impl::readParams(std::vector<ParamDescriptor>* descriptors, std::vector<void*>* cookies) const
{
	if (paramsExt == nullptr) { return; }
	const auto count = paramsExt->count(plugin);
	descriptors->reserve(count);
	cookies->reserve(count);
	for (std::uint32_t i = 0; i < count; ++i)
	{
		clap_param_info_t info{};
		if (!paramsExt->get_info(plugin, i, &info)) { continue; }
		descriptors->push_back(descriptorOf(info));
		cookies->push_back(info.cookie);
	}
}

void HostedPlugin::Impl::scanParams()
{
	params.clear();
	paramCookies.clear();
	paramIds.clear();
	readParams(&params, &paramCookies);
	for (const auto& descriptor : params) { paramIds.push_back(descriptor.id); }
	paramSlots = std::make_unique<ParamSlot[]>(std::max<std::size_t>(params.size(), 1));
	for (std::size_t i = 0; i < params.size(); ++i)
	{
		double value = params[i].defaultValue;
		if (paramsExt != nullptr && paramsExt->get_value(plugin, params[i].id, &value))
		{
			value = sanitizedValue(params[i], value);
		}
		paramSlots[i].value.store(value, std::memory_order_relaxed);
	}
	paramsChanged.store(false, std::memory_order_relaxed);  // a whole new list answers a pending full rescan too
	++paramsRevision;
}

void HostedPlugin::Impl::refreshParamInfo()
{
	if (paramsExt == nullptr || paramsChanged.load(std::memory_order_relaxed)) { return; }  // a full rescan is due
	std::vector<ParamDescriptor> fresh;
	std::vector<void*> cookies;
	readParams(&fresh, &cookies);
	const bool same = fresh.size() == params.size() && cookies == paramCookies
		&& std::equal(params.begin(), params.end(), fresh.begin(), sameShape);
	if (!same)
	{
		// More changed than RESCAN_INFO covers (a plug-in that should have asked for RESCAN_ALL): the list is
		// re-read deactivated, by the restart this asks for, never under a running audio thread.
		paramsChanged.store(true, std::memory_order_relaxed);
		needsReprepare.store(true, std::memory_order_relaxed);
		return;
	}
	// Only fields the audio thread never reads are written: it may be processing right now.
	for (std::size_t i = 0; i < params.size(); ++i)
	{
		params[i].title = fresh[i].title;
		params[i].module = fresh[i].module;
		params[i].periodic = fresh[i].periodic;
		params[i].hidden = fresh[i].hidden;
		params[i].automatable = fresh[i].automatable;
		params[i].requiresProcess = fresh[i].requiresProcess;
		params[i].modulatablePerNote = fresh[i].modulatablePerNote;
		params[i].defaultValue = fresh[i].defaultValue;
		params[i].defaultNormalized = fresh[i].defaultNormalized;
	}
	++paramsRevision;
}

void HostedPlugin::Impl::serviceRescan(std::uint32_t flags)
{
	// A rescan of values, text or info re-reads the values; info re-reads the descriptors in place, and text only
	// needs what shows them to draw again. A rescan of the whole list was taken when it was asked for
	// (paramsRescan): it waits for the next prepare(), deactivated.
	if ((flags & (CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_INFO)) != 0) { mirrorParamValues(true); }
	if ((flags & CLAP_PARAM_RESCAN_INFO) != 0) { refreshParamInfo(); }
	else if ((flags & CLAP_PARAM_RESCAN_TEXT) != 0) { ++paramsRevision; }
}

auto HostedPlugin::paramsRevision() const -> std::uint32_t { return m_impl->paramsRevision; }

auto HostedPlugin::paramIndex(std::uint32_t id) const -> int { return m_impl->indexOfParam(id); }

auto HostedPlugin::paramPlain(std::uint32_t id) const -> double
{
	const auto& impl = *m_impl;
	const auto index = impl.indexOfParam(id);
	return index >= 0 ? impl.paramSlots[index].value.load(std::memory_order_relaxed) : 0.0;
}

void HostedPlugin::readParamEdits(std::uint32_t id, PluginParamGesture::Edits* edits)
{
	auto& impl = *m_impl;
	const auto index = impl.indexOfParam(id);
	if (index < 0) { *edits = {}; return; }
	auto& slot = impl.paramSlots[index];
	slot.gesture.read(slot.value, edits);
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
	const auto clamped = sanitizedValue(descriptor, value);
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

//! The clap.state output stream: the host's blob and whether a write was refused. A failed write is
//! remembered here, because a plug-in is free to ignore write()'s answer and report success anyway.
struct StateStream
{
	QByteArray* out;
	bool failed = false;
};

//! Largest length QByteArray::append() can be handed on this Qt. Qt 6.8 added QByteArray::maxSize(); on
//! earlier Qt 6 releases, bound it by qsizetype's signed maximum. Qt 5's append() takes an int, so the bound
//! is the int maximum. These bounds stay inside the append length type; append() can still reject a lower
//! allocation limit, which is handled below.
constexpr auto maxStateWrite() -> std::uint64_t
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	return static_cast<std::uint64_t>(QByteArray::maxSize());
#elif QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return static_cast<std::uint64_t>(std::numeric_limits<qsizetype>::max());
#else
	return static_cast<std::uint64_t>(std::numeric_limits<int>::max());
#endif
}

auto HostedPlugin::saveState(QByteArray* state) const -> bool
{
	const auto& impl = *m_impl;
	if (!state) { return false; }
	state->clear();
	if (!impl.stateExt) { return false; }

	StateStream sink{state};
	clap_ostream_t stream{};
	stream.ctx = &sink;
	stream.write = [](const clap_ostream_t* self, const void* buffer, std::uint64_t size) -> std::int64_t {
		auto* sink = static_cast<StateStream*>(self->ctx);
		// The count is a uint64 and QByteArray's length is a SIGNED qsizetype (Qt 5: an int): narrowing
		// it first turns a huge size negative, and append() then copies strlen(buffer) bytes while the
		// callback reports the original size - a truncated blob reported as a complete save. Refuse the
		// write instead, and let the plug-in see a short write (-1).
		const auto written = static_cast<std::uint64_t>(sink->out->size());
		const auto room = written < maxStateWrite() ? maxStateWrite() - written : 0;
		if (size > room || (size > 0 && buffer == nullptr))
		{
			sink->failed = true;
			return -1;
		}
		const auto before = sink->out->size();
		try
		{
			sink->out->append(static_cast<const char*>(buffer), static_cast<qsizetype>(size));
		}
		catch (const std::bad_alloc&)
		{
			// Nothing escaped the host: the blob is rolled back to the length it had before this write.
			sink->out->truncate(before);
			sink->failed = true;
			return -1;
		}
		return static_cast<std::int64_t>(size);
	};
	const bool saved = impl.stateExt->save(impl.plugin, &stream);
	if (!saved || sink.failed)
	{
		// Never hand a half-written blob to a caller: saveSettings() stores any non-empty QByteArray, so an
		// empty one is what a failed save must leave behind (the "no state" answer it already understands).
		state->clear();
		return false;
	}
	return true;
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
	impl.serviceRescan(impl.rescanFlags.exchange(0, std::memory_order_relaxed));
	// A flush asked for while the plug-in is inactive is run here. While it is active the
	// request stays raised until process() delivers the parameters, which clears it (review
	// L01-1 #9: it used to be consumed here and lost when the chain was asleep).
	if (!impl.prepared && impl.flushRequested.exchange(false, std::memory_order_relaxed)
		&& impl.paramsExt != nullptr && impl.paramsExt->flush != nullptr)
	{
		static const clap_input_events_t empty{nullptr, [](const clap_input_events_t*) -> std::uint32_t { return 0; },
			[](const clap_input_events_t*, std::uint32_t) -> const clap_event_header_t* { return nullptr; }};
		// Its own output list: impl.outEvents is only set up by prepare(), which has not run (or failed).
		const clap_output_events_t out{&impl, &Impl::outEventsTryPush};
		impl.paramsExt->flush(impl.plugin, &empty, &out);
	}
	return impl.stateDirty.exchange(false, std::memory_order_relaxed);
}

} // namespace lmms::clap
