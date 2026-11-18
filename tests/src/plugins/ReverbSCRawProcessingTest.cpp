/*
 * ReverbSCRawProcessingTest.cpp - raw streaming advances sample positions
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
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
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include <array>
#include <memory>
#include <setjmp.h>

#include <QtTest>

extern "C"
{
#include "base.h"
}

namespace
{

struct RawStreamContext
{
	jmp_buf stop;
	std::array<unsigned long, 3> callbackPositions{};
	int callbackCount = 0;
};

struct RawStreamFixture
{
	sp_data sp{};
	SPFLOAT output = 0.0f;
	RawStreamContext context{};
};

void recordRawFrame(sp_data* sp, void* userData)
{
	auto* context = static_cast<RawStreamContext*>(userData);
	context->callbackPositions[static_cast<std::size_t>(context->callbackCount)] = sp->pos;
	++context->callbackCount;
	sp->out[0] = static_cast<SPFLOAT>(sp->pos);
	if (context->callbackCount == static_cast<int>(context->callbackPositions.size()))
	{
		longjmp(context->stop, 1);
	}
}

} // namespace

class ReverbSCRawProcessingTest : public QObject
{
	Q_OBJECT

private slots:
	void infiniteRawProcessingAdvancesPositionAndPreservesLengthSentinel()
	{
		auto fixture = std::make_unique<RawStreamFixture>();
		fixture->sp.out = &fixture->output;
		fixture->sp.nchan = 1;
		fixture->sp.len = 0;
		fixture->sp.pos = 37;

		if (setjmp(fixture->context.stop) == 0)
		{
			sp_process_raw(&fixture->sp, &fixture->context, recordRawFrame);
		}

		QCOMPARE(fixture->context.callbackCount, 3);
		QCOMPARE(fixture->context.callbackPositions[0], 37UL);
		QCOMPARE(fixture->context.callbackPositions[1], 38UL);
		QCOMPARE(fixture->context.callbackPositions[2], 39UL);
		QCOMPARE(fixture->sp.pos, 39UL);
		QCOMPARE(fixture->sp.len, 0UL);
	}
};

QTEST_GUILESS_MAIN(ReverbSCRawProcessingTest)
#include "ReverbSCRawProcessingTest.moc"
