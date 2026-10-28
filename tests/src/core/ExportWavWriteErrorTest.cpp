/*
 * ExportWavWriteErrorTest.cpp - a WAV export that could not be written is a
 *                               failed export, not a finished one.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA)
 *
 */

/*! BUG 14 (new-p12-317): the WAV export discarded the result of every write
 *  and of the close, so a disk that refused the write produced a truncated
 *  file that the export reported as a finished one.
 *
 *  Two failures are provoked here, both of them real and both of them
 *  deterministic - no timing, no sleeping, no racing:
 *
 *  - a SHORT WRITE: the size of the files this process writes is capped, so
 *    the write of the audio cannot grow the file past the cap and fails with
 *    EFBIG. libsndfile then writes fewer frames than it was given.
 *  - a FAILED FINALISATION: the same cap is used for the header write that
 *    closes the file, so the file is closed without its final header. This is
 *    the failure libsndfile does not report: psf_fwrite() counts a partial
 *    write as a whole one, so sf_close() returns 0 for a file whose header
 *    was only half written.
 *
 *  What is asserted is what a user can see: a file that could not be written
 *  completely is not left behind as if it were an export, and the failure is
 *  reported instead of being swallowed. The assertions that need the API the
 *  fix introduced are compiled in only when the API is there, so this test
 *  also runs against the tree without the fix - where the file assertions
 *  fail.
 *
 *  The report has to survive the whole path, not stop at the device: the last
 *  case here renders a real song through RenderManager (the object `lmms
 *  render` and the export dialog both hold) and asserts the manager says the
 *  export failed and its file is gone.
 */

#include <QtTest>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QtEndian>

#include <cstdio>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#endif

#include "AudioEngine.h"
#include "AudioFileDevice.h"
#include "AudioFileWave.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerSurround.h"
#include "ExportRenderSettings.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "RenderManager.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr int kSampleRate = 44100;

//! True when the device can report a write error (the API the fix introduced).
template <typename T, typename = void>
struct HasWriteError : std::false_type {};

template <typename T>
struct HasWriteError<T, std::void_t<decltype(std::declval<const T&>().hasWriteError())>>
	: std::true_type {};

//! True when the device's writeBuffer() hands back a result (the API the fix
//! introduced). The tree WITHOUT the fix returns nothing, so it cannot report a
//! failed write - the test then fails on the other evidence, never on a compile
//! error.
template <typename T, typename = void>
struct WriteBufferReports : std::false_type {};

template <typename T>
struct WriteBufferReports<T, std::void_t<decltype(std::declval<T&>().writeBuffer(
	std::declval<const SampleFrame*>(), std::declval<f_cnt_t>()))>>
	: std::bool_constant<!std::is_void_v<decltype(std::declval<T&>().writeBuffer(
		std::declval<const SampleFrame*>(), std::declval<f_cnt_t>()))>> {};

//! Writes the frames and reports whether the device reported success. A device
//! whose writeBuffer() returns nothing (the tree without the fix) cannot report
//! a failure, so it answers "no failure" here.
template <typename T>
bool writeBufferSucceeded(T& device, const SampleFrame* frames, f_cnt_t count)
{
	if constexpr (WriteBufferReports<T>::value)
	{
		return device.writeBuffer(frames, count);
	}
	else
	{
		device.writeBuffer(frames, count);
		return true;
	}
}

//! True when the device can be finalised explicitly (the API the fix introduced).
template <typename T, typename = void>
struct HasFinishEncoding : std::false_type {};

template <typename T>
struct HasFinishEncoding<T, std::void_t<decltype(std::declval<T&>().finishEncoding())>>
	: std::true_type {};

//! The write error the device reports. False when the device cannot report one
//! (the tree without the fix), so the test still compiles there.
template <typename T>
bool reportedWriteError(const T& device)
{
	if constexpr (HasWriteError<T>::value)
	{
		return device.hasWriteError();
	}
	else
	{
		return false;
	}
}

//! Finalises the device when it can be finalised explicitly, and says whether
//! it could (false on the tree without the fix).
template <typename T>
bool finalise(T& device, bool& finalised)
{
	if constexpr (HasFinishEncoding<T>::value)
	{
		finalised = device.finishEncoding();
		return true;
	}
	else
	{
		finalised = false;
		return false;
	}
}

//! True when finalising the device SAID the file could not be written completely.
//! A device that cannot report a finalisation at all (the tree without the fix)
//! answers false: the test then fails on its assertion instead of passing because
//! the API was simply absent.
template <typename T>
bool finalisationReportedFailure(T& device)
{
	if constexpr (HasFinishEncoding<T>::value)
	{
		return !device.finishEncoding();
	}
	else
	{
		(void)device;
		return false;
	}
}

