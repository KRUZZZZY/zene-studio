/*
 * SfizzLeakDetectorTest.cpp - residual leak counts are reported exactly
 *
 * The test runs the debug-only leak detector in child processes so its
 * shutdown assertion cannot terminate the QtTest runner.
 */

#include <QtTest>

#include <QApplication>
#include <QCoreApplication>
#include <QProcess>

#include <cstring>

#include "utility/LeakDetector.h"

namespace
{
class TestOwner
{
public:
	LEAK_DETECTOR(TestOwner)
};

struct ChildResult
{
	QProcess::ExitStatus status = QProcess::NormalExit;
	int exitCode = -1;
	QByteArray standardError;
};

bool runChild(const QString& mode, ChildResult& result, QString& error)
{
	QProcess child;
	child.start(QCoreApplication::applicationFilePath(), {mode});
	if (!child.waitForStarted(-1))
	{
		error = child.errorString();
		return false;
	}
	if (!child.waitForFinished(-1))
	{
		error = QStringLiteral("child did not finish: %1").arg(child.errorString());
		return false;
	}
	result.status = child.exitStatus();
	result.exitCode = child.exitCode();
	result.standardError = child.readAllStandardError();
	return true;
}

int runChildMode(const char* mode)
{
	if (std::strcmp(mode, "--clean-child") == 0)
	{
		TestOwner owners[3];
		return 0;
	}
	if (std::strcmp(mode, "--leak-child") == 0)
	{
		static_cast<void>(new TestOwner[3]);
		return 0;
	}
	return -1;
}
} // namespace

class SfizzLeakDetectorTest : public QObject
{
	Q_OBJECT

private slots:
	void cleanShutdownDoesNotReportResiduals()
	{
		ChildResult child;
		QString error;
		QVERIFY2(runChild(QStringLiteral("--clean-child"), child, error), qPrintable(error));
		QCOMPARE(child.status, QProcess::NormalExit);
		QCOMPARE(child.exitCode, 0);
		QVERIFY2(!child.standardError.contains("Leaked "), child.standardError.constData());
	}

	void leakDiagnosticUsesTheResidualInstanceCount()
	{
		ChildResult child;
		QString error;
		QVERIFY2(runChild(QStringLiteral("--leak-child"), child, error), qPrintable(error));
		QCOMPARE(child.status, QProcess::CrashExit);
		QVERIFY2(child.standardError.contains("Leaked 3 instance(s) of class TestOwner"),
			child.standardError.constData());
	}
};

int main(int argc, char** argv)
{
	if (argc == 2)
	{
		const int childResult = runChildMode(argv[1]);
		if (childResult >= 0) { return childResult; }
	}

	QApplication app(argc, argv);
	SfizzLeakDetectorTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "SfizzLeakDetectorTest.moc"
