/*
 * ContainedCrossfade.cpp - a crossfade between a clip and one that lies wholly inside it
 *                          (include/ContainedCrossfade.h).
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

#include "ContainedCrossfade.h"

#include "Clip.h"

namespace lmms::containedcrossfade
{

namespace
{

bool inside(const Clip* inner, const Clip* outer)
{
	return inner->startPosition() >= outer->startPosition() && inner->endPosition() <= outer->endPosition()
		&& (inner->startPosition() != outer->startPosition() || inner->endPosition() != outer->endPosition());
}

} // namespace

bool containment(const Clip* a, const Clip* b, const Clip** container, const Clip** contained)
{
	if (inside(b, a)) { *container = a; *contained = b; return true; }
	if (inside(a, b)) { *container = b; *contained = a; return true; }
	return false;
}

QString ramps(const Clip& container, const Clip& contained, int given, bool givenIsEntry, int* entry, int* exit)
{
	Q_UNUSED(container);
	const int length = contained.length().getTicks();
	const ClipEdits& authored = contained.clipEdits();
	if (given > 0 && givenIsEntry)
	{
		*entry = given;
		*exit = length - given;
	}
	else if (given > 0)
	{
		*exit = given;
		*entry = length - given;
	}
	else
	{
		*entry = authored.fadeInTicks;
		*exit = authored.fadeOutTicks;
	}
	if (*entry > 0 && *exit > 0 && *entry <= length && *exit <= length - *entry) { return QString(); }
	return QStringLiteral("container mode needs positive authored inner fade-in and fade-out ramps that fit inside its "
		"%3-tick length; got entry %1 + exit %2").arg(*entry).arg(*exit).arg(length);
}

void apply(const Clip& container, const Clip& contained, int entry, int exit, FadeShape downShape,
	FadeShape upShape, ClipEdits* containerEdits, ClipEdits* containedEdits)
{
	const tick_t start = contained.startPosition().getTicks();
	const tick_t end = contained.endPosition().getTicks();
	const tick_t containerStart = container.startPosition().getTicks();
	const tick_t containerEnd = container.endPosition().getTicks();

	// The container: down over [start, start + entry), up over [end - exit, end), unfaded elsewhere.
	containerEdits->fadeOutTicks = entry;
	containerEdits->fadeOutShape = downShape;
	containerEdits->fadeOutOffsetTicks = static_cast<int>(containerEnd - (start + entry));
	containerEdits->fadeInTicks = exit;
	containerEdits->fadeInShape = upShape;
	containerEdits->fadeInOffsetTicks = static_cast<int>(end - exit - containerStart);
	containerEdits->fadeOffsetGap = entry + exit < contained.length().getTicks();

	// The contained clip: in over its first `entry` ticks, out over its last `exit`.
	containedEdits->fadeInTicks = entry;
	containedEdits->fadeInShape = upShape;
	containedEdits->fadeInOffsetTicks = 0;
	containedEdits->fadeOutTicks = exit;
	containedEdits->fadeOutShape = downShape;
	containedEdits->fadeOutOffsetTicks = 0;
	containedEdits->fadeOffsetGap = false;
}

} // namespace lmms::containedcrossfade
