/*
 * ControlDetectSupport.h - the helpers the `detect.*` command group shares
 *                          between its read half (ControlCommandsDetect.cpp) and
 *                          its one writing verb (ControlCommandsDetectApply.cpp).
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

#ifndef LMMS_CONTROL_DETECT_SUPPORT_H
#define LMMS_CONTROL_DETECT_SUPPORT_H

#include <QJsonObject>
#include <QString>

#include "ImportDetectionDsp.h"
#include "LmmsTypes.h"
#include "ControlVocabulary.h"

namespace lmms
{

class ImportDetectionResult;
class ProjectKey;
class Song;
class TempoMap;
struct ControlResult;

namespace control
{

/*! The tick an applied tempo lands on: tick 0, the tempo map's own total
 *  override (include/TempoMap.h, decision 2) - every tick from the start of the
 *  timeline answers the detected tempo, and nothing before tick 0 exists. */
constexpr tick_t DetectAppliedEventTick = 0;

//! The one-line accuracy statement every read of this group carries.
QString detectAccuracyNote();
//! The declared bounds, the applied-tempo rounding rule and the event's tick.
QJsonObject detectBoundsJson();
//! The two method names plus the accuracy sentence.
QJsonObject detectMethodJson();
//! One tempo estimate as the wire reports it.
QJsonObject detectTempoJson(const lmms::detection::TempoEstimate& tempo);
//! The key half of an analysis as the wire reports it.
QJsonObject detectKeyJson(const lmms::ImportDetectionResult& analysis);
//! The whole analysis.
QJsonObject detectAnalysisJson(const lmms::ImportDetectionResult& analysis);
//! The project's own key field, as it stands now.
QJsonObject detectProjectKeyJson(const lmms::ProjectKey& key);
//! The optional `max_seconds` bound: absent means the default, out of range is
//! REFUSED rather than clamped.
bool readDetectBound(const QJsonObject& args, double* out, ControlResult* error);
//! The detected tempo as the map can hold it, or -1 when it is outside the
//! map's own bounds (a clamp would be a different tempo, so the caller refuses).
int roundedTempoForMap(double bpm);
//! The recorded inverse of one detect.apply: BOTH halves captured before the
//! write and put back by the step, so one Ctrl+Z is one detection undone.
void recordDetectApplyRestore(const lmms::TempoMap& before, const QString& keyBefore);
//! detect.get_state's payload: what the project holds now.
QJsonObject detectStateJson(lmms::Song* song);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_DETECT_SUPPORT_H
