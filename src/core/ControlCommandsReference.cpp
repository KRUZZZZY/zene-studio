/*
 * ControlCommandsReference.cpp - reference.*: another project open read-only beside this one (WF-042)
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
/*! 0.5.0 F3 (WF-042): the reference project (ReferenceProject.h) over the socket - open, browse, close -
 *  and its three copies into the session: whole tracks (project.import_tracks' path), clips onto a track
 *  of this project (the loader's createClip + restoreState, the clipboard's path), and devices onto a
 *  track's or channel's chain (its <fxchain> document, chain.apply's path). Each copy is one undo step. */

#include <QJsonArray>

#include "Clip.h"
#include "ControlArrangingCommands.h"
#include "ControlChainPresetSupport.h"
#include "ControlCommandDef.h"
#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlProjectTransplant.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "EffectChain.h"
#include "Engine.h"
#include "ReferenceProject.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

ReferenceProject& reference()
{
	return ReferenceProject::instance();
}

ControlResult notOpen()
{
	return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no reference project is open (reference.open opens one)"));
}

//! The element at @a args' integer @a key in @a list, or a null one (with @a error).
QDomElement pick(const QList<QDomElement>& list, const QJsonObject& args, const char* key, const char* what, ControlResult* error)
{
	const int index = args.value(QLatin1String(key)).toInt(-1);
	if (index >= 0 && index < list.size()) { return list[index]; }
	*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the reference project has no %1 %2").arg(QLatin1String(what)).arg(index));
	return {};
}

//! The elements of @a list that @a args' array @a key names (indices; every one when it is left out).
bool chosen(const QList<QDomElement>& list, const QJsonObject& args, const char* key, QList<QDomElement>* out, ControlResult* error)
{
	if (!args.contains(QLatin1String(key))) { *out = list; }
	for (const QJsonValue& value : args.value(QLatin1String(key)).toArray())
	{
		const int index = value.toInt(-1);
		if (index < 0 || index >= list.size())
		{
			*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the reference has no %1 %2").arg(QLatin1String(key)).arg(index));
			return false;
		}
		out->append(list[index]);
	}
	if (out->isEmpty()) { *error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' names nothing to copy").arg(QLatin1String(key))); }
	return !out->isEmpty();
}

//! A copy of @a element, detached from its project's ids and with its media pointed at their files.
QDomElement transplanted(QDomDocument& doc, const QDomElement& element)
{
	QDomElement copy = doc.importNode(element, true).toElement();
	transplant::detach(copy);
	transplant::rebaseMedia(copy, reference().sourceDir());
	return copy;
}

ControlResult openReference(const QJsonObject& args)
{
	QString why;
	if (!reference().open(args.value(QStringLiteral("path")).toString(), &why)) { return ControlResult::failure(ControlErrorKind::NotFound, why); }
	return ControlResult::success(reference().listing());
}

ControlResult closeReference(const QJsonObject&)
{
	reference().close();
	return ControlResult::success(reference().listing());
}

ControlResult referenceState(const QJsonObject&)
{
	return ControlResult::success(reference().listing());
}

ControlResult copyTracks(const QJsonObject& args)
{
	if (!reference().isOpen()) { return notOpen(); }
	ControlResult copied = transplant::importTrackElements(reference().tracks(), args, reference().sourceDir(), QStringLiteral("copied"));
	if (!copied.ok) { return copied; }
	copied.result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("reference"), reference().path()}},
		QStringLiteral("control.undo"), {}, true,
		QStringLiteral("action checkpoint: the recorded undo step removes the copied tracks; its redo copies them again")));
	return copied;
}

