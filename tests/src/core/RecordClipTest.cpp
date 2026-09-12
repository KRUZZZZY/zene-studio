/*
 * RecordClipTest.cpp - DEFECT 6: out-of-range floats must be CLIPPED, not
 *                      wrapped, on the recorder's WAV write path
 *
 * Copyright (c) 2026 LMMS developers
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

// libsndfile's clipping is OFF by default: a float outside [-1, 1] written with
// sf_writef_float to a PCM file is converted by WRAPPING, not clamping. Measured
// on libsndfile 1.2.2, a +1.5 sample is stored as -1073742336 and a +2.0 sample
// as -512 - a full-scale sign flip and a near-silence dropout rather than a
// clipped peak. JACK hands the recorder unbounded floats (AudioJack.cpp casts
// the port buffer with an unchecked static_cast<sample_t>), so a take peaking
// above 0 dBFS was silently corrupted.
//
// The recorder (src/core/audio/TrackRecorder.cpp) now clamps every sample to
// [-1, 1] on the disk-writer thread before writing. The two export writers were
// never exposed to this: AudioFileWave.cpp:89 and AudioFileFlac.cpp:83 already
// enable SFC_SET_CLIPPING on open, and this test pins their behaviour too.
//
// Why clamp in C++ rather than set SFC_SET_CLIPPING on the recorder's sf_open,
// which is what both export writers do? Measured, not assumed: enabling
// libsndfile's clipping also changes the conversion of IN-RANGE negative
// samples (a -0.5 input comes back one 24-bit LSB lower than the same sample
// written without the flag). A recorder has no reason to rewrite audio that was
// never out of range, so the fix keeps in-range samples bit-identical and only
// changes what was corrupt. inRangeTakeIsUnchanged() proves that, and
// controlWithoutTheFixStillWraps() shows the two write sequences differ only
// outside [-1, 1].
//
// The inverted control: the frames are also written through the pre-fix
// sequence (a bare sf_open + sf_writef_float, no clamping anywhere), which
// still wraps. Removing the clamp from TrackRecorder::writerLoop() makes
// hotTakeIsClippedNotWrapped() go red - verified by reverting the fix and
// running this test.

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <QStringList>
#include <QTemporaryDir>

#include <sndfile.h>

#include "SampleFrame.h"
#include "TrackRecorder.h"

using lmms::f_cnt_t;
using lmms::SampleFrame;

namespace
{

constexpr int SampleRate = 48000;

// sf_readf_int scales a 24-bit sample to the full int32 range.
constexpr double Int32FullScale = 2147483648.0;
// One 24-bit LSB, in the int32 read-back scale: 2^31 / 2^23.
constexpr int OneLsbOf24Bit = 256;

//! How a helper writes its WAV.
enum class WriteMode
{
	//! A bare sf_open + sf_writef_float - the recorder's pre-fix sequence.
	NoClip,
	//! SFC_SET_CLIPPING after open - what AudioFileWave/AudioFileFlac do.
	ClipFlag,
	//! Clamp in C++ before the write - the recorder's fix.
	ClampInCpp,
};

struct Readback
{
	bool ok = false;
	std::vector<int> samples;
	int channels = 0;
	int rate = 0;
};

Readback readInts(const QString& path)
{
	Readback out;
	SF_INFO info{};
	SNDFILE* sf = sf_open(path.toUtf8().constData(), SFM_READ, &info);
	if (sf == nullptr) { return out; }
	out.samples.resize(static_cast<std::size_t>(info.frames));
	const auto read = sf_readf_int(sf, out.samples.data(), info.frames);
	out.channels = info.channels;
	out.rate = info.samplerate;
	sf_close(sf);
	out.ok = (read == info.frames);
	return out;
}

bool writeMono24(const QString& path, const std::vector<float>& frames, WriteMode mode)
{
	SF_INFO info{};
	info.samplerate = SampleRate;
	info.channels = 1;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;

	SNDFILE* sf = sf_open(path.toUtf8().constData(), SFM_WRITE, &info);
	if (sf == nullptr) { return false; }
	if (mode == WriteMode::ClipFlag) { sf_command(sf, SFC_SET_CLIPPING, nullptr, SF_TRUE); }

	std::vector<float> out = frames;
	if (mode == WriteMode::ClampInCpp)
	{
		for (auto& v : out) { v = std::clamp(v, -1.0f, 1.0f); }
	}

	const auto written = sf_writef_float(sf, out.data(), static_cast<sf_count_t>(out.size()));
	sf_close(sf);
	return written == static_cast<sf_count_t>(out.size());
}

//! The hot, out-of-range probe signal: a hot JACK input block.
std::vector<SampleFrame> hotInput()
{
	return {
		SampleFrame(1.5f, 1.5f),
		SampleFrame(2.0f, 2.0f),
		SampleFrame(-1.5f, -1.5f),
		SampleFrame(0.5f, 0.5f), // in range: must be untouched
	};
}

QString describe(const std::vector<int>& v)
{
	QStringList parts;
	for (const auto s : v) { parts << QString::number(s); }
	return parts.join(", ");
}

} // namespace


class RecordClipTest : public QObject
{
	Q_OBJECT

private slots:
	//! The premise of DEFECT 6, measured rather than read off a header: the
	//! write path that never clamps stores out-of-range floats as WRAPPED
	//! integers. This is also the inverted control for the fix below.
	void defaultWritePathWrapsOutOfRangeFloats()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = dir.filePath("pre-fix.wav");

		const std::vector<float> hot{1.5f, 2.0f, -1.5f};
		QVERIFY(writeMono24(path, hot, WriteMode::NoClip));

		const auto got = readInts(path);
		QVERIFY2(got.ok, "the control WAV could not be read back");
		qInfo("no clamping anywhere (the recorder's pre-fix sequence) for +1.5, +2.0, -1.5: %s",
			qPrintable(describe(got.samples)));

		QVERIFY2(got.samples[0] < 0,
			qPrintable(QString("+1.5 came back %1, not wrapped: DEFECT 6's premise "
				"(libsndfile wraps out-of-range floats) no longer holds on this "
				"library version").arg(got.samples[0])));
		QVERIFY2(got.samples[1] < 0, "+2.0 did not wrap");
		QVERIFY2(got.samples[2] > 0, "-1.5 did not wrap");
	}

	//! The production recorder path CLIPS a hot take instead of wrapping it.
	void hotTakeIsClippedNotWrapped()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = dir.filePath("recorder-hot.wav");

		lmms::TrackRecorder recorder;
		QVERIFY(recorder.arm(path.toStdString(), SampleRate, 0));

		const auto input = hotInput();
		recorder.processInput(input.data(), static_cast<f_cnt_t>(input.size()));
		recorder.disarm();

		const auto got = readInts(path);
		QVERIFY2(got.ok, "the recorder's WAV could not be read back");
		QCOMPARE(static_cast<qulonglong>(got.samples.size()),
			static_cast<qulonglong>(input.size()));
		QCOMPARE(got.rate, SampleRate);
		QCOMPARE(recorder.writeErrorCount(), static_cast<std::uint64_t>(0));

		qInfo("recorder take: %s", qPrintable(describe(got.samples)));

		// +1.5 wrapped to -1073742336 (sign flipped); clipped it stays positive.
		QVERIFY2(got.samples[0] > 0,
			qPrintable(QString("+1.5 came back %1: sign flipped, i.e. WRAPPED, not clipped")
				.arg(got.samples[0])));
		// +2.0 wrapped to -512 (a near-silence dropout).
		QVERIFY2(got.samples[1] > 0,
			qPrintable(QString("+2.0 came back %1: wrapped to near zero, not clipped")
				.arg(got.samples[1])));
		// Saturation: everything at or above full scale lands on the same value.
		QCOMPARE(got.samples[1], got.samples[0]);
		// -1.5 wrapped to +1073742336 (sign flipped); clipped it stays negative.
		QVERIFY2(got.samples[2] < 0,
			qPrintable(QString("-1.5 came back %1: sign flipped, i.e. WRAPPED, not clipped")
				.arg(got.samples[2])));
		// None of the documented wrap values may appear anywhere in the take.
		for (const auto wrapped : {-1073742336, -512, 1073742336})
		{
			for (std::size_t i = 0; i < got.samples.size(); ++i)
			{
				QVERIFY2(got.samples[i] != wrapped,
					qPrintable(QString("wrap artefact %1 present at frame %2")
						.arg(wrapped).arg(static_cast<qulonglong>(i))));
			}
		}
		// The in-range sample is untouched: 0.5 -> half of int32 full scale.
		QVERIFY2(std::abs(static_cast<double>(got.samples[3]) - 0.5 * Int32FullScale) <= 64.0,
			qPrintable(QString("in-range +0.5 came back %1, expected ~%2")
				.arg(got.samples[3]).arg(0.5 * Int32FullScale)));
	}

	//! Behaviour-preserving where the old behaviour was right: an in-range take
	//! through the recorder is bit-identical to the same frames written by the
	//! pre-fix sequence. Both signs are covered, including exactly full scale,
	//! because that is where enabling libsndfile's own clipping would have moved
	//! an in-range sample.
	void inRangeTakeIsUnchanged()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto recordedPath = dir.filePath("recorder-inrange.wav");
		const auto controlPath = dir.filePath("control-inrange.wav");

		const std::vector<float> values{
			-1.0f, -0.75f, -0.5f, -0.25f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
		QVERIFY(writeMono24(controlPath, values, WriteMode::NoClip));

		lmms::TrackRecorder recorder;
		QVERIFY(recorder.arm(recordedPath.toStdString(), SampleRate, 0));
		std::vector<SampleFrame> input;
		for (const auto v : values) { input.emplace_back(v, v); }
		recorder.processInput(input.data(), static_cast<f_cnt_t>(input.size()));
		recorder.disarm();

		const auto recorded = readInts(recordedPath);
		const auto control = readInts(controlPath);
		QVERIFY2(recorded.ok && control.ok, "a WAV could not be read back");
		QCOMPARE(static_cast<qulonglong>(recorded.samples.size()),
			static_cast<qulonglong>(control.samples.size()));

		qInfo("in-range recorder: %s", qPrintable(describe(recorded.samples)));
		qInfo("in-range control : %s", qPrintable(describe(control.samples)));
		for (std::size_t i = 0; i < control.samples.size(); ++i)
		{
			QVERIFY2(recorded.samples[i] == control.samples[i],
				qPrintable(QString("in-range frame %1 (%2) differs: recorder %3, control %4")
					.arg(static_cast<qulonglong>(i))
					.arg(static_cast<double>(values[i]))
					.arg(recorded.samples[i]).arg(control.samples[i])));
		}
	}

	//! The two write sequences (pre-fix and fixed) differ exactly outside
	//! [-1, 1] - nowhere else. This is what makes "behaviour-preserving except
	//! where it was wrong" a measured statement rather than a claim.
	void clippedAndPreFixPathsDifferOnlyOutOfRange()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto preFixPath = dir.filePath("pre-fix.wav");
		const auto fixedPath = dir.filePath("fixed.wav");
		const auto exportPath = dir.filePath("export-sequence.wav");

		const std::vector<float> frames{
			-2.0f, -1.5f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
		QVERIFY(writeMono24(preFixPath, frames, WriteMode::NoClip));
		QVERIFY(writeMono24(fixedPath, frames, WriteMode::ClampInCpp));
		QVERIFY(writeMono24(exportPath, frames, WriteMode::ClipFlag));

		const auto preFix = readInts(preFixPath);
		const auto fixed = readInts(fixedPath);
		const auto exported = readInts(exportPath);
		QVERIFY2(preFix.ok && fixed.ok && exported.ok, "a WAV could not be read back");

		qInfo("pre-fix sequence        : %s", qPrintable(describe(preFix.samples)));
		qInfo("recorder fix (C++ clamp): %s", qPrintable(describe(fixed.samples)));
		qInfo("export sequence (SFC flag): %s", qPrintable(describe(exported.samples)));

		for (std::size_t i = 0; i < frames.size(); ++i)
		{
			if (frames[i] >= -1.0f && frames[i] <= 1.0f)
			{
				QVERIFY2(fixed.samples[i] == preFix.samples[i],
					qPrintable(QString("in-range frame %1 (%2) changed: %3 -> %4")
						.arg(static_cast<qulonglong>(i)).arg(static_cast<double>(frames[i]))
						.arg(preFix.samples[i]).arg(fixed.samples[i])));
			}
			else
			{
				QVERIFY2(fixed.samples[i] != preFix.samples[i],
					qPrintable(QString("out-of-range frame %1 (%2) was not repaired")
						.arg(static_cast<qulonglong>(i)).arg(static_cast<double>(frames[i]))));
				// The repair is a clamp to the rail: the sign survives.
				QVERIFY2((fixed.samples[i] > 0) == (frames[i] > 0),
					qPrintable(QString("out-of-range frame %1 (%2) lost its sign: %3")
						.arg(static_cast<qulonglong>(i)).arg(static_cast<double>(frames[i]))
						.arg(fixed.samples[i])));
			}
		}

		// The export writers' sequence (SFC_SET_CLIPPING) never wraps either.
		for (std::size_t i = 0; i < frames.size(); ++i)
		{
			QVERIFY2((exported.samples[i] > 0) == (frames[i] > 0),
				qPrintable(QString("the export write sequence wrapped frame %1 (%2): %3")
					.arg(static_cast<qulonglong>(i)).arg(static_cast<double>(frames[i]))
					.arg(exported.samples[i])));
		}
		// The two clipping mechanisms agree to within one 24-bit LSB on every
		// sample, so the recorder does not sound different from an export.
		for (std::size_t i = 0; i < frames.size(); ++i)
		{
			QVERIFY2(std::abs(fixed.samples[i] - exported.samples[i]) <= OneLsbOf24Bit,
				qPrintable(QString("the recorder's clamp and the export writers' SFC flag "
					"disagree by more than 1 LSB at frame %1: %2 vs %3")
					.arg(static_cast<qulonglong>(i))
					.arg(fixed.samples[i]).arg(exported.samples[i])));
		}
	}
} ;


QTEST_APPLESS_MAIN(RecordClipTest)

#include "RecordClipTest.moc"
