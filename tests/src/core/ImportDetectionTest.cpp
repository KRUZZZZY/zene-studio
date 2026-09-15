/*
 * ImportDetectionTest.cpp - the registered ENGINE proof for import detection
 *                           (feature row 34 of docs/FEATURE-LIST-0.3.0.md).
 *
 * WHAT THIS PROVES. That the engine recovers a SYNTHESISED input's own answer,
 * through the SAME path an import goes through (a RIFF/WAVE on disk read by the
 * engine's SampleDecoder, not a buffer handed to the arithmetic):
 *
 *   - a click track built at 128 BPM comes back within 0.5 BPM of 128, with its
 *     first transient within 30 ms of the 0.500 s the fixture puts it at, and a
 *     second fixture at 90 BPM is measured too (one tempo proves a constant, two
 *     prove an arithmetic);
 *   - an A major scale over an A bass comes back as tonic A plus a scale name
 *     the PRE-EXISTING vocabulary answers to (ChordTable::getScaleByName), and
 *     that name is in the list candidateScales() publishes;
 *   - a file with no transients and a path that does not exist are REFUSED, not
 *     guessed at;
 *   - the project's detected-key field round-trips through its own XML, and
 *     clears on an absent element (the reset-on-absence rule that makes the
 *     pre-first-detection state reachable again).
 *
 * WHAT IT DOES NOT PROVE, in the words the release documents use: real-world
 * detection accuracy is UNVERIFIED. A click track is the easy case for an
 * onset/autocorrelation estimate; no real-music corpus was measured on this box,
 * and the scores this test prints are the detector's own, not probabilities.
 * docs/IMPORT-DETECTION.md is the record.
 *
 * The command surface has its own proof - the registered ctest
 * ControlDetectCommands (tests/control-detect-commands.py), which drives the real
 * binary over --control-socket and checks that detect.apply writes the PROJECT's
 * fields, that a save carries them and that one control.undo takes them off.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <cmath>
#include <numbers>
#include <vector>

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QString>
#include <QDomElement>
#include <QTemporaryDir>

#include "ImportDetection.h"
#include "ImportDetectionDsp.h"
#include "ProjectKey.h"

using namespace lmms;

namespace
{

constexpr int kSampleRate = 44100;
constexpr double kClickBpmA = 128.0;
constexpr double kClickBpmB = 90.0;
constexpr double kBpmTolerance = 0.5;
constexpr double kFirstBeatSeconds = 0.5;
constexpr double kOnsetToleranceSeconds = 0.03;
//! The scale fixture's own answer: A (pitch class 9) major, rooted at 220 Hz.
constexpr int kExpectedTonic = 9;

/*! Writes \a samples as a canonical 16-bit mono RIFF/WAVE.
 *
 *  BYTE BY BYTE, not with libsndfile: libsndfile's include directory is private
 *  to lmmsobjs (the note tests/src/core/BrowserTestSupport.h makes), and a
 *  fixture produced by the same library the decoder links would prove less than
 *  one produced independently. The checks below are what validate the writer: a
 *  wrong header makes the decoder refuse the file and every tempo/key assertion
 *  fail. */
bool writeWave(const QString& path, const std::vector<double>& samples)
{
	QByteArray data;
	QDataStream stream(&data, QIODevice::WriteOnly);
	stream.setByteOrder(QDataStream::LittleEndian);
	for (const double value : samples)
	{
		const double clamped = std::max(-1.0, std::min(1.0, value));
		stream << static_cast<qint16>(std::lround(clamped * 32767.0));
	}

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	QDataStream out(&file);
	out.setByteOrder(QDataStream::LittleEndian);
	const quint32 dataBytes = static_cast<quint32>(data.size());
	const quint32 byteRate = kSampleRate * 2;
	out.writeRawData("RIFF", 4);
	out << quint32(36 + dataBytes);
	out.writeRawData("WAVE", 4);
	out.writeRawData("fmt ", 4);
	out << quint32(16) << quint16(1) << quint16(1) << quint32(kSampleRate) << byteRate
		<< quint16(2) << quint16(16);
	out.writeRawData("data", 4);
	out << dataBytes;
	file.write(data);
	file.close();
	return true;
}

//! A click track at \a bpm, 25 ms decaying 1 kHz bursts, first burst at 0.5 s.
std::vector<double> clickTrack(double bpm, double seconds)
{
	std::vector<double> samples(static_cast<std::size_t>(seconds * kSampleRate), 0.0);
	const int burst = kSampleRate / 40;
	for (double beat = kFirstBeatSeconds; beat < seconds; beat += 60.0 / bpm)
	{
		const std::size_t start = static_cast<std::size_t>(beat * kSampleRate);
		for (int i = 0; i < burst && start + i < samples.size(); ++i)
		{
			const double envelope = std::exp(-8.0 * i / burst);
			samples[start + i] += 0.8 * envelope
				* std::sin(2.0 * std::numbers::pi * 1000.0 * i / kSampleRate);
		}
	}
	return samples;
}