//! The track type a copied clip needs: only clips whose content travels - notes and audio. An automation
//! clip's targets and a pattern clip's row are objects of the other project.
bool clipFits(const QString& tag, Track* target, ControlResult* error)
{
	const bool fits = (tag == QLatin1String("midiclip") && target->type() == Track::Type::Instrument)
		|| (tag == QLatin1String("sampleclip") && target->type() == Track::Type::Sample);
	if (!fits)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a %1 cannot go onto %2 (a %3 track): note clips go onto "
			"instrument tracks, audio clips onto sample tracks; automation and pattern clips point at the other project's objects")
			.arg(tag, trackIdOf(target), trackTypeNameOf(target->type())));
	}
	return fits;
}

//! The clips made on @a target from @a clips, their first one at @a position (-1: where they were).
QJsonArray placeClips(Track* target, const QList<QDomElement>& clips, int position)
{
	QJsonArray made;
	const int shift = position < 0 ? 0 : position - clips.first().attribute(QStringLiteral("pos")).toInt();
	for (const QDomElement& clip : clips)
	{
		QDomDocument doc;
		QDomElement copy = transplanted(doc, clip);
		copy.setAttribute(QStringLiteral("pos"), std::max(0, copy.attribute(QStringLiteral("pos")).toInt() + shift));
		Clip* placed = target->createClip(TimePos(0));
		placed->restoreState(copy);
		made.append(clipIdOf(placed));
	}
	return made;
}

ControlResult copyClips(const QJsonObject& args)
{
	if (!reference().isOpen()) { return notOpen(); }
	ControlResult error;
	const QDomElement track = pick(reference().tracks(), args, "track", "track", &error);
	QList<QDomElement> clips;
	if (track.isNull() || !chosen(ReferenceProject::clipsOf(track), args, "clips", &clips, &error)) { return error; }
	Track* target = resolveTrack(args.value(QStringLiteral("to")).toString(), &error);
	if (target == nullptr) { return error; }
	for (const QDomElement& clip : clips) { if (!clipFits(clip.tagName(), target, &error)) { return error; } }
	target->addJournalCheckPoint();
	const QJsonArray made = placeClips(target, clips, args.value(QStringLiteral("position")).toInt(-1));
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("clips"), made}, {QStringLiteral("count"), made.size()}, {QStringLiteral("track"), trackIdOf(target)}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("track"), trackIdOf(target)}}, QStringLiteral("control.undo"),
		{}, true, QStringLiteral("ProjectJournal (Track checkpoint: Track::restoreState re-loads the track's clips as they were before the copy)")));
	return ControlResult::success(result);
}

//! The target's chain as a document with @a effects appended: what the chain becomes.
QString chainWith(const QString& beforeXml, const QList<QDomElement>& effects)
{
	QDomDocument doc;
	doc.setContent(beforeXml);
	QDomElement chain = doc.documentElement();
	for (const QDomElement& effect : effects) { chain.appendChild(transplanted(doc, effect)); }
	int count = 0;  // the chain's own devices: a device's own nested chain is not counted
	for (QDomElement each = chain.firstChildElement(QStringLiteral("effect")); !each.isNull(); each = each.nextSiblingElement(QStringLiteral("effect"))) { ++count; }
	chain.setAttribute(QStringLiteral("numofeffects"), count);
	return doc.toString();
}

void restoreChain(const QString& targetId, const QString& xml)
{
	ControlTarget target;
	ControlResult ignored;
	if (resolveControlTarget(targetId, &target, &ignored)) { controlRestoreChainXml(target.chain, xml); }
}

//! The reference track ('track') or mixer channel ('channel') whose devices are copied.
QDomElement deviceOwner(const QJsonObject& args, ControlResult* error)
{
	return args.contains(QStringLiteral("channel")) ? pick(reference().channels(), args, "channel", "mixer channel", error)
		: pick(reference().tracks(), args, "track", "track", error);
}

