/*
 * ControlCommandsInputMeters.cpp - meter.inputs: every capture channel's level and the armed tracks' inputs
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

/*! Q050-340: meter.inputs - the agent's view of what Large Meters' input meters show: each capture channel's
 *  peak since the agent's last call and each record-armed route's selected input (InputReadings.h). It takes
 *  the agent's own maxima, so it never empties the interface's meters (nor they its). Read only. */

#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "InputReadings.h"

namespace lmms
{

using namespace control;

void registerInputMetersCommand(ControlRegistry& registry)
{
	control::defineCommandWithSchema(registry, QStringLiteral("meter.inputs"), QStringLiteral(
		"Q050-340: how loud each input is - 'channels': every capture channel the input carries (the interface's own "
		"channels, before the stereo bus picks its pair; the stereo bus as channels 0 and 1 for a backend without the "
		"wide capture path), each with its 'peak' (linear) and 'peak_db' (-120 for silence) since the last call and "
		"'clipped'; 'armed': every record-armed route with its 'track' (id and 'track_name'), the 'input_channel' it "
		"records and that channel's level - silence when the channel delivered nothing - whatever the track's input "
		"monitoring. 'channel_count' is 0 until a capture has delivered audio. The agent's readings are its own: Large "
		"Meters' input meters keep theirs. Read only."),
		objectSchema({}), false, [](const QJsonObject&) {
			return ControlResult::success(inputreadings::toJson(inputreadings::take(inputmeter::Reader::Agent)));
		});
}

} // namespace lmms
