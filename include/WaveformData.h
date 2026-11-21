/*
 * WaveformData.h - saved plug-in waveform/graph data, decoded defensively
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
/*! 0.5.0 L10 (Q050-486): the one loader for the base64 float arrays plug-ins save their drawn
 *  waveforms in (BitInvader, DynamicsProcessor, FreeBoy, MultitapEcho, Vibed, Watsyn, WaveShaper,
 *  Xpressive). A saved file is data to survive: decode() bounds the encoded text before it
 *  allocates anything, refuses text that is not strict base64, a byte count that is not a whole
 *  number of floats, a float count outside the caller's range, and any NaN or infinity, and hands
 *  back owning float storage with its validated element count.
 *
 *  Policy for malformed optional graph data: the graph keeps what it had (its previous or default
 *  shape) and the load reports one diagnostic through Song::collectError, the mechanism the error
 *  report box and project.open's `errors` already read. An absent or empty attribute is an older
 *  writer's file, not a malformed one: the graph is kept and nothing is reported. Main thread only
 *  (loadSettings); nothing here runs on the audio thread. */
#ifndef LMMS_WAVEFORM_DATA_H
#define LMMS_WAVEFORM_DATA_H

#include <memory>

#include <QString>

#include "lmms_export.h"

class QDomElement;

namespace lmms
{

class graphModel;

namespace waveform
{

enum class DecodeStatus
{
	Ok,
	Missing,      //!< absent or empty: an older writer's file, kept silently
	Oversized,    //!< more text than maxCount floats can encode; refused before decoding
	Invalid,      //!< not strict base64
	Misaligned,   //!< the byte count is not a whole number of floats
	WrongLength,  //!< a whole number of floats, but not a count the caller accepts
	NonFinite     //!< a NaN or an infinity among the values
};

struct Decoded
{
	DecodeStatus status = DecodeStatus::Missing;
	//! Owning, float-aligned storage of exactly `count` finite values; null unless status is Ok.
	std::unique_ptr<float[]> samples;
	//! Values decoded (Ok, WrongLength, NonFinite); 0 otherwise.
	int count = 0;
	//! Bytes decoded (Ok, Misaligned, WrongLength, NonFinite); 0 otherwise.
	int bytes = 0;

	bool ok() const { return status == DecodeStatus::Ok; }
};

//! The longest encoded text that can hold `maxCount` floats (padded base64, no whitespace).
LMMS_EXPORT int maxEncodedLength(int maxCount);

//! Decodes `encoded` into between `minCount` and `maxCount` finite floats (1 <= minCount <= maxCount).
LMMS_EXPORT Decoded decode(const QString& encoded, int minCount, int maxCount);

//! A short human reason for a refused payload, e.g. "12 values, expected 220".
LMMS_EXPORT QString describe(const Decoded& decoded, int minCount, int maxCount);

//! Reports a refused payload through Song::collectError (no-op for Ok and Missing).
LMMS_EXPORT void reportRefused(const QString& owner, const QString& attribute,
	const Decoded& decoded, int minCount, int maxCount);

//! Replaces `graph`'s samples with `attribute` of `element` when it holds exactly graph.length()
//! finite floats; otherwise keeps the graph and reports a malformed payload. Returns the status.
LMMS_EXPORT DecodeStatus loadGraph(const QDomElement& element, const QString& attribute,
	graphModel& graph, const QString& owner);

} // namespace waveform

} // namespace lmms

#endif // LMMS_WAVEFORM_DATA_H