ControlResult copyDevices(const QJsonObject& args)
{
	if (!reference().isOpen()) { return notOpen(); }
	ControlResult error;
	const QDomElement owner = deviceOwner(args, &error);
	QList<QDomElement> effects;
	if (owner.isNull() || !chosen(ReferenceProject::effectsOf(owner), args, "devices", &effects, &error)) { return error; }
	ControlTarget target;
	if (!resolveControlTarget(args.value(QStringLiteral("to")).toString(), &target, &error)) { return error; }
	const QString before = controlChainXml(target.chain);
	const QString after = before.isEmpty() ? QString() : chainWith(before, effects);
	if (after.isEmpty() || after.size() > ControlSnapshotLimit)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1's chain would be larger than the bounded snapshot (%2 "
			"characters), so the copy could not be undone").arg(target.id).arg(ControlSnapshotLimit));
	}
	const QString id = target.id;
	addUndoStep([id, before]() { restoreChain(id, before); }, [id, after]() { restoreChain(id, after); });
	controlRestoreChainXml(target.chain, after);
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("target"), id}, {QStringLiteral("copied"), effects.size()},
		{QStringLiteral("device_count"), static_cast<int>(target.chain->effects().size())}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, PlainHandler handler)
{
	defineCommand(registry, QStringLiteral("reference.") + QLatin1String(verb), description, args, required, mutating, handler);
}

} // namespace

void registerReferenceCommands(ControlRegistry& registry)
{
	const QString readOnly = QStringLiteral(" The reference is read-only interface state: never played, never saved with a project, and it "
		"stays open across project.open and project.switch.");
	add(registry, "open", QStringLiteral("Open another project file ('path', .mmp / .mmpz) as the reference, beside the session: parsed, "
		"browsable, copied from - several projects at once without a second engine. Answers what reference.get_state does.") + readOnly,
		{{QStringLiteral("path"), stringProperty()}}, {QStringLiteral("path")}, false, openReference);
	add(registry, "close", QStringLiteral("Close the reference project."), {}, {}, false, closeReference);
	add(registry, "get_state", QStringLiteral("The reference project: its file (and 'stale' when the file changed since it was parsed), "
		"its tracks - index, name, type, instrument, clips (index, kind, name, position, length) and devices (index, plugin) - and "
		"its mixer channels with their devices."), {}, {}, false, referenceState);
	add(registry, "copy_tracks", QStringLiteral("Copy the reference's 'tracks' (indices; every one when left out) into this project - "
		"device, effects, clips and automation - as project.import_tracks does: each plays into the master, with ids of its own. "
		"One undo step removes them."), {{QStringLiteral("tracks"), arrayProperty()}}, {}, true, copyTracks);
	add(registry, "copy_clips", QStringLiteral("Copy clips of the reference's track 'track' (index) - 'clips' (indices; every one when "
		"left out) - onto this project's track 'to' (trk-<n>): note clips onto an instrument track, audio clips onto a sample track. "
		"They keep their positions, or with 'position' (ticks) the first lands there and the rest keep their spacing. One undo step."),
		{{QStringLiteral("track"), integerProperty(0, 100000)}, {QStringLiteral("clips"), arrayProperty()}, {QStringLiteral("to"), stringProperty()},
			{QStringLiteral("position"), integerProperty(0, MaxSongLength)}},
		{QStringLiteral("track"), QStringLiteral("to")}, true, copyClips);
	add(registry, "copy_devices", QStringLiteral("Copy devices - 'devices' (indices; every one when left out) of the reference's track "
		"'track' or mixer channel 'channel' (indices) - onto the end of the chain of 'to' (trk-<n> or ch-<n>) in this project, with "
		"their settings. The chain is rebuilt from its document (chain.apply's path), so its device ids renumber. One undo step."),
		{{QStringLiteral("track"), integerProperty(0, 100000)}, {QStringLiteral("channel"), integerProperty(0, 100000)},
			{QStringLiteral("devices"), arrayProperty()}, {QStringLiteral("to"), stringProperty()}},
		{QStringLiteral("to")}, true, copyDevices);
}

} // namespace lmms
