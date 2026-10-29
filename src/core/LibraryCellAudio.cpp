/*
 * LibraryCellAudio.cpp - the audio a launcher cell's source file holds, saved into the library
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

#include "LibraryCellAudio.h"

#include <algorithm>
#include <new>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <sndfile.h>

#include "SampleDecoder.h"
#include "SampleFrame.h"

namespace lmms::libraryaudio
{

namespace
{

//! Frames one read/convert/write step holds. Bounded and independent of the file's length.
constexpr sf_count_t BlockFrames = 8192;

//! The test seam env names (see the header). Read per call, compiled only into a test build.
constexpr const char* MaxDecodedBytesEnv = "ZENE_TEST_LIBRARY_SAVE_MAX_DECODED_BYTES";
constexpr const char* AllocFaultEnv = "ZENE_TEST_LIBRARY_SAVE_ALLOC_FAULT";
constexpr const char* WriteFaultEnv = "ZENE_TEST_LIBRARY_SAVE_WRITE_FAULT";

qint64 configuredBudget()
{
#ifdef LMMS_TESTING
	bool ok = false;
	const qint64 lowered = qEnvironmentVariable(MaxDecodedBytesEnv).toLongLong(&ok);
	if (ok && lowered > 0) { return lowered; }
#endif
	return CellAudio::MaxDecodedBytes;
}

bool allocFault()
{
#ifdef LMMS_TESTING
	return !qEnvironmentVariableIsEmpty(AllocFaultEnv);
#else
	return false;
#endif
}

//! "short-write" makes one block report fewer frames written; "before-commit" lets the whole
//! WAV be written and closed but does not publish it. Both are deterministic on every platform.
bool shortWriteFault()
{
#ifdef LMMS_TESTING
	return qEnvironmentVariable(WriteFaultEnv) == QLatin1String("short-write");
#else
	return false;
#endif
}

bool beforeCommitFault()
{
#ifdef LMMS_TESTING
	return qEnvironmentVariable(WriteFaultEnv) == QLatin1String("before-commit");
#else
	return false;
#endif
}

//! Whether @a frames stereo float frames fit the decoded-PCM budget (frames x 2 x 4 bytes).
bool fitsBudget(qint64 frames, qint64 budget)
{
	return frames >= 0 && frames <= budget / 8;
}

QString overBudgetMessage(const QString& source, qint64 frames, int sampleRate, qint64 budget)
{
	return QStringLiteral("'%1' decodes to %2 frames (%3 MiB, about %4 minutes at %5 Hz), more than the %6 MiB of "
		"decoded audio one library save may hold; nothing was written")
		.arg(source).arg(frames)
		.arg(static_cast<double>(frames) * 8.0 / (1024.0 * 1024.0), 0, 'f', 1)
		.arg(static_cast<double>(frames) / (60.0 * std::max(1, sampleRate)), 0, 'f', 1)
		.arg(sampleRate)
		.arg(static_cast<double>(budget) / (1024.0 * 1024.0), 0, 'f', 0);
}

//! A stereo float WAV written through a QSaveFile: libsndfile encodes into the temporary Qt
//! created, and the destination is replaced only by a complete, closed file's commit().
class WavWriter
{
public:
	WavWriter(const QString& path, int sampleRate) : m_path(path), m_sampleRate(sampleRate), m_file(path) {}

	~WavWriter() { discard(); }

	WavWriter(const WavWriter&) = delete;
	WavWriter& operator=(const WavWriter&) = delete;

	bool open(QString* error)
	{
		if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
		{
			*error = QStringLiteral("cannot create the directory for %1").arg(m_path);
			return false;
		}
		if (!m_file.open(QIODevice::WriteOnly))
		{
			*error = QStringLiteral("cannot write %1: %2").arg(m_path, m_file.errorString());
			return false;
		}
		SF_INFO info{};
		info.samplerate = m_sampleRate;
		info.channels = 2;
		info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
		m_snd = sf_open_fd(m_file.handle(), SFM_WRITE, &info, SF_FALSE);
		if (m_snd == nullptr)
		{
			m_file.cancelWriting();
			*error = QStringLiteral("cannot write %1: %2").arg(m_path, QString::fromLocal8Bit(sf_strerror(nullptr)));
			return false;
		}
		return true;
	}

	//! @a data is interleaved stereo float PCM; false (with @a error) when the write failed.
	bool write(const float* data, sf_count_t frames, QString* error)
	{
		bool written = false;
		if (frames > 0 && shortWriteFault())
		{
			// The seam: one frame short, so the caller's short-write path runs on every platform.
			const sf_count_t wanted = frames - 1;
			sf_writef_float(m_snd, data, wanted);
			written = false;
		}
		else
		{
			written = frames == 0 || sf_writef_float(m_snd, data, frames) == frames;
		}
		if (!written)
		{
			*error = QStringLiteral("writing %1 failed").arg(m_path);
			return false;
		}
		m_frames += frames;
		return true;
	}

	//! Closes the encoder and publishes the file; false (with @a error) leaves the target as it was.
	bool finish(qint64* frames, QString* error)
	{
		const bool closed = sf_close(m_snd) == 0;
		m_snd = nullptr;
		if (!closed || beforeCommitFault())
		{
			m_file.cancelWriting();
			*error = closed ? QStringLiteral("cannot replace %1: the write was not completed").arg(m_path)
				: QStringLiteral("writing %1 failed").arg(m_path);
			return false;
		}
		if (!m_file.commit())
		{
			*error = QStringLiteral("cannot replace %1: %2").arg(m_path, m_file.errorString());
			return false;
		}
		*frames = m_frames;
		return true;
	}

	//! Cancels an unfinished write: the destination keeps the bytes it had.
	void discard()
	{
		if (m_snd != nullptr)
		{
			sf_close(m_snd);
			m_snd = nullptr;
		}
		if (m_file.isOpen()) { m_file.cancelWriting(); }
	}

private:
	QString m_path;
	int m_sampleRate = 0;
	QSaveFile m_file;
	SNDFILE* m_snd = nullptr;
	qint64 m_frames = 0;
};

//! Writes @a data (interleaved stereo float frames, @a frames of them) as the library's WAV.
bool writeFrames(const QString& target, const float* data, sf_count_t frames, int sampleRate,
	CellAudio::Result* result, QString* error)
{
	WavWriter writer(target, sampleRate);
	if (!writer.open(error)) { return false; }
	sf_count_t at = 0;
	while (at < frames)
	{
		const sf_count_t n = std::min<sf_count_t>(BlockFrames, frames - at);
		if (!writer.write(data + static_cast<std::size_t>(at) * 2, n, error)) { return false; }
		at += n;
	}
	qint64 published = 0;
	if (!writer.finish(&published, error)) { return false; }
	result->ok = true;
	result->frames = published;
	result->sampleRate = sampleRate;
	result->bytes = QFileInfo(target).size();
	return true;
}

//! The libsndfile path: the source's header gives the decoded size up front, and the audio is
//! streamed block by block, so neither the whole source nor a second interleaved copy is ever
//! held. Every failure cancels the write, leaving the destination as it was.
auto saveThroughSndfile(const QString& source, const QString& target, SNDFILE* snd, int channels,
	int sampleRate, qint64 declaredFrames, CellAudio::Result* result) -> bool
{
	if (channels < 1 || sampleRate <= 0 || channels > 64)
	{
		result->error = QStringLiteral("'%1' has no usable channels or sample rate").arg(source);
		return false;
	}
	const qint64 budget = CellAudio::decodedByteBudget();
	if (declaredFrames > 0 && !fitsBudget(declaredFrames, budget))
	{
		result->error = overBudgetMessage(source, declaredFrames, sampleRate, budget);
		return false;
	}
	if (allocFault())
	{
		result->error = QStringLiteral("ran out of memory decoding '%1': nothing was written").arg(source);
		return false;
	}

	WavWriter writer(target, sampleRate);
	QString error;
	if (!writer.open(&error))
	{
		result->error = error;
		return false;
	}
	// libsndfile hands back INTERLEAVED floats (channels per frame), so one block-sized read
	// buffer and one block-sized stereo output scratch are the whole working set - never a copy
	// of the file. A mono source is upmixed the way SampleDecoder upmixes it (both channels equal);
	// a source with more than two channels keeps its first two, as the decoder does.
	std::vector<float> readBuffer;
	std::vector<float> interleaved;
	try
	{
		readBuffer.resize(static_cast<std::size_t>(BlockFrames) * static_cast<std::size_t>(channels));
		interleaved.resize(static_cast<std::size_t>(BlockFrames) * 2);
	}
	catch (const std::bad_alloc&)
	{
		result->error = QStringLiteral("ran out of memory decoding '%1': nothing was written").arg(source);
		return false;
	}
	qint64 done = 0;
	while (true)
	{
		const sf_count_t got = sf_readf_float(snd, readBuffer.data(), BlockFrames);
		if (got < 0)
		{
			result->error = QStringLiteral("'%1' could not be decoded: %2")
				.arg(source, QString::fromLocal8Bit(sf_strerror(snd)));
			return false;
		}
		if (got == 0) { break; }
		if (!fitsBudget(done + got, budget))
		{
			result->error = overBudgetMessage(source, done + got, sampleRate, budget);
			return false;
		}
		for (sf_count_t f = 0; f < got; ++f)
		{
			const auto frame = static_cast<std::size_t>(f) * static_cast<std::size_t>(channels);
			interleaved[static_cast<std::size_t>(f) * 2] = readBuffer[frame];
			interleaved[static_cast<std::size_t>(f) * 2 + 1] = readBuffer[frame + (channels > 1 ? 1 : 0)];
		}
		if (!writer.write(interleaved.data(), got, &error))
		{
			result->error = error;
			return false;
		}
		done += got;
	}
	if (declaredFrames > 0 && done != declaredFrames)
	{
		// A file that ends early would be saved truncated; refused rather than published short.
		result->error = QStringLiteral("'%1' ended after %2 of its %3 frames; nothing was written")
			.arg(source).arg(done).arg(declaredFrames);
		return false;
	}
	qint64 published = 0;
	if (!writer.finish(&published, &error))
	{
		result->error = error;
		return false;
	}
	result->ok = true;
	result->frames = published;
	result->sampleRate = sampleRate;
	result->bytes = QFileInfo(target).size();
	return true;
}

/*! The built-in decoder path (Ogg Vorbis where libsndfile was built without it, DrumSynth):
 *  the file's own length is read from a one-second bounded decode BEFORE the full decode, so a
 *  source past the decoded-size bound is refused without ever being decoded whole. */
