/*
 * VideoFramePump.cpp - video frames decoded on a worker thread and handed to the interface
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

#include "VideoFramePump.h"

#include "VideoDecoder.h"

namespace lmms
{

VideoFramePump::VideoFramePump(QObject* parent) :
	QObject(parent),
	m_worker([this] { run(); })
{
}

VideoFramePump::~VideoFramePump()
{
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_quit = true;
	}
	m_wake.notify_one();
	m_worker.join();
}

void VideoFramePump::request(const QString& path, double seconds, const QSize& box, int tag)
{
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_pending = Request{path, seconds, box, tag};
	}
	m_wake.notify_one();
}

void VideoFramePump::run()
{
	VideoDecoder decoder;
	QString openPath;
	for (;;)
	{
		Request next;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_wake.wait(lock, [this] { return m_quit || m_pending.has_value(); });
			if (m_quit) { return; }
			next = *m_pending;
			m_pending.reset();
		}
		if (next.path != openPath)
		{
			openPath = next.path;
			QString ignored;  // a file that does not open answers with null images; the commands report why
			if (openPath.isEmpty() || !decoder.open(openPath, &ignored)) { decoder.close(); }
		}
		emit frameReady(decoder.frameAt(next.seconds, next.box), next.seconds, next.tag);
	}
}

} // namespace lmms
