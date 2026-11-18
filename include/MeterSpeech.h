/*
 * MeterSpeech.h - spoken meter readings and the input/master peak watch
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_GUI_METER_SPEECH_H
#define LMMS_GUI_METER_SPEECH_H

#include <QString>

#include "lmms_export.h"

class QObject;

namespace lmms::gui::meterspeech
{

/*! 0.5.0 Q050-100: the threshold watcher half of the spoken meters. It runs on the GUI
 *  thread and reads only published peaks (inputmeter::watch, MasterLoudnessTap::snapshot),
 *  so the audio callback does no Qt work and never waits for speech.
 *
 *  One announcement per rising crossing: each source latches when its peak reaches the
 *  threshold and stays quiet until it falls below it again. The threshold and the enabled
 *  flag are the meter.set_peak_watch preference.
 */
LMMS_EXPORT void start(QObject* parent);

/*! Evaluate one controlled pair of peaks, in linear sample units. This is the seam the
 *  tests drive; poll() calls it with the live input and master readings. */
LMMS_EXPORT void observe(float inputPeak, float masterPeak);

//! Forget both latches (a threshold change, or a test starting a new case).
LMMS_EXPORT void reset();

//! Read the two published sources now and evaluate them (the timer calls this).
LMMS_EXPORT void poll();

//! The last message this watcher produced, for tests and diagnostics.
LMMS_EXPORT QString lastMessage();

} // namespace lmms::gui::meterspeech

#endif // LMMS_GUI_METER_SPEECH_H
