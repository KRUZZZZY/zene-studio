/*
 * ClipLockFeedback.h - a locked clip shown as locked, and a refused edit of one said out loud
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

#ifndef LMMS_GUI_CLIP_LOCK_FEEDBACK_H
#define LMMS_GUI_CLIP_LOCK_FEEDBACK_H

#include "lmms_export.h"

class QString;

namespace lmms
{
class Clip;
}

namespace lmms::gui
{

class ClipView;
class TextFloat;

/*! 0.5.0 theme 2 (DESIGN-DECISIONS.md "clip moves and locks"): a locked clip - or a clip of a locked
 *  track (EditLocks) - stays selectable for inspection, wears a padlock badge in its corner, and an edit
 *  the mouse tries on it (a drag, a knife cut, a delete, a drop onto it that would trim it) is refused
 *  with a message that says why and how to unlock. Interface only: nothing here edits the project. */
namespace cliplocks
{

//! Create, show or hide @a view's padlock badge to match its clip's lock; cheap when nothing changed,
//! and creates nothing for a clip that was never locked.
LMMS_EXPORT void syncBadge(ClipView* view);
//! Whether @a view shows a badge now (a test's view of it).
LMMS_EXPORT bool badgeShown(const ClipView* view);

//! The message a refused @a action ("moved", "split", ...) of the locked @a clip shows. With no
//! @a timeoutMs it stays until the caller deletes it (the gesture's end); with one it goes by itself.
LMMS_EXPORT TextFloat* refuse(const Clip* clip, const QString& action, int timeoutMs = 0);
//! The message a drop onto the locked @a under shows, for a while.
LMMS_EXPORT void refuseDropOnto(const Clip* under);
//! A short message that @a kept locked clips stayed out of an edit of several; none when 0.
LMMS_EXPORT void announceKept(int kept);

//! Have every clip view's badge follow a lock change at once (EditLocks' change hook). Once.
LMMS_EXPORT void install();

} // namespace cliplocks

} // namespace lmms::gui

#endif // LMMS_GUI_CLIP_LOCK_FEEDBACK_H
