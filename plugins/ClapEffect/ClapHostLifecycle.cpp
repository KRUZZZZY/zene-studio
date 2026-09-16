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

//! The CLAP host's main-thread ladder around process(): the activation pair
//! (prepare, release) the plug-in wrappers drive, and the module scan
//! (listClasses) that opens a module and reads its factory without
//! instantiating anything - what the plug-in browser and the device catalogue
//! call. Split out of ClapHost.cpp so the host core file stays under the
//! file-length ratchet; ClapHostInternals.h is the state the TUs share.

#include "ClapHost.h"
#include "ClapHostInternals.h"

#include "ClapLoader.h"

#include <algorithm>
#include <cstdint>
#include <memory>

#include <clap/clap.h>

namespace lmms::clap
{

auto HostedPlugin::prepare(double sampleRate, int maxBlockSize, QString* error) -> bool
{
	auto& impl = *m_impl;
	if (!impl.plugin)
	{
		setError(error, QStringLiteral("No CLAP plug-in loaded"));
		return false;
	}
	if (sampleRate <= 0.0 || maxBlockSize <= 0)
	{
		setError(error, QStringLiteral("Invalid sample rate or block size"));
		return false;
	}
	release();

	impl.sampleRate = sampleRate;
	impl.maxFrames = maxBlockSize;
	impl.steadyTime = 0;

	// Every buffer the audio thread touches is allocated here, once.
	impl.inPtrs.assign(std::max<std::size_t>(impl.layout.inputs, 1), nullptr);
	impl.outPtrs.assign(std::max<std::size_t>(impl.layout.outputs, 1), nullptr);
	impl.inBuffers.assign(impl.layout.inputPortChannels.size(), clap_audio_buffer_t{});
	impl.outBuffers.assign(impl.layout.outputPortChannels.size(), clap_audio_buffer_t{});
	impl.silence.assign(static_cast<std::size_t>(maxBlockSize), 0.0f);
	impl.outputScratch.assign(static_cast<std::size_t>(impl.layout.outputs) * maxBlockSize, 0.0f);
	impl.paramEvents.assign(impl.params.size(), clap_event_param_value_t{});
	impl.noteEvents.assign(static_cast<std::size_t>(kMaxNoteEventsPerBlock), clap_event_note_t{});
	impl.lastSeenRevision.assign(impl.params.size(), 0);
	// The note queue is emptied here: prepare() is main-thread and the audio
	// thread cannot be popping yet (the transport setup calls it before the
	// plug-in is activated and processing starts).
	impl.noteQueue.reset();

	impl.eventState.events = impl.paramEvents.data();
	impl.eventState.count = 0;
	impl.eventState.notes = impl.noteEvents.data();
	impl.eventState.noteCount = 0;
	impl.inEvents.ctx = &impl.eventState;
	impl.inEvents.size = &Impl::inEventsSize;
	impl.inEvents.get = &Impl::inEventsGet;
	impl.outEvents.ctx = &impl;
	impl.outEvents.try_push = &Impl::outEventsTryPush;

	if (!impl.plugin->activate(impl.plugin, sampleRate, 1, static_cast<std::uint32_t>(maxBlockSize)))
	{
		setError(error, QStringLiteral("clap_plugin.activate() failed"));
		impl.freeRealtimeBuffers();
		return false;
	}
	if (!impl.plugin->start_processing(impl.plugin))
	{
		impl.plugin->deactivate(impl.plugin);
		setError(error, QStringLiteral("clap_plugin.start_processing() failed"));
		impl.freeRealtimeBuffers();
		return false;
	}
	impl.prepared = true;
	impl.needsReprepare.store(false, std::memory_order_relaxed);
	return true;
}

void HostedPlugin::release()
{
	auto& impl = *m_impl;
	if (impl.plugin && impl.prepared)
	{
		impl.plugin->stop_processing(impl.plugin);
		impl.plugin->deactivate(impl.plugin);
		impl.prepared = false;
	}
	impl.freeRealtimeBuffers();
}

auto listClasses(const QString& modulePath, loader::Status* status, QString* error) -> std::vector<ClassInfo>
{
	std::vector<ClassInfo> classes;
	// The same ladder load() uses, so a scan reports the SAME typed reason an
	// instance would: a module with no clap_entry, a CLAP 0.x module and a
	// module whose init() fails are three different answers here too, not one
	// "not a usable CLAP module" (which is what this function used to say).
	loader::Library library;
	loader::Status failure;
	if (!loader::load(modulePath, library, failure))
	{
		if (status) { *status = failure; }
		setError(error, failure.message());
		return classes;
	}
	const auto* factory = library.factory;
	const auto count = factory->get_plugin_count(factory);
	for (std::uint32_t i = 0; i < count; ++i)
	{
		const auto* descriptor = factory->get_plugin_descriptor(factory, i);
		if (descriptor) { classes.push_back(describe(*descriptor)); }
	}
	if (status) { *status = {}; }
	library.reset();
	return classes;
}

auto listClasses(const QString& modulePath, QString* error) -> std::vector<ClassInfo>
{
	return listClasses(modulePath, nullptr, error);
}
} // namespace lmms::clap
