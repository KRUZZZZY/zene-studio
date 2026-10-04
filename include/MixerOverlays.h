/*
 * MixerOverlays.h - mixer view modes: folded folders' strips hidden, and faders that show one send
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

#ifndef LMMS_GUI_MIXER_OVERLAYS_H
#define LMMS_GUI_MIXER_OVERLAYS_H

#include <QSet>
#include <QVector>

#include "lmms_export.h"

namespace lmms::gui
{

class MixerChannelView;

/*! 0.5.0 L09: two ways the mixer view shows the same channels differently, applied by
 *  MixerView::syncWithMixer to its strips (interface state - nothing in the project changes):
 *  - MX-066: a collapsed folder track (track.folder_set_collapsed, the arranger's own flag) hides its
 *    children's channel strips - their current channels, and in routing mode the ones they had;
 *  - MX-049: 'sends on faders' - every channel's fader drives its send to one return, until it is
 *    switched back. 0.5.0 theme 6 (the owner's decision, Pro Tools / Logic style): one chosen send flips
 *    across every fader; a banner names it (MixerSendsBanner), every fader says what it drives, and a
 *    channel with no send there keeps its fader in place, dimmed and still, reading "No send" - a strip
 *    never loses its fader. */
namespace mixeroverlays
{

//! What a strip's fader drives: its channel's level (the channel faders; and, with the faders on a return's
//! sends, the return's own and the master's), its send to that return, or nothing - the channel has no
//! send there.
enum class FaderRole
{
	Volume,
	Send,
	NoSend
};

//! The return channel (index) the faders show sends to, or -1 for the channel faders. It is kept by the
//! channel's stable id, so it follows that channel as others are added, deleted or moved, and ends with it.
LMMS_EXPORT int sendsOnFaders();
LMMS_EXPORT void setSendsOnFaders(int returnIndex);
//! The channel indices a collapsed folder hides.
LMMS_EXPORT QSet<int> foldedChannels();
//! What channel @a index's fader drives in the current mode.
LMMS_EXPORT FaderRole faderRole(int index);
//! Apply both to @a views (index = channel index): the folded strips hidden, every fader on what it drives.
LMMS_EXPORT void apply(const QVector<MixerChannelView*>& views);

} // namespace mixeroverlays

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_OVERLAYS_H
