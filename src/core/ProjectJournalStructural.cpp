/*
 * ProjectJournalStructural.cpp - the STRUCTURAL undo step: an inverse carried
 *                                by a measured document (Zene Studio, task #664,
 *                                feature row 75).
 *
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

/*
 * WHY THIS IS ITS OWN TRANSLATION UNIT
 *
 * The same reason ProjectJournalBounds.cpp is: ProjectJournal.cpp is
 * upstream-inherited code this fork declares in tests/upstream-modifications.txt,
 * and the fork's per-file length ratchet measures a file as a unit. A new step
 * KIND belongs beside the bound and the coalescing primitive - the mechanism's
 * own additions - rather than in the middle of the historical stack.
 *
 * WHAT THE KIND IS, AND WHY THE ACTION CHECKPOINT WAS NOT ENOUGH
 *
 * An action checkpoint (`addJournalAction`) records a pair of operations and
 * declares its serialised size to be zero, because nothing is serialised: the
 * operations are closures. That is right for a scalar (a settings value, a
 * transport flag) and wrong for a STRUCTURE. A structural inverse carries the
 * document it captured - a deleted track's own XML with its clips and their
 * notes, a removed device's state - and that document is not free. Measured on
 * this tree: a four-clip track snapshot is a few KB and the same headroom as a
 * full Song checkpoint is 16 MiB, so a hundred structural deletes would sit
 * under the count cap while the byte budget reported ZERO retained bytes and
 * evicted nothing. The declared budget would then be a number that never
 * applies to the steps most likely to be large.
 *
 * So the step KIND is the same recorded pair with the payload measured and
 * counted: eviction, `retainedBytes()` and the `bounded` flag a client reads
 * then treat a structural step exactly as they treat a captured object state.
 */

#include <climits>
#include <utility>

#include "ProjectJournal.h"

namespace lmms
{

void ProjectJournal::addJournalStructure( std::function<void()> undoAction,
	std::function<void()> redoAction, qint64 payloadBytes )
{
	if( !isJournalling() || !undoAction ) { return; }

	m_redoCheckPoints.clear();

	CheckPoint step = actionCheckPoint( std::move( undoAction ), std::move( redoAction ) );
	// The document the closure holds, measured by its caller (one measurement,
	// taken where the document was written). Clamped rather than trusted: the
	// per-step field is an int, and a caller reporting more than that has
	// already been refused a capture upstream - counting a wrapped negative
	// would CREDIT the budget instead of charging it.
	step.bytes = payloadBytes < 0 ? 0
		: static_cast<int>( payloadBytes > static_cast<qint64>( INT_MAX ) ? INT_MAX : payloadBytes );
	step.serial = ++m_stepSerial;
	m_undoCheckPoints.push( step );
	m_retainedBytes += step.bytes;
	trimUndoStack();
}

} // namespace lmms
