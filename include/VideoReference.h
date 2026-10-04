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

	bool attached() const { return !path.isEmpty(); }
	bool operator==(const VideoAttachment&) const = default;
};

/*! 0.5.0 ED-054: the project's video reference - ONE video per project in 0.5.0 (a second attach replaces the
 *  first). Saved as <video> only while one is attached (a project that never had one re-saves byte for byte; a
 *  project without the element loads with none). The path is kept absolute, as given to video.attach. UI-thread
 *  state, written only through the video.* commands, each one undo step; the decoder, the video window and the
 *  arranger's video lane read it. The file itself is never read here: VideoDecoder does that, on its own thread. */
class LMMS_EXPORT VideoReference : public QObject, public ProjectSection
{
	Q_OBJECT
public:
	static VideoReference& instance();

	const VideoAttachment& attachment() const { return m_video; }
	//! Replace the attachment (an empty one detaches); emits changed() when it differs.
	void set(const VideoAttachment& video);

	//! The video's own time, in seconds, at song tick @a tick (negative before its first frame), through the tempo map.
	double secondsAt(tick_t tick) const;
	//! The song tick at which the video has played @a seconds (the inverse of secondsAt).
	tick_t tickAt(double seconds) const;
	//! The song tick of the video's end.
	tick_t endTick() const { return tickAt(m_video.duration); }

	//! @a seconds as HH:MM:SS:FF at @a fps (25 when unknown), "-" in front before the video's start.
	static QString timecode(double seconds, double fps);

	QString elementName() const override;
	bool shouldPersist() const override { return m_video.attached(); }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override;

signals:
	void changed();

private:
	VideoReference() = default;

	VideoAttachment m_video;
};

} // namespace lmms

#endif // LMMS_VIDEO_REFERENCE_H