auto saveThroughSampleDecoder(const QString& source, const QString& target, CellAudio::Result* result) -> bool
{
	const auto window = SampleDecoder::decodeLimited(source, 1.0);
	if (!window || window->totalFrames <= 0 || window->window.sampleRate <= 0)
	{
		result->error = QStringLiteral("'%1' could not be decoded: it is empty, damaged, or of a kind this build does not read")
			.arg(source);
		return false;
	}
	const qint64 budget = CellAudio::decodedByteBudget();
	if (!fitsBudget(window->totalFrames, budget))
	{
		result->error = overBudgetMessage(source, window->totalFrames, window->window.sampleRate, budget);
		return false;
	}
	if (allocFault())
	{
		result->error = QStringLiteral("ran out of memory decoding '%1': nothing was written").arg(source);
		return false;
	}
	const auto decoded = SampleDecoder::decode(source);
	if (!decoded || decoded->data.empty() || decoded->sampleRate <= 0)
	{
		result->error = QStringLiteral("'%1' could not be decoded: it is empty, damaged, or of a kind this build does not read")
			.arg(source);
		return false;
	}
	// SampleFrame is two floats, so the decoded buffer already IS interleaved stereo PCM: it is
	// handed to the encoder block by block, with no second full copy.
	QString error;
	return writeFrames(target, &decoded->data[0][0], static_cast<sf_count_t>(decoded->data.size()),
		decoded->sampleRate, result, &error);
}

