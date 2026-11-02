/*
 * CarlaVst2Instrument.h - a VST2 instrument hosted through Carla (Q050-481.1).
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

#ifndef LMMS_CARLA_VST2_INSTRUMENT_H
#define LMMS_CARLA_VST2_INSTRUMENT_H

#include <atomic>

#include <QLibrary>
#include <QString>

#include "Carla.h"
#include "CarlaVst2Discovery.h"
#include "PluginNativeEditor.h"

class QTimer;

namespace lmms
{

/*!
 * One VST2 plug-in inside one Carla Rack instance. Everything the product
 * already does with a Carla Rack is inherited unchanged: the audio and MIDI
 * path (CarlaInstrument::playImpl and its lock-free MIDI hand-off), the
 * parameter models (automatable FloatModels, one per plug-in parameter), and
 * the state (Carla's own project XML, which carries the plug-in's binary path,
 * its parameter values and its chunk). What this class adds is the choice of
 * plug-in (the sub-plugin key), its own editor, and Carla's non-realtime idle,
 * which a bridged plug-in needs to deliver its replies.
 */
class CarlaVst2Instrument : public CarlaInstrument
{
	Q_OBJECT
public:
	CarlaVst2Instrument(InstrumentTrack* track, const Descriptor* descriptor,
		const Descriptor::SubPluginFeatures::Key& key);
	~CarlaVst2Instrument() override;

	//! True when the plug-in the key names is loaded in Carla; otherwise loadError() says why.
	bool isLoaded() const { return m_loadError.isEmpty(); }
	QString loadError() const { return m_loadError; }

	bool hostsSinglePlugin() const override { return true; }
	QString unavailableReason() const override { return m_loadError; }

	void loadSettings(const QDomElement& elem) override;

	bool openNativeEditor(QString* error) override;
	void closeNativeEditor() override;
	bool nativeEditorOpen() const override;

private:
	//! The few CarlaHost.h functions this host needs, resolved from the Carla runtime's
	//! libcarla_host-plugin beside libcarla_native-plugin (the pairing Carla documents for
	//! carla_create_native_plugin_host_handle).
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

	//! Carla's engine callback: records the size an embedded editor reports (ENGINE_CALLBACK_EMBED_UI_RESIZED,
	//! sent from inside the embed call on the GUI thread). Stores two atomics and nothing else, so it is
	//! safe from whichever thread Carla calls it on.
	static void engineCallback(void* ptr, EngineCallbackOpcode action, uint pluginId, int value1, int value2,
		int value3, float valuef, const char* valueStr);

	bool isBridged() const;
	bool openHostApi(QString* error);
	bool loadPlugin(QString* error);
	bool addToRack(const QString& file, CarlaBackend::BinaryType type, QString* error);
	void idle();

	Descriptor::SubPluginFeatures::Key m_pluginKey;
	QString m_loadError;
	QLibrary m_hostLibrary;
	HostApi m_api;
	CarlaHostHandle m_hostHandle = nullptr;
	QTimer* m_idleTimer = nullptr;
	gui::PluginNativeEditor m_editor;
	bool m_bridgedEditorShown = false;
	std::atomic<int> m_editorWidth{0};
	std::atomic<int> m_editorHeight{0};
};

} // namespace lmms

#endif // LMMS_CARLA_VST2_INSTRUMENT_H
