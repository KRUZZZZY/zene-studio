/*
 * VideoFramePump.h - video frames decoded on a worker thread and handed to the interface
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

#ifndef LMMS_VIDEO_FRAME_PUMP_H
#define LMMS_VIDEO_FRAME_PUMP_H

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 ED-054: one VideoDecoder on its own worker thread - never the audio thread, never the interface's.
 *
 *  The interface asks for "the frame of file F at time T, fitting box B" with request(); the worker opens F when
 *  it differs from the file it has, decodes, scales and hands the image back through frameReady(). The handoff
 *  is the interface thread's only: the signal is emitted on the worker and Qt queues it to receivers on the
 *  interface thread, which alone touch the image from then on (QImage is implicitly shared; the worker keeps no
 *  reference). A request is a one-slot mailbox under a mutex the worker and the interface share - the audio
 *  thread never takes it: a newer request replaces one the worker has not started, so a caller that asks again
 *  before the last answer arrived skips the frames in between rather than queueing them (dropping, not lagging).
 *  The video window asks only once the previous frame has arrived, so at most one frame is ever in flight. */
class LMMS_EXPORT VideoFramePump : public QObject
{
	Q_OBJECT
public:
	explicit VideoFramePump(QObject* parent = nullptr);
	//! Stops the worker and waits for it (a decode in progress finishes first).
	~VideoFramePump() override;

	//! Ask for the frame of @a path at @a seconds fitting @a box; @a tag comes back with it. An empty @a path closes
	//! the file (the answer is a null image).
	void request(const QString& path, double seconds, const QSize& box, int tag = 0);

signals:
	//! The answer to a request: a null @a image when the file did not open or nothing decoded. Emitted on the worker.
	void frameReady(const QImage& image, double seconds, int tag);

private:
	struct Request
	{
		QString path;
		double seconds = 0.0;
		QSize box;
		int tag = 0;
	};

	void run();

	std::mutex m_mutex;
	std::condition_variable m_wake;
	std::optional<Request> m_pending;
	bool m_quit = false;
	std::thread m_worker;  //!< last: started once every member it reads exists
};

} // namespace lmms

#endif // LMMS_VIDEO_FRAME_PUMP_H
