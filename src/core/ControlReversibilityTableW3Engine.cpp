/*
 * ControlReversibilityTableW3Engine.cpp - the 0.5.0 W3 engine lane's rows of the SPEC A16 table
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
#include "ControlReversibility.h"

namespace lmms
{
namespace control
{

namespace
{

using RC = ReversibilityClass;

//! A literal row: R(id, class, reversible, reason, mechanism, fallback).
#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev, nullptr }

const ReversibilityRow kW3EngineRows[] = {
	R("automation.latency_compensation", RC::TrueInverse, true,
		"it switches latency-compensated automation on or off for the project (one flag, saved with the project)",
		"action checkpoint: the recorded undo step puts the previous switch back and recomputes the clips' lags", ""),
	R("mixer.post_inserts", RC::NotMutating, false, "it reads a channel's post-fader devices. Nothing is written", "no write", ""),
	R("chain.receive", RC::TrueInverse, true, "it adds a sidechain send (unless one was there) and a Receiver device to a "
		"channel's chain",
		"action checkpoint: the recorded undo step removes the device and the send it made; its redo makes both again", ""),
	R("clip.pre_fade", RC::TrueInverse, true, "it sets an audio clip's pre-fades (two clip edits)",
		"ProjectJournal (Clip checkpoint): Clip::saveClipEdits writes them onto the clip's element", ""),
};

#undef R

constexpr int kW3EngineRowCount = static_cast<int>(sizeof(kW3EngineRows) / sizeof(kW3EngineRows[0]));

} // namespace

const ReversibilityRow* reversibilityW3EngineRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kW3EngineRowCount; }
	return kW3EngineRows;
}

} // namespace control
} // namespace lmms
