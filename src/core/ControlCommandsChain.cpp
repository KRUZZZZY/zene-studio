/*
 * ControlCommandsChain.cpp - the READ/CAPTURE half of the chain.* command group
 *                            (SPEC A11-A16): chain.list, chain.get_state and
 *                            chain.save.
 *
 * WHAT A "CHAIN PRESET" IS HERE. A named, storable effect chain: the ordered
 * device list plus each device's own state. It is the item the 0.3.0 ladder
 * carries as "plugin-chain presets" (WAVE-1-BRIEFS.md, OWNER-31 item 2), and it
 * is NOT rack.add_chain: a rack chain is one more parallel signal path inside
 * one channel's rack (rack.chain(index), docs/RACKS.md), while a chain preset is
 * a COPY of a chain's devices and their settings that can be applied to another
 * track at any later time. Nothing here touches a rack.
 *
 * WHERE IT IS STORED, AND WHY NOT IN THE PROJECT. The store is the product's own
 * user preset tree (ConfigManager::userPresetsDir() + "chainpresets/"), one .zcp
 * document per preset. A preset exists to be reused, and "reused" means another
 * track in another project: project state would make chain.save a command that
 * only works until the caller opens something else, which is not a preset. The
 * store therefore survives project.save/project.open by construction (it is not
 * in the document at all), and docs/KNOWN-LIMITATIONS.md states that it is
 * per-user, not per-project.
 *
 * THE THREE PARTS OF THE CONTRACT THIS FILE IS: the engine half is
 * ControlChainPresetSupport.cpp (the document, the identity, the apply), these
 * are the commands, the contract rows are in ControlReversibilityTableAction.cpp
 * / ControlReversibilityTablePassive.cpp, the proof is the registered ctest
 * ControlChainPresets (tests/control-chain-presets.py) plus the in-process
 * ControlChainPresetTest, and the one-line UI absence is in the release notes
 * and docs/KNOWN-LIMITATIONS.md.
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

#include "ControlChainPresetSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The preset's own record plus the store's directory, so a caller never has to
//! guess where the store lives.
QJsonObject presetResult(const ControlChainPreset& preset, bool detailed)
{
	QJsonObject out = controlChainPresetJson(preset, detailed);
	out.insert(QStringLiteral("dir"), controlChainPresetDir());
	return out;
}

//! Reads one stored preset by name, or the typed not_found naming the store.
bool readPresetByName(const QString& name, ControlChainPreset* preset, ControlResult* error)
{
	QString path;
	if (!controlChainPresetPath(name, &path, error)) { return false; }
	if (!QFileInfo::exists(path))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no chain preset '%1' in the store (%2 holds %3)")
				.arg(name, controlChainPresetDir())
				.arg(controlChainPresetFiles().size()));
		return false;
	}
	return controlReadChainPreset(path, preset, error);
}

//! Captures every device of \a target's chain, in the chain's own order.
bool captureDevices(const ControlTarget& target, QList<ControlChainPresetDevice>* out,
	ControlResult* error)
{
	const int count = static_cast<int>(target.chain->effects().size());
	for (int i = 0; i < count; ++i)
	{
		// The device is resolved the way plugin.state_save resolves it, so the
		// captured bytes are that command's bytes and not a second serialisation.
		ControlDeviceHandle handle;
		if (!resolveControlDevice(target, control::effectId(i), &handle, error)) { return false; }
		ControlChainPresetDevice device;
		device.state = controlDeviceStateBytes(handle);
		if (!controlChainPresetIdentity(&device, error)) { return false; }
		out->append(device);
	}
	return true;
}

/*! The recorded inverse of a store write: a file this command created is
 *  REMOVED, a file it replaced gets its previous bytes back. The redo half
 *  re-writes what this command wrote, so a redo is the command again rather than
 *  a dropped step (the same rule the groove pool's recorded step follows).
 */
void recordStoreWrite(const QString& path, bool replaced, const QByteArray& previous,
	const QByteArray& written)
{
	addUndoStep(
		[path, replaced, previous]() {
			ControlResult ignored;
			if (replaced) { controlWriteFileBytes(path, previous, true, &ignored); }
			else { QFile::remove(path); }
		},
		[path, written]() {
			ControlResult ignored;
			controlWriteFileBytes(path, written, true, &ignored);
		});
}

