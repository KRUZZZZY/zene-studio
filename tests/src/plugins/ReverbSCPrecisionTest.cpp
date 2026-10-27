/*
 * ReverbSCPrecisionTest.cpp - ReverbSC's output buffer is sized for SPFLOAT
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

#include <QtTest>

extern "C"
{
#include "base.h"
}

namespace
{
	std::size_t g_callocCount = 0;
	std::size_t g_callocSize = 0;
	bool g_failNextCalloc = false;
}

// base.c is compiled with SPFLOAT=double and calloc renamed to this shim,
// so the request made by sp_createn is observable and can be refused.
extern "C" void* sp_precision_test_calloc(std::size_t count, std::size_t size)
{
	g_callocCount = count;
	g_callocSize = size;
	if (g_failNextCalloc)
	{
		g_failNextCalloc = false;
		return nullptr;
	}
	return std::calloc(count, size);
}

class ReverbSCPrecisionTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QCOMPARE(sizeof(SPFLOAT), sizeof(double));
	}

	void outputBufferHoldsOneSampleOfSpfloatPerChannel()
	{
		for (const int channels : {1, 2, 5})
		{
			g_callocCount = 0;
			g_callocSize = 0;
			sp_data* sp = nullptr;
			QCOMPARE(sp_createn(&sp, channels), SP_OK);
			QVERIFY(sp != nullptr);
			QCOMPARE(g_callocCount, static_cast<std::size_t>(channels));
			QCOMPARE(g_callocSize, sizeof(SPFLOAT));
			QCOMPARE(sp_destroy(&sp), SP_OK);
		}
	}

	void refusedOutputAllocationLeavesNoState()
	{
		g_failNextCalloc = true;
		sp_data* sp = nullptr;
		QCOMPARE(sp_createn(&sp, 3), SP_NOT_OK);
		QVERIFY(sp == nullptr);
		QCOMPARE(g_callocSize, sizeof(SPFLOAT));
	}

};

QTEST_GUILESS_MAIN(ReverbSCPrecisionTest)

#include "ReverbSCPrecisionTest.moc"
