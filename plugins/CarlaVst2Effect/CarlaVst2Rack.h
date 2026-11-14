/*
 * CarlaVst2Rack.h - one Carla Rack holding exactly one VST2 plug-in, for the
 *                   VST2 effect host (Q050-481.2).
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

#ifndef LMMS_CARLA_VST2_RACK_H
#define LMMS_CARLA_VST2_RACK_H

#include <atomic>
#include <functional>
#include <span>
#include <vector>

#include <QLibrary>
#include <QString>

#include <CarlaNativePlugin.h>

#include "CarlaVst2Discovery.h"
#include "SampleFrame.h"

namespace lmms::carlavst2
{

/*!
 * The Carla side of a VST2 effect: a Carla Rack instance (libcarla_native-plugin)
 * with the one VST2 plug-in a sub-plugin key names, added through Carla's host
 * API (libcarla_host-plugin, carla_create_native_plugin_host_handle). Native
 * binaries run in process; Windows ones in Carla's Wine bridge. No VST2 header
 * is compiled here (AGENTS.md rule 8): Carla does the VST2 work.
 *
 * Threads: process() and setParameterValue() run on the effect's render worker
 * only (CarlaVst2Renderer), never the audio thread; everything else on the GUI
 * thread. Carla's own callbacks reach the owner through Callbacks.
 */
class Rack
{
public:
	struct Callbacks
	{
		//! The plug-in (its editor) changed a parameter; GUI thread.
		std::function<void(std::uint32_t index, float value)> uiParameterChanged;
		//! Carla re-read the plug-in's parameter list (a state was loaded, the plug-in changed it).
		std::function<void()> parametersReloaded;
	};

	explicit Rack(Callbacks callbacks);
	~Rack();

	Rack(const Rack&) = delete;
	Rack& operator=(const Rack&) = delete;

	//! Adds the plug-in @a file (binary kind @a binary, VST2 unique id @a uniqueId) to the rack. False with
	//! @a error set when there is no Carla runtime, no such binary, or Carla cannot load it.
	bool load(const QString& file, Binary binary, qint64 uniqueId, QString* error);
	bool hasPlugin() const;
	QString lastError() const;

	//! Render worker: Carla's process for @a inOut.size() frames (at most one engine period), in place.
	void process(std::span<SampleFrame> inOut);

	std::uint32_t parameterCount() const;
	const NativeParameter* parameterInfo(std::uint32_t index) const;
	float parameterValue(std::uint32_t index) const;
	//! Render worker: the value the plug-in processes with (Carla's setParameterValueRT).
	void setParameterValue(std::uint32_t index, float value);
	//! GUI thread: tells the plug-in's editor about a value the host changed.
	void uiSetParameterValue(std::uint32_t index, float value);

	//! Carla's project XML for the rack (the plug-in's binary, parameters and chunk), or empty.
	QString state() const;
	//! GUI thread, worker paused: replaces the rack's plug-in with the one @a xml describes.
	void setState(const QString& xml);

	//! The plug-in's own editor: embedded into @a parent (native), or shown by Carla in its own window.
	void embedEditor(void* parent);
	void showEditor(bool show);
	int editorWidth() const { return m_editorWidth.load(std::memory_order_relaxed); }
	int editorHeight() const { return m_editorHeight.load(std::memory_order_relaxed); }

	//! GUI thread: Carla's non-realtime idle (an embedded editor's idle, deferred plug-in deletion).
	void idle();
	void sampleRateChanged();

private:
	//! The CarlaHost.h functions this host needs, from the runtime's libcarla_host-plugin.
	struct HostApi
	{
		using AddPlugin = bool (*)(CarlaHostHandle, CarlaBackend::BinaryType, CarlaBackend::PluginType,
			const char*, const char*, const char*, int64_t, const void*, uint);
		using LastError = const char* (*)(CarlaHostHandle);
		using PluginCount = uint32_t (*)(CarlaHostHandle);
		using EmbedUi = void* (*)(CarlaHostHandle, uint, void*);
		using ShowUi = void (*)(CarlaHostHandle, uint, bool);
		using SetCallback = void (*)(CarlaHostHandle, EngineCallbackFunc, void*);
		AddPlugin addPlugin = nullptr;
		LastError lastError = nullptr;
		PluginCount pluginCount = nullptr;
		EmbedUi embedUi = nullptr;
		ShowUi showUi = nullptr;
		SetCallback setCallback = nullptr;
		bool complete() const { return addPlugin && lastError && pluginCount && embedUi && showUi && setCallback; }
	};

	bool openHostApi(QString* error);
	void fillTimeInfo();

	static uint32_t hostBufferSize(NativeHostHandle handle);
	static double hostSampleRate(NativeHostHandle handle);
	static bool hostIsOffline(NativeHostHandle handle);
	static const NativeTimeInfo* hostTimeInfo(NativeHostHandle handle);
	static bool hostWriteMidi(NativeHostHandle handle, const NativeMidiEvent* event);
	static void hostUiParameterChanged(NativeHostHandle handle, uint32_t index, float value);
	static void hostUiCustomDataChanged(NativeHostHandle handle, const char* key, const char* value);
	static void hostUiClosed(NativeHostHandle handle);
	static const char* hostUiFile(NativeHostHandle handle, bool isDir, const char* title, const char* filter);
	static intptr_t hostDispatcher(NativeHostHandle handle, NativeHostDispatcherOpcode opcode, int32_t index,
		intptr_t value, void* ptr, float opt);
	//! Carla's engine callback: records an embedded editor's size. Two relaxed atomics, any thread.
	static void engineCallback(void* ptr, EngineCallbackOpcode action, uint pluginId, int value1, int value2,
		int value3, float valuef, const char* valueStr);

	Callbacks m_callbacks;
	NativeHostDescriptor m_host{};
	const NativePluginDescriptor* m_descriptor = nullptr;
	NativePluginHandle m_handle = nullptr;
	NativeTimeInfo m_timeInfo{};
	QLibrary m_hostLibrary;
	HostApi m_api;
	CarlaHostHandle m_hostHandle = nullptr;
	std::atomic<int> m_editorWidth{0};
	std::atomic<int> m_editorHeight{0};
	//! Planar buffers for Carla's process, preallocated for the largest period.
	std::vector<float> m_left;
	std::vector<float> m_right;
};

} // namespace lmms::carlavst2

#endif // LMMS_CARLA_VST2_RACK_H
