/*
 * AudioFileOggInitTest.cpp - destroying an Ogg writer after rejected setup
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

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include "AudioFileDevice.h"
#include "AudioFileOgg.h"
#include "Engine.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

//! True when the OGG encoder carries the page-write test seam (the API this fix
//! introduced). On a tree without it there is no way to drive a short page write,
//! and the regression says so with a failed assertion rather than a skip.
template <typename T, typename = void>
struct HasPageWriteSeam : std::false_type {};

template <typename T>
struct HasPageWriteSeam<T, std::void_t<
	decltype(std::declval<T&>().setNextPageWriteShortForTest(0)),
	decltype(T::setFirstPageWriteShortForTest(0))>> : std::true_type {};

//! True when the device can report a write error (the API the fix introduced).
template <typename T, typename = void>
struct HasWriteError : std::false_type {};

template <typename T>
struct HasWriteError<T, std::void_t<decltype(std::declval<const T&>().hasWriteError())>>
	: std::true_type {};

template <typename T>
bool reportedWriteError(const T& device)
{
	if constexpr (HasWriteError<T>::value) { return device.hasWriteError(); }
	else { return false; }
}

//! True when writeBuffer() hands back a result (the API the fix introduced); the
//! tree without the fix returns nothing, so this keeps the test compiling there.
template <typename T, typename = void>
struct WriteBufferReports : std::false_type {};

template <typename T>
struct WriteBufferReports<T, std::void_t<decltype(std::declval<T&>().writeBuffer(
	std::declval<const SampleFrame*>(), std::declval<f_cnt_t>()))>>
	: std::bool_constant<!std::is_void_v<decltype(std::declval<T&>().writeBuffer(
		std::declval<const SampleFrame*>(), std::declval<f_cnt_t>()))>> {};

template <typename T>
bool writeBufferSucceeded(T& device, const SampleFrame* frames, f_cnt_t count)
{
	if constexpr (WriteBufferReports<T>::value) { return device.writeBuffer(frames, count); }
	else { device.writeBuffer(frames, count); return true; }
}

//! Arm the page-write one-shot on @a device, when the seam exists (a template, so
//! `if constexpr` can discard the branch on a tree without the seam).
template <typename T>
void armNextPageWriteShort(T& device, int bytes)
{
	if constexpr (HasPageWriteSeam<T>::value) { device.setNextPageWriteShortForTest(bytes); }
	else { (void)device; (void)bytes; }
}

//! Arm the one-shot for the next device's first page, when the seam exists.
template <typename T>
void armFirstPageWriteShort(int bytes)
{
	if constexpr (HasPageWriteSeam<T>::value) { T::setFirstPageWriteShortForTest(bytes); }
	else { (void)bytes; }
}

//! The registered OGG encoder, or null when this build does not carry it.
const ProjectRenderer::FileEncodeDevice* oggEncoder()
{
	for (const ProjectRenderer::FileEncodeDevice& candidate : ProjectRenderer::fileEncodeDevices)
	{
		if (candidate.m_fileFormat == ProjectRenderer::ExportFileFormat::Ogg && candidate.isAvailable())
		{
			return &candidate;
		}
	}
	return nullptr;
}

} // namespace

class AudioFileOggInitTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void failedEncoderInitializationCanBeDestroyed()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());

		OutputSettings settings(44100, 192, OutputSettings::BitDepth::Depth16Bit);
		alignas(AudioFileOgg) std::array<std::byte, sizeof(AudioFileOgg)> storage;
		std::memset(storage.data(), 0xa5, storage.size());

		bool successful = true;
		auto* device = new (storage.data()) AudioFileOgg(settings, 0, successful,
			directory.filePath("failed.ogg"), Engine::audioEngine());
		const bool rejected = !successful;

		// Poisoning the untouched codec objects makes the old destructor's reads
		// of state after the constructor's early return deterministic.
		device->~AudioFileOgg();
		QVERIFY(rejected);
	}

	/*! BUG 14 (new-p12-317): a SHORT POSITIVE page write is a failure.
	 *
	 *  writeData() answers with the bytes QFile accepted, so a page that is only
	 *  partly stored is a positive count - not the -1 the round-1 check waited for.
	 *  QFile buffers anything under its 16 KiB buffer and the encoder's pages are
	 *  30..4343 body bytes (measured through the real encoder here), so a file-size
	 *  cap can only reach the flushed tail, never writePage()'s per-page count. The
	 *  fix exposes a one-shot LMMS_TESTING seam that makes the next page store fewer
	 *  bytes and answer with that short count - exactly what a nearly-full disk
	 *  returns - and this asserts on the comparison writePage() really makes.
	 *  Nothing is timed or raced: the short count is an injected number, consumed by
	 *  one page.
	 */
	void aShortPositiveOggPageWriteIsReported()
	{
		QVERIFY2(HasPageWriteSeam<AudioFileOgg>::value,
			"the OGG page-write test seam is missing: a short positive page write cannot be "
			"injected, and the page-write check is not under test");
		const ProjectRenderer::FileEncodeDevice* encoder = oggEncoder();
		QVERIFY2(encoder != nullptr, "the build has libogg but no OGG encoder registered");

		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString path = directory.filePath(QStringLiteral("short-page.ogg"));
		const OutputSettings settings(44100, 160, OutputSettings::BitDepth::Depth16Bit,
			OutputSettings::StereoMode::Stereo);
		bool successful = false;
		std::unique_ptr<AudioFileDevice> device(
			encoder->m_getDevInst(path, settings, DEFAULT_CHANNELS, Engine::audioEngine(), successful));
		QVERIFY(successful);
		QVERIFY(device != nullptr);

		// Two seconds of audio, fed in periods, so the encoder emits audio pages while writing.
		const std::vector<SampleFrame> frames(44100 * 2, SampleFrame(0.4f, -0.4f));
		bool writeReportedFailure = false;
		for (std::size_t at = 0; at < frames.size(); at += 4096)
		{
			const auto count = static_cast<f_cnt_t>(std::min<std::size_t>(4096, frames.size() - at));
			if (at >= 44100) { armNextPageWriteShort(*static_cast<AudioFileOgg*>(device.get()), 5); }
			if (!writeBufferSucceeded(*device, frames.data() + at, count)) { writeReportedFailure = true; }
		}
		const bool reported = reportedWriteError(*device);
		device.reset();

		QVERIFY2(writeReportedFailure, "a short positive OGG page write was reported as a success");
		QVERIFY2(reported, "the short OGG page write was not recorded");
	}

	/*! BUG 14 (new-p12-317), the constructor: a partial header page must reject the
	 *  device. The device only exists after its constructor wrote the header pages,
	 *  so the short write is armed across devices and consumed by the stream's
	 *  opening page. The caller is told by `successful`, and no half-written OGG is
	 *  left as an export.
	 */
	void aPartialOggHeaderPageRejectsTheDevice()
	{
		QVERIFY2(HasPageWriteSeam<AudioFileOgg>::value,
			"the OGG page-write test seam is missing: a partial header page cannot be injected, "
			"and the constructor's header-page check is not under test");
		const ProjectRenderer::FileEncodeDevice* encoder = oggEncoder();
		QVERIFY2(encoder != nullptr, "the build has libogg but no OGG encoder registered");

		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString path = directory.filePath(QStringLiteral("short-header-page.ogg"));
		const OutputSettings settings(44100, 160, OutputSettings::BitDepth::Depth16Bit,
			OutputSettings::StereoMode::Stereo);

		bool successful = true;
		armFirstPageWriteShort<AudioFileOgg>(5);
		std::unique_ptr<AudioFileDevice> device(encoder->m_getDevInst(path, settings,
			DEFAULT_CHANNELS, Engine::audioEngine(), successful));
		const bool created = device != nullptr;
		const bool reported = created && reportedWriteError(*device);
		device.reset();

		QVERIFY2(created, "the OGG device could not be created at all, so nothing was tested");
		QVERIFY2(!successful, "a partial OGG header page was accepted as a successful device");
		QVERIFY2(reported, "the partial OGG header page was not recorded");
		QVERIFY2(!QFile::exists(path), "a partial OGG header was left behind as an export");
	}
};

QTEST_GUILESS_MAIN(AudioFileOggInitTest)

#include "AudioFileOggInitTest.moc"
