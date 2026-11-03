/*
 * AudioDeviceDiagnostics.h - the audio device's negotiated timing and xruns, as mixer.cpu_load reports them
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
 *
 */

#ifndef LMMS_AUDIO_DEVICE_DIAGNOSTICS_H
#define LMMS_AUDIO_DEVICE_DIAGNOSTICS_H

#include <QJsonObject>

#include "lmms_export.h"

namespace lmms
{

class AudioEngine;

/*! Q050-074.2 (WF-104, R-performance-78): mixer.cpu_load's 'audio_device' - the backend, the engine's
 *  own rate and period, and what the device negotiated (rate, period and buffer frames, input/output
 *  latency with the basis of each) beside the device's own xrun count. Every device reading is
 *  {available, value, unit}; a value the backend does not report is available:false with no value,
 *  never a zero. The engine's late periods stay in mixer.cpu_load's 'late_periods': a late period is
 *  the engine missing its deadline, an xrun is the device running dry - two different facts.
 *  Main thread (it reads the device's own API, never on the audio thread). */
LMMS_EXPORT QJsonObject audioDeviceReport(AudioEngine* engine);

} // namespace lmms

#endif // LMMS_AUDIO_DEVICE_DIAGNOSTICS_H
