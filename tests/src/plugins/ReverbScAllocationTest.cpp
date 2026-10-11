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

#include <QtTest>

extern "C"
{
#include "base.h"
#include "dcblock.h"
}

extern "C" void* sp_dcblock_test_malloc(std::size_t)
{
	return nullptr;
}

class ReverbScAllocationTest : public QObject
{
	Q_OBJECT

private slots:
	void createReportsAllocationFailure();
};

void ReverbScAllocationTest::createReportsAllocationFailure()
{
	sp_dcblock* block = nullptr;
	QCOMPARE(sp_dcblock_create(&block), SP_NOT_OK);
	QVERIFY(block == nullptr);
}

QTEST_GUILESS_MAIN(ReverbScAllocationTest)

#include "ReverbScAllocationTest.moc"
