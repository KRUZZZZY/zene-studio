/*
 * ControlMixerSupport.h - shared helpers for the pdc.* / bus.* / routing.* and
 *                         mixer routing command groups (SPEC A11-A16).
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

#ifndef LMMS_CONTROL_MIXER_SUPPORT_H
#define LMMS_CONTROL_MIXER_SUPPORT_H

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "lmms_export.h"

namespace lmms
{

class MixerChannel;
class MixerRoute;
class MixerSidechainRoute;

//! Tap point of a sidechain send (Mixer.h); an enum class with a fixed
//! underlying type, so it can be named here without the whole mixer header.
enum class SidechainTapPoint : int;

namespace control
{

/*! Resolve a "ch-<n>" id against the live mixer.
 *
 * One definition, shared by pdc.* / bus.* / routing.* / the mixer routing verbs:
 * the mixer group's own file-local copy predates this header and is left alone
 * so mixer.get_state's refusals are not re-worded by a refactor.
 */
LMMS_EXPORT MixerChannel* resolveMixerChannel(const QString& id, ControlResult* error);

//! Wire name of a tap point: "post_fader" | "pre_fx" | "pre_fader" |
//! "post_fader_no_gain". The four names are the engine's own enum values
//! (include/Mixer.h, SidechainTapPoint), lowercased.
LMMS_EXPORT QString sidechainTapPointName(SidechainTapPoint point);

//! Parses a wire name back; false when it is not one of the four above.
LMMS_EXPORT bool sidechainTapPointFromName(const QString& name, SidechainTapPoint* point);

//! Every tap point name, in the engine's enum order - the schema's enum.
LMMS_EXPORT QStringList sidechainTapPointNames();

//! One regular send/path, as pdc.report reports it.
LMMS_EXPORT QJsonObject routeJson(MixerRoute& route);

//! One sidechain send, as pdc.report reports it.
LMMS_EXPORT QJsonObject sidechainRouteJson(MixerSidechainRoute& route);

/*! The PDC view of one mixer channel: what the mixer aligns this channel's
 *  input to, the latency this channel's own chain adds, and the routing this
 *  channel owns.
 *
 *  Every number is READ from the engine (`MixerChannel::inputLatencyFrames()`,
 *  `EffectChain::latencyFrames()`), never recomputed here: a client that wants
 *  the mixer's own view must not be handed a second implementation of it.
 */
LMMS_EXPORT QJsonObject channelLatencyJson(MixerChannel& channel);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_MIXER_SUPPORT_H
