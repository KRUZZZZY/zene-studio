/*
 * StemTrackBuilder.cpp - turn separated stems into SampleTracks
 *
 * Copyright (c) 2026 LMMS Developers
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

#include "StemSeparation/StemTrackBuilder.h"

#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "TimePos.h"
#include "TrackContainer.h"

namespace lmms
{

namespace
{

void setError(QString* error, const QString& text)
{
	if (error != nullptr)
	{
		*error = text;
	}
}

} // namespace




QList<SampleTrack*> StemTrackBuilder::createStemTracks(TrackContainer* container,
	const StemSet& stems,
	const TimePos& position,
	const QString& sourceName,
	QString* error)
{
	QList<SampleTrack*> tracks;

	if (container == nullptr)
	{
		setError(error, QStringLiteral("No track container"));
		return tracks;
	}

	// Validate everything first: the operation is all-or-nothing, so a broken
	// job result never leaves half a set of stem tracks in the project.
	for (int i = 0; i < NumStems; ++i)
	{
		const auto& buffer = stems[static_cast<size_t>(i)];
		if (buffer == nullptr || buffer->empty())
		{
			setError(error, QStringLiteral("Stem '%1' has no audio")
				.arg(QString::fromLatin1(stemName(static_cast<Stem>(i)))));
			return tracks;
		}
	}

	for (int i = 0; i < NumStems; ++i)
	{
		const auto stem = static_cast<Stem>(i);
		const auto& buffer = stems[static_cast<size_t>(i)];

		auto* track = new SampleTrack(container);
		const auto stemLabel = QString::fromLatin1(stemName(stem));
		track->setName(sourceName.isEmpty()
			? QStringLiteral("Stem - %1").arg(stemLabel)
			: QStringLiteral("%1 - %2").arg(sourceName, stemLabel));
		// The Track constructor already registers the track with its container
		// (Track.cpp: m_trackContainer->addTrack(this)); calling addTrack() here
		// would list it twice and double-free it at teardown.

		auto* clip = new SampleClip(track);
		// The Clip constructor already registers the clip with its track
		// (Clip.cpp: getTrack()->addClip(this)); a second addClip() would
		// list it twice and double-free it at teardown.
		// Shares the buffer, no copy of the audio data.
		clip->setSampleBuffer(buffer);
		clip->movePosition(position);

		tracks.append(track);
	}

	return tracks;
}

} // namespace lmms
