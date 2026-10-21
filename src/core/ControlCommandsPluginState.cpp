/*
 * ControlCommandsPluginState.cpp - plugin.state_save / plugin.state_load, the
 *                                  device state files (SPEC A14/A16).
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

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlDeviceSupport.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "PluginFactory.h"
#include "Song.h"
#include "ControlEdit.h"

#include "ControlVocabulary.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The device state as a bounded snapshot record for the transaction.
QString boundedState(const QByteArray& bytes, bool* truncated)
{
	QString xml = QString::fromUtf8(bytes);
	*truncated = xml.size() > ControlSnapshotLimit;
	if (*truncated) { xml.truncate(ControlSnapshotLimit); }
	return xml;
}

void registerStateSave(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.state_save");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("state_save");
	cmd.description = QStringLiteral("Write a device's state to a file path. An effect writes "
		"this fork's zenepluginstate document; an instrument ('inst') writes the product's own "
		"instrument-track preset document, the one the browser loads. An existing file is "
		"refused unless \"overwrite\":true, so a save cannot silently destroy a state file.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("overwrite"), booleanProperty()},
	}, {QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("path")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("bytes"), integerProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("replaced"), booleanProperty()},
		{QStringLiteral("plugin"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlTarget target;
		ControlResult error;
		if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error))
		{
			return error;
		}
		ControlDeviceHandle handle;
		if (!resolveControlDevice(target, args.value(QStringLiteral("plugin")).toString(), &handle,
				&error))
		{
			return error;
		}
		const QString path = args.value(QStringLiteral("path")).toString();
		if (!path.startsWith(QLatin1Char('/')))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'path' must be an absolute path"));
		}
		const QJsonObject snapshot = controlFileSnapshot(path);
		const bool replaced = QFileInfo::exists(path);
		const QByteArray bytes = controlDeviceStateBytes(handle);
		if (!controlWriteFileBytes(path, bytes, args.value(QStringLiteral("overwrite")).toBool(false),
				&error))
		{
			return error;
		}

		QJsonObject result;
		result.insert(QStringLiteral("path"), path);
		result.insert(QStringLiteral("bytes"), bytes.size());
		result.insert(QStringLiteral("sha256"), controlSha256OfBytes(bytes));
		result.insert(QStringLiteral("replaced"), replaced);
		result.insert(QStringLiteral("plugin"), handle.pluginName);

		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"), snapshot);
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("plugin.state_load")},
				{QStringLiteral("args"),
					QJsonObject{{QStringLiteral("target"), target.id},
						{QStringLiteral("plugin"), args.value(QStringLiteral("plugin")).toString()},
						{QStringLiteral("note"), QStringLiteral("this undoes the *device*, not "
							"the file write; 'before.previous_content' holds the replaced "
							"revision")}}}});
		transaction.insert(QStringLiteral("reversible"), false);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("file-level snapshot, no automatic inverse: the command writes a file "
				"OUTSIDE the project (the project is untouched), and the replaced revision is "
				"recorded in 'before.previous_content' (bounded). The fallback is to write that "
				"back with plugin.state_save, or delete the file when before.replaced was false"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerStateLoad(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.state_load");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("state_load");
	cmd.description = QStringLiteral("Restore a device's state from a file written by "
		"plugin.state_save. A document written for a different device is refused, so a state "
		"file cannot be pushed into the wrong plugin.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
	}, {QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("path")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("restored"), booleanProperty()},
		{QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("previous_sha256"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlTarget target;
		ControlResult error;
		if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error))
		{
			return error;
		}
		ControlDeviceHandle handle;
		if (!resolveControlDevice(target, args.value(QStringLiteral("plugin")).toString(), &handle,
				&error))
		{
			return error;
		}
		const QString path = args.value(QStringLiteral("path")).toString();
		QByteArray bytes;
		if (!controlReadFileBytes(path, &bytes, &error)) { return error; }

		// Keep the pre-load settings before they are overwritten: this is what
		// makes the recorded snapshot an honest record (SPEC A16).
		const QByteArray previous = controlDeviceStateBytes(handle);
		// ... and the SAME bytes are the inverse: ONE action step restores them
		// by re-resolving the device at undo time (a handle holds raw device
		// pointers, so the step carries ids, not pointers).
		const QString targetId = args.value(QStringLiteral("target")).toString();
		const QString pluginId = args.value(QStringLiteral("plugin")).toString();
		// Restored FIRST: the step is registered only once the load has
		// succeeded, because a refused document changes nothing - a step taken
		// for it would undo a device that was never altered and push the real
		// step before it one further from the keyboard.
		ControlResult restored = controlRestoreDeviceState(handle, bytes);
		if (!restored.ok) { return restored; }
		control::addUndoStep(
			[targetId, pluginId, previous]() {
				controlRestoreCapturedState(targetId, pluginId, previous);
			},
			// The LOADED bytes, not the path: a redo replays what this command
			// applied. Re-reading the path made a file edited (or replaced) after
			// the load decide what the redo did.
			[targetId, pluginId, bytes]() {
				controlRestoreCapturedState(targetId, pluginId, bytes);
			});

		QJsonObject result = restored.result;
		result.insert(QStringLiteral("path"), path);
		result.insert(QStringLiteral("previous_sha256"), controlSha256OfBytes(previous));

		bool truncated = false;
		QJsonObject snapshot;
		snapshot.insert(QStringLiteral("path"), path);
		snapshot.insert(QStringLiteral("plugin"), handle.pluginName);
		snapshot.insert(QStringLiteral("state_xml"), boundedState(previous, &truncated));
		snapshot.insert(QStringLiteral("state_truncated"), truncated);

		// The inverse IS the action checkpoint recorded above, so it is advertised as
		// the one command that replays it - the same shape every other action
		// checkpoint carries. Naming plugin.state_save here (with only a `note`)
		// advertised a command that needs target, plugin and path, and saving a
		// device's CURRENT state would not restore the previous one anyway.
		result.insert(QStringLiteral("__transaction"), transactionPayload(snapshot,
			QStringLiteral("control.undo"), {}, true,
			QStringLiteral("action checkpoint: the recorded undo step restores the captured "
				"settings XML by re-resolving the device from the transaction's target and plugin "
				"ids (a handle's raw device pointers must not outlive the command)")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

//! The first message the instrument reported while opening a file that it had not reported before, else empty.
QString newInstrumentError(const QHash<QString, int>& before)
{
	const QHash<QString, int>& now = Engine::getSong()->errors();
	for (auto it = now.constBegin(); it != now.constEnd(); ++it)
	{
		if (it.value() > before.value(it.key(), 0)) { return it.key(); }
	}
	return {};
}

//! Refuses a file the target's instrument does not open; true when it does (same extension rule as browser.load).
bool instrumentOpensFile(const ControlTarget& target, const QString& path, ControlResult* error)
{
	const QString extension = QFileInfo(path).suffix().toLower();
	const QString opener = getPluginFactory()->pluginSupportingExtension(extension).info.name();
	const Instrument* instrument = target.instrumentTrack != nullptr ? target.instrumentTrack->instrument() : nullptr;
	if (instrument != nullptr && !opener.isEmpty() && opener == QString::fromUtf8(instrument->descriptor()->name)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("the instrument on %1 does not open .%2 files").arg(target.id, extension));
	return false;
}

ControlResult openFileInInstrument(const QJsonObject& args)
{
	ControlTarget target;
	ControlResult error;
	if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error)) { return error; }
	const QString path = args.value(QStringLiteral("path")).toString();
	// The platform's own rule for "absolute": /dir/x.sfz, C:\dir\x.sfz, C:/dir/x.sfz or \\server\share\x.sfz.
	if (!QFileInfo(path).isAbsolute() || !QFileInfo(path).isFile())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'path' must be an absolute path to an existing file"));
	}
	ControlDeviceHandle handle;
	if (!instrumentOpensFile(target, path, &error) || !resolveControlDevice(target, QStringLiteral("inst"), &handle, &error)) { return error; }

	const QByteArray previous = controlDeviceStateBytes(handle);
	const QHash<QString, int> errorsBefore = Engine::getSong()->errors();
	target.instrumentTrack->instrument()->loadFile(path);
	if (const QString failure = newInstrumentError(errorsBefore); !failure.isEmpty())
	{
		controlRestoreDeviceState(handle, previous);  // the instrument keeps what it had
		return ControlResult::failure(ControlErrorKind::Refused, failure);
	}
	Engine::getSong()->setModified();

	const QByteArray loaded = controlDeviceStateBytes(handle);
	const QString targetId = args.value(QStringLiteral("target")).toString();
	control::addUndoStep(
		[targetId, previous]() { controlRestoreCapturedState(targetId, QStringLiteral("inst"), previous); },
		[targetId, loaded]() { controlRestoreCapturedState(targetId, QStringLiteral("inst"), loaded); });

	bool truncated = false;
	QJsonObject snapshot{{QStringLiteral("path"), path}, {QStringLiteral("plugin"), handle.pluginName},
		{QStringLiteral("state_xml"), boundedState(previous, &truncated)}, {QStringLiteral("state_truncated"), truncated}};
	QJsonObject result{{QStringLiteral("target"), targetId}, {QStringLiteral("plugin"), QStringLiteral("inst")},
		{QStringLiteral("device_plugin"), handle.pluginName}, {QStringLiteral("path"), path}};
	result.insert(QStringLiteral("__transaction"), transactionPayload(snapshot, QStringLiteral("control.undo"), {}, true,
		QStringLiteral("action checkpoint: the recorded undo step restores the instrument's previous state XML (its file and settings)")));
	return ControlResult::success(result);
}

void registerOpenFile(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.open_file");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("open_file");
	cmd.description = QStringLiteral("Open a file in the instrument a track already carries - what the instrument window's Open button "
		"does: an .sfz in the SFZ Player, a sample in the Audio File Processor. The instrument must be the one that opens the "
		"file's extension (browser.load puts a file on a NEW track instead). A file the instrument cannot load is refused and the "
		"instrument keeps what it had. One control.undo step restores the previous file and settings.");
	cmd.argsSchema = objectSchema({{QStringLiteral("target"), stringProperty()}, {QStringLiteral("path"), stringProperty()}},
		{QStringLiteral("target"), QStringLiteral("path")});
	cmd.resultSchema = objectSchema({{QStringLiteral("target"), stringProperty()}, {QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("device_plugin"), stringProperty()}, {QStringLiteral("path"), stringProperty()}});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return openFileInInstrument(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerPluginStateCommands(ControlRegistry& registry)
{
	registerStateSave(registry);
	registerStateLoad(registry);
	registerOpenFile(registry);
	registerPluginPresetCommands(registry);
}

} // namespace lmms
