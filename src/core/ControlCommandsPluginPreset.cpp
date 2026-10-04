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
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlDeviceSupport.h"

#include "ControlVocabulary.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "DeviceAbState.h"
#include "Effect.h"
#include "InstrumentTrack.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

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

/*! 0.5.0 theme 12: what holds the name of the preset a device last loaded or saved, for its card - the effect, or
 *  the instrument's track (an instrument preset can put a new instrument object on it). A dynamic property: interface
 *  state, never saved with the project, and no part of any reply. */
QObject* presetNameHolder(const ControlDeviceHandle& handle)
{
	return handle.effect != nullptr ? static_cast<QObject*>(handle.effect) : static_cast<QObject*>(handle.track);
}

QString presetNameOf(const ControlDeviceHandle& handle)
{
	const QObject* holder = presetNameHolder(handle);
	return holder != nullptr ? holder->property(presetNameProperty()).toString() : QString();
}

void notePresetName(const ControlDeviceHandle& handle, const QString& name)
{
	if (QObject* holder = presetNameHolder(handle)) { holder->setProperty(presetNameProperty(), name); }
}

//! The same for the device @a pluginId on @a targetId names now: an undo or a redo of a preset load.
void notePresetName(const QString& targetId, const QString& pluginId, const QString& name)
{
	ControlTarget target;
	ControlDeviceHandle handle;
	ControlResult ignored;
	if (resolveControlTarget(targetId, &target, &ignored) && resolveControlDevice(target, pluginId, &handle, &ignored))
	{
		notePresetName(handle, name);
	}
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
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
	}, {QStringLiteral("target"), QStringLiteral("plugin")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("presets"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("dir"), stringProperty()},
		{QStringLiteral("factory_dir"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
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
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("overwrite"), booleanProperty()},
	}, {QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("name")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("bytes"), integerProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
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
		notePresetName(handle, name);

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
			QStringLiteral("file-level snapshot, no automatic inverse: the command writes a preset "
				"file OUTSIDE the project (the project is untouched), and the replaced revision is "
				"recorded in 'before.previous_content' (bounded). The fallback is to write that "
				"back with plugin.preset_save, or delete the file when before.replaced was false"));
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
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
	}, {QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("name")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("restored"), booleanProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
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
		// SPEC A16: the same action-step inverse plugin.state_load uses.
		control::addUndoStep(
			[targetId = target.id, pluginId = args.value(QStringLiteral("plugin")).toString(),
				previous, previousName = presetNameOf(handle)]() {
				controlRestoreCapturedState(targetId, pluginId, previous);
				notePresetName(targetId, pluginId, previousName);
			},
			[targetId = target.id, pluginId = args.value(QStringLiteral("plugin")).toString(),
				path, name]() {
				QByteArray bytes;
				ControlResult ignored;
				if (controlReadFileBytes(path, &bytes, &ignored))
				{
					controlRestoreCapturedState(targetId, pluginId, bytes);
					notePresetName(targetId, pluginId, name);
				}
			});
		ControlResult restored = controlRestoreDeviceState(handle, bytes);
		if (!restored.ok) { return restored; }
		notePresetName(handle, name);

		QJsonObject result = restored.result;
		result.insert(QStringLiteral("path"), path);

		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"), stateSnapshot(previous, handle));
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("plugin.state_load")},
				{QStringLiteral("args"),
					QJsonObject{{QStringLiteral("note"), QStringLiteral("write "
						"before.state_xml to a path and plugin.state_load it back")}}}});
		transaction.insert(QStringLiteral("reversible"), true);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("action checkpoint: the recorded undo step restores the captured "
				"settings XML by re-resolving the device from the transaction's target and plugin "
				"ids, so one control.undo or one Ctrl+Z reverses the preset load"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

//! 0.5.0 L11 (WF-025): the user preset @a args names, for removing or renaming it - factory presets are
//! read-only. False with @a error set when the device or the preset cannot be found.
bool userPreset(const QJsonObject& args, QString* path, ControlResult* error)
{
	ControlTarget target;
	ControlDeviceHandle handle;
	if (!resolveDeviceArg(args, &target, &handle, error)) { return false; }
	const QString name = args.value(QStringLiteral("name")).toString();
	if (!controlSafePresetName(name, error)) { return false; }
	*path = controlUserPresetDir(handle) + controlPresetFileName(name);
	if (QFileInfo::exists(*path)) { return true; }
	*error = ControlResult::failure(findPreset(handle, controlPresetFileName(name)).isEmpty() ? ControlErrorKind::NotFound
		: ControlErrorKind::Refused, QStringLiteral("no user preset '%1' for '%2' (factory presets are read-only)").arg(name, handle.pluginName));
	return false;
}

ControlResult removePreset(const QJsonObject& args)
{
	QString path;
	ControlResult error;
	if (!userPreset(args, &path, &error)) { return error; }
	QByteArray bytes;
	if (!controlReadFileBytes(path, &bytes, &error)) { return error; }
	if (!QFile::remove(path)) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("cannot remove %1").arg(path)); }
	// The file comes back with its bytes; the step counts them against the undo budget.
	control::addStructuralUndoStep([path, bytes]() { ControlResult ignored; controlWriteFileBytes(path, bytes, true, &ignored); },
		[path]() { QFile::remove(path); }, bytes.size());
	return ControlResult::success({{QStringLiteral("removed"), path}, {QStringLiteral("bytes"), bytes.size()}});
}

