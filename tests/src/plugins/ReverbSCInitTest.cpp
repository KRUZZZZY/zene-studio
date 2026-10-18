/*
 * ReverbSCInitTest.cpp - allocation failure contract for ReverbSC's reverb core
 *
 * This file is part of Zene Studio - https://zene.studio
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
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA.
 */

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <QCoreApplication>
#include <QProcess>
#include <QtTest>

extern "C"
{
#include "base.h"
#include "revsc.h"
}

extern "C" void* sp_revsc_test_malloc(std::size_t size);

namespace
{
int g_failNextAllocation = 0;

/* The failure has to be provoked inside a child process: before the fix the
 * rejected auxiliary buffer is still handed to init_delay_line(), which
 * memsets through the null pointer and takes the whole process down. The
 * parent turns that crash into an ordinary assertion failure. */
int runCreateChild()
{
	sp_revsc* revsc = nullptr;
	g_failNextAllocation = 1;
	const int result = sp_revsc_create(&revsc);
	const bool refused = (result == SP_NOT_OK) && (revsc == nullptr);
	if (revsc != nullptr)
	{
		sp_revsc_destroy(&revsc);
	}
	return refused ? EXIT_SUCCESS : EXIT_FAILURE;
}

int runInitChild()
{
	sp_data* sp = nullptr;
	if (sp_createn(&sp, 2) != SP_OK)
	{
		return EXIT_FAILURE;
	}
	sp_revsc* revsc = nullptr;
	if (sp_revsc_create(&revsc) != SP_OK)
	{
		sp_destroy(&sp);
		return EXIT_FAILURE;
	}

	g_failNextAllocation = 1;
	const int result = sp_revsc_init(sp, revsc);
	/* A state that refused its delay lines must not advertise itself as
	 * initialised, must not keep a buffer pointer it never received, and
	 * must stay safe to feed to compute(). */
	const bool refused = (result == SP_NOT_OK) && (revsc->initDone == 0)
		&& (revsc->aux.ptr == nullptr);

	SPFLOAT in1 = 0.0f, in2 = 0.0f, out1 = 0.0f, out2 = 0.0f;
	const int computed = sp_revsc_compute(sp, revsc, &in1, &in2, &out1, &out2);

	sp_revsc_destroy(&revsc);
	sp_destroy(&sp);
	return (refused && (computed == SP_NOT_OK)) ? EXIT_SUCCESS : EXIT_FAILURE;
}

int runChild(const char* which)
{
	if (std::strcmp(which, "create") == 0)
	{
		return runCreateChild();
	}
	if (std::strcmp(which, "init") == 0)
	{
		return runInitChild();
	}
	return EXIT_FAILURE;
}
}

extern "C" void* sp_revsc_test_malloc(std::size_t size)
{
	if (g_failNextAllocation != 0)
	{
		g_failNextAllocation = 0;
		return nullptr;
	}
	return std::malloc(size);
}

class ReverbSCInitTest : public QObject
{
	Q_OBJECT
private slots:
	void createReportsStateAllocationFailure()
	{
		verifyChildRefuses("create");
	}

	void initReportsDelayLineAllocationFailure()
	{
		verifyChildRefuses("init");
	}

	void initialisesDelayLinesWhenAllocationSucceeds()
	{
		sp_data* sp = nullptr;
		QCOMPARE(sp_createn(&sp, 2), SP_OK);
		sp_revsc* revsc = nullptr;
		QCOMPARE(sp_revsc_create(&revsc), SP_OK);
		QVERIFY(revsc != nullptr);
		QCOMPARE(sp_revsc_init(sp, revsc), SP_OK);
		QCOMPARE(revsc->initDone, 1);
		QVERIFY(revsc->aux.ptr != nullptr);
		for (int line = 0; line < 8; ++line)
		{
			QVERIFY(revsc->delayLines[line].buf != nullptr);
			QVERIFY(revsc->delayLines[line].bufferSize > 0);
		}
		sp_revsc_destroy(&revsc);
		sp_destroy(&sp);
	}

private:
	void verifyChildRefuses(const QString& which)
	{
		QProcess child;
		child.start(QCoreApplication::applicationFilePath(),
			{"--allocation-failure", which});
		QVERIFY2(child.waitForStarted(), qPrintable(child.errorString()));
		QVERIFY2(child.waitForFinished(5000), qPrintable(child.errorString()));
		QVERIFY(child.exitStatus() == QProcess::NormalExit);
		QCOMPARE(child.exitCode(), EXIT_SUCCESS);
	}
};

int main(int argc, char** argv)
{
	if (argc == 3 && std::strcmp(argv[1], "--allocation-failure") == 0)
	{
		return runChild(argv[2]);
	}

	QCoreApplication app(argc, argv);
	ReverbSCInitTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "ReverbSCInitTest.moc"