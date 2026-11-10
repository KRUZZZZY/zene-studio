/*
 * CarlaVst2Rack.cpp - one Carla Rack holding exactly one VST2 plug-in, for the
 *                     VST2 effect host (Q050-481.2).
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

#include "CarlaVst2Rack.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <QDir>
#include <QFileInfo>

#include "AudioEngine.h"
#include "CarlaVst2Renderer.h"
#include "Engine.h"
#include "Song.h"

namespace lmms::carlavst2
{

namespace
{

//! Carla's time info counts ticks per beat this way (as plugins/CarlaBase/Carla.cpp does).
constexpr double TicksPerBeat = 48.0;

Rack* rackOf(NativeHostHandle handle) { return static_cast<Rack*>(handle); }

//! Carla's binary type for @a binary (BINARY_NONE for one this host cannot run).
CarlaBackend::BinaryType carlaBinaryType(Binary binary)
{
	using namespace CarlaBackend;  // BINARY_NATIVE is a macro naming one of its enumerators
	switch (binary)
	{
	case Binary::Native: return BINARY_NATIVE;
	case Binary::Win64: return BINARY_WIN64;
	case Binary::Win32: return BINARY_WIN32;
	case Binary::Unknown: break;
	}
	return BINARY_NONE;
}

} // namespace

Rack::Rack(Callbacks callbacks) :
	m_callbacks(std::move(callbacks)),
	m_descriptor(carla_get_native_rack_plugin()),
	m_left(Renderer::MaxPeriodFrames),
	m_right(Renderer::MaxPeriodFrames)
{
	if (m_descriptor == nullptr) { return; }  // the weak stand-in: no runtime
	m_host.handle = this;
	m_host.uiName = strdup("Zene VST2 effect");
	m_host.uiParentId = 0;
	// The rack's own (PyQt) frontend is never shown for a single hosted plug-in; Carla wants a path all the same.
	m_host.resourceDir = strdup(QDir(runtimeFolder()).filePath(QStringLiteral("resources")).toUtf8().constData());
	m_host.get_buffer_size = hostBufferSize;
	m_host.get_sample_rate = hostSampleRate;
	m_host.is_offline = hostIsOffline;
	m_host.get_time_info = hostTimeInfo;
	m_host.write_midi_event = hostWriteMidi;
	m_host.ui_parameter_changed = hostUiParameterChanged;
	m_host.ui_custom_data_changed = hostUiCustomDataChanged;
	m_host.ui_closed = hostUiClosed;
	m_host.ui_open_file = hostUiFile;
	m_host.ui_save_file = hostUiFile;
	m_host.dispatcher = hostDispatcher;
	m_timeInfo.bbt.valid = true;
	m_handle = m_descriptor->instantiate(&m_host);
	if (m_handle != nullptr && m_descriptor->activate != nullptr) { m_descriptor->activate(m_handle); }
}

Rack::~Rack()
{
	if (m_hostHandle != nullptr)
	{
		// The rack goes next: nothing may call back into this object then.
		m_api.setCallback(m_hostHandle, nullptr, nullptr);
		carla_host_handle_free(m_hostHandle);
		m_hostHandle = nullptr;
	}
	if (m_handle != nullptr)
	{
		if (m_descriptor->deactivate != nullptr) { m_descriptor->deactivate(m_handle); }
		if (m_descriptor->cleanup != nullptr) { m_descriptor->cleanup(m_handle); }
		m_handle = nullptr;
	}
	std::free(const_cast<char*>(m_host.uiName));
	std::free(const_cast<char*>(m_host.resourceDir));
}

bool Rack::openHostApi(QString* error)
{
	if (m_api.complete() && m_hostHandle != nullptr) { return true; }
	const QString folder = runtimeFolder();
	m_hostLibrary.setFileName(QDir(folder).filePath(QStringLiteral("libcarla_host-plugin")));
	if (folder.isEmpty() || !m_hostLibrary.load())
	{
		*error = QStringLiteral("the Carla runtime has no host library (libcarla_host-plugin) beside "
			"libcarla_native-plugin in '%1': %2").arg(folder, m_hostLibrary.errorString());
		return false;
	}
	m_api.addPlugin = reinterpret_cast<HostApi::AddPlugin>(m_hostLibrary.resolve("carla_add_plugin"));
	m_api.lastError = reinterpret_cast<HostApi::LastError>(m_hostLibrary.resolve("carla_get_last_error"));
	m_api.pluginCount = reinterpret_cast<HostApi::PluginCount>(m_hostLibrary.resolve("carla_get_current_plugin_count"));
	m_api.embedUi = reinterpret_cast<HostApi::EmbedUi>(m_hostLibrary.resolve("carla_embed_custom_ui"));
	m_api.showUi = reinterpret_cast<HostApi::ShowUi>(m_hostLibrary.resolve("carla_show_custom_ui"));
	m_api.setCallback = reinterpret_cast<HostApi::SetCallback>(m_hostLibrary.resolve("carla_set_engine_callback"));
	if (!m_api.complete())
	{
		*error = QStringLiteral("the Carla host library '%1' lacks the host API this needs").arg(m_hostLibrary.fileName());
		return false;
	}
	m_hostHandle = carla_create_native_plugin_host_handle(m_descriptor, m_handle);
	if (m_hostHandle == nullptr)
	{
		*error = QStringLiteral("Carla gave no host handle for its rack instance");
		return false;
	}
	m_api.setCallback(m_hostHandle, &Rack::engineCallback, this);
	return true;
}

bool Rack::load(const QString& file, Binary binary, qint64 uniqueId, QString* error)
{
	if (m_handle == nullptr)
	{
		*error = QStringLiteral("no Carla runtime is installed (a Carla Rack could not be instantiated)");
		return false;
	}
	if (!QFileInfo::exists(file))
	{
		*error = QStringLiteral("the VST2 binary '%1' is not there").arg(file);
		return false;
	}
	if (!openHostApi(error)) { return false; }
	if (binary == Binary::Unknown) { binary = binaryOf(file); }
	const CarlaBackend::BinaryType type = carlaBinaryType(binary);
	if (type == CarlaBackend::BINARY_NONE)
	{
		*error = QStringLiteral("'%1' is not a VST2 binary this host can run").arg(file);
		return false;
	}
	const QByteArray path = file.toLocal8Bit();
	// PLUGIN_OPTIONS_NULL: Carla's defaults for the plug-in, which include saving its chunk.
	if (!m_api.addPlugin(m_hostHandle, type, CarlaBackend::PLUGIN_VST2, path.constData(), nullptr, nullptr,
			uniqueId, nullptr, CarlaBackend::PLUGIN_OPTIONS_NULL)
		|| m_api.pluginCount(m_hostHandle) == 0)
	{
		*error = QStringLiteral("Carla could not load the VST2 plug-in '%1': %2").arg(file, lastError());
		return false;
	}
	return true;
}

bool Rack::hasPlugin() const
{
	return m_hostHandle != nullptr && m_api.pluginCount(m_hostHandle) > 0;
}

QString Rack::lastError() const
{
	return m_hostHandle != nullptr ? QString::fromUtf8(m_api.lastError(m_hostHandle)) : QString();
}

void Rack::fillTimeInfo()
{
	Song* song = Engine::getSong();
	if (song == nullptr) { return; }
	m_timeInfo.playing = song->isPlaying();
	m_timeInfo.frame = song->getPlayPos(song->playMode()).frames(Engine::framesPerTick());
	m_timeInfo.usecs = song->getMilliseconds() * 1000;
	m_timeInfo.bbt.bar = song->getBars() + 1;
	m_timeInfo.bbt.beat = song->getBeat() + 1;
	m_timeInfo.bbt.tick = song->getBeatTicks();
	m_timeInfo.bbt.barStartTick = TicksPerBeat * song->getTimeSigModel().getNumerator() * song->getBars();
	m_timeInfo.bbt.beatsPerBar = song->getTimeSigModel().getNumerator();
	m_timeInfo.bbt.beatType = song->getTimeSigModel().getDenominator();
	m_timeInfo.bbt.ticksPerBeat = TicksPerBeat;
	m_timeInfo.bbt.beatsPerMinute = song->getTempo();
}

void Rack::process(std::span<SampleFrame> inOut)
{
	const auto frames = static_cast<std::uint32_t>(std::min<std::size_t>(inOut.size(), m_left.size()));
	if (m_handle == nullptr || frames == 0) { return; }
	fillTimeInfo();
	for (std::uint32_t f = 0; f < frames; ++f)
	{
		m_left[f] = inOut[f].left();
		m_right[f] = inOut[f].right();
	}
	// Carla's rack processes in place (its input is copied before the plug-ins run).
	float* buffers[] = {m_left.data(), m_right.data()};
#if CARLA_VERSION_HEX >= 0x30000
	m_descriptor->process(m_handle, const_cast<const float**>(buffers), buffers, frames, nullptr, 0);
#else
	m_descriptor->process(m_handle, buffers, buffers, frames, nullptr, 0);  // Carla 2: a non-const input
#endif
	for (std::uint32_t f = 0; f < frames; ++f) { inOut[f] = SampleFrame(m_left[f], m_right[f]); }
}

std::uint32_t Rack::parameterCount() const
{
	return m_handle != nullptr && m_descriptor->get_parameter_count != nullptr
		? m_descriptor->get_parameter_count(m_handle) : 0;
}

const NativeParameter* Rack::parameterInfo(std::uint32_t index) const
{
	return m_handle != nullptr && m_descriptor->get_parameter_info != nullptr
		? m_descriptor->get_parameter_info(m_handle, index) : nullptr;
}

float Rack::parameterValue(std::uint32_t index) const
{
	return m_handle != nullptr && m_descriptor->get_parameter_value != nullptr
		? m_descriptor->get_parameter_value(m_handle, index) : 0.f;
}

void Rack::setParameterValue(std::uint32_t index, float value)
{
	if (m_handle != nullptr && m_descriptor->set_parameter_value != nullptr)
	{
		m_descriptor->set_parameter_value(m_handle, index, value);
	}
}

void Rack::uiSetParameterValue(std::uint32_t index, float value)
{
	if (m_handle != nullptr && m_descriptor->ui_set_parameter_value != nullptr)
	{
		m_descriptor->ui_set_parameter_value(m_handle, index, value);
	}
}

QString Rack::state() const
{
	if (m_handle == nullptr || m_descriptor->get_state == nullptr) { return QString(); }
	char* const state = m_descriptor->get_state(m_handle);
	if (state == nullptr) { return QString(); }
	const QString xml = QString::fromUtf8(state);
	std::free(state);
	return xml;
}

void Rack::setState(const QString& xml)
{
	if (m_handle == nullptr || m_descriptor->set_state == nullptr || xml.isEmpty()) { return; }
	m_descriptor->set_state(m_handle, xml.toUtf8().constData());
}

void Rack::embedEditor(void* parent)
{
	// effEditOpen with the host window, then effEditGetRect, whose size comes back through engineCallback().
	m_editorWidth.store(0, std::memory_order_relaxed);
	m_editorHeight.store(0, std::memory_order_relaxed);
	if (hasPlugin()) { m_api.embedUi(m_hostHandle, 0, parent); }
}

void Rack::showEditor(bool show)
{
	if (hasPlugin()) { m_api.showUi(m_hostHandle, 0, show); }
}

void Rack::idle()
{
	if (m_handle != nullptr && m_descriptor->ui_idle != nullptr) { m_descriptor->ui_idle(m_handle); }
}

void Rack::sampleRateChanged()
{
	if (m_handle != nullptr && m_descriptor->dispatcher != nullptr)
	{
		m_descriptor->dispatcher(m_handle, NATIVE_PLUGIN_OPCODE_SAMPLE_RATE_CHANGED, 0, 0, nullptr,
			static_cast<float>(hostSampleRate(this)));
	}
}

uint32_t Rack::hostBufferSize(NativeHostHandle)
{
	return static_cast<uint32_t>(Renderer::periodFrames());
}

double Rack::hostSampleRate(NativeHostHandle)
{
	return Engine::audioEngine()->outputSampleRate();
}

bool Rack::hostIsOffline(NativeHostHandle)
{
	// While a project is exported Carla may block (on the render WORKER, never the audio thread): it then waits for
	// a plug-in its idle holds, and for the Wine bridge, rather than pass a block through unprocessed (review r1).
	const Song* song = Engine::getSong();
	return song != nullptr && song->isExporting();
}

const NativeTimeInfo* Rack::hostTimeInfo(NativeHostHandle handle)
{
	return &rackOf(handle)->m_timeInfo;
}

bool Rack::hostWriteMidi(NativeHostHandle, const NativeMidiEvent*)
{
	return false;  // an effect's MIDI output goes nowhere
}

void Rack::hostUiParameterChanged(NativeHostHandle handle, uint32_t index, float value)
{
	const Rack* rack = rackOf(handle);
	if (rack->m_callbacks.uiParameterChanged) { rack->m_callbacks.uiParameterChanged(index, value); }
}

void Rack::hostUiCustomDataChanged(NativeHostHandle, const char*, const char*)
{
}

void Rack::hostUiClosed(NativeHostHandle)
{
}

const char* Rack::hostUiFile(NativeHostHandle, bool, const char*, const char*)
{
	return nullptr;  // the rack's own frontend, which would ask, is never shown
}

intptr_t Rack::hostDispatcher(NativeHostHandle handle, NativeHostDispatcherOpcode opcode, int32_t, intptr_t, void*,
	float)
{
	const Rack* rack = rackOf(handle);
	if ((opcode == NATIVE_HOST_OPCODE_RELOAD_PARAMETERS || opcode == NATIVE_HOST_OPCODE_RELOAD_ALL)
		&& rack->m_callbacks.parametersReloaded)
	{
		rack->m_callbacks.parametersReloaded();
	}
	return 0;
}

void Rack::engineCallback(void* ptr, EngineCallbackOpcode action, uint, int value1, int value2, int, float,
	const char*)
{
	if (action != CarlaBackend::ENGINE_CALLBACK_EMBED_UI_RESIZED || ptr == nullptr) { return; }
	auto* self = static_cast<Rack*>(ptr);
	self->m_editorWidth.store(value1, std::memory_order_relaxed);
	self->m_editorHeight.store(value2, std::memory_order_relaxed);
}

} // namespace lmms::carlavst2