//! The WAV header fields libsndfile rewrites at close: the RIFF size and the data
//! chunk's own size. Read straight from the file so the test asserts on what a
//! reader sees, not on the code's own notion of the layout.
struct RiffSizes
{
	quint32 riff = 0;
	quint32 data = 0;
	quint32 dataOffset = 0;
	bool found = false;
};

#ifdef Q_OS_UNIX // only the POSIX write-error cases read the RIFF sizes back
RiffSizes riffSizes(const QString& path)
{
	RiffSizes sizes;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return sizes; }
	const QByteArray bytes = file.read(4096);
	if (bytes.size() < 12 || bytes.left(4) != "RIFF") { return sizes; }
	sizes.riff = qFromLittleEndian<quint32>(bytes.constData() + 4);
	for (int at = 12; at + 8 <= bytes.size(); )
	{
		const quint32 size = qFromLittleEndian<quint32>(bytes.constData() + at + 4);
		if (bytes.mid(at, 4) == "data")
		{
			sizes.data = size;
			sizes.dataOffset = static_cast<quint32>(at);
			sizes.found = true;
			return sizes;
		}
		if (size == 0) { return sizes; }
		at += 8 + static_cast<int>(size) + static_cast<int>(size & 1u);
	}
	return sizes;
}
#endif // Q_OS_UNIX

#ifdef Q_OS_UNIX
//! Caps the size of the files this process writes: a write that would grow a
//! file past the cap fails with EFBIG. SIGXFSZ is ignored while the cap is
//! in place, so the failure is an error return and not a signal that kills
//! the test.
class FileSizeLimit
{
public:
	explicit FileSizeLimit(rlim_t bytes)
	{
		if (getrlimit(RLIMIT_FSIZE, &m_previous) != 0) { return; }
		struct rlimit limit = m_previous;
		if (limit.rlim_max != RLIM_INFINITY && bytes > limit.rlim_max) { bytes = limit.rlim_max; }
		// bytes == 0 is a real limit ("no write may extend the file"), and it is what a still
		// unflushed QFile reports as its on-disk length; it must not silently disable the cap.
		limit.rlim_cur = bytes;
		if (setrlimit(RLIMIT_FSIZE, &limit) != 0) { return; }
		struct sigaction ignored{};
		ignored.sa_handler = SIG_IGN;
		sigemptyset(&ignored.sa_mask);
		if (sigaction(SIGXFSZ, &ignored, &m_previousAction) != 0) { return; }
		m_active = true;
	}

	~FileSizeLimit() { restore(); }

	void restore()
	{
		if (!m_active) { return; }
		setrlimit(RLIMIT_FSIZE, &m_previous);
		sigaction(SIGXFSZ, &m_previousAction, nullptr);
		m_active = false;
	}

private:
	struct rlimit m_previous{};
	struct sigaction m_previousAction{};
	bool m_active = false;
};
#endif // Q_OS_UNIX

} // namespace

class ExportWavWriteErrorTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		ExportRenderSettings::reset();
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ExportRenderSettings::reset();
		Engine::destroy();
	}

	//! The file a healthy export writes is left alone: the fix must not turn
	//! a good export into a failed one.
	void aSuccessfulExportKeepsItsFileAndReportsNoError()
	{
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("healthy.wav"));
		bool successful = false;
		std::unique_ptr<AudioFileDevice> device = makeDevice(path, successful);
		QVERIFY(successful);
		QVERIFY(device != nullptr);

		const std::vector<SampleFrame> frames(512, SampleFrame(0.25f, -0.25f));
		device->writeBuffer(frames.data(), static_cast<f_cnt_t>(frames.size()));

		bool finalised = false;
		finalise(*device, finalised);
		const bool reported = reportedWriteError(*device);
		device.reset();

		QVERIFY2(QFile::exists(path), "a healthy export lost its file");
		QVERIFY2(!reported, "a healthy export reported a write error");
	}

