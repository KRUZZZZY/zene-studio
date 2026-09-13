/*
 * ControlCommandsChainEdit.cpp - the EDIT half of the chain.* command group
 *                                (SPEC A11-A16): chain.apply, chain.rename and
 *                                chain.remove.
 *
 * Kept apart from the read/capture half (ControlCommandsChain.cpp) for the same
 * reason the automation and warp groups are split: the two halves answer
 * different questions, and the file-length ratchet measures a file as a unit.
 *
 * THE ONE INVERSE THAT NEEDS AN ARGUMENT. chain.apply REPLACES the target's
 * chain, which is project state with no live object of its own a journal
 * checkpoint restores (EffectChain is a Model + SerializingObject; the
 * <fxchain> element lives inside the track or mixer channel that owns it, and
 * the device-level state is not journaled either). The inverse recorded is
 * therefore the chain's own XML, captured before the write through
 * EffectChain::saveSettings - the same call the project file's <fxchain> is
 * written by - and the recorded step writes it back through
 * EffectChain::loadSettings, the project loader's own path. That is a real
 * inverse, applied by control.undo itself, not a described fallback; the
 * refusal when the captured XML would exceed the bounded snapshot is what keeps
 * it honest (a truncated chain is a corrupt chain).
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
#include "EffectChain.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The stored preset named \a name, or the typed not_found naming the store.
bool storedPreset(const QString& name, ControlChainPreset* preset, ControlResult* error)
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

//! The refusal a rename onto an occupied name gets: the name IS the key, so
//! renaming onto another preset's name would destroy it, and an edit that
//! silently deletes a preset nobody named is not an edit.
ControlResult occupiedRefusal(const QString& to)
{
	return ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("the store already holds a chain preset called '%1'; remove it or rename "
			"it first").arg(to));
}

//! chain.apply: the whole handler, one device list applied in one step.
ControlResult applyPreset(const ControlTarget& target, const ControlChainPreset& preset)
{
	EffectChain* chain = target.chain;
	const QString beforeXml = controlChainXml(chain);
	if (beforeXml.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1's chain is larger than the bounded snapshot (%2 characters), so "
				"no inverse could be recorded; a chain that cannot be captured cannot be "
				"replaced reversibly").arg(target.id).arg(ControlSnapshotLimit));
	}
	const int before = static_cast<int>(chain->effects().size());

	// The step is recorded BEFORE the write, and it is the reason a failure
	// part-way through an apply is recoverable: the previous chain's own XML is
	// already on the undo stack.
	addUndoStep(
		[chain, beforeXml]() {
			controlRestoreChainXml(chain, beforeXml);
		},
		[chain, preset]() {
			controlApplyChainPreset(chain, preset);
		});

	ControlResult applied = controlApplyChainPreset(chain, preset);
	if (!applied.ok) { return applied; }

	QJsonObject result = applied.result;
	result.insert(QStringLiteral("target"), target.id);
	result.insert(QStringLiteral("name"), preset.name);
	result.insert(QStringLiteral("path"), preset.path);
	result.insert(QStringLiteral("replaced"), before);

	QJsonObject beforeState;
	beforeState.insert(QStringLiteral("target"), target.id);
	beforeState.insert(QStringLiteral("device_count"), before);
	beforeState.insert(QStringLiteral("chain_xml"), beforeXml);
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(beforeState, QStringLiteral("chain.apply"),
			QJsonObject{{QStringLiteral("name"), preset.name},
				{QStringLiteral("target"), target.id},
				{QStringLiteral("note"), QStringLiteral("the INVERSE of this step is the "
					"recorded checkpoint: it writes before.chain_xml back into the target's chain. "
					"Re-issuing chain.apply with this name is the REDO, not the inverse")}},
			true,
			QStringLiteral("action checkpoint: the recorded step restores the chain's own <fxchain> "
				"XML (EffectChain::saveSettings, captured before the write) through "
				"EffectChain::loadSettings - the project loader's own path - because the device "
				"list is not a live object a checkpoint restores")));
	return ControlResult::success(result);
}

void registerChainApply(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("chain.apply");
	cmd.group = QStringLiteral("chain");
	cmd.verb = QStringLiteral("apply");
	cmd.description = QStringLiteral("Apply a stored chain preset to a target: the target's "
		"chain becomes the preset's devices, IN THE PRESET'S ORDER, each one restored from its "
		"own state document, so the parameters are the values the chain was captured with. The "
		"target's existing devices are removed (the plugin.unload lifetime). A preset naming a "
		"device this build cannot load is refused and the target's chain is left untouched. "
		"Reversible: one control.undo puts the previous chain back, devices and settings.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("target"), stringProperty()},
	}, {QStringLiteral("name"), QStringLiteral("target")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("device_count"), integerProperty()},
		{QStringLiteral("devices"), arrayProperty()},
		{QStringLiteral("replaced"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlChainPreset preset;
		ControlResult error;
		if (!storedPreset(args.value(QStringLiteral("name")).toString(), &preset, &error))
		{
			return error;
		}
		ControlTarget target;
		if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error))
		{
			return error;
		}
		return applyPreset(target, preset);
	};
	registry.registerCommand(cmd);
}

void registerChainRename(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("chain.rename");
	cmd.group = QStringLiteral("chain");
	cmd.verb = QStringLiteral("rename");
	cmd.description = QStringLiteral("Rename a stored chain preset. The name is the key, so a "
		"rename onto a name the store already holds is refused, typed, and changes nothing. "
		"Reversible through a recorded action checkpoint that renames the file back.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("to"), stringProperty()},
	}, {QStringLiteral("name"), QStringLiteral("to")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("renamed_to"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("device_count"), integerProperty()},
		{QStringLiteral("dir"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlChainPreset preset;
		ControlResult error;
		if (!storedPreset(args.value(QStringLiteral("name")).toString(), &preset, &error))
		{
			return error;
		}
		QString toPath;
		if (!controlChainPresetPath(args.value(QStringLiteral("to")).toString(), &toPath, &error))
		{
			return error;
		}
		const QString to = controlChainPresetName(toPath);
		if (toPath != preset.path && QFileInfo::exists(toPath)) { return occupiedRefusal(to); }

		const QString from = preset.path;
		if (toPath != from && !QFile::rename(from, toPath))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("could not rename %1 to %2; the store is unchanged")
					.arg(from, toPath));
		}
		// The recorded inverse is the rename itself, backwards; the redo half is
		// the rename forwards, so a redo restores the new name rather than
		// leaving the preset under two names or none.
		addUndoStep(
			[from, toPath]() {
				if (QFileInfo::exists(toPath)) { QFile::rename(toPath, from); }
			},
			[from, toPath]() {
				if (QFileInfo::exists(from)) { QFile::rename(from, toPath); }
			});

		ControlChainPreset renamed;
		if (!controlReadChainPreset(toPath, &renamed, &error)) { return error; }

		QJsonObject result;
		result.insert(QStringLiteral("name"), preset.name);
		result.insert(QStringLiteral("renamed_to"), to);
		result.insert(QStringLiteral("path"), toPath);
		result.insert(QStringLiteral("device_count"), renamed.devices.size());
		result.insert(QStringLiteral("dir"), controlChainPresetDir());
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(QJsonObject{{QStringLiteral("name"), preset.name},
					{QStringLiteral("path"), from},
					{QStringLiteral("device_count"), preset.devices.size()}},
				QStringLiteral("chain.rename"),
				QJsonObject{{QStringLiteral("name"), to}, {QStringLiteral("to"), preset.name}},
				true,
				QStringLiteral("action checkpoint: the recorded step renames the preset's file "
					"back to before.path, the same operation chain.rename performs")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerChainRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("chain.remove");
	cmd.group = QStringLiteral("chain");
	cmd.verb = QStringLiteral("remove");
	cmd.description = QStringLiteral("Delete a chain preset from the store. The preset's own "
		"bytes are captured first, so the removal is reversible: one control.undo writes the "
		"same file, byte for byte, back to its own path.");
	cmd.argsSchema = objectSchema({ {QStringLiteral("name"), stringProperty()} },
		{QStringLiteral("name")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("removed"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("device_count"), integerProperty()},
		{QStringLiteral("remaining"), integerProperty()},
		{QStringLiteral("dir"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlChainPreset preset;
		ControlResult error;
		if (!storedPreset(args.value(QStringLiteral("name")).toString(), &preset, &error))
		{
			return error;
		}
		// Read the bytes BEFORE the removal: they are the inverse, and a file
		// that cannot be read must not be removed (nothing to restore it with).
		QByteArray bytes;
		if (!controlReadFileBytes(preset.path, &bytes, &error)) { return error; }
		if (!QFile::remove(preset.path))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("could not remove %1; the store is unchanged").arg(preset.path));
		}
		addUndoStep(
			[path = preset.path, bytes]() {
				ControlResult ignored;
				controlWriteFileBytes(path, bytes, true, &ignored);
			},
			[path = preset.path]() {
				QFile::remove(path);
			});

		QJsonObject result;
		result.insert(QStringLiteral("removed"), preset.name);
		result.insert(QStringLiteral("path"), preset.path);
		result.insert(QStringLiteral("device_count"), preset.devices.size());
		result.insert(QStringLiteral("remaining"), controlChainPresetFiles().size());
		result.insert(QStringLiteral("dir"), controlChainPresetDir());
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(QJsonObject{{QStringLiteral("name"), preset.name},
					{QStringLiteral("path"), preset.path},
					{QStringLiteral("bytes"), bytes.size()},
					{QStringLiteral("sha256"), controlSha256OfBytes(bytes)}},
				QStringLiteral("chain.remove"),
				QJsonObject{{QStringLiteral("name"), preset.name},
					{QStringLiteral("note"), QStringLiteral("the INVERSE is the recorded "
						"checkpoint, which writes the removed preset's bytes back to its own "
						"path; no registered command writes a preset from bytes")}},
				true,
				QStringLiteral("action checkpoint: the recorded step writes the captured bytes "
					"back to before.path, so one control.undo restores the removed preset byte "
					"for byte")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerChainEditCommands(ControlRegistry& registry)
{
	registerChainApply(registry);
	registerChainRename(registry);
	registerChainRemove(registry);
}

} // namespace lmms
