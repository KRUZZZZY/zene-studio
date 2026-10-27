#include <QtTest>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>

#include <QFile>
#include <QTemporaryDir>

#include "DrumSynth.h"

using namespace lmms;

class DrumSynthLevelTest : public QObject
{
	Q_OBJECT

private slots:
	void largeToneLevelDoesNotWrapBeforeSquaring();
	void largeToneLevelWithDistortionClampsBeforeShortCast();
	void inRangeToneLevelWithDistortionKeepsClipPoint();
};

void DrumSynthLevelTest::largeToneLevelDoesNotWrapBeforeSquaring()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.filePath("large-level.ds");
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
	QVERIFY(file.write(
		"[General]\n"
		"Version=DrumSynth v2.0\n"
		"Stretch=100\n"
		"[Tone]\n"
		"On=1\n"
		"Level=50000\n"
		"F1=200\n"
		"F2=200\n"
		"Phase=90\n"
		"Envelope=0,100 2400,100 4800,0\n") > 0);
	file.close();

	int16_t* output = nullptr;
	DrumSynth synth;
	const int frames = synth.GetDSFileSamples(path, output, 1, 44100);
	const std::unique_ptr<int16_t[]> samples{output};
	QVERIFY(frames > 0);
	QVERIFY(samples != nullptr);
	QCOMPARE(samples[0], static_cast<int16_t>(32700));
}

// Tone Level=50000 squares to 2.5e9, which is outside the short range. With
// distortion on, that loudest-envelope product is the clip point, so it must be
// clamped to the short range before it is converted. The clip point is capped at
// 32700, so the rendered tone must peak exactly there and must not go silent.
void DrumSynthLevelTest::largeToneLevelWithDistortionClampsBeforeShortCast()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.filePath("large-level-distortion.ds");
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
	QVERIFY(file.write(
		"[General]\n"
		"Version=DrumSynth v2.0\n"
		"Stretch=100\n"
		"[Tone]\n"
		"On=1\n"
		"Level=50000\n"
		"F1=200\n"
		"F2=200\n"
		"Phase=90\n"
		"Envelope=0,100 2400,100 4800,0\n"
		"[Distortion]\n"
		"On=1\n"
		"Rate=0\n"
		"Bits=0\n"
		"Clipping=0\n") > 0);
	file.close();

	int16_t* output = nullptr;
	DrumSynth synth;
	const int frames = synth.GetDSFileSamples(path, output, 1, 44100);
	const std::unique_ptr<int16_t[]> samples{output};
	QVERIFY(frames > 0);
	QVERIFY(samples != nullptr);

	int peak = 0;
	for (int i = 0; i < frames; ++i)
	{
		peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
	}
	QCOMPARE(peak, 32700);
}

// Tone Level=100 squares to 10000, which is inside the short range. The clip point
// must stay exactly 10000, as it was before the clamp, so ordinary distorted files
// render unchanged.
void DrumSynthLevelTest::inRangeToneLevelWithDistortionKeepsClipPoint()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.filePath("in-range-level-distortion.ds");
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
	QVERIFY(file.write(
		"[General]\n"
		"Version=DrumSynth v2.0\n"
		"Stretch=100\n"
		"[Tone]\n"
		"On=1\n"
		"Level=100\n"
		"F1=200\n"
		"F2=200\n"
		"Phase=90\n"
		"Envelope=0,100 2400,100 4800,0\n"
		"[Distortion]\n"
		"On=1\n"
		"Rate=0\n"
		"Bits=0\n"
		"Clipping=0\n") > 0);
	file.close();

	int16_t* output = nullptr;
	DrumSynth synth;
	const int frames = synth.GetDSFileSamples(path, output, 1, 44100);
	const std::unique_ptr<int16_t[]> samples{output};
	QVERIFY(frames > 0);
	QVERIFY(samples != nullptr);

	int peak = 0;
	for (int i = 0; i < frames; ++i)
	{
		peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
	}
	QCOMPARE(peak, 10000);
}

QTEST_APPLESS_MAIN(DrumSynthLevelTest)

#include "DrumSynthLevelTest.moc"
