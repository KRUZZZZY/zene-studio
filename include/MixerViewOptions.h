/*
 * MixerViewOptions.h - the mixer view's options: sections, strip width, channel filter, meter mode and scale
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
#ifndef LMMS_GUI_MIXER_VIEW_OPTIONS_H
#define LMMS_GUI_MIXER_VIEW_OPTIONS_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{
class MixerChannel;
}

namespace lmms::gui
{

class Fader;
class MixerChannelView;

/*! 0.5.0 L08: how the mixer view draws its strips, set by window.mixer_view and kept in the config
 *  file (ui/mixerview) - the user's interface, never the project:
 *  - MX-003: which sections a strip shows (sends, pan, channel number, the meter line - and, from L10,
 *    its inserts by name (MX-073) and its rack's first macros as knobs (MX-090));
 *  - MX-004: the strip width (narrow, normal, wide);
 *  - MX-005: which channels are shown (all, instruments, audio, returns, used);
 *  - MX-127 / MX-129: what the meter shows (the post-fader peak, RMS, the pre-fader peak);
 *  - MX-128: the meter's scale (-42 dB - the inherited one -, -60 dB, -24 dB, or linear);
 *  - MX-126 / MX-130: the meter line under a strip - the latched clip light and the gain reduction;
 *  - 0.5.0 theme 16: the VIEW - what a strip is for (mix: levels, pan and processing; routing: where the
 *    signal comes from and goes, the sends row always shown, the inserts and macros left to the mix view) -
 *    and, independent of it, the DENSITY - how much a strip stacks (expanded: every section the options
 *    show; compact: the essentials - no inserts, no macros, in the mix view no sends row, routing on one
 *    line). Density never narrows a strip: its width stays strip_width's, and a project too wide for the
 *    screen scrolls, or is filtered, rather than shrinking strips below readability. */
namespace mixerviewoptions
{

struct Options
{
	bool sends = true;
	bool pan = true;
	bool number = true;
	bool meterInfo = true;
	bool inserts = true;  //!< L10 (MX-073): the inserts by name
	bool macros = true;   //!< L10 (MX-090): the rack's first macros
	QString width = QStringLiteral("normal");
	QString filter = QStringLiteral("all");
	QString meterMode = QStringLiteral("peak");
	QString meterScale = QStringLiteral("db42");
	QString view = QStringLiteral("mix");          //!< theme 16: mix / routing
	QString density = QStringLiteral("expanded");  //!< theme 16: compact / expanded
};

LMMS_EXPORT const QStringList& widthNames();
LMMS_EXPORT const QStringList& filterNames();
LMMS_EXPORT const QStringList& meterModeNames();
LMMS_EXPORT const QStringList& meterScaleNames();
LMMS_EXPORT const QStringList& viewNames();
LMMS_EXPORT const QStringList& densityNames();

LMMS_EXPORT const Options& options();
//! Store and save @a next; every change bumps generation() so the view re-lays its strips.
LMMS_EXPORT void setOptions(const Options& next);
LMMS_EXPORT int generation();
//! Theme 16: the options with the view and the density applied - the sections a strip actually shows.
LMMS_EXPORT Options effective(const Options& options);
LMMS_EXPORT bool routingView();
LMMS_EXPORT bool compactDensity();

LMMS_EXPORT QJsonObject toJson(const Options& options);
//! @a args over @a base (the window.mixer_view field names); false with @a error on a bad name.
LMMS_EXPORT bool merge(const QJsonObject& args, Options* base, QString* error);

//! Whether the filter shows channel @a index (the master always shows).
LMMS_EXPORT bool shows(int index);
//! The sections, width, scale and meter line on one strip.
LMMS_EXPORT void applyToStrip(MixerChannelView* view, MixerChannel* channel);
//! In a meter mode other than the peak, feed @a fader from that meter and say so (the caller skips
//! the inherited peak feed); false in the peak mode.
LMMS_EXPORT bool feedMeter(Fader* fader, MixerChannel* channel);

} // namespace mixerviewoptions

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_VIEW_OPTIONS_H
