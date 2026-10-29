/*
 * SampleBuffer.cpp - container-class SampleBuffer
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "SampleBuffer.h"

#include <QDebug>
#include <QMessageBox>
#include <cstring>

#include "UnattendedRun.h"
#include "GuiApplication.h"
#include "PathUtil.h"
#include "SampleDecoder.h"

namespace lmms {

SampleBuffer::SampleBuffer(const SampleFrame* data, size_t numFrames, int sampleRate, int channels)
	: m_data(data, data + numFrames)
	, m_sampleRate(sampleRate)
	, m_channels(channels)
{
}

SampleBuffer::SampleBuffer(std::vector<SampleFrame> data, int sampleRate, const QString& audioFile,
	int channels)
	: m_data(std::move(data))
	, m_audioFile(audioFile)
	, m_sampleRate(sampleRate)
	, m_channels(channels)
{
}

void swap(SampleBuffer& first, SampleBuffer& second) noexcept
{
	using std::swap;
	swap(first.m_data, second.m_data);
	swap(first.m_audioFile, second.m_audioFile);
	swap(first.m_sampleRate, second.m_sampleRate);
	swap(first.m_channels, second.m_channels);
}

QString SampleBuffer::toBase64() const
{
	// TODO: Replace with non-Qt equivalent
	const auto data = reinterpret_cast<const char*>(m_data.data());
	const auto size = static_cast<int>(m_data.size() * sizeof(SampleFrame));
	const auto byteArray = QByteArray{data, size};
	return byteArray.toBase64();
}

auto SampleBuffer::engineSampleRate() -> sample_rate_t
{
	const AudioEngine* const engine = Engine::audioEngine();
	return engine != nullptr ? engine->outputSampleRate() : static_cast<sample_rate_t>(SUPPORTED_SAMPLERATES.front());
}

auto SampleBuffer::emptyBuffer() -> std::shared_ptr<const SampleBuffer>
{
	// The shared buffer takes its rate from the first caller. One from before Engine::init() (a
	// metronome sample that did not decode at command registration) gets a buffer of its own, so the
	// shared one still carries the engine's rate, as it always has.
	if (Engine::audioEngine() == nullptr) { return std::make_shared<const SampleBuffer>(); }
	static auto s_buffer = std::make_shared<const SampleBuffer>();
	return s_buffer;
}

std::shared_ptr<const SampleBuffer> SampleBuffer::fromFile(const QString& filePath)
{
	if (filePath.isEmpty()) { return SampleBuffer::emptyBuffer(); }

	const auto absolutePath = PathUtil::toAbsolute(filePath);
	const auto storedPath = PathUtil::toShortestRelative(filePath);

	auto result = SampleDecoder::decode(absolutePath);

	if (!result)
	{
		// TODO: Improve error handling. We dont always want to show a message box on failure when there is a GUI (e.g.
		// when loading the project), and this function also shouldn't be concerned with handling the error.
		// An agent instance (--control-socket) has nobody to answer the box
		// (task #625), so it is shown only when a human is really there.
		if (gui::getGUI() && !lmms::isUnattendedRun())
		{
			QMessageBox::warning(nullptr, QObject::tr("Failed to load sample"),
				QObject::tr("The sample may be corrupted or unsupported."));
		}
		else
		{
			qWarning() << QObject::tr(
				"Failed to load sample at path %1, the file may not exist, be corrupted, or is unsupported.")
							  .arg(absolutePath);
		}

		return SampleBuffer::emptyBuffer();
	}

	auto& [data, sampleRate] = *result;
	return std::make_shared<SampleBuffer>(std::move(data), sampleRate, storedPath);
}

std::shared_ptr<const SampleBuffer> SampleBuffer::fromBase64(const QString& str, int sampleRate,
	int channels)
{
	if (str.isEmpty()) { return SampleBuffer::emptyBuffer(); }

	// A rate that is not positive - "0", a negative one, or text a caller read
	// back as 0 - would build a buffer whose frames-per-tick is 0, and the clip
	// mapping divides by that rate (Engine::framesPerTick). An unusable rate
	// loads the way an absent one loads: the engine's own output rate - the
	// rule SampleClip::loadSettings applies to a project's stated sample_rate -
	// so the frames survive and the buffer always carries a rate the mapping can
	// divide by.
	if (sampleRate <= 0)
	{
		qWarning() << QObject::tr(
			"Base64 sample carries an invalid sample rate, loading it at the engine's output rate");
		sampleRate = engineSampleRate();
	}

	const auto bytes = QByteArray::fromBase64(str.toUtf8());

	if (bytes.size() % sizeof(SampleFrame) != 0)
	{
		// TODO: Improve error handling. We dont always want to show a message box on failure when there is a GUI (e.g.
		// when loading the project), and this function also shouldn't be concerned with handling the error.
		if (gui::getGUI() && !lmms::isUnattendedRun())
		{
			QMessageBox::warning(
				nullptr, QObject::tr("Failed to load sample"), QObject::tr("The sample size is invalid."));
		}
		else
		{
			qWarning() << QObject::tr("Failed to load Base64 sample, invalid size");
		}

		return SampleBuffer::emptyBuffer();
	}

	auto data = std::vector<SampleFrame>(bytes.size() / sizeof(SampleFrame));
	std::memcpy(reinterpret_cast<char*>(data.data()), bytes, bytes.size());
	return std::make_shared<SampleBuffer>(std::move(data), sampleRate, QString(), channels);
}

} // namespace lmms
