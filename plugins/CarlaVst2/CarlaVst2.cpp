/*
 * CarlaVst2.cpp - VST2 instruments hosted through Carla (Q050-481.1).
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

/*
 * WHY THIS MODULE EXISTS
 * ----------------------
 * The product's own VST2 host (VstBase/Vestige/VstEffect) is ~7k lines on a
 * reverse-engineered header. The owner approved replacing it with Carla, which
 * the product already links for Carla Rack (research/reviews/hotspots-2026-10-19).
 * This module is stage 1 of that replacement: VST2 INSTRUMENTS, native or
 * Windows-through-Wine, each one a Carla Rack holding exactly that plug-in. No
 * VST2 header is compiled here: Carla does the VST2 work, this module only
 * names the binary (AGENTS.md rule 8).
 */

#include <QDir>
#include <QDomElement>
#include <QFileInfo>
#include <QTimer>

#include "CarlaVst2Discovery.h"
#include "CarlaVst2Instrument.h"
#include "InstrumentTrack.h"
#include "PluginVendors.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

namespace
{

//! The VST2 instruments Carla's discovery found, as sub-plugin keys of the carlavst2 descriptor.
class CarlaVst2SubPluginFeatures : public Plugin::Descriptor::SubPluginFeatures
{
public:
	explicit CarlaVst2SubPluginFeatures(Plugin::Type type) : SubPluginFeatures(type) {}

	void listSubPluginKeys(const Plugin::Descriptor* descriptor, KeyList& keys) const override
	{
		for (const carlavst2::Vst2Record& record : carlavst2::discoverAll())
		{
			// Stage 1 hosts instruments; a VST2 effect gets its own host in stage 2.
			if (!record.isSynth) { continue; }
			Key::AttributeMap attributes;
			attributes.insert(carlavst2::fileAttribute(), record.file);
			attributes.insert(carlavst2::nameAttribute(), record.name);
			attributes.insert(carlavst2::uniqueIdAttribute(), QString::number(record.uniqueId));
			attributes.insert(carlavst2::binaryAttribute(), carlavst2::binaryName(record.binary));
			attributes.insert(carlavst2::makerAttribute(), record.maker);
			pluginvendors::remember(record.file, record.name, record.maker);
			keys.append(Key(descriptor, record.name, attributes));
		}
	}

protected:
	QString displayName(const Key& key) const override
	{
		return key.attributes.value(carlavst2::nameAttribute(), key.name);
	}

	QString description(const Key& key) const override
	{
		const bool bridged = carlavst2::binaryFromName(key.attributes.value(carlavst2::binaryAttribute()))
			!= carlavst2::Binary::Native;
		return bridged ? QStringLiteral("VST2 instrument (Windows, through Carla's Wine bridge)")
			: QStringLiteral("VST2 instrument (through Carla)");
	}
};

} // namespace

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT carlavst2_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"VST2 (Carla)",
	QT_TRANSLATE_NOOP("PluginBrowser", "VST2 instruments, hosted through Carla"),
	"Zene Studio contributors",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader("logo"),
	nullptr,
	new CarlaVst2SubPluginFeatures(Plugin::Type::Instrument),
};

PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* model, void* data)
{
	// No Carla runtime (the build's weak stand-in library answers nullptr) or no key naming a
	// plug-in: there is nothing to host. Plugin::instantiate turns nullptr into the
	// placeholder that keeps a project's XML (Q050-018).
	const auto* key = static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data);
	if (carla_get_native_rack_plugin() == nullptr || key == nullptr
		|| key->attributes.value(carlavst2::fileAttribute()).isEmpty())
	{
		return nullptr;
	}
	auto* instrument = new CarlaVst2Instrument(static_cast<InstrumentTrack*>(model),
		&carlavst2_plugin_descriptor, *key);
	if (!instrument->isLoaded())
	{
		qWarning("carlavst2: %s", qPrintable(instrument->loadError()));
		delete instrument;
		return nullptr;
	}
	return instrument;
}

} // extern "C"

CarlaVst2Instrument::CarlaVst2Instrument(InstrumentTrack* track, const Descriptor* descriptor,
	const Descriptor::SubPluginFeatures::Key& key) :
	CarlaInstrument(track, descriptor, false, &key),
	m_pluginKey(key)
{
	setHideUnusedParameters(true);
	QString error;
	if (!loadPlugin(&error)) { m_loadError = error; }
	reloadParameters();
	// Carla's non-realtime idle on the GUI thread: an embedded editor's effEditIdle and the
	// deferred deletion of a replaced plug-in run here, never on the audio thread.
	m_idleTimer = new QTimer(this);
	m_idleTimer->setInterval(30);
	connect(m_idleTimer, &QTimer::timeout, this, &CarlaVst2Instrument::idle);
	m_idleTimer->start();
}

CarlaVst2Instrument::~CarlaVst2Instrument()
{
	m_editor.close();
	if (m_hostHandle != nullptr)
	{
		// The rack is torn down after this object (~CarlaInstrument): nothing may call back into it then.
		m_api.setCallback(m_hostHandle, nullptr, nullptr);
		carla_host_handle_free(m_hostHandle);
	}
	m_hostHandle = nullptr;
}

bool CarlaVst2Instrument::openHostApi(QString* error)
{
	if (m_api.complete() && m_hostHandle != nullptr) { return true; }
	const QString folder = carlavst2::runtimeFolder();
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
	m_hostHandle = carla_create_native_plugin_host_handle(nativeDescriptor(), nativeHandle());
	if (m_hostHandle == nullptr)
	{
		*error = QStringLiteral("Carla gave no host handle for its rack instance");
		return false;
	}
	m_api.setCallback(m_hostHandle, &CarlaVst2Instrument::engineCallback, this);
	return true;
}

