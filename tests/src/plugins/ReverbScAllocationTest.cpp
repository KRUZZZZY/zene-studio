/*
 * ReverbScAllocationTest.cpp - allocation failure handling for ReverbSC's DC blocker
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <cstddef>
#include <cstdlib>

#include <QCoreApplication>
#include <QProcess>
#include <QtTest>

extern "C"
{
#include "base.h"
#include "dcblock.h"
}

namespace
{
	int g_failOnAllocation = 0;
	int g_allocationCount = 0;

	int runAllocationFailureChild(int failOnAllocation)
	{
		g_failOnAllocation = failOnAllocation;
		sp_dcblock* blocks[2] = {nullptr, nullptr};
		const int status = sp_dcblock_create_and_init_pair(nullptr, blocks, 1);

		if (status != SP_NOT_OK || blocks[0] != nullptr || blocks[1] != nullptr)
		{
			return 1;
		}
		return 0;
	}
}

extern "C" void* sp_dcblock_test_malloc(std::size_t size)
{
	++g_allocationCount;
	if (g_allocationCount == g_failOnAllocation)
	{
		return nullptr;
	}
	return std::malloc(size);
}

class ReverbScAllocationTest : public QObject
{
	Q_OBJECT

private slots:
	void createReportsAllocationFailure();
	void pairAllocationFailureDoesNotReachInit();
};

void ReverbScAllocationTest::createReportsAllocationFailure()
{
	g_failOnAllocation = 1;
	g_allocationCount = 0;
	sp_dcblock* block = nullptr;
	QCOMPARE(sp_dcblock_create(&block), SP_NOT_OK);
	QVERIFY(block == nullptr);
}

void ReverbScAllocationTest::pairAllocationFailureDoesNotReachInit()
{
	bool handledBothFailures = true;
	for (const int failOnAllocation : {1, 2})
	{
		QProcess child;
		child.start(QCoreApplication::applicationFilePath(),
			{"--dcblock-allocation-failure-child", QString::number(failOnAllocation)});
		QVERIFY2(child.waitForStarted(), "failed to start allocation-failure child");
		if (!child.waitForFinished())
		{
			handledBothFailures = false;
			continue;
		}
		if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0)
		{
			handledBothFailures = false;
		}
	}
	QVERIFY2(handledBothFailures, "allocation failure crashed the DC blocker setup caller");
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if (argc == 3 && QByteArray(argv[1]) == "--dcblock-allocation-failure-child")
	{
		return runAllocationFailureChild(std::atoi(argv[2]));
	}
	ReverbScAllocationTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "ReverbScAllocationTest.moc"
