/*
 * ControlRoom.h - the monitor section: speakers, dim, mono and cut on what you hear, never on a render
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
#ifndef LMMS_CONTROL_ROOM_H
#define LMMS_CONTROL_ROOM_H

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class SampleFrame;

/*! 0.5.0 L09 (MX-038, MX-039): the control room - what reaches the speakers, after the master: three speaker sets
 *  (A, B, C) each with its calibration trim in dB, dim (-20 dB unless set), a mono check and cut. It works on the
 *  live output only: an export skips it (Song::startExport/stopExport), so nothing here can reach a render, and
 *  nothing here is saved with a project. The trims and the chosen speaker are preferences (config controlroom/...);
 *  dim, mono and cut start off every session. apply() runs on the audio thread: relaxed atomics, no allocation,
 *  skipped wholesale while neutral. */
namespace controlroom
{

struct State
{
	int speaker = 0;  //!< 0 = A, 1 = B, 2 = C
	float trimsDb[3] = {0.f, 0.f, 0.f};
	bool dim = false;
	float dimDb = -20.f;
	bool mono = false;
	bool cut = false;
};

LMMS_EXPORT State state();
//! Apply @a state (the trims and the speaker are written to the config file).
LMMS_EXPORT void setState(const State& state);
//! Read the preferences once at start-up.
LMMS_EXPORT void loadPreferences();
//! Export hooks: while exporting, apply() leaves the audio alone.
LMMS_EXPORT void setExporting(bool exporting);
//! The audio thread: the control room on the period about to be heard.
LMMS_EXPORT void apply(SampleFrame* buffer, f_cnt_t frames);

} // namespace controlroom

} // namespace lmms

#endif // LMMS_CONTROL_ROOM_H