void CarlaVst2Instrument::engineCallback(void* ptr, EngineCallbackOpcode action, uint, int value1, int value2, int,
	float, const char*)
{
	if (action != CarlaBackend::ENGINE_CALLBACK_EMBED_UI_RESIZED || ptr == nullptr) { return; }
	auto* self = static_cast<CarlaVst2Instrument*>(ptr);
	self->m_editorWidth.store(value1, std::memory_order_relaxed);
	self->m_editorHeight.store(value2, std::memory_order_relaxed);
}

bool CarlaVst2Instrument::isBridged() const
{
	return carlavst2::binaryFromName(m_pluginKey.attributes.value(carlavst2::binaryAttribute()))
		!= carlavst2::Binary::Native;
}

bool CarlaVst2Instrument::loadPlugin(QString* error)
{
	if (nativeHandle() == nullptr)
	{
		*error = QStringLiteral("no Carla runtime is installed (Carla Rack could not be instantiated)");
		return false;
	}
	const QString file = m_pluginKey.attributes.value(carlavst2::fileAttribute());
	if (!QFileInfo::exists(file))
	{
		*error = QStringLiteral("the VST2 binary '%1' is not there").arg(file);
		return false;
	}
	if (!openHostApi(error)) { return false; }
	carlavst2::Binary binary = carlavst2::binaryFromName(m_pluginKey.attributes.value(carlavst2::binaryAttribute()));
	if (binary == carlavst2::Binary::Unknown) { binary = carlavst2::binaryOf(file); }
	CarlaBackend::BinaryType type = CarlaBackend::BINARY_NONE;
	switch (binary)
	{
	case carlavst2::Binary::Native: {
		using namespace CarlaBackend;  // BINARY_NATIVE is a macro naming one of its enumerators
		type = BINARY_NATIVE;
		break;
	}
	case carlavst2::Binary::Win64: type = CarlaBackend::BINARY_WIN64; break;
	case carlavst2::Binary::Win32: type = CarlaBackend::BINARY_WIN32; break;
	case carlavst2::Binary::Unknown:
		*error = QStringLiteral("'%1' is not a VST2 binary this host can run").arg(file);
		return false;
	}
	const qint64 uniqueId = m_pluginKey.attributes.value(carlavst2::uniqueIdAttribute()).toLongLong();
	const QByteArray path = file.toLocal8Bit();
	// PLUGIN_OPTIONS_NULL: Carla's defaults for the plug-in, which include saving its chunk.
	if (!m_api.addPlugin(m_hostHandle, type, CarlaBackend::PLUGIN_VST2, path.constData(), nullptr, nullptr,
			uniqueId, nullptr, CarlaBackend::PLUGIN_OPTIONS_NULL)
		|| m_api.pluginCount(m_hostHandle) == 0)
	{
		*error = QStringLiteral("Carla could not load the VST2 plug-in '%1': %2")
			.arg(file, QString::fromUtf8(m_api.lastError(m_hostHandle)));
		return false;
	}
	return true;
}

void CarlaVst2Instrument::loadSettings(const QDomElement& elem)
{
	// Carla's state (a project with this one plug-in: binary, parameters, chunk) replaces what the
	// key loaded. A preset or project saved by this host always carries one.
	CarlaInstrument::loadSettings(elem);
	if (m_hostHandle != nullptr && m_api.pluginCount(m_hostHandle) == 0)
	{
		m_loadError = QStringLiteral("Carla could not restore the saved VST2 plug-in: %1")
			.arg(QString::fromUtf8(m_api.lastError(m_hostHandle)));
	}
}

void CarlaVst2Instrument::idle()
{
	if (nativeHandle() != nullptr && nativeDescriptor()->ui_idle != nullptr) { nativeDescriptor()->ui_idle(nativeHandle()); }
}

bool CarlaVst2Instrument::openNativeEditor(QString* error)
{
	if (!isLoaded() || m_hostHandle == nullptr || m_api.pluginCount(m_hostHandle) == 0)
	{
		*error = QStringLiteral("no VST2 plug-in is loaded");
		return false;
	}
	if (isBridged())
	{
		// A Windows plug-in runs in Wine, in another process: Carla shows its editor in a window of its own.
		m_api.showUi(m_hostHandle, 0, true);
		m_bridgedEditorShown = true;
		return true;
	}
	return m_editor.open(gui::PluginNativeEditor::Hooks{
		[this](void* parent, QString*) {
			// Carla's embed: effEditOpen with the host window, then effEditGetRect, whose size comes
			// back through engineCallback(). A VST2 editor does not report failure reliably, so the
			// window is the editor's from here on.
			m_editorWidth.store(0, std::memory_order_relaxed);
			m_editorHeight.store(0, std::memory_order_relaxed);
			m_api.embedUi(m_hostHandle, 0, parent);
			return true;
		},
		[this] {
			if (m_hostHandle != nullptr && m_api.pluginCount(m_hostHandle) > 0) { m_api.showUi(m_hostHandle, 0, false); }
		},
		[this](int* width, int* height) {
			*width = m_editorWidth.load(std::memory_order_relaxed);
			*height = m_editorHeight.load(std::memory_order_relaxed);
		}}, displayName(), error, gui::PluginNativeEditor::Owner{nullptr, this});
}

void CarlaVst2Instrument::closeNativeEditor()
{
	m_editor.close();
	if (m_bridgedEditorShown && m_hostHandle != nullptr && m_api.pluginCount(m_hostHandle) > 0)
	{
		m_api.showUi(m_hostHandle, 0, false);
	}
	m_bridgedEditorShown = false;
}

bool CarlaVst2Instrument::nativeEditorOpen() const
{
	return m_editor.isOpen() || m_bridgedEditorShown;
}

} // namespace lmms
