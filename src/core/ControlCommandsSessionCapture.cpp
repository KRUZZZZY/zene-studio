/*
 * ControlCommandsSessionCapture.cpp - session.capture_to_slot: record what was played into a launcher cell
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
/*! 0.5.0 L02 (WF-055): record into a launcher cell. A cell plays a pattern of the pattern store
 *  through the store's own instrument tracks (SessionPlayback.h), so what was played - the MIDI
 *  retro-capture window, kept while midi.retro_capture_arm is on - goes into a NEW pattern: a pattern
 *  track is added, the chosen store instrument's clip of that pattern gets the notes (the same writer
 *  midi.retro_capture_to_clip uses), and the cell is pointed at the pattern. One undo step takes the
 *  cell's previous content back and the pattern away; its redo records it again from the same notes. */

#include <memory>
#include <vector>

#include "ControlCommandsSessionShared.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MidiClip.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "RetroMidiClipWriter.h"
#include "Song.h"

namespace lmms
{

using namespace control;
using namespace sessioncontrol;

namespace
{

//! What one recording made and what it needs to make it again.
struct Captured
{
	int track = 0;
	int scene = 0;
	int instrument = 0;
	std::vector<RetroMidiEvent> events;
	tick_t start = 0;
	tick_t length = 0;
	QString session;  //!< the <session> block before
	Track* pattern = nullptr;
	int written = 0;
};

//! Make the pattern, write the notes into the store instrument's clip of it, point the cell at it.
bool build(Captured* c)
{
	Song* song = Engine::getSong();
	PatternStore* store = Engine::patternStore();
	if (c->instrument < 0 || c->instrument >= static_cast<int>(store->tracks().size())) { return false; }
	auto* patternTrack = dynamic_cast<PatternTrack*>(Track::create(Track::Type::Pattern, song));
	if (patternTrack == nullptr) { return false; }
	const int pattern = patternTrack->patternIndex();
	store->createClipsForPattern(pattern);
	auto* clip = dynamic_cast<MidiClip*>(store->tracks()[static_cast<std::size_t>(c->instrument)]->getClip(static_cast<std::size_t>(pattern)));
	if (clip != nullptr)
	{
		c->written = writeWindowToClip(clip, c->events.data(), c->events.size(), c->start).notes;
		clip->changeLength(TimePos(c->length));
		clip->setAutoResize(false);
		clip->dataChanged();
	}
	ClipSlot& slot = song->sessionModel().slot(c->track, c->scene);
	slot.setPatternReference(pattern);
	slot.setName(QObject::tr("Recorded %1").arg(pattern + 1));
	c->pattern = patternTrack;
	return true;
}

void unbuild(Captured* c)
{
	if (Song* song = Engine::getSong()) { restoreSessionBlock(song->sessionModel(), c->session); }
	if (c->pattern != nullptr) { removeTrack(c->pattern); }
	c->pattern = nullptr;
}

ControlResult captureToSlot(const QJsonObject& args)
{
	ControlResult error;
	SessionModel* model = sessionModelOrNull(&error);
	if (model == nullptr) { return error; }
	auto c = std::make_shared<Captured>();
	c->track = args.value(QStringLiteral("track")).toInt();
	c->scene = args.value(QStringLiteral("scene")).toInt();
	c->instrument = args.value(QStringLiteral("instrument")).toInt(0);
	if (!gridContains(*model, c->track, c->scene, &error)) { return error; }
	if (c->instrument >= static_cast<int>(Engine::patternStore()->tracks().size()))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the pattern store has no instrument %1").arg(c->instrument));
	}
	tick_t end = 0;
	if (!readRetroWindow(&c->events, &c->start, &end, &error)) { return error; }
	const tick_t bar = std::max<tick_t>(1, Engine::getSong()->ticksPerBar());
	const int bars = args.value(QStringLiteral("bars")).toInt(0);
	c->length = bars > 0 ? bars * bar : ((end - c->start) / bar + 1) * bar;
	c->session = sessionBlockXml(*model);
	if (!build(c.get())) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the pattern could not be made")); }
	addUndoStep([c]() { unbuild(c.get()); }, [c]() { build(c.get()); });
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("slot"), clipSlotState(c->track, c->scene, model->slot(c->track, c->scene))},
		{QStringLiteral("pattern"), model->slot(c->track, c->scene).patternId()}, {QStringLiteral("notes"), c->written},
		{QStringLiteral("length"), static_cast<double>(c->length)}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("track"), c->track}, {QStringLiteral("scene"), c->scene}},
		QStringLiteral("control.undo"), {}, true, QStringLiteral("action checkpoint: the recorded undo step restores the "
			"<session> block and removes the pattern track it made; its redo records the same notes again")));
	return ControlResult::success(result);
}

} // namespace

void registerSessionCaptureCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.capture_to_slot");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("capture_to_slot");
	cmd.description = QStringLiteral("Record what was just played into a launcher cell ('track', 'scene'): the MIDI kept by "
		"midi.retro_capture_arm becomes a new pattern - in the clip of pattern-store instrument 'instrument' (0, the first, "
		"when left out), 'bars' long (the playing rounded up to whole bars when left out) - and the cell plays that pattern. "
		"One undo step.");
	cmd.argsSchema = objectSchema({{QStringLiteral("track"), integerProperty(0, 255)}, {QStringLiteral("scene"), integerProperty(0, 511)},
		{QStringLiteral("instrument"), integerProperty(0, 255)}, {QStringLiteral("bars"), integerProperty(1, 64)}},
		{QStringLiteral("track"), QStringLiteral("scene")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = captureToSlot;
	registry.registerCommand(cmd);
}

} // namespace lmms
