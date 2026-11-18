#include <QCoreApplication>
#include <QProcess>
#include <QtTest>

#include <cstdlib>
#include <cstring>

extern "C"
{
#include "../../../plugins/ReverbSC/base.h"
}

extern "C"
{
void reverbscTestFailNextMalloc();
void reverbscTestFailNextCalloc();
}

namespace
{
int runAllocatorFailureChild(const char* allocator)
{
	sp_data* sp = nullptr;
	if (std::strcmp(allocator, "malloc") == 0)
	{
		reverbscTestFailNextMalloc();
	}
	else if (std::strcmp(allocator, "calloc") == 0)
	{
		reverbscTestFailNextCalloc();
	}
	else
	{
		return EXIT_FAILURE;
	}

	const auto result = sp_createn(&sp, 2);
	const bool refused = result == SP_NOT_OK && sp == nullptr;
	if (sp != nullptr)
	{
		sp_destroy(&sp);
	}
	return refused ? EXIT_SUCCESS : EXIT_FAILURE;
}
}

class ReverbSCCreateTest : public QObject
{
	Q_OBJECT
private slots:
	void rejectsNonPositiveChannels()
	{
		for (const int channels : {0, -1})
		{
			sp_data* sp = nullptr;
			const auto result = sp_createn(&sp, channels);
			const bool outputWasCleared = sp == nullptr;
			if (sp != nullptr)
			{
				sp_destroy(&sp);
			}
			QCOMPARE(result, SP_NOT_OK);
			QVERIFY(outputWasCleared);
		}
	}

	void reportsMallocFailure()
	{
		verifyAllocatorFailure("malloc");
	}

	void reportsCallocFailure()
	{
		verifyAllocatorFailure("calloc");
	}

	void createsRequestedChannels()
	{
		sp_data* sp = nullptr;
		QCOMPARE(sp_createn(&sp, 2), SP_OK);
		QVERIFY(sp != nullptr);
		QCOMPARE(sp->nchan, 2);
		QVERIFY(sp->out != nullptr);
		QCOMPARE(sp->out[0], 0.0f);
		QCOMPARE(sp->out[1], 0.0f);
		sp_destroy(&sp);
	}

private:
	void verifyAllocatorFailure(const QString& allocator)
	{
		QProcess child;
		child.start(QCoreApplication::applicationFilePath(),
			{"--allocator-failure", allocator});
		QVERIFY2(child.waitForStarted(), qPrintable(child.errorString()));
		QVERIFY2(child.waitForFinished(5000), qPrintable(child.errorString()));
		QVERIFY(child.exitStatus() == QProcess::NormalExit);
		QCOMPARE(child.exitCode(), EXIT_SUCCESS);
	}
};

int main(int argc, char** argv)
{
	if (argc == 3 && std::strcmp(argv[1], "--allocator-failure") == 0)
	{
		return runAllocatorFailureChild(argv[2]);
	}

	QCoreApplication app(argc, argv);
	ReverbSCCreateTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "ReverbSCCreateTest.moc"
