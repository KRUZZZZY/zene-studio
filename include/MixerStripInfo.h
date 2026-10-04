/*
 * MixerStripInfo.h - what a mixer strip is and where it routes, in words: its type, format, routes and modes
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
#ifndef LMMS_GUI_MIXER_STRIP_INFO_H
#define LMMS_GUI_MIXER_STRIP_INFO_H

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "AudioIoSetup.h"
#include "lmms_export.h"

namespace lmms
{
class Mixer;
}

namespace lmms::gui
{

class MixerChannelView;

/*! 0.5.0 theme 16 (the mixer reworked around clear modes): a strip says what it is and where its signal
 *  goes in words, never by colour or style alone and never by an internal id -
 *  - its type: Master, Return (a bus sends land on before its fader), Group (a folder track's channel),
 *    Track (a channel tracks play into) or Channel (nothing plays into it yet); a VCA strip is its own
 *    widget (VcaStripView) and wears the same badge;
 *  - its surround format beside the type ("Return 5.1"), stereo saying nothing;
 *  - where it comes from (the tracks playing into it, the channels sending into it) and goes to (every
 *    send by the receiver's name, pre-fader ones marked, and its hardware output);
 *  - the modes it is in: listening (AFL / PFL), solo safe, the surround panner on, cue sends, a hardware
 *    insert. The sends-on-faders mode is the fader's own label (theme 6, MixerOverlays.h).
 *  Read on the interface thread every half second (MixerView::syncWithMixer): a Context is built once per
 *  pass, so a large project costs one walk of its tracks, not one per strip. */
namespace mixerstripinfo
{

enum class ChannelType
{
	Master,
	Return,
	Group,
	Track,
	Channel
};

//! One send of the strip, by its receiver's name.
struct Route
{
	QString to;
	double amount = 1.0;
	bool preFader = false;
	bool enabled = true;
};

struct StripInfo
{
	ChannelType type = ChannelType::Channel;
	QString format;        //!< "LCR", "Quad", "5.1", "7.1"; empty for stereo
	QStringList inputs;    //!< the tracks playing into it, then the channels sending into it
	QVector<Route> outputs;
	QString hardware;      //!< its hardware output's label ("Out 3/4", a bus name); empty when it has none
	QStringList states;    //!< the modes it is in, as their short words
};

//! What a pass over the strips reads once.
struct Context
{
	QHash<int, QStringList> tracksInto;  //!< channel index -> the names of the tracks playing into it
	QSet<int> folderChannels;            //!< the channels folder tracks own
	audioio::Side outputs;               //!< the interface's outputs, for the hardware labels
	QSet<int> listening;
	bool preFaderListen = false;
};

LMMS_EXPORT Context buildContext();
LMMS_EXPORT StripInfo describe(Mixer& mixer, int index, const Context& context);

//! "Master", "Return", "Group", "Track", "Channel" - and the form agents read ("master", "return", ...).
LMMS_EXPORT QString typeName(ChannelType type);
LMMS_EXPORT QString typeKey(ChannelType type);
//! The badge: the type, and the format after it ("Return 5.1").
LMMS_EXPORT QString badgeText(const StripInfo& info);
//! The mix view's routing lines: every send by its receiver's name ("-> Master", "-> Verb (pre)"; the first
//! three, then how many more) and the hardware output; @a compact keeps one line ("-> Master +2").
LMMS_EXPORT QString routingSummary(const StripInfo& info, bool compact);
//! The routing view's lines: where the signal comes from ("<- Kick"), where it goes (with each send's amount
//! when not full) and the hardware output; @a compact keeps one line each way.
LMMS_EXPORT QString routingDetail(const StripInfo& info, bool compact);
//! The modes as one line ("AFL - Solo safe"), with the hardware output when @a withHardware (the compact
//! density, whose routing line has no room for it); empty when there are none.
LMMS_EXPORT QString stateLine(const StripInfo& info, bool withHardware);
//! Everything above in sentences, for a screen reader (the strip's accessible description).
LMMS_EXPORT QString spokenText(const StripInfo& info);
//! The same for window.mixer_view's answer, every channel named by its name: type, format, inputs,
//! outputs, hardware, states.
LMMS_EXPORT QJsonObject toJson(const StripInfo& info);

/*! A strip's tooltip - its delay compensation (the figures pdc.report gives), its note (MX-016) and, on a
 *  return, what sets it apart (MX-045) - and its accessible name, which follows the channel's name, an
 *  agent's rename included (set only on a change: each set is announced). */
LMMS_EXPORT void describeStrip(MixerChannelView* view, Mixer* mixer, int index);

} // namespace mixerstripinfo

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_STRIP_INFO_H