//! Closes the read handle on every exit of save().
class ReadHandle
{
public:
	ReadHandle(QFile& file, SNDFILE* snd) : m_file(file), m_snd(snd) {}
	~ReadHandle()
	{
		if (m_snd != nullptr) { sf_close(m_snd); }
		if (m_file.isOpen()) { m_file.close(); }
	}
	ReadHandle(const ReadHandle&) = delete;
	ReadHandle& operator=(const ReadHandle&) = delete;

private:
	QFile& m_file;
	SNDFILE* m_snd;
};

} // namespace

auto CellAudio::decodedByteBudget() -> qint64
{
	return configuredBudget();
}

auto CellAudio::save(const QString& source, const QString& target) -> Result
{
	Result result;
	const QFileInfo info(source);
	if (source.isEmpty() || !info.isFile())
	{
		result.error = QStringLiteral("the audio file '%1' is not there").arg(source);
		return result;
	}
	{
		QFile file(source);
		if (!file.open(QIODevice::ReadOnly))
		{
			result.error = QStringLiteral("'%1' cannot be read: %2").arg(source, file.errorString());
			return result;
		}
		SF_INFO sfInfo{};
		SNDFILE* snd = sf_open_fd(file.handle(), SFM_READ, &sfInfo, SF_FALSE);
		const ReadHandle guard(file, snd);
		if (snd != nullptr && sf_error(snd) == 0)
		{
			saveThroughSndfile(source, target, snd, sfInfo.channels, sfInfo.samplerate, sfInfo.frames, &result);
			return result;
		}
	}
	// libsndfile does not read this file: the built-in decoder's fallbacks do, behind a
	// bounded length probe (a DrumSynth file, or Ogg where libsndfile lacks it).
	saveThroughSampleDecoder(source, target, &result);
	return result;
}

} // namespace lmms::libraryaudio
