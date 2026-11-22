/*
 * MasteringSequence.h - the ordered release sequence of a mastering document
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

#ifndef LMMS_MASTERING_SEQUENCE_H
#define LMMS_MASTERING_SEQUENCE_H

#include <QString>
#include <QVector>

#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

/*! One song of a mastering release: an audio file, the processing it is mastered with, its
 *  fades and the silence that follows it.
 *
 *  `processing` is the NAME of one of the engine's own mastering candidates (MasteringJob::
 *  defaultCandidates, e.g. "streaming-14" or "ebu-r128"), or "none" for a song played as it
 *  is. The candidate's numbers are NOT copied into the song: the name is the state, and the
 *  chain settings are derived from it when a render runs, so a saved song cannot carry numbers
 *  that disagree with the candidate set it names.
 *
 *  The gap is the silence AFTER this song and before the next one: it starts when the song's
 *  fade-out has ended and runs to the next song's first sample, which is where that song's
 *  fade-in starts. The last song's gap is kept (it is the silence after the release) but is
 *  never inserted by a render, and is reported as such. */
struct MasteringSong
{
	int id = 0;             //!< stable "song-<id>" on the wire, never a position
	QString source;         //!< absolute path of the audio file
	QString title;          //!< a label for the release page; may be empty
	QString processing;     //!< a MasteringJob candidate name, or "none"
	int fadeInMs = 0;
	int fadeOutMs = 0;
	int gapMs = 0;

	bool operator==(const MasteringSong&) const = default;
};

/*! 0.5.0 L12 (Q050-317.1): the release sequence of a mastering document - songs in release
 *  order, each with its own processing, fades and the gap that follows it.
 *
 *  Saved as <mastering-sequence> only when it holds a song, so a project that never used one
 *  re-saves byte for byte, and an absent element loads as an EMPTY sequence (the reset-on-
 *  absence rule every ProjectSection follows). UI-thread state, edited through the
 *  mastering.song_* commands, one undo step each; the audio thread never reads it. */
class LMMS_EXPORT MasteringSequence : public ProjectSection
{
public:
	static constexpr int MaxSongs = 512;
	//! The longest fade, in milliseconds (ten minutes - longer than any song the page expects).
	static constexpr int MaxFadeMs = 600000;
	//! The longest gap, in milliseconds.
	static constexpr int MaxGapMs = 600000;

	//! The state a song is in when a field is not given: no processing, no fades, no gap.
	static constexpr const char* NoProcessing = "none";

	static MasteringSequence& instance();

	//! The songs in release order.
	const QVector<MasteringSong>& songs() const { return m_songs; }
	const MasteringSong* find(int id) const;
	//! The song's position in release order, or -1.
	int indexOf(int id) const;
	//! Append @a song (its `id` is ignored and one assigned); its id, or -1 when full or nameless.
	int add(const MasteringSong& song);
	//! Replace the song with @a id (the id and the order are kept); false when it does not exist.
	bool replace(int id, const MasteringSong& song);
	bool remove(int id);
	//! Move the song with @a id to position @a to (0-based, clamped to the sequence); false when it does not exist.
	bool move(int id, int to);
	//! Put @a songs back as they were (the undo step's inverse and redo); ids are re-derived from the state.
	void restore(const QVector<MasteringSong>& songs);

	QString elementName() const override { return QStringLiteral("mastering-sequence"); }
	bool shouldPersist() const override { return !m_songs.isEmpty(); }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override;

private:
	MasteringSequence() = default;
	QVector<MasteringSong> m_songs;
	int m_nextId = 1;
};

} // namespace lmms

#endif // LMMS_MASTERING_SEQUENCE_H
