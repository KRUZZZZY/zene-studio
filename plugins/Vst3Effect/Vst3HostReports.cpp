/*
 * Vst3HostReports.cpp - what the VST3 host reports about its plug-in: latency, tail, output events,
 *                       and (0.5.0) its output buses and note-expression types
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

#include <algorithm>
#include <climits>

#include "Vst3HostInternals.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"

namespace lmms::vst3
{

auto HostedPlugin::latency() const -> std::uint32_t
{
	const auto& d = *m_impl;
	return d.prepared && d.processor ? d.processor->getLatencySamples() : 0;
}

auto HostedPlugin::tail() const -> int
{
	const auto& d = *m_impl;
	if (!d.prepared || !d.processor) { return 0; }
	const uint32 frames = d.processor->getTailSamples();
	return frames == kInfiniteTail ? -1 : static_cast<int>(std::min<uint32>(frames, INT32_MAX));
}

void HostedPlugin::setLatencyChangedCallback(std::function<void()> callback)
{
	m_impl->onLatencyChanged = std::move(callback);
	if (m_impl->handlerImpl != nullptr) { m_impl->handlerImpl->setOnLatencyChanged(m_impl->onLatencyChanged); }
}

auto HostedPlugin::outputEventCount() const -> int { return m_impl->blockOutputCount; }

auto HostedPlugin::droppedOutputEvents() const -> std::uint64_t
{
	return m_impl->droppedOutput.load(std::memory_order_relaxed);
}

auto HostedPlugin::outputEvent(int index) const -> const MidiEventIn&
{
	return m_impl->blockOutput[static_cast<std::size_t>(index)];
}

// ---------------------------------------------------------------------------
// 0.5.0 MX-114 (output buses) and MX-120 (note expressions)
// ---------------------------------------------------------------------------

auto HostedPlugin::outputBusDescriptors() const -> const std::vector<BusDescriptor>&
{
	return m_impl->outputBusDescs;
}

auto HostedPlugin::activateOutputBuses(int upTo) -> bool
{
	auto& d = *m_impl;
	if (d.prepared || !d.component) { return false; }
	const int last = std::min(upTo, static_cast<int>(d.outputBusDescs.size()) - 1);
	for (int bus = 1; bus <= last; ++bus)
	{
		auto& desc = d.outputBusDescs[static_cast<std::size_t>(bus)];
		if (desc.active) { continue; }
		if (d.component->activateBus(kAudio, kOutput, bus, true) != kResultOk) { return false; }
		desc.active = true;
	}
	std::vector<BusDescriptor> all = d.inputBusDescs;
	all.insert(all.end(), d.outputBusDescs.begin(), d.outputBusDescs.end());
	d.layout = mapBuses(all);
	return upTo <= last;
}

void HostedPlugin::refreshNoteExpressions()
{
	auto& d = *m_impl;
	d.noteExpressions.clear();
	if (!d.controller || d.eventInputBusIndex < 0) { return; }
	FUnknownPtr<INoteExpressionController> expressions(d.controller.get());
	if (!expressions) { return; }
	const int32 count = expressions->getNoteExpressionCount(d.eventInputBusIndex, 0);
	for (int32 i = 0; i < count; ++i)
	{
		NoteExpressionTypeInfo info{};
		if (expressions->getNoteExpressionInfo(d.eventInputBusIndex, 0, i, info) != kResultOk) { continue; }
		Impl::NoteExpression entry;
		entry.typeId = info.typeId;
		entry.title = QString::fromUtf16(reinterpret_cast<const char16_t*>(info.title));
		entry.parameter = info.associatedParameterId;
		entry.hasParameter = (info.flags & NoteExpressionTypeInfo::kAssociatedParameterIDValid) != 0;
		d.noteExpressions.push_back(entry);
	}
}

auto HostedPlugin::hasNoteExpressions() const -> bool
{
	return !m_impl->noteExpressions.empty();
}

auto HostedPlugin::noteExpressionFor(std::uint32_t id, const QString& title) const -> std::int64_t
{
	for (const auto& entry : m_impl->noteExpressions)
	{
		if (entry.hasParameter && entry.parameter == id) { return entry.typeId; }
	}
	for (const auto& entry : m_impl->noteExpressions)
	{
		if (entry.title.compare(title, Qt::CaseInsensitive) == 0) { return entry.typeId; }
	}
	return -1;
}

} // namespace lmms::vst3