//! The A major scale over an A bass, with fades so the tones have no click.
std::vector<double> majorFixture()
{
	std::vector<double> samples(static_cast<std::size_t>(8.0 * kSampleRate), 0.0);
	const std::array<double, 8> scale = {220.00, 246.94, 277.18, 293.66,
		329.63, 369.99, 415.30, 440.00};
	auto addTone = [&samples](double hz, double startSeconds, double lengthSeconds,
		double amplitude) {
		const std::size_t start = static_cast<std::size_t>(startSeconds * kSampleRate);
		const std::size_t length = static_cast<std::size_t>(lengthSeconds * kSampleRate);
		const std::size_t fade = static_cast<std::size_t>(0.02 * kSampleRate);
		for (std::size_t i = 0; i < length && start + i < samples.size(); ++i)
		{
			double envelope = 1.0;
			if (i < fade) { envelope = static_cast<double>(i) / fade; }
			else if (i + fade >= length) { envelope = static_cast<double>(length - i) / fade; }
			samples[start + i] += amplitude * envelope
				* std::sin(2.0 * std::numbers::pi * hz * i / kSampleRate);
		}
	};
	for (int repeat = 0; repeat < 2; ++repeat)
	{
		for (std::size_t note = 0; note < scale.size(); ++note)
		{
			addTone(scale[note], repeat * 3.0 + note * 0.35, 0.32, 0.35);
		}
	}
	addTone(110.0, 0.0, 8.0, 0.45);
	return samples;
}

//! A steady tone: no transients at all.
std::vector<double> steadyTone()
{
	std::vector<double> samples(static_cast<std::size_t>(6.0 * kSampleRate), 0.0);
	for (std::size_t i = 0; i < samples.size(); ++i)
	{
		samples[i] = 0.4 * std::sin(2.0 * std::numbers::pi * 220.0 * i / kSampleRate);
	}
	return samples;
}

} // namespace

class ImportDetectionTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY2(m_dir.isValid(), "no temporary directory for the detection fixtures");
		m_clickA = QDir(m_dir.path()).filePath(QStringLiteral("click-128.wav"));
		m_clickB = QDir(m_dir.path()).filePath(QStringLiteral("click-90.wav"));
		m_major = QDir(m_dir.path()).filePath(QStringLiteral("a-major.wav"));
		m_tone = QDir(m_dir.path()).filePath(QStringLiteral("steady-tone.wav"));
		m_missing = QDir(m_dir.path()).filePath(QStringLiteral("no-such-file.wav"));

		QVERIFY(writeWave(m_clickA, clickTrack(kClickBpmA, 10.0)));
		QVERIFY(writeWave(m_clickB, clickTrack(kClickBpmB, 10.0)));
		QVERIFY(writeWave(m_major, majorFixture()));
		QVERIFY(writeWave(m_tone, steadyTone()));
	}

	/*! Two known tempos, through the decoder. The engine reports the method it
	 *  used beside the number, so the check also pins the method's name. */
	void theTempoOfASynthesisedClickTrackIsRecovered()
	{
		for (const auto& pair : {std::make_pair(kClickBpmA, m_clickA),
				std::make_pair(kClickBpmB, m_clickB)})
		{
			const ImportDetectionResult analysis = analyseAudioFile(pair.second);
			QVERIFY2(analysis.ok, qPrintable(analysis.error));
			QVERIFY2(analysis.tempo.found, "no tempo was found in a click track");
			QVERIFY2(std::abs(analysis.tempo.bpm - pair.first) <= kBpmTolerance,
				qPrintable(QStringLiteral("a click track at %1 BPM was measured at %2 BPM")
					.arg(pair.first).arg(analysis.tempo.bpm, 0, 'f', 3)));
			qInfo("%s click track measured %.3f BPM (confidence %.3f, %d transients)",
				qPrintable(QString::number(pair.first)), analysis.tempo.bpm,
				analysis.tempo.confidence, analysis.tempo.onsets);
			QCOMPARE(analysis.tempoMethod, QString::fromLatin1(detection::TempoMethodName));
			QVERIFY2(analysis.tempo.confidence > 0.2,
				"the reported correlation is below the floor the estimate promises");
		}
	}

	//! The first transient is part of the answer: BACKLOG.md item 10 names it.
	void theFirstTransientIsFound()
	{
		const ImportDetectionResult analysis = analyseAudioFile(m_clickA);
		QVERIFY2(analysis.ok, qPrintable(analysis.error));
		QVERIFY2(std::abs(analysis.tempo.firstOnsetSeconds - kFirstBeatSeconds)
			<= kOnsetToleranceSeconds,
			qPrintable(QStringLiteral("the fixture's first click is at %1 s, the estimate found %2 s")
				.arg(kFirstBeatSeconds).arg(analysis.tempo.firstOnsetSeconds, 0, 'f', 3)));
	}

	/*! The key, named in the vocabulary that already existed. The name is
	 *  checked against ChordTable through scaleNameIsKnown() AND against the list
	 *  the commands publish, so a name that leaked in from anywhere else fails. */
	void theKeyIsNamedInThePreexistingVocabulary()
	{
		const ImportDetectionResult analysis = analyseAudioFile(m_major);
		QVERIFY2(analysis.ok, qPrintable(analysis.error));
		QVERIFY2(analysis.key.found, "no key was found in a scale fixture");
		QCOMPARE(analysis.key.tonicPitchClass, kExpectedTonic);
		qInfo("A major fixture measured tonic %s, scale %s (score %.3f, margin %.3f)",
			qPrintable(analysis.tonicName), qPrintable(analysis.scaleName),
			analysis.key.score, analysis.key.margin);
		QCOMPARE(analysis.tonicName, QStringLiteral("A"));
		QVERIFY(analysis.scaleKnown);
		QVERIFY2(scaleNameIsKnown(analysis.scaleName),
			qPrintable(QStringLiteral("`%1` is not a name ChordTable answers to")
				.arg(analysis.scaleName)));

		const QVector<ScaleCandidate>& candidates = candidateScales();
		QVERIFY2(!candidates.isEmpty(), "the scale vocabulary came back empty");
		bool listed = false;
		for (const ScaleCandidate& candidate : candidates)
		{
			QVERIFY2(scaleNameIsKnown(candidate.name),
				qPrintable(QStringLiteral("candidate `%1` is not a ChordTable scale")
					.arg(candidate.name)));
			QVERIFY(detection::maskDegreeCount(candidate.mask) <= detection::MaxTemplateDegrees);
			if (candidate.name == analysis.scaleName) { listed = true; }
		}
		QVERIFY2(listed, "the reported scale is not in the published vocabulary");
		QCOMPARE(analysis.keyMethod, QString::fromLatin1(detection::KeyMethodName));
	}

	//! Nothing to find is REFUSED rather than guessed at.
	void aFileWithNoTransientsIsRefused()
	{
		const ImportDetectionResult tone = analyseAudioFile(m_tone);
		QVERIFY2(tone.ok, qPrintable(tone.error));
		QVERIFY2(!tone.tempo.found,
			qPrintable(QStringLiteral("a steady tone produced a tempo: %1 BPM from %2 transients")
				.arg(tone.tempo.bpm).arg(tone.tempo.onsets)));

		const ImportDetectionResult missing = analyseAudioFile(m_missing);
		QVERIFY2(!missing.ok, "a path that does not exist was reported as analysed");
		qInfo("a path that does not exist is refused: %s", qPrintable(missing.error));
		QVERIFY2(!missing.error.isEmpty(), "the refusal carries no message");

		const ImportDetectionResult unnamed = analyseAudioFile(QString());
		QVERIFY2(!unnamed.ok, "an empty path was reported as analysed");

		// The arithmetic's own refusal path: an empty vocabulary has no key.
		const std::vector<float> silence(4096, 0.0f);
		const detection::KeyEstimate none =
			detection::estimateKey(silence.data(), silence.size(), kSampleRate, {});
		QVERIFY(!none.found);
	}

	/*! The project's own field: the XML round trip the A16 action checkpoint
	 *  rides on, and the reset-on-absence rule. */
	void theProjectKeyRoundTripsThroughItsXml()
	{
		ProjectKey key;
		QVERIFY(key.empty());
		QVERIFY(!key.shouldPersist());
		QCOMPARE(key.tonicPitchClass(), ProjectKey::UnknownPitchClass);

		key.set(QStringLiteral("A"), 9, QStringLiteral("Major"), 1.16, 0.06,
			QString::fromLatin1(detection::KeyMethodName), QStringLiteral("/tmp/a-major.wav"));
		QVERIFY(!key.empty());
		QVERIFY(key.shouldPersist());

		const QString xml = key.toXml();
		QVERIFY(xml.contains(QStringLiteral("<detected-key")));
		QVERIFY(xml.contains(QStringLiteral("tonic=\"A\"")));

		ProjectKey restored;
		QVERIFY(restored.fromXml(xml));
		QCOMPARE(restored.tonicName(), QStringLiteral("A"));
		QCOMPARE(restored.tonicPitchClass(), 9);
		QCOMPARE(restored.scaleName(), QStringLiteral("Major"));
		QCOMPARE(restored.method(), QString::fromLatin1(detection::KeyMethodName));
		QCOMPARE(restored.sourcePath(), QStringLiteral("/tmp/a-major.wav"));

		// Absence is a VALUE (the empty key), which is what makes the
		// pre-first-detection state reachable from a journal checkpoint.
		ProjectKey cleared;
		QVERIFY(cleared.fromXml(xml));
		cleared.loadSettings(QDomElement());
		QVERIFY(cleared.empty());
		QVERIFY(cleared.tonicName().isEmpty());
	}

private:
	QTemporaryDir m_dir;
	QString m_clickA;
	QString m_clickB;
	QString m_major;
	QString m_tone;
	QString m_missing;
};

QTEST_MAIN(ImportDetectionTest)
#include "ImportDetectionTest.moc"
