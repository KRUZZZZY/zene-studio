/*
 * WaveformData.cpp - saved plug-in waveform/graph data, decoded defensively
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

#include "WaveformData.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <QByteArray>
#include <QCoreApplication>
#include <QDomElement>

#include "Engine.h"
#include "Graph.h"
#include "Song.h"

namespace lmms::waveform
{

int maxEncodedLength(int maxCount)
{
	const qint64 bytes = static_cast<qint64>(std::max(maxCount, 0)) * static_cast<qint64>(sizeof(float));
	const qint64 encoded = 4 * ((bytes + 2) / 3);
	return static_cast<int>(std::min<qint64>(encoded, std::numeric_limits<int>::max()));
}

Decoded decode(const QString& encoded, int minCount, int maxCount)
{
	Decoded result;
	if (encoded.isEmpty()) { return result; }
	// The bound comes first: nothing below allocates for text longer than the largest payload.
	if (encoded.size() > maxEncodedLength(maxCount))
	{
		result.status = DecodeStatus::Oversized;
		return result;
	}
	const auto decodedText = QByteArray::fromBase64Encoding(encoded.toLatin1(),
		QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
	// toLatin1() maps any character outside Latin-1 to '?', which the strict decoder refuses.
	if (!decodedText)
	{
		result.status = DecodeStatus::Invalid;
		return result;
	}
	const QByteArray& bytes = decodedText.decoded;
	result.bytes = static_cast<int>(bytes.size());
	if (bytes.size() % static_cast<int>(sizeof(float)) != 0)
	{
		result.status = DecodeStatus::Misaligned;
		return result;
	}
	result.count = static_cast<int>(bytes.size() / static_cast<int>(sizeof(float)));
	if (result.count < std::max(minCount, 1) || result.count > maxCount)
	{
		result.status = DecodeStatus::WrongLength;
		return result;
	}
	auto samples = std::make_unique<float[]>(result.count);
	std::memcpy(samples.get(), bytes.constData(), static_cast<std::size_t>(bytes.size()));
	if (!std::all_of(samples.get(), samples.get() + result.count, [](float value) { return std::isfinite(value); }))
	{
		result.status = DecodeStatus::NonFinite;
		return result;
	}
	result.samples = std::move(samples);
	result.status = DecodeStatus::Ok;
	return result;
}

QString describe(const Decoded& decoded, int minCount, int maxCount)
{
	const QString expected = minCount == maxCount
		? QString::number(maxCount)
		: QStringLiteral("%1 to %2").arg(minCount).arg(maxCount);
	switch (decoded.status)
	{
	case DecodeStatus::Ok: return QStringLiteral("loaded");
	case DecodeStatus::Missing: return QStringLiteral("absent");
	case DecodeStatus::Oversized: return QStringLiteral("larger than %1 values").arg(maxCount);
	case DecodeStatus::Invalid: return QStringLiteral("not valid base64");
	case DecodeStatus::Misaligned:
		return QStringLiteral("%1 bytes, not a whole number of values").arg(decoded.bytes);
	case DecodeStatus::WrongLength:
		return QStringLiteral("%1 values, expected %2").arg(decoded.count).arg(expected);
	case DecodeStatus::NonFinite: return QStringLiteral("contains NaN or infinite values");
	}
	return {};
}

void reportRefused(const QString& owner, const QString& attribute, const Decoded& decoded, int minCount, int maxCount)
{
	if (decoded.ok() || decoded.status == DecodeStatus::Missing) { return; }
	Song* song = Engine::getSong();
	if (song == nullptr) { return; }
	song->collectError(QCoreApplication::translate("WaveformData",
		"%1: the saved waveform \"%2\" was not loaded (%3); the current or default waveform is kept")
		.arg(owner, attribute, describe(decoded, minCount, maxCount)));
}

DecodeStatus loadGraph(const QDomElement& element, const QString& attribute, graphModel& graph, const QString& owner)
{
	const int expected = graph.length();
	const Decoded decoded = decode(element.attribute(attribute), expected, expected);
	if (decoded.ok()) { graph.setSamples(decoded.samples.get(), decoded.count); }
	else { reportRefused(owner, attribute, decoded, expected, expected); }
	return decoded.status;
}

} // namespace lmms::waveform
