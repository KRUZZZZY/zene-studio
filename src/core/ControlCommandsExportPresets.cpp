/*
 * ControlCommandsExportPresets.cpp - the SAVED-STORE half of the export.* command
 *                                     group (SPEC A11-A16): export.preset_list,
 *                                     export.preset_add, export.preset_apply and
 *                                     export.preset_remove.
 *
 * WHAT IT IS. Feature row 70 of the 0.3.0 list ("Render / export presets"): a
 * named bundle of the three OutputSettings fields a render can be launched with,
 * so the 24/96 master or the 16-bit distribution render is a NAME an agent
 * applies instead of flags it restates. Nothing like it existed: a grep for
 * "export preset" / "RenderPreset" / "batch export" returned nothing.
 *
 * THE CONTRACT: the engine half is ControlExportPresetSupport.cpp (the document,
 * the name rule, the store, the settings the next render asks for); these are the
 * commands; the rows are in ControlReversibilityTableExportPresets.cpp; the proof
 * is the registered ctest ControlRenderPresets (tests/control-render-presets.py),
 * which drives the real binary over the socket and measures an applied preset in
 * the rendered WAV's own header; the UI-absence lines are in
 * docs/RELEASE-NOTES-v0.3.0-alpha.md and docs/KNOWN-LIMITATIONS.md.
 *
 * THE SHAPE IS THE CHAIN-PRESET GROUP'S (ControlCommandsChain.cpp /
 * ControlCommandsChainEdit.cpp): a file store outside the project, the file name
 * as the key, an existing entry refused unless the caller says overwrite, and
 * every write reversed by a recorded ACTION checkpoint. What differs is the
 * DOCUMENT: three scalars, so JSON, and the reader REFUSES a field it cannot
 * honour rather than parsing a document it only half understands.
 *
 * AN APPLY IS NOT A RENDER. export.preset_apply moves the SELECTION; it starts no
 * render and writes no audio. The file arrives only from render.render, which
 * reads the selection through ControlExportPresetSettings::effective() and passes
 * it to the child process it already started - no second renderer anywhere.
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

#include "ControlDeviceSupport.h" // the file helpers every store command uses
#include "ControlEdit.h"
#include "ControlExportPresetSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The store's own name and the settings it carries, or the typed refusal. One
//! helper, so the three commands that read a preset out of the store name a
//! missing preset the same way.
bool storedPreset(const QString& name, ControlExportPreset* preset, ControlResult* error)
{
	QString path;
	if (!controlExportPresetPath(name, &path, error)) { return false; }
	if (!QFileInfo::exists(path))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no render preset '%1' in the store (%2 holds %3)")
				.arg(name, controlExportPresetDir())
				.arg(controlExportPresetFiles().size()));
		return false;
	}
	QByteArray bytes;
	if (!controlReadFileBytes(path, &bytes, error)) { return false; }
	return controlExportPresetFromBytes(bytes, path, preset, error);
}

//! The settings the render path is on right now, as the wire reports them, plus
//! the name of the preset they came from (empty when they are the CLI's own
//! defaults). Reported by every verb here, because "which settings is the NEXT
//! render going to use" is the question an apply is asked to answer.
QJsonObject activeSettingsJson()
{
	ControlExportPreset applied;
	const bool active = ControlExportPresetSettings::active(&applied);
	QJsonObject out = controlExportPresetJson(ControlExportPresetSettings::effective(), false);
	out.insert(QStringLiteral("applied_preset"), active ? QJsonValue(applied.name) : QJsonValue());
	out.insert(QStringLiteral("active"), active);
	return out;
}

/*! The recorded inverse of a store write: a document this command created is
 *  REMOVED, one it replaced gets its previous bytes back; the redo half re-writes
 *  what this command wrote (the chain-preset store's rule).
 */
void recordPresetWrite(const QString& path, bool replaced, const QByteArray& previous,
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

/*! Writes the document into the store and records the checkpoint that puts the
 *  file back the way it was (the revision it replaced, or no file at all).
 *
 *  An existing preset is refused BEFORE the previous revision is captured, so a
 *  refused add leaves no undo step and no half-written document behind. \a
 *  existed and \a snapshot come back for the transaction record and are never
 *  guessed afterwards - the file has changed by then.
 */
bool writePresetDocument(const QString& path, const QByteArray& bytes, bool overwrite,
	bool* existed, QJsonObject* snapshot, ControlResult* error)
{
	*existed = QFileInfo::exists(path);
	if (*existed && !overwrite)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 already exists; pass \"overwrite\":true to replace it").arg(path));
		return false;
	}
	QByteArray previous;
	if (*existed) { controlReadFileBytes(path, &previous, error); }
	*snapshot = controlFileSnapshot(path);

	if (!QDir().mkpath(controlExportPresetDir()))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("cannot create the preset store %1").arg(controlExportPresetDir()));
		return false;
	}
	if (!controlWriteFileBytes(path, bytes, true, error)) { return false; }
	recordPresetWrite(path, *existed, previous, bytes);
	return true;
}