void registerChainList(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("chain.list");
	cmd.group = QStringLiteral("chain");
	cmd.verb = QStringLiteral("list");
	cmd.description = QStringLiteral("Every chain preset in the store: its name, its file, its "
		"device count and one summary per device (the plugin, the format and the size and hash "
		"of the device's own state document). Pass \"name\" for one preset in full. The store "
		"lives OUTSIDE the project - the user preset tree, chainpresets/ - so the presets are "
		"the same ones whichever project is open, which is the point of a preset. Writes "
		"nothing.");
	cmd.argsSchema = objectSchema({ {QStringLiteral("name"), stringProperty()} });
	cmd.resultSchema = objectSchema({
		{QStringLiteral("presets"), arrayProperty()},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("dir"), stringProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		const QString name = args.value(QStringLiteral("name")).toString();
		if (name.isEmpty())
		{
			QJsonArray presets;
			for (const QString& path : controlChainPresetFiles())
			{
				ControlChainPreset preset;
				ControlResult error;
				if (!controlReadChainPreset(path, &preset, &error)) { return error; }
				presets.append(controlChainPresetJson(preset, false));
			}
			QJsonObject result;
			result.insert(QStringLiteral("presets"), presets);
			result.insert(QStringLiteral("count"), presets.size());
			result.insert(QStringLiteral("dir"), controlChainPresetDir());
			return ControlResult::success(result);
		}

		ControlChainPreset preset;
		ControlResult error;
		if (!readPresetByName(name, &preset, &error)) { return error; }
		QJsonObject result;
		result.insert(QStringLiteral("presets"), QJsonArray{presetResult(preset, true)});
		result.insert(QStringLiteral("count"), 1);
		result.insert(QStringLiteral("dir"), controlChainPresetDir());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerChainGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("chain.get_state");
	cmd.group = QStringLiteral("chain");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("One chain preset in full: its name, its file, its device "
		"count and every device in order with the plugin's own identity (the LADSPA file and "
		"label, the LV2 URI, or the built-in name), the format, and the size and SHA-256 of the "
		"device's state document. The state documents themselves are not returned - they are "
		"what chain.apply writes back, and chain.list's hashes are what a caller compares.");
	cmd.argsSchema = objectSchema({ {QStringLiteral("name"), stringProperty()} },
		{QStringLiteral("name")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("bytes"), integerProperty()},
		{QStringLiteral("device_count"), integerProperty()},
		{QStringLiteral("devices"), arrayProperty()},
		{QStringLiteral("dir"), stringProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ControlChainPreset preset;
		ControlResult error;
		if (!readPresetByName(args.value(QStringLiteral("name")).toString(), &preset, &error))
		{
			return error;
		}
		return ControlResult::success(presetResult(preset, true));
	};
	registry.registerCommand(cmd);
}

void registerChainSave(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("chain.save");
	cmd.group = QStringLiteral("chain");
	cmd.verb = QStringLiteral("save");
	cmd.description = QStringLiteral("Capture a target's effect chain as a named preset: every "
		"device in the chain's own order, each with the state document plugin.state_save writes "
		"for it (enabled, wet, autoquit and every parameter). Writes ONE file in the store "
		"(chainpresets/<name>.zcp), outside the project, so the preset can be applied to a "
		"track in another project, and so project.save/project.open cannot lose it. An existing "
		"preset of that name is refused unless \"overwrite\":true. 'target' is a trk-<n> or "
		"ch-<n> id. Reversible through a recorded action checkpoint that puts the file - or the "
		"revision it replaced - back.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("overwrite"), booleanProperty()},
	}, {QStringLiteral("target"), QStringLiteral("name")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("bytes"), integerProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("device_count"), integerProperty()},
		{QStringLiteral("devices"), arrayProperty()},
		{QStringLiteral("replaced"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlTarget target;
		ControlResult error;
		if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error))
		{
			return error;
		}
		if (target.chain->effects().empty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("%1 carries no devices, so there is nothing to capture; load a "
					"device with plugin.load first").arg(target.id));
		}
		const QString name = args.value(QStringLiteral("name")).toString();
		QString path;
		if (!controlChainPresetPath(name, &path, &error)) { return error; }

		QList<ControlChainPresetDevice> devices;
		if (!captureDevices(target, &devices, &error)) { return error; }
		const QByteArray bytes = controlChainPresetDocument(controlChainPresetName(path), devices);

		// The refusal happens BEFORE the previous revision is captured and
		// before anything is written, so a refused save leaves no undo step
		// behind and no half-written preset (the rule every mutating handler in
		// this surface follows).
		const bool existed = QFileInfo::exists(path);
		if (existed && !args.value(QStringLiteral("overwrite")).toBool(false))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("%1 already exists; pass \"overwrite\":true to replace it")
					.arg(path));
		}
		const QJsonObject snapshot = controlFileSnapshot(path);
		QByteArray previous;
		if (existed) { controlReadFileBytes(path, &previous, &error); }

		if (!QDir().mkpath(controlChainPresetDir()))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("cannot create the preset store %1").arg(controlChainPresetDir()));
		}
		if (!controlWriteFileBytes(path, bytes, true, &error)) { return error; }
		recordStoreWrite(path, existed, previous, bytes);

		ControlChainPreset preset;
		preset.name = controlChainPresetName(path);
		preset.path = path;
		preset.devices = devices;

		QJsonObject result = presetResult(preset, true);
		result.insert(QStringLiteral("sha256"), controlSha256OfBytes(bytes));
		result.insert(QStringLiteral("replaced"), existed);
		result.insert(QStringLiteral("target"), target.id);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(snapshot,
				existed ? QStringLiteral("chain.save") : QStringLiteral("chain.remove"),
				existed ? QJsonObject{{QStringLiteral("name"), preset.name},
						{QStringLiteral("note"), QStringLiteral("re-issue the same capture to "
							"rebuild this revision")}}
					: QJsonObject{{QStringLiteral("name"), preset.name}},
				true,
				existed
					? QStringLiteral("action checkpoint: the recorded step writes the revision "
						"this command replaced (before.previous_sha256) back to the preset's own "
						"path, so one control.undo restores the store exactly")
					: QStringLiteral("action checkpoint: the recorded step removes the preset "
						"file this command created, so one control.undo leaves the store as it "
						"was")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerChainReadCommands(ControlRegistry& registry)
{
	registerChainList(registry);
	registerChainGetState(registry);
	registerChainSave(registry);
}

} // namespace lmms
