/*
 * ControlCommandsTrackPresets.cpp - track.preset_save / preset_load and plugin.replace
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
/*! 0.5.0 L11:
 *  - WF-024 track presets: track.preset_save writes any track's preset - its device, chain and
 *    settings, the document the instrument window's Save preset writes, without the mute and solo -
 *    and track.preset_load puts one on a track of the same type (one undo step, a Track checkpoint);
 *  - WF-016 hot-swap: plugin.replace puts another device in a device's place, at the same slot of the
 *    same chain, as one undo step. */

#include <algorithm>

#include <QFileInfo>

#include "ControlDevicePlacement.h"
#include "ControlDeviceSupport.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "DataFile.h"
#include "Engine.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

ControlResult presetSave(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	const QString path = args.value(QStringLiteral("path")).toString();
	if (!QFileInfo(path).isAbsolute()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'path' must be an absolute file path")); }
	DataFile file(DataFile::Type::InstrumentTrackSettings);
	QDomElement& content = file.content();
	track->savePreset(file, content);
	for (const char* attribute : {"muted", "solo", "mutedBeforeSolo"}) { content.setAttribute(QLatin1String(attribute), 0); }
	if (!file.writeFile(path)) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("'%1' could not be written").arg(path)); }
	return ControlResult::success({{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("path"), path}});
}

ControlResult presetLoad(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	const QString path = args.value(QStringLiteral("path")).toString();
	if (!QFileInfo(path).isFile()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no preset file '%1'").arg(path)); }
	DataFile file(path);
	const QDomElement content = file.content();
	// The track type this check reads has to HAVE parsed: toInt() answers 0 for
	// a missing or nonnumeric attribute, and 0 is the instrument track's own
	// type - so a malformed preset used to pass the check for an instrument
	// track and be loaded. A file whose content element did not resolve at all
	// is refused here too, before any undo step is added.
	bool typeParsed = false;
	const int presetType = content.attribute(QStringLiteral("type")).toInt(&typeParsed);
	if (content.isNull() || !typeParsed || presetType != static_cast<int>(track->type()))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the preset is for another kind of track"));
	}
	addUndoStep(QVector<JournallingObject*>{track});
	const QString name = track->name();
	track->loadPreset(content);
	track->setName(name);  // a preset brings the sound, the track keeps its name
	QJsonObject result{{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("path"), path}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("track"), trackIdOf(track)}},
		QStringLiteral("control.undo"), {}, true,
		QStringLiteral("ProjectJournal (Track checkpoint: control.undo restores the track's previous state)")));
	return ControlResult::success(result);
}

ControlResult pluginReplace(const QJsonObject& args)
{
	ControlTarget target;
	ControlResult error;
	if (!resolveChainTarget(args, &target, &error)) { return error; }
	Effect* old = resolveControlEffect(target, args.value(QStringLiteral("plugin")).toString(), &error);
	if (old == nullptr) { return error; }
	ControlDeviceEntry entry;
	int deviceIndex = -1;
	if (!controlDeviceById(args.value(QStringLiteral("device")).toString(), &entry, &deviceIndex, &error)) { return error; }
	if (entry.kind != QLatin1String("effect")) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a device's place takes an effect")); }
	EffectChain* chain = old->effectChain();  // a rack chain's device too (MX-102)
	const std::vector<Effect*>& effects = chain->effects();
	const int slot = static_cast<int>(std::find(effects.begin(), effects.end(), old) - effects.begin());
	Effect* fresh = controlInstantiateDevice(entry, chain, &error);
	if (fresh == nullptr) { return error; }
	placeEffect(chain, fresh, slot);
	// The new device's step first, so the undo takes the old one back before it takes the new one out. Neither keeps
	// the chain or a device: a track.remove and its undo rebuild them, so both find them again by id and slot.
	journalEffectLoad(chain, fresh, entry, slot);
	journalEffectRemoval(chain, old, slot + 1);
	chain->removeEffect(old);
	old->deleteLater();
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("target"), target.id}, {QStringLiteral("plugin"), effectIdOf(fresh)}, {QStringLiteral("index"), slot},
		{QStringLiteral("device"), deviceId(deviceIndex)}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("target"), target.id}}, QStringLiteral("control.undo"), {},
		true, QStringLiteral("action checkpoints: the recorded undo steps recreate the replaced device at its slot, with its "
			"settings, and take the new one out")));
	return ControlResult::success(result);
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), true, handler);
}

} // namespace

void registerTrackPresetCommands(ControlRegistry& registry)
{
	const QJsonObject trackPath{{QStringLiteral("track"), stringProperty()}, {QStringLiteral("path"), stringProperty()}};
	// Not mutating: it writes a file and changes nothing in the session (its contract row is not_mutating).
	control::defineCommand(registry, QStringLiteral("track.preset_save"), QStringLiteral("Save a track as a preset ('path', an absolute "
		"file - .xpf by custom): its device, effects and settings, without its mute and solo. Writes the file only."), trackPath,
		{QStringLiteral("track"), QStringLiteral("path")}, false, presetSave);
	add(registry, "track.preset_load", QStringLiteral("Put a track preset ('path') on a track of the same kind: its device, effects "
		"and settings; the track keeps its name. One undo step."), trackPath, {QStringLiteral("track"), QStringLiteral("path")}, presetLoad);
	add(registry, "plugin.replace", QStringLiteral("Put another device ('device', plugin.list's dev-<n>) in the place of a device "
		"('plugin', fx-<n> on 'target' - 'chain' for a rack chain): the same slot of the same chain. One undo step brings the "
		"old one back with its settings."), {{QStringLiteral("target"), stringProperty()}, {QStringLiteral("chain"), integerProperty(0, 64)},
		{QStringLiteral("plugin"), stringProperty()}, {QStringLiteral("device"), stringProperty()}},
		{QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("device")}, pluginReplace);
}

} // namespace lmms
