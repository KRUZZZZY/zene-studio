/*
 * TakeRecorder.cpp - an insert that records what passes it to a WAV file, off the audio thread
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

#include "TakeRecorder.h"

#include <algorithm>
#include <chrono>
#include <map>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "ConfigManager.h"
#include "SampleFrame.h"

namespace lmms
{

namespace
{

std::map<const Effect*, TakeRecorder*>& registry()
{
	static std::map<const Effect*, TakeRecorder*> recorders;
	return recorders;
}

} // namespace

TakeRecorder::TakeRecorder() :
	m_ring(RingFrames * 2, 0.f),
	m_batch(BatchFrames * 2, 0.f)
{
}

TakeRecorder::~TakeRecorder()
{
	stop();
	detach();
}

bool TakeRecorder::start(const QString& path, int sampleRate, QString* why)
{
	if (recording())
	{
		if (why != nullptr) { *why = QStringLiteral("a take is being recorded already: %1").arg(m_lastTake); }
		return false;
	}
	QDir().mkpath(QFileInfo(path).absolutePath());
	SF_INFO info{};
	info.samplerate = sampleRate;
	info.channels = 2;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
	m_file = sf_open(QFile::encodeName(path).constData(), SFM_WRITE, &info);
	if (m_file == nullptr)
	{
		if (why != nullptr) { *why = QStringLiteral("cannot write %1: %2").arg(path, QString::fromUtf8(sf_strerror(nullptr))); }
		return false;
	}
	m_write.store(0, std::memory_order_relaxed);
	m_read.store(0, std::memory_order_relaxed);
	m_written.store(0, std::memory_order_relaxed);
	m_dropped.store(0, std::memory_order_relaxed);
	m_stopping.store(false, std::memory_order_relaxed);
	m_lastTake = path;
	m_thread = std::thread([this] { writerLoop(); });
	m_recording.store(true, std::memory_order_release);
	return true;
}

void TakeRecorder::stop()
{
	if (!m_thread.joinable()) { return; }
	m_recording.store(false, std::memory_order_release);
	m_stopping.store(true, std::memory_order_release);
	m_thread.join();
	sf_close(m_file);
	m_file = nullptr;
}

void TakeRecorder::push(const SampleFrame* frames, f_cnt_t count) noexcept
{
	if (!recording()) { return; }
	const std::size_t write = m_write.load(std::memory_order_relaxed);
	const std::size_t read = m_read.load(std::memory_order_acquire);
	const std::size_t room = RingFrames - (write - read);
	const auto n = std::min(static_cast<std::size_t>(count), room);
	const auto* samples = reinterpret_cast<const float*>(frames);
	for (std::size_t i = 0; i < n; ++i)
	{
		const std::size_t slot = ((write + i) % RingFrames) * 2;
		m_ring[slot] = samples[2 * i];
		m_ring[slot + 1] = samples[2 * i + 1];
	}
	m_write.store(write + n, std::memory_order_release);
	if (n < count) { m_dropped.fetch_add(count - n, std::memory_order_relaxed); }
}

bool TakeRecorder::writeAvailable()
{
	const std::size_t read = m_read.load(std::memory_order_relaxed);
	const std::size_t write = m_write.load(std::memory_order_acquire);
	const std::size_t n = std::min(write - read, BatchFrames);
	if (n == 0) { return false; }
	for (std::size_t i = 0; i < n; ++i)
	{
		const std::size_t slot = ((read + i) % RingFrames) * 2;
		m_batch[2 * i] = m_ring[slot];
		m_batch[2 * i + 1] = m_ring[slot + 1];
	}
	sf_writef_float(m_file, m_batch.data(), static_cast<sf_count_t>(n));
	m_read.store(read + n, std::memory_order_release);
	m_written.fetch_add(n, std::memory_order_relaxed);
	return true;
}

void TakeRecorder::writerLoop()
{
	for (;;)
	{
		if (writeAvailable()) { continue; }
		// Nothing waiting: done once a stop was asked for (the ring is drained), else a short nap.
		if (m_stopping.load(std::memory_order_acquire)) { break; }
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	while (writeAvailable()) {}
}

void TakeRecorder::attach(const Effect* effect)
{
	detach();
	m_owner = effect;
	registry()[effect] = this;
}

void TakeRecorder::detach()
{
	if (m_owner == nullptr) { return; }
	registry().erase(m_owner);
	m_owner = nullptr;
}

TakeRecorder* TakeRecorder::of(const Effect* effect)
{
	const auto found = registry().find(effect);
	return found != registry().end() ? found->second : nullptr;
}

QString TakeRecorder::defaultTakePath()
{
	const QString folder = QDir(ConfigManager::inst()->userSamplesDir()).filePath(QStringLiteral("Recordings"));
	return QDir(folder).filePath(QStringLiteral("Take %1.wav").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh.mm.ss.zzz"))));
}

} // namespace lmms
