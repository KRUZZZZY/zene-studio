/*
 * VideoReference.h - the video a project is scored to: its file, where it starts, its rate and length
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

#ifndef LMMS_VIDEO_REFERENCE_H
#define LMMS_VIDEO_REFERENCE_H

#include <QObject>
#include <QString>
#include <QVector>

#include "LmmsTypes.h"
#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

//! What a project knows of its video: the file, the song tick its first frame plays at, and what the file said it was.
struct VideoAttachment
{
	QString path;        //!< absolute; empty when no video is attached
	tick_t offset = 0;   //!< the song tick the video's first frame is shown at
	double fps = 0.0;
	double duration = 0.0;  //!< seconds
	int width = 0;
	int height = 0;
	bool hasAudio = false;
	//! Stable per-project identity of the clip on the one video track ("video<n>"); empty on an attachment built by
	//! hand, VideoReference::setClips() assigns one. Saved as <clip id>.
	QString id;

	bool attached() const { return !path.isEmpty(); }
	bool operator==(const VideoAttachment&) const = default;
};

/*! 0.5.0 ED-054, extended by Q050-139/Q050-180.1: the project's video track - several independently placed clips on
 *  ONE track, each with its own file, song-tick start and stable id. A second video.attach still replaces the whole
 *  track (the legacy one-video behaviour); video.clip_add appends. The first clip mirrors the legacy <video> attributes so an
 *  older build sees that clip, and the new <clip> children carry every clip's stable id. Saved only while at least
 *  one clip is attached. The path is kept absolute. UI-thread state, written only through the video.* commands,
 *  each one undo step; the decoder, the video window and the arranger's video lane read it. VideoDecoder probes the
 *  file on attach and when project loading finds invalid timing metadata; the frame pump reads it on its worker
 *  thread. */
class LMMS_EXPORT VideoReference : public QObject, public ProjectSection
{
	Q_OBJECT
public:
	static VideoReference& instance();

	//! The clips on the video track, in the order they were added; empty when none.
	const QVector<VideoAttachment>& clips() const { return m_clips; }
	//! The first clip - the legacy one-video view. An empty attachment when none.
	const VideoAttachment& attachment() const;
	//! Replace the whole track with @a video (an empty one detaches); emits changed() when it differs.
	void set(const VideoAttachment& video);
	//! Replace every clip at once (the video.* commands, undo, load); clips without a file are dropped and a missing
	//! or repeated id becomes the next free "video<n>". Emits changed() when the list differs.
	void setClips(const QVector<VideoAttachment>& clips);

	//! The clip @a id, or nullptr.
	const VideoAttachment* clip(const QString& id) const;
	//! The clip whose span [offset, end) contains @a tick - the first in list order where clips overlap - or
	//! nullptr before, after or in a gap.
	const VideoAttachment* clipAt(tick_t tick) const;

	//! The clip's own time, in seconds, at song tick @a tick (negative before its first frame), through the tempo map.
	double secondsAt(tick_t tick, const VideoAttachment& clip) const;
	//! The song tick at which the clip has played @a seconds (the inverse of secondsAt).
	tick_t tickAt(double seconds, const VideoAttachment& clip) const;
	//! The song tick of the clip's end.
	tick_t endTick(const VideoAttachment& clip) const { return tickAt(clip.duration, clip); }

	//! The legacy single-video view: secondsAt() reads the clip at @a tick (else the first clip), tickAt() and
	//! endTick() the first clip.
	double secondsAt(tick_t tick) const;
	tick_t tickAt(double seconds) const;
	tick_t endTick() const { return endTick(attachment()); }

	//! @a seconds as HH:MM:SS:FF at @a fps (25 when unknown), "-" in front before the video's start.
	static QString timecode(double seconds, double fps);

	QString elementName() const override;
	bool shouldPersist() const override { return !m_clips.isEmpty(); }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override;

signals:
	void changed();

private:
	VideoReference() = default;

	QVector<VideoAttachment> m_clips;
};

} // namespace lmms

#endif // LMMS_VIDEO_REFERENCE_H
