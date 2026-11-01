/*
 * ControlStemSource.h - what stem.job_start reads of its source, and the decode its worker runs (Q050-010)
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

#ifndef LMMS_CONTROL_STEM_SOURCE_H
#define LMMS_CONTROL_STEM_SOURCE_H

#include <atomic>
#include <memory>

#include <QString>

namespace lmms
{

class SampleBuffer;

namespace control
{

/*! The length and rate of @a source, from its header where libsndfile can open it, so the caller decodes nothing.
 *  A file it cannot open (a .ds drum sample, garbage) gets the decoders' verdict here instead, so an undecodable
 *  file is still refused typed; such a file is tiny or fails fast. False with @a error for a refusal. */
bool stemSourceLength(const QString& source, qint64* frames, int* rate, QString* error);
//! @a frames at @a rate, in frames at the model's rate.
int stemFramesAtModelRate(qint64 frames, int rate);
//! All a job does to its source before separating, for the worker: decode it, then resample it to the model's rate.
std::shared_ptr<const SampleBuffer> stemDecodeAtModelRate(const QString& source, const std::atomic<bool>& cancel, QString* error);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_STEM_SOURCE_H
