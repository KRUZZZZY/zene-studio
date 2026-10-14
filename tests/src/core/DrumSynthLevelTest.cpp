#include <QtTest>

#include <cstdint>
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

QTEST_APPLESS_MAIN(DrumSynthLevelTest)

#include "DrumSynthLevelTest.moc"
