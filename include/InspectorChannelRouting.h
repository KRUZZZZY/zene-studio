/*
 * InspectorChannelRouting.h - a mixer channel's inserts, sends and strip settings in the Inspector
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
#ifndef LMMS_GUI_INSPECTOR_CHANNEL_ROUTING_H
#define LMMS_GUI_INSPECTOR_CHANNEL_ROUTING_H

#include <functional>

#include <QString>

class QWidget;

namespace lmms
{
class MixerChannel;
}

namespace lmms::gui::inspectorrouting
{

/*! 0.5.0 L08 (MX-012): the channel window's depth where the channel's fader already is - its
 *  inserts (each with its on/off, plugin.bypass), its sends (amount, pan, pre-fader, removal -
 *  mixer.route_remove; a new one through mixer.send_to's form) and its strip settings (trim,
 *  width, polarity, mono, solo safe - MixerStrip.h). Every control is a model's knob or a
 *  registry command, so a script and a click take one path. @a rebuild is called when a send's
 *  models go (the section must not paint a freed model) and after a command changes the lists. */
QWidget* routingSection(MixerChannel* channel, const QString& id, QWidget* parent, const std::function<void()>& rebuild);

} // namespace lmms::gui::inspectorrouting

#endif // LMMS_GUI_INSPECTOR_CHANNEL_ROUTING_H
