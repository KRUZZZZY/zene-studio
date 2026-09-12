/*
 * ControlCommandsPluginPreset.cpp - plugin.preset_list / preset_load /
 *                                   preset_save (SPEC A14/A16).
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

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"

namespace lmms
{

namespace
{

QJsonObject schemaObject(QJsonObject properties, QJsonArray required = {})
{
	QJsonObject schema;
	schema.insert(QStringLiteral("type"), QStringLiteral("object"));
	schema.insert(QStringLiteral("properties"), std::move(properties));
	schema.insert(QStringLiteral("required"), std::move(required));
	schema.insert(QStringLiteral("additionalProperties"), false);
	return schema;
}

QJsonObject stringProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
}

QJsonObject booleanProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}};
}

QJsonObject intProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
}

//! The preset files of one directory.
QJsonArray presetsIn(const QString& dir, bool factory)
{
	QJsonArray presets;
	const QString path = dir;
	for (const QFileInfo& entry : QDir(path).entryInfoList({QStringLiteral("*.xpf")},
			QDir::Files, QDir::Name))
	{
		QJsonObject preset;
		preset.insert(QStringLiteral("name"), entry.completeBaseName());
		preset.insert(QStringLiteral("path"), entry.absoluteFilePath());
		preset.insert(QStringLiteral("factory"), factory);
		preset.insert(QStringLiteral("bytes"), static_cast<qint64>(entry.size()));
		presets.append(preset);
	}
	return presets;
}

//! The bounded pre-change record both preset commands write into the
//! transaction (SPEC A16: snapshot fallback, documented per command).
QJsonObject presetSnapshot(const QString& path, const ControlDeviceHandle& handle)
{
	QJsonObject snapshot = controlFileSnapshot(path);
	snapshot.insert(QStringLiteral("path"), path);
	snapshot.insert(QStringLiteral("plugin"), handle.pluginName);
	return snapshot;
}

QJsonObject stateSnapshot(const QByteArray& bytes, const ControlDeviceHandle& handle)
{
	QString xml = QString::fromUtf8(bytes);
	const bool truncated = xml.size() > ControlSnapshotLimit;
	if (truncated) { xml.truncate(ControlSnapshotLimit); }
	QJsonObject snapshot;
	snapshot.insert(QStringLiteral("plugin"), handle.pluginName);
	snapshot.insert(QStringLiteral("state_xml"), xml);
	snapshot.insert(QStringLiteral("state_truncated"), truncated);
	return snapshot;
}

//! Resolves target + device from the command args; false and *error set on
//! failure.
bool resolveDeviceArg(const QJsonObject& args, ControlTarget* target,
	ControlDeviceHandle* handle, ControlResult* error)
{
	if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), target, error))
	{
		return false;
	}
	return resolveControlDevice(*target, args.value(QStringLiteral("plugin")).toString(), handle,
		error);
}

//! The preset's absolute path: the user directory first, then the factory one.
QString findPreset(const ControlDeviceHandle& handle, const QString& fileName)
{
	const QStringList candidates = {
		controlUserPresetDir(handle) + fileName,
		controlFactoryPresetDir(handle) + fileName,
	};
	for (const QString& candidate : candidates)
	{
		if (QFileInfo::exists(candidate)) { return candidate; }
	}
	return QString();
}

void registerPresetList(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.preset_list");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("preset_list");
	cmd.description = QStringLiteral("The preset files (*.xpf) available to a device: the user "
		"preset directory and the factory preset directory, the same two the product's browser "
		"reads. An effect's directory is the device's own name (its LADSPA label when hosted); "
		"an instrument's is its product preset folder.");
	cmd.argsSchema = schemaObject({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
	}, {QStringLiteral("target"), QStringLiteral("plugin")});
	cmd.resultSchema = schemaObject({
		{QStringLiteral("presets"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("count"), intProperty()},
		{QStringLiteral("dir"), stringProperty()},
	});
	cmd.handler = [](const QJsonObject& args) {
		ControlTarget target;
		ControlDeviceHandle handle;
		ControlResult error;
		if (!resolveDeviceArg(args, &target, &handle, &error)) { return error; }

		QJsonArray presets = presetsIn(controlUserPresetDir(handle), false);
		for (const QJsonValue& factory : presetsIn(controlFactoryPresetDir(handle), true))
		{
			presets.append(factory);
		}

		QJsonObject result;
		result.insert(QStringLiteral("presets"), presets);
		result.insert(QStringLiteral("count"), presets.size());
		result.insert(QStringLiteral("plugin"), handle.pluginName);
		result.insert(QStringLiteral("dir"), controlUserPresetDir(handle));
		result.insert(QStringLiteral("factory_dir"), controlFactoryPresetDir(handle));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerPresetSave(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.preset_save");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("preset_save");
	cmd.description = QStringLiteral("Save the current device state as a preset (*.xpf) in the "
		"product's user preset directory. An instrument writes the instrument-track preset "
		"document the browser loads; an effect writes the device's own state document, because "
		"this build has no separate effect-preset format. An existing preset of the same name is "
		"refused unless \"overwrite\":true.");
	cmd.argsSchema = schemaObject({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("overwrite"), booleanProperty()},
	}, {QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("name")});
	cmd.resultSchema = schemaObject({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("bytes"), intProperty()},
		{QStringLiteral("sha256"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlTarget target;
		ControlDeviceHandle handle;
		ControlResult error;
		if (!resolveDeviceArg(args, &target, &handle, &error)) { return error; }
		const QString name = args.value(QStringLiteral("name")).toString();
		if (!controlSafePresetName(name, &error)) { return error; }

		const QString dir = controlUserPresetDir(handle);
		if (!QDir().mkpath(dir))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("cannot create the preset directory %1").arg(dir));
		}
		const QString path = dir + controlPresetFileName(name);
		const QJsonObject snapshot = presetSnapshot(path, handle);
		const QByteArray bytes = controlDeviceStateBytes(handle);
		if (!controlWriteFileBytes(path, bytes,
				args.value(QStringLiteral("overwrite")).toBool(false), &error))
		{
			return error;
		}

		QJsonObject result;
		result.insert(QStringLiteral("path"), path);
		result.insert(QStringLiteral("name"), controlPresetFileName(name));
		result.insert(QStringLiteral("bytes"), bytes.size());
		result.insert(QStringLiteral("sha256"), controlSha256OfBytes(bytes));
		result.insert(QStringLiteral("plugin"), handle.pluginName);

		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"), snapshot);
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("plugin.preset_load")},
				{QStringLiteral("args"),
					QJsonObject{{QStringLiteral("note"), QStringLiteral("this undoes the "
						"*device*, not the file write; 'before.previous_content' holds the "
						"replaced revision")}}}});
		transaction.insert(QStringLiteral("reversible"), false);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("file-level: a preset file is created or replaced; the previous "
				"revision is recorded in 'before.previous_content' (bounded)"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerPresetLoad(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.preset_load");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("preset_load");
	cmd.description = QStringLiteral("Load a preset (*.xpf) into a device: the user preset "
		"directory is searched first, then the factory one. A document written for a different "
		"device is refused.");
	cmd.argsSchema = schemaObject({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
	}, {QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("name")});
	cmd.resultSchema = schemaObject({
		{QStringLiteral("restored"), booleanProperty()},
		{QStringLiteral("path"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlTarget target;
		ControlDeviceHandle handle;
		ControlResult error;
		if (!resolveDeviceArg(args, &target, &handle, &error)) { return error; }
		const QString name = args.value(QStringLiteral("name")).toString();
		if (!controlSafePresetName(name, &error)) { return error; }

		const QString path = findPreset(handle, controlPresetFileName(name));
		if (path.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("no preset '%1' for plugin '%2' (looked in %3)")
					.arg(name, handle.pluginName)
					.arg(controlUserPresetDir(handle) + QLatin1String(", ") +
						controlFactoryPresetDir(handle)));
		}
		QByteArray bytes;
		if (!controlReadFileBytes(path, &bytes, &error)) { return error; }

		const QByteArray previous = controlDeviceStateBytes(handle);
		ControlResult restored = controlRestoreDeviceState(handle, bytes);
		if (!restored.ok) { return restored; }

		QJsonObject result = restored.result;
		result.insert(QStringLiteral("path"), path);

		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"), stateSnapshot(previous, handle));
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("plugin.state_load")},
				{QStringLiteral("args"),
					QJsonObject{{QStringLiteral("note"), QStringLiteral("write "
						"before.state_xml to a path and plugin.state_load it back")}}}});
		transaction.insert(QStringLiteral("reversible"), false);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("snapshot only: loading a preset replaces the device's settings; the "
				"previous settings XML is recorded in 'before.state_xml'"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerPluginPresetCommands(ControlRegistry& registry)
{
	registerPresetList(registry);
	registerPresetSave(registry);
	registerPresetLoad(registry);
}

} // namespace lmms
