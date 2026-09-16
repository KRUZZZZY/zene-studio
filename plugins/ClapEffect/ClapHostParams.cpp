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

	// The plug-in restored its own values; mirror them into the lock-free paramSlots
	// without bumping the revisions (that would echo the values back).
	if (impl.paramsExt)
	{
		for (std::size_t i = 0; i < impl.params.size(); ++i)
		{
			double value = impl.params[i].defaultValue;
			if (impl.paramsExt->get_value(impl.plugin, impl.params[i].id, &value))
			{
				impl.paramSlots[i].value.store(value, std::memory_order_relaxed);
			}
		}
	}
	return true;
}

} // namespace lmms::clap
