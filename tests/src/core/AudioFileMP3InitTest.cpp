/*
 * AudioFileMP3InitTest.cpp - an MP3 writer whose LAME encoder never started
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

#include <QFileInfo>
#include <QTemporaryDir>

#include <memory>
#include <vector>

#include "AudioFileDevice.h"
#include "AudioFileMP3.h"
#include "Engine.h"
#include "OutputSettings.h"
#include "SampleFrame.h"

using namespace lmms;

class AudioFileMP3InitTest : public QObject
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

	//! Normal path: a stereo LAME encoder starts, encodes, and flushes its tail exactly once.
	void normalEncoderFlushesItsTailOnce()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString path = directory.filePath(QStringLiteral("normal.mp3"));
		const OutputSettings settings(44100, 192, OutputSettings::BitDepth::Depth16Bit,
			OutputSettings::StereoMode::Stereo);

		bool successful = false;
		const int flushesBefore = AudioFileMP3::flushCallsForTest();
		std::unique_ptr<AudioFileDevice> device(
			AudioFileMP3::getInst(path, settings, DEFAULT_CHANNELS, Engine::audioEngine(), successful));
		QVERIFY(device != nullptr);
		QVERIFY2(successful, "a stereo MP3 encoder with a working LAME was rejected");

		const std::vector<SampleFrame> frames(4410, SampleFrame(0.25f, -0.25f));
		QVERIFY(device->writeBuffer(frames.data(), static_cast<f_cnt_t>(frames.size())));
		QVERIFY2(device->finishEncoding(), "a complete MP3 export was reported as unfinished");
		QVERIFY(!device->hasWriteError());
		device.reset();

		QCOMPARE(AudioFileMP3::flushCallsForTest() - flushesBefore, 1);
		QVERIFY2(QFileInfo(path).size() > 0, "the encoded MP3 did not reach the file");
	}

	/*! BUG new-p15-259: a device whose lame_init() returned null is rejected by ProjectRenderer, which
	 *  deletes it without touching it. Its destructor then flushed the null handle: lame_encode_flush()
	 *  answers -3 for it, which the flush's assertion rejects in a debug build, and the -3 length went on
	 *  to writeData(). The destruction must make no LAME call and write nothing.
	 */
	void rejectedDeviceIsDestroyedWithoutTouchingLame()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString path = directory.filePath(QStringLiteral("rejected.mp3"));
		const OutputSettings settings(44100, 192, OutputSettings::BitDepth::Depth16Bit,
			OutputSettings::StereoMode::Stereo);

		bool successful = true;
		const int flushesBefore = AudioFileMP3::flushCallsForTest();
		AudioFileMP3::failNextLameInitForTest();
		std::unique_ptr<AudioFileDevice> device(
			AudioFileMP3::getInst(path, settings, DEFAULT_CHANNELS, Engine::audioEngine(), successful));
		QVERIFY2(device != nullptr, "the MP3 device could not be created at all, so nothing was tested");
		QVERIFY2(!successful, "an MP3 device without a LAME encoder was accepted as successful");
		device.reset();

		QCOMPARE(AudioFileMP3::flushCallsForTest() - flushesBefore, 0);
		QCOMPARE(QFileInfo(path).size(), qint64{0});
	}

	//! A rejected device must not report a finished export, and the calls on it must not reach LAME.
	void rejectedDeviceReportsItsFailureWithoutTouchingLame()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString path = directory.filePath(QStringLiteral("failed.mp3"));
		const OutputSettings settings(44100, 192, OutputSettings::BitDepth::Depth16Bit,
			OutputSettings::StereoMode::Stereo);

		bool successful = true;
		const int flushesBefore = AudioFileMP3::flushCallsForTest();
		AudioFileMP3::failNextLameInitForTest();
		std::unique_ptr<AudioFileDevice> device(
			AudioFileMP3::getInst(path, settings, DEFAULT_CHANNELS, Engine::audioEngine(), successful));
		QVERIFY2(device != nullptr, "the MP3 device could not be created at all, so nothing was tested");
		QVERIFY(!successful);

		const std::vector<SampleFrame> frames(4410, SampleFrame(0.25f, -0.25f));
		QVERIFY2(!device->writeBuffer(frames.data(), static_cast<f_cnt_t>(frames.size())),
			"a write into an encoder that never started reported success");
		QVERIFY2(!device->finishEncoding(), "an encoder that never started reported a finished export");
		QVERIFY2(device->hasWriteError(), "an encoder that never started recorded no write error");
		device.reset();

		QCOMPARE(AudioFileMP3::flushCallsForTest() - flushesBefore, 0);
		QCOMPARE(QFileInfo(path).size(), qint64{0});
	}
};

QTEST_GUILESS_MAIN(AudioFileMP3InitTest)

#include "AudioFileMP3InitTest.moc"
