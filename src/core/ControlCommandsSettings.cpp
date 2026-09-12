/*
 * ControlCommandsSettings.cpp - the settings.*, audio.*, midi.* and app.*
 *                               command group (SPEC A11-A14).
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
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "AudioEngine.h"

#include "ControlVocabulary.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MidiClient.h"
#include "lmmsversion.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The control protocol version (AGENT-TOOLING.md #2), as control.version
//! reports it.
constexpr int ControlProtocolVersion = 1;

//! Never a real config value (a config value comes from an XML attribute), so
//! it distinguishes "unset" from "set to the empty string".
const QString UnsetSentinel = QStringLiteral("\u0001unset\u0001");

//! Splits a config key "<class>/<attribute>"; false when malformed.
bool splitKey(const QString& key, QString* cls, QString* attribute, ControlResult* error)
{
	const int slash = key.indexOf(QLatin1Char('/'));
	if (slash <= 0 || slash == key.size() - 1)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a config key of the form <class>/<attribute>, e.g. "
				"audioengine/audiodev").arg(key));
		return false;
	}
	*cls = key.left(slash);
	*attribute = key.mid(slash + 1);
	return true;
}

//! The config value, and whether the key is present at all.
QString configValue(const QString& cls, const QString& attribute, bool* present)
{
	const QString value = ConfigManager::inst()->value(cls, attribute, UnsetSentinel);
	*present = value != UnsetSentinel;
	return *present ? value : QString();
}

QJsonObject settingJson(const QString& key)
{
	QString cls;
	QString attribute;
	ControlResult ignored;
	QJsonObject out;
	if (!splitKey(key, &cls, &attribute, &ignored)) { return out; }
	bool present = false;
	const QString value = configValue(cls, attribute, &present);
	out.insert(QStringLiteral("key"), key);
	out.insert(QStringLiteral("value"), value);
	out.insert(QStringLiteral("present"), present);
	return out;
}

//! The audio backends this box can be built with. Filtered through the engine's
//! own table (AudioEngine::isAudioDevNameValid), so a name the running build
//! does not know is never reported as available.
QStringList audioDeviceCandidates()
{
	return {
		QStringLiteral("Dummy (no sound output)"),
		QStringLiteral("SDL (Simple DirectMedia Layer)"),
		QStringLiteral("ALSA (Advanced Linux Sound Architecture)"),
		QStringLiteral("PulseAudio"),
		QStringLiteral("OSS (Open Sound System)"),
		QStringLiteral("sndio"),
		QStringLiteral("JACK (JACK Audio Connection Kit)"),
		QStringLiteral("PortAudio"),
		QStringLiteral("soundio"),
	};
}

void registerSettingsGet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("settings.get");
	cmd.group = QStringLiteral("settings");
	cmd.verb = QStringLiteral("get");
	cmd.description = QStringLiteral("Read one UI/engine setting by the key the config file "
		"uses, '<class>/<attribute>' (for example audioengine/audiodev, ui/saveinterval, "
		"app/configured). 'value' is the config file's own string form and 'present' says "
		"whether the key is set at all rather than defaulted.");
	cmd.argsSchema = objectSchema({{QStringLiteral("key"), stringProperty()}},
		{QStringLiteral("key")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("key"), stringProperty()},
		{QStringLiteral("value"), stringProperty()},
		{QStringLiteral("present"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
	});
	cmd.handler = [](const QJsonObject& args) {
		const QString key = args.value(QStringLiteral("key")).toString();
		QString cls;
		QString attribute;
		ControlResult error;
		if (!splitKey(key, &cls, &attribute, &error)) { return error; }
		return ControlResult::success(settingJson(key));
	};
	registry.registerCommand(cmd);
}

void registerSettingsSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("settings.set");
	cmd.group = QStringLiteral("settings");
	cmd.verb = QStringLiteral("set");
	cmd.description = QStringLiteral("Write one UI/engine setting by its config-file key and "
		"persist the config file, exactly as the settings dialog does on OK. 'value' is the "
		"config file's own string form (booleans are \"1\"/\"0\"). Settings the engine reads at "
		"startup (audioengine/audiodev, samplerate, ...) take effect on the next start. The "
		"write is recorded with the previous value as its inverse, but ConfigManager is not "
		"journalled, so control.undo cannot reverse it.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("key"), stringProperty()},
		{QStringLiteral("value"), stringProperty()},
	}, {QStringLiteral("key"), QStringLiteral("value")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("key"), stringProperty()},
		{QStringLiteral("value"), stringProperty()},
		{QStringLiteral("previous"), stringProperty()},
		{QStringLiteral("persisted"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const QString key = args.value(QStringLiteral("key")).toString();
		const QString value = args.value(QStringLiteral("value")).toString();
		QString cls;
		QString attribute;
		ControlResult error;
		if (!splitKey(key, &cls, &attribute, &error)) { return error; }

		bool present = false;
		const QString previous = configValue(cls, attribute, &present);
		ConfigManager::inst()->setValue(cls, attribute, value);
		ConfigManager::inst()->saveConfigFile();

		QJsonObject result = settingJson(key);
		result.insert(QStringLiteral("previous"), previous);
		result.insert(QStringLiteral("persisted"), true);

		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"),
			QJsonObject{{QStringLiteral("key"), key},
				{QStringLiteral("value"), previous},
				{QStringLiteral("present"), present}});
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), present ? QStringLiteral("settings.set")
													   : QStringLiteral("UNIMPLEMENTED: unset "
														   "the key (ConfigManager::deleteValue "
														   "has no command)")},
				{QStringLiteral("args"),
					QJsonObject{{QStringLiteral("key"), key},
						{QStringLiteral("value"), previous}}}});
		transaction.insert(QStringLiteral("reversible"), false);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("snapshot only: ConfigManager is not a JournallingObject, so "
				"control.undo cannot apply the recorded inverse; the config file itself is "
				"rewritten in place"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

QJsonObject audioDevicesJson()
{
	const QString current = Engine::audioEngine() != nullptr
		? Engine::audioEngine()->audioDevName()
		: QString();
	const QString configured = ConfigManager::inst()->value(QStringLiteral("audioengine"),
		QStringLiteral("audiodev"));

	QJsonArray devices;
	for (const QString& candidate : audioDeviceCandidates())
	{
		if (!AudioEngine::isAudioDevNameValid(candidate)) { continue; }
		QJsonObject entry;
		entry.insert(QStringLiteral("name"), candidate);
		entry.insert(QStringLiteral("current"), candidate == current);
		entry.insert(QStringLiteral("configured"), candidate == configured);
		devices.append(entry);
	}
	QJsonObject out;
	out.insert(QStringLiteral("devices"), devices);
	out.insert(QStringLiteral("count"), devices.size());
	out.insert(QStringLiteral("current"), current);
	out.insert(QStringLiteral("configured"), configured);
	if (Engine::audioEngine() != nullptr)
	{
		out.insert(QStringLiteral("start_failed"), Engine::audioEngine()->audioDevStartFailed());
	}
	return out;
}

void registerAudioDeviceList(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("audio.device_list");
	cmd.group = QStringLiteral("audio");
	cmd.verb = QStringLiteral("device_list");
	cmd.description = QStringLiteral("The audio backends this build knows, with the one that is "
		"running and the one the config file selects. Requires no audio device: the running "
		"name is whatever the engine opened, the dummy device when nothing else would open.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("devices"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("current"), stringProperty()},
		{QStringLiteral("configured"), stringProperty()},
	});
	cmd.handler = [](const QJsonObject&) { return ControlResult::success(audioDevicesJson()); };
	registry.registerCommand(cmd);
}

void registerAudioDeviceSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("audio.device_set");
	cmd.group = QStringLiteral("audio");
	cmd.verb = QStringLiteral("device_set");
	cmd.description = QStringLiteral("Choose the audio backend for the next start: the device "
		"name is written to the config file's audioengine/audiodev, the same key the settings "
		"dialog writes. The running device is NOT switched - the engine constructs its device in "
		"AudioEngine::initDevices() during startup and the product's own settings dialog also "
		"only stores the preference and warns that a restart is needed. The result says so "
		"explicitly with applied=\"next_start\".");
	cmd.argsSchema = objectSchema({{QStringLiteral("device"), stringProperty()}},
		{QStringLiteral("device")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("device"), stringProperty()},
		{QStringLiteral("previous"), stringProperty()},
		{QStringLiteral("applied"), stringProperty()},
		{QStringLiteral("restart_required"), QJsonObject{{QStringLiteral("type"),
			QStringLiteral("boolean")}}},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const QString device = args.value(QStringLiteral("device")).toString();
		if (!AudioEngine::isAudioDevNameValid(device))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("'%1' is not an audio backend of this build (see "
					"audio.device_list)").arg(device));
		}
		bool present = false;
		const QString previous = configValue(QStringLiteral("audioengine"),
			QStringLiteral("audiodev"), &present);
		ConfigManager::inst()->setValue(QStringLiteral("audioengine"), QStringLiteral("audiodev"),
			device);
		ConfigManager::inst()->saveConfigFile();

		QJsonObject result;
		result.insert(QStringLiteral("device"), device);
		result.insert(QStringLiteral("previous"), previous);
		result.insert(QStringLiteral("applied"), QStringLiteral("next_start"));
		result.insert(QStringLiteral("restart_required"), true);
		result.insert(QStringLiteral("live_switch"), false);

		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"),
			QJsonObject{{QStringLiteral("device"), previous},
				{QStringLiteral("present"), present}});
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("audio.device_set")},
				{QStringLiteral("args"), QJsonObject{{QStringLiteral("device"), previous}}}});
		transaction.insert(QStringLiteral("reversible"), false);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("snapshot only: the preference is written to the config file, which "
				"is not journalled; the running device is unchanged until the next start"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerMidiDeviceList(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("midi.device_list");
	cmd.group = QStringLiteral("midi");
	cmd.verb = QStringLiteral("device_list");
	cmd.description = QStringLiteral("The MIDI client that is running, the client the config "
		"file selects, and every readable/writable port it exposes. With no MIDI backend open "
		"the client is the dummy one and both port lists are empty, which is reported as such "
		"rather than as an error.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("client"), stringProperty()},
		{QStringLiteral("configured"), stringProperty()},
		{QStringLiteral("readable"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("writable"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("count"), integerProperty()},
	});
	cmd.handler = [](const QJsonObject&) {
		AudioEngine* engine = Engine::audioEngine();
		QJsonObject result;
		result.insert(QStringLiteral("client"),
			engine != nullptr ? engine->midiClientName() : QString());
		result.insert(QStringLiteral("configured"), settingJson(QStringLiteral("audioengine"
			"/mididev")).value(QStringLiteral("value")).toString());
		MidiClient* client = engine != nullptr ? engine->midiClient() : nullptr;
		QJsonArray readable;
		QJsonArray writable;
		if (client != nullptr)
		{
			for (const QString& port : client->readablePorts()) { readable.append(port); }
			for (const QString& port : client->writablePorts()) { writable.append(port); }
		}
		result.insert(QStringLiteral("readable"), readable);
		result.insert(QStringLiteral("writable"), writable);
		result.insert(QStringLiteral("count"), readable.size() + writable.size());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerAppVersion(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("app.version");
	cmd.group = QStringLiteral("app");
	cmd.verb = QStringLiteral("version");
	cmd.description = QStringLiteral("The product name, the version string, the control protocol "
		"version and the build options the binary was compiled with.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("product"), stringProperty()},
		{QStringLiteral("version"), stringProperty()},
		{QStringLiteral("proto"), integerProperty()},
		{QStringLiteral("build_options"), stringProperty()},
	});
	cmd.handler = [](const QJsonObject&) {
		QJsonObject result;
		result.insert(QStringLiteral("product"), QStringLiteral("Zene Studio"));
		result.insert(QStringLiteral("version"), QString::fromUtf8(LMMS_VERSION));
		result.insert(QStringLiteral("proto"), ControlProtocolVersion);
		result.insert(QStringLiteral("build_options"), QString::fromUtf8(LMMS_BUILD_OPTIONS));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerSettingsCommands(ControlRegistry& registry)
{
	registerSettingsGet(registry);
	registerSettingsSet(registry);
	registerAudioDeviceList(registry);
	registerAudioDeviceSet(registry);
	registerMidiDeviceList(registry);
	registerAppVersion(registry);
}

} // namespace lmms
