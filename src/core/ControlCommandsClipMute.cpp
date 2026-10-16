/*
 * ControlCommandsClipMute.cpp - the clip group's mute verb (SPEC A11-A16): clip.set_mute.
 *
 * 0.5.0 Q050-040 (L07): the clip mute was the one clip state the agent surface could
 * not write - the song editor toggled it straight off the model (ClipView::toggleMute
 * -> Clip::toggleMute), so an agent had no registered way to unmute a clip under the
 * playhead while track.set_mute could do the same for a track. The GUI's mute action
 * dispatches THIS command now (one action, one implementation, SPEC A11), and its undo
 * rides the same one-step BoolModel checkpoint the track's does.
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

#include <QJsonObject>

#include "Clip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "PatternStore.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

/*! \a ref set to the clip whose id \a id names, searching the arrangement first
 *  (resolveClip, the clip surface's home) and then the pattern editor's container.
 *
 *  The GUI's clip mute lives on every clip view, and the PatternStore tracks the
 *  pattern editor shows hold live clips the arrangement's enumeration does not
 *  carry. resolveClip's not-found for one of those is a REFUSAL for the GUI's
 *  own mute - a second GUI implementation would be the alternative - so the
 *  command resolves both containers by the same stable clip-<n> id. The id
 *  space is one project counter shared by every family (SPEC-stable-ids.md
 *  R1), so the search order cannot collide: an arrangement clip is found first
 *  and a pattern-store clip only ever has the arrangement's leftovers.
 */
bool resolveAnyClip(const QString& id, ClipRef* ref, ControlResult* error)
{
	if (resolveClip(id, ref, error)) { return true; }
	const TrackContainer::TrackList& tracks = Engine::patternStore()->tracks();
	const int wanted = idToIndex(id, QStringLiteral("clip-"));  // resolveClip validated the id's form
	for (Track* track : tracks)
	{
		for (Clip* clip : track->getClips())
		{
			if (clip->id() == wanted)
			{
				ref->clip = clip;
				ref->track = track;
				ref->id = clip->id();
				return true;
			}
		}
	}
	return false;
}

void registerClipSetMute(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.set_mute");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("set_mute");
	cmd.description = QStringLiteral("Mute or unmute a clip. The song editor's own clip mute "
		"(Ctrl+middle-click and the clip context menu) dispatches this command. A clip of the song "
		"arrangement or of the pattern editor is addressed by its stable clip-<n> id. With the chase "
		"preference on, unmuting a clip under the playhead starts the notes it was holding. "
		"Reversible through the ProjectJournal (the clip's mute BoolModel checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("muted"), booleanProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("muted")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("muted"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ClipRef ref;
		const QString id = args.value(QStringLiteral("clip")).toString();
		if (!resolveAnyClip(id, &ref, &error))
		{
			return error;
		}
		const bool previous = ref.clip->isMuted();
		QJsonObject before;
		before.insert(QStringLiteral("clip"), id);
		before.insert(QStringLiteral("muted"), previous);

		// The muted model of a Clip is a BoolModel, i.e. a JournallingObject: its
		// setValue is ONE journal checkpoint, the same mechanism the track's mute
		// (track.set_mute) and the GUI's own toggle reverse through.
		ref.clip->setMuted(args.value(QStringLiteral("muted")).toBool());

		QJsonObject result;
		result.insert(QStringLiteral("clip"), id);
		result.insert(QStringLiteral("muted"), ref.clip->isMuted());
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), id);
		inverseArgs.insert(QStringLiteral("muted"), previous);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(before, QStringLiteral("clip.set_mute"), inverseArgs, true,
				QStringLiteral("ProjectJournal (Clip mute BoolModel checkpoint)")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerClipMuteCommands(ControlRegistry& registry)
{
	registerClipSetMute(registry);
}

} // namespace lmms
