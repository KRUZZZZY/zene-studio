/*
 * ControlCommandsIdContract.cpp - control.id_contract, the readable form of the
 *                                 stable-id contract (SPEC-stable-ids.md).
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

/*! Its OWN translation unit, for the reason registerUndoBoundsCommands records
 *  in ControlCommandsControl.cpp: that file is at the file-length ratchet's
 *  500-line limit, and this command's six-family table is big enough to push it
 *  over. The group and the registration are unchanged - the declaration lives in
 *  include/ControlRegistryGroups.h beside the other split-out groups, and
 *  registerControlGroupCommands() still calls it.
 */

#include <QJsonArray>
#include <QJsonObject>

#include "ControlDeviceSupport.h"  // ControlTarget, resolveControlTarget()
#include "ControlRegistry.h"
#include "ControlVocabulary.h"  // objectSchema(), trackIdOf(), channelIdOf(), ...
#include "Effect.h"  // effectIdOf() takes an Effect*
#include "EffectChain.h"
#include "Engine.h"
#include "MidiClip.h"  // the note count walks MidiClip::notes()
#include "Mixer.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

void registerIdContractCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.id_contract");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("id_contract");
	cmd.description = QStringLiteral("The stable-id contract: every id family, its persistence, "
		"its form, the document element it lives on, and the current count.");
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("families"), arrayProperty()},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("persistent"), integerProperty()},
		{QStringLiteral("index_derived"), integerProperty()},
	});
	cmd.handler = [](const QJsonObject&) {
		int clipCount = 0;
		int noteCount = 0;
		int fxCount = 0;
		Song* song = Engine::getSong();
		if (song != nullptr)
		{
			for (Track* track : song->tracks())
			{
				clipCount += static_cast<int>(track->getClips().size());
				for (Clip* clip : track->getClips())
				{
					if (auto midiClip = dynamic_cast<MidiClip*>(clip))
					{
						noteCount += static_cast<int>(midiClip->notes().size());
					}
				}
				// The device count goes through resolveControlTarget, the one
				// resolver dsp.get_state and plugin.* address a chain with,
				// instead of reaching into the track's own audio port: one
				// answer to "which effects does this track carry", never a
				// second one that can drift from the one the surface acts on.
				ControlTarget target;
				ControlResult ignored;
				if (resolveControlTarget(control::trackIdOf(track), &target, &ignored))
				{
					fxCount += static_cast<int>(target.chain->effects().size());
				}
			}
		}
		Mixer* mixer = Engine::mixer();
		for (int i = 0; mixer != nullptr && i < static_cast<int>(mixer->numChannels()); ++i)
		{
			ControlTarget target;
			ControlResult ignored;
			if (resolveControlTarget(control::channelIdOf(mixer->mixerChannel(i)), &target, &ignored))
			{
				fxCount += static_cast<int>(target.chain->effects().size());
			}
		}
		const int trackCount = song != nullptr ? static_cast<int>(song->tracks().size()) : 0;
		const int channelCount = mixer != nullptr ? static_cast<int>(mixer->numChannels()) : 0;
		const int deviceCount = controlDeviceCatalogue().size();

		QJsonArray families;
		families.append(QJsonObject{
			{QStringLiteral("prefix"), QStringLiteral("trk-")},
			{QStringLiteral("persistence"), QStringLiteral("persistent")},
			{QStringLiteral("form"), QStringLiteral("trk-<n>")},
			{QStringLiteral("document"), QStringLiteral("track element id attribute")},
			{QStringLiteral("count"), trackCount},
		});
		families.append(QJsonObject{
			{QStringLiteral("prefix"), QStringLiteral("clip-")},
			{QStringLiteral("persistence"), QStringLiteral("persistent")},
			{QStringLiteral("form"), QStringLiteral("clip-<n>")},
			{QStringLiteral("document"), QStringLiteral("clip element id attribute")},
			{QStringLiteral("count"), clipCount},
		});
		families.append(QJsonObject{
			{QStringLiteral("prefix"), QStringLiteral("note-")},
			{QStringLiteral("persistence"), QStringLiteral("persistent")},
			{QStringLiteral("form"), QStringLiteral("note-<n>")},
			{QStringLiteral("document"), QStringLiteral("note element id attribute")},
			{QStringLiteral("count"), noteCount},
		});
		families.append(QJsonObject{
			{QStringLiteral("prefix"), QStringLiteral("ch-")},
			{QStringLiteral("persistence"), QStringLiteral("persistent")},
			{QStringLiteral("form"), QStringLiteral("ch-<n>")},
			{QStringLiteral("document"), QStringLiteral("mixerchannel element id attribute")},
			{QStringLiteral("count"), channelCount},
		});
		families.append(QJsonObject{
			{QStringLiteral("prefix"), QStringLiteral("fx-")},
			{QStringLiteral("persistence"), QStringLiteral("persistent")},
			{QStringLiteral("form"), QStringLiteral("fx-<n>")},
			{QStringLiteral("document"), QStringLiteral("effect element id attribute")},
			{QStringLiteral("count"), fxCount},
		});
		families.append(QJsonObject{
			{QStringLiteral("prefix"), QStringLiteral("dev-")},
			{QStringLiteral("persistence"), QStringLiteral("catalogue")},
			{QStringLiteral("form"), QStringLiteral("dev-<n>")},
			{QStringLiteral("document"), QStringLiteral("not a document identity: build catalogue selector")},
			{QStringLiteral("count"), deviceCount},
		});

		QJsonObject result;
		result.insert(QStringLiteral("families"), families);
		result.insert(QStringLiteral("count"), families.size());
		result.insert(QStringLiteral("persistent"), 5);
		result.insert(QStringLiteral("index_derived"), 1);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace lmms