#ifdef Q_OS_UNIX
	/*! BUG 14, the header-rewrite half: closing a FULL-LENGTH file whose final
	 *  header rewrite did not land is a failed export.
	 *
	 *  sf_close() rewrites the RIFF and data sizes in place. When that rewrite is
	 *  refused, the old header bytes are still there, so the file's length is
	 *  exactly what it was - a size-only check calls this a success. The cap is
	 *  set to the file's CURRENT length (the audio is already on disk, so nothing
	 *  is truncated), which leaves the rewrite no room at all.
	 */
	void aFailedHeaderRewriteOnAFullLengthFileIsReported()
	{
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("stale-header.wav"));
		bool successful = false;
		std::unique_ptr<AudioFileDevice> device = makeDevice(path, successful);
		QVERIFY(successful);

		const std::vector<SampleFrame> frames(1024, SampleFrame(0.5f, 0.5f));
		QVERIFY(writeBufferSucceeded(*device, frames.data(), static_cast<f_cnt_t>(frames.size())));

		// The header libsndfile wrote when the file was opened still says "0 frames": the audio
		// is in the file, the sizes are stale. The close is what has to fix them in place.
		const qint64 fullLength = QFileInfo(path).size();
		QVERIFY(fullLength > 44);
		const RiffSizes before = riffSizes(path);
		QVERIFY(before.found);
		QCOMPARE(before.data, quint32(0));

		// The pre-fix check was "is the file shorter than 44 + the samples?": this file is longer
		// than that, so a size-only check accepts it however stale the header fields are.
		const qint64 samplesBytes = qint64(frames.size()) * DEFAULT_CHANNELS * 2;
		QVERIFY(fullLength >= 44 + samplesBytes);

		// The size cap stops at the data chunk's size field: that in-place rewrite (and nothing
		// else) is refused, while every audio byte is already on disk - so the file cannot get
		// shorter and the length a size check reads is unchanged.
		bool reportedFailure = false;
		{
			FileSizeLimit limit(static_cast<rlim_t>(before.dataOffset + 4));
			reportedFailure = finalisationReportedFailure(*device);
		}

		// The whole point: the file did NOT get shorter, so the old size check could not see this.
		const bool reported = reportedWriteError(*device);
		device.reset();

		QVERIFY2(reportedFailure, "a WAV whose final header rewrite failed was reported as a success");
		QVERIFY2(reported, "the failed WAV header rewrite was not reported");
		QVERIFY2(!QFile::exists(path), "a WAV with a stale header was left behind as an export");
	}

	/*! BUG 14: the discrete-layout channel-mask patch is part of finalising the
	 *  file, so a patch that cannot be written is a failed export - not a
	 *  silently wrong header.
	 *
	 *  The writer's own handle is already open, so sf_close() still rewrites the
	 *  header through it. The patch REOPENS the path for writing, and a file the
	 *  process may only read cannot be reopened that way - while it is still
	 *  perfectly readable, so the header check and the file's removal both work.
	 *  That isolates the patch: the close and the read-back succeed.
	 */
	void aFailedDiscreteChannelMaskPatchIsReported()
	{
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("mask-patch.wav"));
		bool successful = false;
		std::unique_ptr<AudioFileDevice> device = makeDiscreteDevice(path, successful);
		if (device == nullptr)
		{
			QSKIP("this build cannot open a discrete 6-channel WAVEX export");
		}
		QVERIFY(successful);

		std::vector<float> samples(static_cast<std::size_t>(256) * 6, 0.25f);
		QVERIFY(device->writeInterleaved(samples.data(), 256));

		// Precondition: the patch's open really is refused, and the file really is still readable.
		// If this process can write through a read-only file (it runs as root), the injection has
		// no fault to inject - say so instead of asserting on a run that cannot fail.
		QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner));
		{
			QFile probe(path);
			if (probe.open(QIODevice::ReadWrite)) { probe.close(); }
			if (probe.isOpen())
			{
				QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
				QSKIP("this process may write a read-only file; the patch failure cannot be injected");
			}
		}
		{
			QFile readable(path);
			QVERIFY2(readable.open(QIODevice::ReadOnly), "the header patch's target must stay readable");
		}

		const bool reportedFailure = finalisationReportedFailure(*device);
		const bool reported = reportedWriteError(*device);
		const bool removed = !QFile::exists(path);
		device.reset();

		QVERIFY2(reportedFailure, "a WAVEX file whose channel-mask patch failed was reported as a success");
		QVERIFY2(reported, "the failed channel-mask patch was not reported");
		QVERIFY2(removed, "a WAVEX file with an unpatched mask was left behind as an export");
	}

	//! BUG 14: a write that does not reach the file is a failed export.
	void aShortWriteIsReportedAndLeavesNoFileBehind()
	{
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("short-write.wav"));
		bool successful = false;
		std::unique_ptr<AudioFileDevice> device = makeDevice(path, successful);
		QVERIFY(successful);
		QVERIFY(device != nullptr);

		// The header libsndfile wrote when the file was opened is the size the
		// file has now: the audio does not fit under that cap any more.
		const rlim_t cap = static_cast<rlim_t>(QFileInfo(path).size());

		const std::vector<SampleFrame> frames(1024, SampleFrame(0.5f, 0.5f));
		{
			FileSizeLimit limit(cap);
			device->writeBuffer(frames.data(), static_cast<f_cnt_t>(frames.size()));
			limit.restore();
		}

		const bool reported = reportedWriteError(*device);
		device.reset();

		QVERIFY2(!QFile::exists(path),
			"a WAV write that did not reach the file left its broken export behind");
		QVERIFY2(reported, "the failed WAV write was not reported");
	}

	//! BUG 14: a file that is closed without its final header is a failed
	//! export too - the one libsndfile itself does not report.
	void aFailedFinalisationIsReportedAndLeavesNoFileBehind()
	{
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("failed-close.wav"));
		bool successful = false;
		std::unique_ptr<AudioFileDevice> device = makeDevice(path, successful);
		QVERIFY(successful);
		QVERIFY(device != nullptr);

		const std::vector<SampleFrame> frames(1024, SampleFrame(0.5f, 0.5f));
		device->writeBuffer(frames.data(), static_cast<f_cnt_t>(frames.size()));

		// The header the close writes cannot fit under the cap, and the file is
		// emptied first, so the close leaves a file without its final header -
		// the state libsndfile reports as a success.
		bool finalised = true;
		{
			FileSizeLimit limit(16);
			QFile::resize(path, 0);
			finalise(*device, finalised);
			limit.restore();
		}

		const bool reported = reportedWriteError(*device);
		device.reset();

		QVERIFY2(!QFile::exists(path),
			"a WAV file that was closed without its final header was left behind");
		QVERIFY2(reported, "the failed WAV finalisation was not reported");
		QVERIFY2(!finalised || !HasFinishEncoding<AudioFileDevice>::value,
			"the failed WAV finalisation was reported as a success");
	}


	/*! BUG 14, the sibling encoders' finalisation: the flush that finishes an
	 *  OGG or MP3 stream runs where the render can see it, so a tail that does
	 *  not reach the file is a failed export instead of a destructor's secret.
	 *
	 *  Both encoders buffer their output in the QFile; the cap is the file's
	 *  current length, so the final flush has no room. The assertion is on the
	 *  verdict finishEncoding() returns - the same call ProjectRenderer::run()
	 *  makes before it reads the result.
	 */
	void aFailedSiblingFinalFlushIsReported()
	{
		for (const auto format : {ProjectRenderer::ExportFileFormat::Ogg,
				ProjectRenderer::ExportFileFormat::MP3})
		{
			const QString name = format == ProjectRenderer::ExportFileFormat::Ogg
				? QStringLiteral("tail.ogg") : QStringLiteral("tail.mp3");
			const QString path = QDir(m_dir.path()).filePath(name);
			QFile::remove(path);

			const OutputSettings settings(kSampleRate, 160, OutputSettings::BitDepth::Depth16Bit,
				OutputSettings::StereoMode::Stereo);
			const ProjectRenderer::FileEncodeDevice* encoder = nullptr;
			for (const ProjectRenderer::FileEncodeDevice& candidate : ProjectRenderer::fileEncodeDevices)
			{
				if (candidate.m_fileFormat == format && candidate.isAvailable()) { encoder = &candidate; }
			}
			if (encoder == nullptr) { QSKIP("this build has no such encoder"); }

			bool successful = false;
			std::unique_ptr<AudioFileDevice> device(
				encoder->m_getDevInst(path, settings, DEFAULT_CHANNELS, Engine::audioEngine(), successful));
			if (device == nullptr || !successful)
			{
				QSKIP("this build cannot open that encoder");
			}

			const std::vector<SampleFrame> frames(2048, SampleFrame(0.4f, -0.4f));
			QVERIFY(writeBufferSucceeded(*device, frames.data(), static_cast<f_cnt_t>(frames.size())));

			bool reportedFailure = false;
			{
				const qint64 fullLength = QFileInfo(path).size();
				FileSizeLimit limit(static_cast<rlim_t>(fullLength));
				reportedFailure = finalisationReportedFailure(*device);
			}

			const bool reported = reportedWriteError(*device);
			device.reset();
			QFile::remove(path);

			QVERIFY2(reportedFailure, qPrintable(QStringLiteral(
				"%1's final flush did not reach the file but was reported as a success").arg(name)));
			QVERIFY2(reported, qPrintable(QStringLiteral(
				"%1's failed final flush was not reported").arg(name)));
		}
	}

	/*! BUG 14, the propagation half: the failure has to reach whoever started
	 *  the render, not stop at the device.
	 *
	 *  A real song (one second of constant audio, so there are periods to
	 *  write) is rendered through RenderManager - the object `lmms render` and
	 *  the export dialog both hold - while the size of the files this process
	 *  writes is capped just above the WAV header. The render's own audio
	 *  writes then fail the same deterministic way the device cases above make
	 *  them fail, and what is asserted is what a caller can read afterwards:
	 *  the manager reports the failure, and no broken file is left behind.
	 */
	void aFailedRenderIsReportedByRenderManagerAndLeavesNoFileBehind()
	{
		const QString fixtureError = buildOneSecondSong();
		QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("render-write-error.wav"));
		QFile::remove(path);

		const OutputSettings settings(kSampleRate, 160, OutputSettings::BitDepth::Depth16Bit,
			OutputSettings::StereoMode::Stereo);
		bool reported = false;
		bool fileGone = false;
		{
			RenderManager manager(settings, ProjectRenderer::ExportFileFormat::Wave, path);
			QEventLoop loop;
			bool finished = false;
			QObject::connect(&manager, &RenderManager::finished, &loop,
				[&]() { finished = true; loop.quit(); });
			{
				FileSizeLimit limit(4096);
				manager.renderProject();
				// A render that never started quits synchronously, before the
				// loop runs; the ordinary one quits from inside it.
				if (!finished) { loop.exec(); }
			}
			reported = reportedWriteError(manager);
			fileGone = !QFile::exists(path);
		}

		QVERIFY2(reported,
			"a render whose WAV writes failed was reported as a finished export");
		QVERIFY2(fileGone,
			"a render whose WAV writes failed left its broken file behind");
	}
