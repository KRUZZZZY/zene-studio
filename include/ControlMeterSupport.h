/*
 * ControlMeterSupport.h - the helpers the `meter.*` group and its file half share
 *                         (SPEC A11-A16).
 *
 * Feature row 24 of docs/FEATURE-LIST-0.3.0.md ("LUFS / loudness metering"). The
 * group is two halves that must not drift apart: the LIVE readout
 * (include/MasterLoudnessTap.h, registered by src/core/ControlCommandsMeter.cpp)
 * and the FILE measurement (src/core/ControlCommandsMeterFile.cpp, which reads a
 * rendered file through the merged `LoudnessReport`). Both publish the same five
 * numbers under the same names, both carry the same target, and both refuse the
 * same way - so those pieces live here, once, and not twice.
 *
 * WHY THE FILE HALF IS ITS OWN TRANSLATION UNIT. The command group's own file
 * crossed this fork's 500-line file ratchet the moment the libsndfile reader and
 * the JSON layer landed in it; the split is the same one the automation, warp,
 * rack, comp, vca and mastering groups made, and the seam is the real one
 * (README-shaped registration + live handlers | the document reader).
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

#ifndef LMMS_CONTROL_METER_SUPPORT_H
#define LMMS_CONTROL_METER_SUPPORT_H

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

//! The reply to one command, defined as a `struct` in ControlRegistry.h:70.
//! The class-key here MUST match that definition: msvc-x64 builds with /WX, so
//! C4099 ("type name first seen using 'class' now seen using 'struct'") is
//! promoted to C2220 and fails the job. Run 34870198514 reported it at
//! ControlRegistry.h:70 in the TUs that reach THIS header first
//! (ControlCommandsMeter.cpp:73/:74, ControlCommandsMeterFile.cpp:63/:66).
struct ControlResult;

namespace control
{

/*! A loudness or peak value as JSON: **null** when the meter has no measurement
 *  (its -infinity sentinel - silence, or a window that has not filled), the
 *  number otherwise.
 *
 *  This is the one rule both halves publish their readings under, and it is the
 *  same rule MasteringReport.cpp applies to the same values: a caller never has
 *  to compare against a magic float, and a silent master or file can never be
 *  mistaken for a measurement of `-70` or `-inf` - JSON has no infinity, and a
 *  plausible-looking number here would be a lie.
 */
LMMS_EXPORT QJsonValue meterReadingJson(float value);

/*! The five numbers, under the names the whole group uses: `integrated_lufs`,
 *  `momentary_lufs`, `short_term_lufs`, `short_term_max_lufs`, `true_peak_dbtp`.
 *  One function, so the live readout and a measured file cannot drift into two
 *  spellings of "integrated_lufs".
 */
LMMS_EXPORT QJsonObject meterReadingsJson(float integratedLufs, float momentaryLufs,
	float shortTermLufs, float shortTermMaxLufs, float truePeakDbtp);

/*! The target this release grades against, published rather than implied: EBU
 *  R 128's delivery guidance over the ITU-R BS.1770-4 measurement (EBU Tech
 *  3343) - -23.0 LUFS-I +/- 0.5 LU with a -1.0 dBTP ceiling - plus the streaming
 *  services' -14 LUFS-I figure as an informative CONVENTION (it has no published
 *  tolerance, which is why nothing grades against it). The numbers are read from
 *  LoudnessReport's own constants, never restated.
 */
LMMS_EXPORT QJsonObject meterTargetJson();

/*! The FILE half: measure the rendered file at \a path NOW, from its own bytes,
 *  with the same BS.1770-4 meter the render path feeds (`LoudnessReport`, which
 *  owns a `LufsMeter`).
 *
 *  Reads in bounded chunks and writes nothing: the file's sha256 is in the
 *  result, so a caller can see it is unchanged. 1 to 6 channels (mono and stereo
 *  exactly; 5.1 with BS.1770-4's channel weights); a file with more, a file with
 *  no frames, a path that is not absolute and a file libsndfile cannot read are
 *  each refused, typed, with the library's own words where it has them.
 *
 *  Returns the whole `meter.measure_file` payload on success (readings, verdict,
 *  target, file facts) - built here rather than in the command handler, because
 *  the pieces it shares with the live half live in this file.
 */
LMMS_EXPORT ControlResult meterMeasureFile(const QString& path);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_METER_SUPPORT_H