ControlResult renamePreset(const QJsonObject& args)
{
	QString path;
	ControlResult error;
	if (!userPreset(args, &path, &error)) { return error; }
	const QString newName = args.value(QStringLiteral("new_name")).toString();
	if (!controlSafePresetName(newName, &error)) { return error; }
	const QString renamed = QFileInfo(path).absolutePath() + QLatin1Char('/') + controlPresetFileName(newName);
	if (QFileInfo::exists(renamed))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a preset named '%1' exists already").arg(newName));
	}
	if (!QFile::rename(path, renamed)) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("cannot rename %1").arg(path)); }
	control::addUndoStep([path, renamed]() { QFile::rename(renamed, path); }, [path, renamed]() { QFile::rename(path, renamed); });
	return ControlResult::success({{QStringLiteral("path"), renamed}, {QStringLiteral("name"), controlPresetFileName(newName)}});
}

void registerPresetFileEdits(ControlRegistry& registry)
{
	const QJsonObject device{{QStringLiteral("target"), stringProperty()}, {QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("name"), stringProperty()}};
	const QJsonArray required{QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("name")};
	ControlCommand remove;
	remove.id = QStringLiteral("plugin.preset_remove");
	remove.group = QStringLiteral("plugin");
	remove.verb = QStringLiteral("preset_remove");
	remove.description = QStringLiteral("Delete one of the device's user presets ('name'); factory presets are read-only. One undo "
		"step writes the file back.");
	remove.argsSchema = objectSchema(device, required);
	remove.resultSchema = objectProperty();
	remove.mutating = true;
	remove.handler = removePreset;
	registry.registerCommand(remove);
	ControlCommand rename = remove;
	rename.id = QStringLiteral("plugin.preset_rename");
	rename.verb = QStringLiteral("preset_rename");
	rename.description = QStringLiteral("Rename one of the device's user presets from 'name' to 'new_name' (refused when that name "
		"is taken). One undo step.");
	QJsonObject renameArgs = device;
	renameArgs.insert(QStringLiteral("new_name"), stringProperty());
	QJsonArray renameRequired = required;
	renameRequired.append(QStringLiteral("new_name"));
	rename.argsSchema = objectSchema(renameArgs, renameRequired);
	rename.handler = renamePreset;
	registry.registerCommand(rename);
}

} // namespace

void registerPluginPresetCommands(ControlRegistry& registry)
{
	registerPresetList(registry);
	registerPresetSave(registry);
	registerPresetLoad(registry);
	registerPresetFileEdits(registry);
}

} // namespace lmms
