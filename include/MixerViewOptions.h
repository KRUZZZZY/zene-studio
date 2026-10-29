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

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

class QWidget;

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
 *    its inserts by name (MX-073) and its rack's first macros as knobs (MX-090); from Q050-088 its Console
 *    section, off by default);
 *  - MX-004: the strip width (narrow, normal, wide);
 *  - MX-005: which channels are shown (all, instruments, audio, returns, used, selected - the channels picked
 *    for it, by window.mixer_view's selected_channels, a Ctrl+click on a strip or the mode bar's picker);
 *  - MX-127 / MX-129: what the meter shows (the post-fader peak, RMS, the pre-fader peak);
 *  - MX-128: the meter's scale (-42 dB - the inherited one -, -60 dB, -24 dB, or linear), and its ballistics:
 *    how fast a bar falls (fast - the inherited 1.25 per 60 Hz tick, about 116 dB/s -, medium, slow) and how
 *    long the peak line holds (off, 1.5 s - the inherited one -, 3 s, 6 s);
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
	bool console = false;  //!< Q050-088: the Console section (trim, polarity, EQ, the Console's drive and glue)
	QString width = QStringLiteral("normal");
	QString filter = QStringLiteral("all");
	QString meterMode = QStringLiteral("peak");
	QString meterScale = QStringLiteral("db42");
	QString meterFallOff = QStringLiteral("fast");  //!< MX-128: fast (the inherited 1.25 per tick), medium, slow
	QString peakHold = QStringLiteral("1.5s");      //!< MX-128: off, 1.5s (the inherited hold), 3s, 6s
	QString view = QStringLiteral("mix");          //!< theme 16: mix / routing
	QString density = QStringLiteral("expanded");  //!< theme 16: compact / expanded
};

LMMS_EXPORT const QStringList& widthNames();
LMMS_EXPORT const QStringList& filterNames();
LMMS_EXPORT const QStringList& meterModeNames();
LMMS_EXPORT const QStringList& meterScaleNames();
LMMS_EXPORT const QStringList& meterFallOffNames();
LMMS_EXPORT const QStringList& peakHoldNames();
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
//! MX-128: what a meter bar is divided by on each tick it has no higher reading (the main window's 60 Hz tick).
LMMS_EXPORT float meterFallOff();
//! MX-128: how long a peak line holds before it falls, in ms (0: no hold - the line is the level).
LMMS_EXPORT int peakHoldMs(const QString& name);

LMMS_EXPORT QJsonObject toJson(const Options& options);
//! @a args over @a base (the window.mixer_view field names); false with @a error on a bad name.
LMMS_EXPORT bool merge(const QJsonObject& args, Options* base, QString* error);

/*! MX-005: the channels the Selected filter shows. Kept by each channel's stable id (MixerChannel::id()), so
 *  it follows a channel as others are added, deleted or moved; interface state - never saved, and forgotten
 *  when the mixer view is cleared for another project (the ids start again there). */
LMMS_EXPORT QList<int> selectedChannels();  //!< the indices, in mixer order
LMMS_EXPORT bool isSelected(int index);
LMMS_EXPORT void setSelectedChannels(const QList<int>& indices);
LMMS_EXPORT void clearSelection();
//! The selection as ch-<n> ids, in mixer order (window.mixer_view's selected_channels).
LMMS_EXPORT QJsonArray selectionJson();
//! A strip's Ctrl+click: channel @a index in or out of the selection, through window.mixer_view.
LMMS_EXPORT void toggleSelected(QWidget* origin, int index);

//! Whether the filter shows channel @a index (the master always shows).
LMMS_EXPORT bool shows(int index);
//! The sections, width, scale and meter line on one strip.
LMMS_EXPORT void applyToStrip(MixerChannelView* view, MixerChannel* channel);
//! Q050-108: the meter mode @a channel's strip shows - its own (mixer.set_strip 'meter_mode'), else the view's.
LMMS_EXPORT QString meterModeOf(const MixerChannel* channel);
//! In a meter mode other than the peak, feed @a fader from that meter and say so (the caller skips
//! the inherited peak feed); false in the peak mode.
LMMS_EXPORT bool feedMeter(Fader* fader, MixerChannel* channel);

} // namespace mixerviewoptions

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_VIEW_OPTIONS_H
