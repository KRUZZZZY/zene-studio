/*
 * ControlCommandsInputTransform.cpp - track.input_transform_get / set: what an instrument track does to live MIDI input
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

/*! ED-145 / ED-113 (0.5.0 L07): an instrument track's MIDI input transform (MidiInputTransform.h) -
 *  channel filter, key range, transpose, snap to the scale context, velocity scale / offset / fixed.
 *  A change is one undo step (an action step holding the whole previous transform). */

#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlScaleShared.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "PatternStore.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

InstrumentTrack* instrumentTrackById(int id)
{
	Track* track = Engine::getSong()->findTrackById(id);
	if (track == nullptr && Engine::patternStore() != nullptr) { track = Engine::patternStore()->findTrackById(id); }
	return dynamic_cast<InstrumentTrack*>(track);
}

//! Put @a state (a whole transform, as toJson gives it) on track @a id, if it still exists.
void restore(int id, const QJsonObject& state)
{
	if (InstrumentTrack* track = instrumentTrackById(id))
	{
		QString ignored;
		track->inputTransform().reset();
		track->inputTransform().fromJson(state, &ignored);
	}
}

InstrumentTrack* resolveInstrumentTrack(const QJsonObject& args, ControlResult* error)
{
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), error);
	if (track == nullptr) { return nullptr; }
	auto* instrument = dynamic_cast<InstrumentTrack*>(track);
	if (instrument == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a MIDI input transform belongs to an instrument track"));
	}
	return instrument;
}

//! 'scale_snap' as the scale mask: the scale context's pitch classes (true), none (false).
bool scaleSnapMask(const QJsonObject& args, QJsonObject* fields, ControlResult* error)
{
	if (!args.contains(QStringLiteral("scale_snap"))) { return true; }
	if (!args.value(QStringLiteral("scale_snap")).toBool())
	{
		fields->insert(QStringLiteral("scale_mask"), 0);
		return true;
	}
	ResolvedScale scale;
	if (!resolveScale({}, &scale, error)) { return false; }
	int mask = 0;
	for (const int pitchClass : scale.classes) { mask |= 1 << (pitchClass % 12); }
	if (mask == 0)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("scale_snap needs a scale (scale.set)"));
		return false;
	}
	fields->insert(QStringLiteral("scale_mask"), mask);
	return true;
}

ControlResult setTransform(const QJsonObject& args)
{
	ControlResult error;
	InstrumentTrack* track = resolveInstrumentTrack(args, &error);
	if (track == nullptr) { return error; }
	QJsonObject fields = args;
	fields.remove(QStringLiteral("track"));
	fields.remove(QStringLiteral("scale_snap"));
	fields.remove(QStringLiteral("reset"));
	if (!scaleSnapMask(args, &fields, &error)) { return error; }
	MidiInputTransform& transform = track->inputTransform();
	const QJsonObject before = transform.toJson();
	MidiInputTransform probe;  // validate on a copy first: a refusal changes nothing
	QString why;
	if (!probe.fromJson(fields, &why)) { return ControlResult::failure(ControlErrorKind::InvalidArgs, why); }
	if (args.value(QStringLiteral("reset")).toBool()) { transform.reset(); }
	transform.fromJson(fields, &why);
	const QJsonObject after = transform.toJson();
	const int id = track->id();
	addUndoStep([id, before] { restore(id, before); }, [id, after] { restore(id, after); });
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("transform"), after}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("transform"), before}},
		QStringLiteral("control.undo"), {}, true,
		QStringLiteral("action checkpoint: the recorded undo step puts the track's previous transform back")));
	return ControlResult::success(result);
}

ControlResult getTransform(const QJsonObject& args)
{
	ControlResult error;
	InstrumentTrack* track = resolveInstrumentTrack(args, &error);
	if (track == nullptr) { return error; }
	return ControlResult::success({{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("transform"), track->inputTransform().toJson()}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("track.") + QLatin1String(verb), description, args, mutating, handler);
}

} // namespace

void registerInputTransformCommands(ControlRegistry& registry)
{
	add(registry, "input_transform_get", QStringLiteral("An instrument track's MIDI input transform: channel (0 any), "
		"key_low / key_high, transpose, scale_mask (12 bits, C = bit 0; 0 no snapping), velocity_scale (%), "
		"velocity_offset, fixed_velocity (0 off), active."), objectSchema({{QStringLiteral("track"), stringProperty()}},
		{QStringLiteral("track")}), false, getTransform);
	add(registry, "input_transform_set", QStringLiteral("Change what an instrument track does to LIVE MIDI input (not to "
		"clips): channel 0..16 (0 any), key_low / key_high (notes outside are dropped), transpose -48..48, scale_snap "
		"(true snaps to the scale context's notes, false stops), velocity_scale 1..400 %, velocity_offset -127..127, "
		"fixed_velocity 0..127 (0 off); a field left out keeps its value, 'reset' first restores the identity. A "
		"note-off follows its note-on, so a change while a key is held leaves no note hanging. One undo step."),
		objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("channel"), integerProperty(0, 16)},
			{QStringLiteral("key_low"), integerProperty(0, 127)}, {QStringLiteral("key_high"), integerProperty(0, 127)},
			{QStringLiteral("transpose"), integerProperty(-48, 48)}, {QStringLiteral("scale_snap"), booleanProperty()},
			{QStringLiteral("velocity_scale"), integerProperty(1, 400)}, {QStringLiteral("velocity_offset"), integerProperty(-127, 127)},
			{QStringLiteral("fixed_velocity"), integerProperty(0, 127)}, {QStringLiteral("reset"), booleanProperty()}},
			{QStringLiteral("track")}), true, setTransform);
}

} // namespace lmms
