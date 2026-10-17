#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QString>
#include <QThread>

#include <fstream>
#include <sstream>
#include <string>

#include "AudioEngineProfiler.h"

class AudioEngineProfilerTest : public QObject
{
	Q_OBJECT

private slots:
	void finishPeriodDoesNotTouchOutputSynchronously()
	{
		std::ifstream in("../../src/core/AudioEngineProfiler.cpp");
		QVERIFY(in.is_open());
		std::ostringstream ss;
		ss << in.rdbuf();
		const std::string source = ss.str();

		const std::string marker = "void AudioEngineProfiler::finishPeriod";
		const std::size_t start = source.find(marker);
		QVERIFY(start != std::string::npos);
		const std::size_t end = source.find("\n}\n", start);
		QVERIFY(end != std::string::npos);
		const std::string body = source.substr(start, end - start);

		QVERIFY2(body.find("m_outputFile") == std::string::npos,
			"finishPeriod still references the output file on the audio path");
		QVERIFY2(body.find("QString") == std::string::npos,
			"finishPeriod still formats a QString on the audio path");
	}

	void writerThreadOwnsFileLifecycle()
	{
		std::ifstream in("../../src/core/AudioEngineProfiler.cpp");
		QVERIFY(in.is_open());
		std::ostringstream ss;
		ss << in.rdbuf();
		const std::string source = ss.str();

		auto extractFunction = [&](const std::string& name) -> std::string {
			const std::string marker = "void AudioEngineProfiler::" + name;
			const std::size_t start = source.find(marker);
			if (start == std::string::npos) return "";
			const std::size_t end = source.find("\n}\n", start);
			if (end == std::string::npos) return "";
			return source.substr(start, end - start);
		};

		const std::string setOutputFile = extractFunction("setOutputFile");
		QVERIFY2(!setOutputFile.empty(), "setOutputFile not found");
		QVERIFY2(setOutputFile.find("m_outputFile.open") == std::string::npos,
			"setOutputFile opens the file on caller thread");

		const std::string stopWriter = extractFunction("stopWriter");
		QVERIFY2(!stopWriter.empty(), "stopWriter not found");
		QVERIFY2(stopWriter.find("m_outputFile") == std::string::npos,
			"stopWriter accesses the file on caller thread");

		const std::string writerLoop = extractFunction("writerLoop");
		QVERIFY2(!writerLoop.empty(), "writerLoop not found");
		QVERIFY2(writerLoop.find("m_outputFile.open") != std::string::npos,
			"writerLoop does not open the file");
		QVERIFY2(writerLoop.find("m_outputFile.close") != std::string::npos,
			"writerLoop does not close the file");
	}

	void writesMeasurementToOutputFile()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.path() + "/profile.txt";

		{
			lmms::AudioEngineProfiler profiler;
			profiler.setOutputFile(path);
			profiler.finishPeriod(48000, 256);
			QThread::msleep(50);
		}

		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		const QByteArray data = file.readAll();
		QVERIFY(!data.isEmpty());
	}
};

QTEST_GUILESS_MAIN(AudioEngineProfilerTest)

#include "AudioEngineProfilerTest.moc"
