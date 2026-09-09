/*
 * ClapHost.h - in-process CLAP host for LMMS
 *
 * Copyright (c) 2026 LMMS contributors
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

#ifndef LMMS_CLAP_HOST_H
#define LMMS_CLAP_HOST_H

#include <cstdint>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QString>

#include "ClapBusMap.h"
#include "ClapParamDescriptor.h"

namespace lmms::clap
{

//! The parameter description lives in the plug-in namespace (like the VST3
//! lane's Vst3ParamDescriptor) so that the host and the controls share it.
using ParamDescriptor = ClapParamDescriptor;

//! Static identity of one plug-in class inside a CLAP module.
struct ClassInfo
{
	QString id;
	QString name;
	QString vendor;
	QString version;
	QString description;
	bool isInstrument = false;
};

/*!
 * Lists every plug-in class inside a CLAP module. Loads the module, queries the
 * clap.plugin-factory extension and unloads it again. Main thread only.
 */
auto listClasses(const QString& modulePath, QString* error) -> std::vector<ClassInfo>;

/*!
 * In-process host for one CLAP plug-in instance. Mirrors the VST3 lane's
 * HostedPlugin: all lifetime and state calls happen on the main thread, while
 * process() and the parameter setters are safe to call from the audio thread.
 */
class HostedPlugin
{
public:
	HostedPlugin();
	~HostedPlugin();

	HostedPlugin(const HostedPlugin&) = delete;
	HostedPlugin& operator=(const HostedPlugin&) = delete;

	// --- main thread ------------------------------------------------------
	auto load(const QString& modulePath, const QString& pluginId, QString* error) -> bool;
	void unload();
	auto isLoaded() const -> bool;
	auto classInfo() const -> const ClassInfo&;
	auto className() const -> QString;
	auto vendor() const -> QString;
	auto isInstrument() const -> bool;
	auto parameters() const -> const std::vector<ParamDescriptor>&;
	auto ports() const -> const std::vector<PortDescriptor>&;
	auto busLayout() const -> const PortLayout&;
	auto latency() const -> std::uint32_t;

	auto paramIndex(std::uint32_t id) const -> int;
	auto paramPlain(std::uint32_t id) const -> double;
	auto paramNormalized(std::uint32_t id) const -> float;
	auto paramDisplayValue(std::uint32_t id, double plainValue) const -> QString;

	//! Persists the plug-in state through clap.state (empty if unsupported).
	auto saveState(QByteArray* state) const -> bool;
	//! Restores the plug-in state and re-reads the parameter values.
	auto loadState(const QByteArray& state) -> bool;

	auto prepare(double sampleRate, int maxBlockSize, QString* error) -> bool;
	void release();
	auto isPrepared() const -> bool;

	//! The plug-in asked for a restart (block size/sample rate change).
	auto needsReprepare() const -> bool;
	void clearNeedsReprepare();
	//! The plug-in asked to be called on the main thread; poll from the GUI.
	auto takeCallbackRequest() -> bool;

	void setTempo(double bpm);
	void setTransportPlaying(bool playing);

	// --- audio thread -----------------------------------------------------
	//! Writes the value of one parameter into the plug-in's event stream.
	//! Lock-free and allocation-free; the change is delivered with the next
	//! process() call.
	void setParamPlain(std::uint32_t id, double value);
	void setParamNormalized(std::uint32_t id, float normalized);

	/*!
	 * Processes one block. inputs/outputs are planar channel pointer arrays;
	 * channels beyond the plug-in's port layout are ignored on input and left
	 * untouched on output, missing channels are fed silence. Real-time safe:
	 * no allocation, no locking, no I/O.
	 */
	auto process(const float* const* inputs, float* const* outputs, int inputChannels,
		int outputChannels, int frames) -> bool;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace lmms::clap

#endif // LMMS_CLAP_HOST_H
