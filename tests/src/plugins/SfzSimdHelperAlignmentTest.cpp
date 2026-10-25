/*
 * SfzSimdHelperAlignmentTest.cpp - independent buffer alignment in sfizz's SSE helpers
 *
 * The division helper must handle an unaligned divisor even when its input and output are aligned.
 */

#include <QtTest>

#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>

#include <array>

#include "sfizz/simd/HelpersSSE.h"

class SfzSimdHelperAlignmentTest : public QObject
{
	Q_OBJECT

private:
	bool isProbeProcess() const
	{
		return qEnvironmentVariableIsSet("SFIZZ_DIVIDE_SSE_PROBE");
	}

	bool runProbe(const char* functionName, QString& failure) const
	{
		QProcess probe;
		auto environment = QProcessEnvironment::systemEnvironment();
		environment.insert(QStringLiteral("SFIZZ_DIVIDE_SSE_PROBE"), QStringLiteral("1"));
		probe.setProcessEnvironment(environment);
		probe.start(QCoreApplication::applicationFilePath(), {QString::fromLatin1(functionName)});
		if (!probe.waitForStarted(-1))
		{
			failure = QStringLiteral("probe could not start: %1").arg(probe.errorString());
			return false;
		}
		if (!probe.waitForFinished(-1))
		{
			failure = QStringLiteral("probe did not finish: %1").arg(probe.errorString());
			return false;
		}
		if (probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) { return true; }
		failure = QStringLiteral("probe failed: %1").arg(QString::fromLocal8Bit(probe.readAllStandardError()));
		return false;
	}

private slots:
	void unalignedDivisorWithAlignedInputAndOutput()
	{
		if (isProbeProcess())
		{
			alignas(16) const std::array<float, 8> input{2.f, 4.f, 6.f, 8.f, 10.f, 12.f, 14.f, 16.f};
			alignas(16) const std::array<float, 9> divisorStorage{0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
			alignas(16) std::array<float, 8> output{};
			output.fill(-1.f);

			divideSSE(input.data(), divisorStorage.data() + 1, output.data(), 7);

			for (unsigned i = 0; i < 7; ++i) { QCOMPARE(output[i], 2.f); }
			QCOMPARE(output[7], -1.f);
			return;
		}

		QString failure;
		QVERIFY2(runProbe(__func__, failure), qPrintable(failure));
	}

	void unalignedGainWithAlignedInputAndOutputIsSafeForMultiplyAdd()
	{
		if (isProbeProcess())
		{
			alignas(16) const std::array<float, 8> gainInput{1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
			alignas(16) const std::array<float, 9> gainStorage{0.f, 2.f, 2.f, 2.f, 2.f, 2.f, 2.f, 2.f, 2.f};
			alignas(16) std::array<float, 8> output{1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};

			multiplyAddSSE(gainStorage.data() + 1, gainInput.data(), output.data(), 7);

			for (unsigned i = 0; i < 7; ++i) { QCOMPARE(output[i], 3.f); }
			QCOMPARE(output[7], 1.f);
			return;
		}

		QString failure;
		QVERIFY2(runProbe(__func__, failure), qPrintable(failure));
	}

	void unalignedGainWithAlignedInputAndOutputIsSafeForMultiplyMul()
	{
		if (isProbeProcess())
		{
			alignas(16) const std::array<float, 8> input{1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
			alignas(16) const std::array<float, 9> gainStorage{0.f, 3.f, 3.f, 3.f, 3.f, 3.f, 3.f, 3.f, 3.f};
			alignas(16) std::array<float, 8> output{2.f, 2.f, 2.f, 2.f, 2.f, 2.f, 2.f, 2.f};

			multiplyMulSSE(gainStorage.data() + 1, input.data(), output.data(), 7);

			for (unsigned i = 0; i < 7; ++i) { QCOMPARE(output[i], 6.f); }
			QCOMPARE(output[7], 2.f);
			return;
		}

		QString failure;
		QVERIFY2(runProbe(__func__, failure), qPrintable(failure));
	}

	void alignedBuffersUseVectorPath()
	{
		alignas(16) const std::array<float, 8> input{2.f, 4.f, 6.f, 8.f, 10.f, 12.f, 14.f, 16.f};
		alignas(16) const std::array<float, 8> divisor{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
		alignas(16) std::array<float, 8> output{};

		divideSSE(input.data(), divisor.data(), output.data(), static_cast<unsigned>(input.size()));

		for (const float value : output) { QCOMPARE(value, 2.f); }
	}

	void scalarAndEmptyRangesAreSafe()
	{
		alignas(16) const std::array<float, 4> input{2.f, 4.f, 6.f, 8.f};
		alignas(16) const std::array<float, 5> divisorStorage{0.f, 1.f, 2.f, 3.f, 4.f};
		alignas(16) std::array<float, 4> output{};
		output.fill(-1.f);

		divideSSE(input.data(), divisorStorage.data() + 1, output.data(), 3);
		for (unsigned i = 0; i < 3; ++i) { QCOMPARE(output[i], 2.f); }
		QCOMPARE(output[3], -1.f);

		divideSSE(input.data(), divisorStorage.data() + 1, output.data(), 0);
		QCOMPARE(output[3], -1.f);
	}

	void unalignedInputOrOutputUsesScalarFallback()
	{
		alignas(16) const std::array<float, 9> inputStorage{0.f, 2.f, 4.f, 6.f, 8.f, 10.f, 12.f, 14.f, 16.f};
		alignas(16) const std::array<float, 8> alignedInput{2.f, 4.f, 6.f, 8.f, 10.f, 12.f, 14.f, 16.f};
		alignas(16) const std::array<float, 8> divisor{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
		alignas(16) std::array<float, 8> outputStorage{};

		divideSSE(inputStorage.data() + 1, divisor.data(), outputStorage.data(), 7);
		for (unsigned i = 0; i < 7; ++i) { QCOMPARE(outputStorage[i], 2.f); }

		outputStorage.fill(-1.f);
		divideSSE(alignedInput.data(), divisor.data(), outputStorage.data() + 1, 7);
		for (unsigned i = 1; i < 8; ++i) { QCOMPARE(outputStorage[i], 2.f); }
		QCOMPARE(outputStorage[0], -1.f);
	}
};

QTEST_GUILESS_MAIN(SfzSimdHelperAlignmentTest)
#include "SfzSimdHelperAlignmentTest.moc"