#endif // Q_OS_UNIX

private:
	//! A WAV file device for `path`, the way ProjectRenderer opens one.
	std::unique_ptr<AudioFileDevice> makeDevice(const QString& path, bool& successful)
	{
		OutputSettings settings(44100, 160, OutputSettings::BitDepth::Depth16Bit,
			OutputSettings::StereoMode::Stereo);
		return std::unique_ptr<AudioFileDevice>(AudioFileWave::getInst(path, settings,
			DEFAULT_CHANNELS, Engine::audioEngine(), successful));
	}

	/*! A discrete 6-channel WAVEX WAV device: the layout whose header needs the mask patch.
	 *  A discrete master has no speaker positions, so formatForChannels() keeps it discrete
	 *  (mask 0) instead of writing 5.1's mask - this is the path writeZeroChannelMask() serves. */
	std::unique_ptr<AudioFileDevice> makeDiscreteDevice(const QString& path, bool& successful)
	{
		Engine::mixer()->mixerChannel(0)->m_surround.setFormat(surround::Format::Discrete6);
		OutputSettings settings(44100, 160, OutputSettings::BitDepth::Depth16Bit,
			OutputSettings::StereoMode::Stereo);
		settings.setChannelMode(OutputSettings::ChannelMode::Surround);
		auto device = std::unique_ptr<AudioFileDevice>(AudioFileWave::getInst(path, settings, 6,
			Engine::audioEngine(), successful));
		Engine::mixer()->mixerChannel(0)->m_surround.setFormat(surround::Format::Stereo);
		return device;
	}

	/*! One second of constant audio, so a render has real periods to write and
	 *  the size cap has writes to fail against - an empty song renders no audio
	 *  at all and would prove nothing. Same fixture shape as
	 *  tests/src/core/ClipFadesRenderTest.cpp. Empty return on success, else why
	 *  the fixture could not be built. */
	QString buildOneSecondSong()
	{
		Song* song = Engine::getSong();
		if (song == nullptr) { return QStringLiteral("the engine has no song"); }
		song->clearProject();
		auto* track = new SampleTrack(song);
		track->setName(QStringLiteral("WriteErrorFixture"));
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		if (clip == nullptr) { return QStringLiteral("the fixture track made no sample clip"); }
		const auto frames = static_cast<std::size_t>(kSampleRate);
		clip->setSampleBuffer(std::make_shared<const SampleBuffer>(
			std::vector<SampleFrame>(frames, SampleFrame(0.5f, 0.5f)).data(), frames, kSampleRate));
		// A whole bar, so the song has a length in bars (Track::length() counts
		// whole bars) and the render has far more audio than the size cap allows.
		clip->changeLength(TimePos(TimePos::ticksPerBar()));
		clip->setAutoResize(false);
		song->updateLength();
		if (song->length() <= 0) { return QStringLiteral("the fixture song has no length"); }
		return QString();
	}

	QTemporaryDir m_dir;
};

QTEST_MAIN(ExportWavWriteErrorTest)

#include "ExportWavWriteErrorTest.moc"