void registerExportPresetList(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("export.preset_list");
	cmd.group = QStringLiteral("export");
	cmd.verb = QStringLiteral("preset_list");
	cmd.description = QStringLiteral("Every saved render/export preset in the store and the "
		"settings the NEXT render will be started with. A preset is a name plus the three "
		"OutputSettings fields a render can be told - sample_rate, bit_depth, stereo_mode - and "
		"passing \"name\" returns one of them in full. The store lives OUTSIDE the project "
		"(the user preset tree's renderpresets/, one JSON document per preset), so the presets "
		"are the same ones whichever project is open, which is what makes them presets. Reports "
		"\"active\" (the applied preset, null when none is) and the settings in force. Writes "
		"nothing.");
	cmd.argsSchema = objectSchema({ {QStringLiteral("name"), stringProperty()} });
	cmd.resultSchema = objectSchema({
		{QStringLiteral("presets"), arrayProperty()},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("dir"), stringProperty()},
		{QStringLiteral("applied_preset"), stringProperty()},
		{QStringLiteral("active"), booleanProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		const QString name = args.value(QStringLiteral("name")).toString();
		QJsonArray presets;
		if (name.isEmpty())
		{
			for (const QString& path : controlExportPresetFiles())
			{
				ControlExportPreset preset;
				ControlResult error;
				QByteArray bytes;
				if (!controlReadFileBytes(path, &bytes, &error)) { return error; }
				if (!controlExportPresetFromBytes(bytes, path, &preset, &error)) { return error; }
				presets.append(controlExportPresetJson(preset, false));
			}
		}
		else
		{
			ControlExportPreset preset;
			ControlResult error;
			if (!storedPreset(name, &preset, &error)) { return error; }
			presets.append(controlExportPresetJson(preset, true));
		}

		QJsonObject settings = activeSettingsJson();
		QJsonObject result;
		result.insert(QStringLiteral("presets"), presets);
		result.insert(QStringLiteral("count"), presets.size());
		result.insert(QStringLiteral("dir"), controlExportPresetDir());
		result.insert(QStringLiteral("applied_preset"), settings.value(QStringLiteral("applied_preset")));
		result.insert(QStringLiteral("active"), settings.value(QStringLiteral("active")));
		result.insert(QStringLiteral("settings"), settings);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerExportPresetAdd(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("export.preset_add");
	cmd.group = QStringLiteral("export");
	cmd.verb = QStringLiteral("preset_add");
	cmd.description = QStringLiteral("Save a named render/export preset: the sample rate, the bit "
		"depth and the stereo mode a render is launched with. Writes ONE document in the store "
		"(renderpresets/<name>.zrp), outside the project, so the preset can be applied to another "
		"project and project.save/project.open cannot lose it. sample_rate must be inside the "
		"window the render path accepts (44100 to 192000 Hz - the shipped CLI's own check), "
		"bit_depth is one of 16, 24 or 32 and stereo_mode one of mono, stereo or jointstereo; a "
		"value nothing could honour is refused here rather than discovered by a failed render. An "
		"existing preset of that name is refused unless \"overwrite\":true. Reversible through a "
		"recorded action checkpoint that removes the document - or writes back the revision it "
		"replaced.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty(44100, 192000)},
		{QStringLiteral("bit_depth"), enumProperty({QStringLiteral("16"), QStringLiteral("24"),
			QStringLiteral("32")})},
		{QStringLiteral("stereo_mode"), enumProperty({QStringLiteral("mono"),
			QStringLiteral("stereo"), QStringLiteral("jointstereo")})},
		{QStringLiteral("overwrite"), booleanProperty()},
	}, {QStringLiteral("name"), QStringLiteral("sample_rate"), QStringLiteral("bit_depth"),
		QStringLiteral("stereo_mode")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("bytes"), integerProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("bit_depth"), stringProperty()},
		{QStringLiteral("stereo_mode"), stringProperty()},
		{QStringLiteral("replaced"), booleanProperty()},
		{QStringLiteral("dir"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlExportPreset preset;
		preset.sampleRate = static_cast<sample_rate_t>(
			args.value(QStringLiteral("sample_rate")).toInt());
		const QString depthName = args.value(QStringLiteral("bit_depth")).toString();
		if (!controlExportPresetBitDepthFromName(depthName, &preset.bitDepth))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("unknown bit_depth '%1'; expected 16, 24 or 32").arg(depthName));
		}
		const QString modeName = args.value(QStringLiteral("stereo_mode")).toString();
		if (!controlExportPresetStereoModeFromName(modeName, &preset.stereoMode))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("unknown stereo_mode '%1'; expected mono, stereo or jointstereo")
					.arg(modeName));
		}

		const QString invalid = controlExportPresetValidate(preset);
		if (!invalid.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, invalid);
		}

		const QString name = args.value(QStringLiteral("name")).toString();
		QString path;
		ControlResult error;
		if (!controlExportPresetPath(name, &path, &error)) { return error; }
		preset.name = controlExportPresetName(path);
		preset.path = path;

		const QByteArray bytes = controlExportPresetDocument(preset);
		bool existed = false;
		QJsonObject snapshot;
		if (!writePresetDocument(path, bytes, args.value(QStringLiteral("overwrite")).toBool(false),
				&existed, &snapshot, &error))
		{
			return error;
		}

		QJsonObject result = controlExportPresetJson(preset, true);
		result.insert(QStringLiteral("sha256"), controlSha256OfBytes(bytes));
		result.insert(QStringLiteral("replaced"), existed);
		result.insert(QStringLiteral("dir"), controlExportPresetDir());
		// The store is a file tree outside the project and no Song checkpoint
		// carries it, so the inverse is the recorded action step itself: it
		// removes the document this call created, or writes the revision it
		// replaced back to the same path.
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(snapshot, QStringLiteral("export.preset_remove"),
				QJsonObject{{QStringLiteral("name"), preset.name}},
				true,
				QStringLiteral("action checkpoint: the recorded step removes the document this "
					"command created, or writes the revision it replaced "
					"(before.previous_sha256) back to the preset's own path")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerExportPresetApply(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("export.preset_apply");
	cmd.group = QStringLiteral("export");
	cmd.verb = QStringLiteral("preset_apply");
	cmd.description = QStringLiteral("Put the render path on a saved preset: the next render "
		"(render.render) is started with that preset's sample rate, bit depth and stereo mode. "
		"Passing no \"name\" - or an empty one - returns the render path to its OWN defaults "
		"(44100 Hz, 16-bit, joint stereo), which is what a render with no apply uses. The "
		"selection is process-wide and NOT project state: it is not saved with the project and "
		"project.open does not change it, exactly like export.set_dither beside it. Reversible: "
		"one control.undo restores the selection this call replaced. A render already performed "
		"is not undone by that: its file stays where it was written, so the fallback for a render "
		"made under the wrong preset is to apply the right one and render again.");
	cmd.argsSchema = objectSchema({ {QStringLiteral("name"), stringProperty()} });
	cmd.resultSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("applied_preset"), stringProperty()},
		{QStringLiteral("active"), booleanProperty()},
		{QStringLiteral("previous_preset"), stringProperty()},
		{QStringLiteral("previous"), objectProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("bit_depth"), stringProperty()},
		{QStringLiteral("stereo_mode"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const QString requested = args.value(QStringLiteral("name")).toString();
		const QString previous = ControlExportPresetSettings::activeName();
		const ControlExportPreset before = ControlExportPresetSettings::effective();

		ControlExportPreset preset;
		if (requested.isEmpty())
		{
			// No name: back to the render path's own defaults.
			ControlExportPresetSettings::clear();
		}
		else
		{
			ControlResult error;
			if (!storedPreset(requested, &preset, &error)) { return error; }
			ControlExportPresetSettings::setActive(preset);
		}

		// The inverse is a recorded undo STEP, the mechanism export.set_dither
		// uses beside this one for the same reason: the selection is a
		// process-wide scalar the project's journal does not carry. It carries
		// the previous settings BY VALUE, so a preset removed between the apply
		// and the undo still comes back on control.undo.
		ControlExportPreset previousSettings = before;
		previousSettings.name = previous;
		addUndoStep(
			[previousSettings, previous]() {
				if (previous.isEmpty()) { ControlExportPresetSettings::clear(); }
				else { ControlExportPresetSettings::setActive(previousSettings); }
			},
			[preset, requested]() {
				if (requested.isEmpty()) { ControlExportPresetSettings::clear(); }
				else { ControlExportPresetSettings::setActive(preset); }
			});

		QJsonObject result = activeSettingsJson();
		result.insert(QStringLiteral("name"), requested);
		result.insert(QStringLiteral("path"), requested.isEmpty() ? QString() : preset.path);
		result.insert(QStringLiteral("previous_preset"), previous);
		result.insert(QStringLiteral("previous"), controlExportPresetJson(before, false));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(QJsonObject{{QStringLiteral("preset"), previous},
					{QStringLiteral("settings"), controlExportPresetJson(before, false)}},
				QStringLiteral("export.preset_apply"),
				QJsonObject{{QStringLiteral("name"), previous},
					{QStringLiteral("note"), QStringLiteral("an empty name is the render path's "
						"own defaults, so the inverse of the FIRST apply is the revert call")}},
				true,
				QStringLiteral("action checkpoint: the recorded step restores the selection this "
					"command replaced - the preset that was applied before it, or the defaults "
					"when none was")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerExportPresetRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("export.preset_remove");
	cmd.group = QStringLiteral("export");
	cmd.verb = QStringLiteral("preset_remove");
	cmd.description = QStringLiteral("Delete one render/export preset from the store. The "
		"document's bytes are captured before the removal, so one control.undo writes the preset "
		"back byte for byte. Removing a preset that is currently APPLIED leaves the render path "
		"on the settings it already has - the applied selection is a copy, and what goes is the "
		"store entry, not the render settings (use export.preset_apply with no name to go back "
		"to the defaults). A name the store does not hold is a typed not_found and changes "
		"nothing.");
	cmd.argsSchema = objectSchema({ {QStringLiteral("name"), stringProperty()} },
		{QStringLiteral("name")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("bytes"), integerProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("dir"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const QString name = args.value(QStringLiteral("name")).toString();
		QString path;
		ControlResult error;
		if (!controlExportPresetPath(name, &path, &error)) { return error; }
		if (!QFileInfo::exists(path))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("no render preset '%1' in the store (%2 holds %3)")
					.arg(name, controlExportPresetDir())
					.arg(controlExportPresetFiles().size()));
		}

		QByteArray bytes;
		if (!controlReadFileBytes(path, &bytes, &error)) { return error; }
		const QJsonObject snapshot = controlFileSnapshot(path);
		const int bytesRemoved = bytes.size();
		const QString sha = controlSha256OfBytes(bytes);

		// Recorded BEFORE the removal: a failure to delete then leaves the undo
		// step pointing at a document that is still there, which is recoverable,
		// while a delete without a captured inverse is not.
		addUndoStep(
			[path, bytes]() {
				ControlResult ignored;
				controlWriteFileBytes(path, bytes, true, &ignored);
			},
			[path]() { QFile::remove(path); });
		if (!QFile::remove(path))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("%1 could not be removed").arg(path));
		}

		ControlExportPreset preset;
		if (!controlExportPresetFromBytes(bytes, path, &preset, &error)) { return error; }

		QJsonObject result = controlExportPresetJson(preset, false);
		result.insert(QStringLiteral("bytes"), bytesRemoved);
		result.insert(QStringLiteral("sha256"), sha);
		result.insert(QStringLiteral("count"), controlExportPresetFiles().size());
		result.insert(QStringLiteral("dir"), controlExportPresetDir());
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(snapshot, QStringLiteral("export.preset_add"),
				QJsonObject{{QStringLiteral("name"), preset.name},
					{QStringLiteral("sample_rate"), static_cast<int>(preset.sampleRate)},
					{QStringLiteral("bit_depth"),
						QString::fromLatin1(controlExportPresetBitDepthName(preset.bitDepth))},
					{QStringLiteral("stereo_mode"),
						QString::fromLatin1(controlExportPresetStereoModeName(preset.stereoMode))},
					{QStringLiteral("note"), QStringLiteral("re-issuing export.preset_add with "
						"these values rebuilds this document; the recorded step is the exact "
						"bytes")}},
				true,
				QStringLiteral("action checkpoint: the document's bytes are captured before the "
					"removal and the recorded step writes them back to the same path, byte for "
					"byte")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerExportPresetCommands(ControlRegistry& registry)
{
	registerExportPresetList(registry);
	registerExportPresetAdd(registry);
	registerExportPresetApply(registry);
	registerExportPresetRemove(registry);
}

} // namespace lmms
