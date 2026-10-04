/*
 * MixerStripTags.h - a mixer strip's type badge, routing lines and mode line
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
#ifndef LMMS_GUI_MIXER_STRIP_TAGS_H
#define LMMS_GUI_MIXER_STRIP_TAGS_H

#include <QString>
#include <QVector>

#include "MixerStripInfo.h"
#include "lmms_export.h"

class QLabel;
class QWidget;

namespace lmms
{
class Mixer;
}

namespace lmms::gui
{

class MixerChannelView;

/*! 0.5.0 theme 16: the words on a strip (MixerStripInfo.h says what they are) -
 *  - the badge at its top: its type and surround format, boxed so it reads as a label ("Return 5.1"; a VCA
 *    strip wears one too);
 *  - the routing lines under its name: in the mix view where it sends (and its hardware output), in the
 *    routing view where it comes from as well, each send's amount and its hardware output;
 *  - the mode line under them: AFL / PFL, solo safe, surround pan, cue, hardware insert (and, compact, the
 *    hardware output), hidden while it has none.
 *  Each line is elided to the strip's width, its whole text on its tooltip and its accessible name; the
 *  strip's accessible description says all of it in sentences. A text is set only when it changes (each set
 *  is announced). Sizes come from the design tokens' caption font and spacing, so the global zoom and
 *  accessible mode scale them. */
namespace mixerstriptags
{

//! A badge reading @a text (a strip's type), outlined so it reads as a label, not a button.
LMMS_EXPORT QLabel* makeBadge(QWidget* parent, const QString& text = QString());
//! A caption line (the routing or the mode line), hidden until it has text.
LMMS_EXPORT QLabel* makeLine(QWidget* parent, const QString& objectName);
//! Put @a text on @a label, each line elided to @a width; hidden while @a text is empty.
LMMS_EXPORT void setLines(QLabel* label, const QString& text, int width);
//! Put @a info on @a view's badge, routing lines and mode line for the current view and density.
LMMS_EXPORT void apply(MixerChannelView* view, const mixerstripinfo::StripInfo& info);
//! The same for every shown strip in @a views (index = channel index), from @a context.
LMMS_EXPORT void applyAll(Mixer* mixer, const QVector<MixerChannelView*>& views, const mixerstripinfo::Context& context);

} // namespace mixerstriptags

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_STRIP_TAGS_H
