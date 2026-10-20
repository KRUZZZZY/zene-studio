/*
 * ReverbSCBaseTest.cpp - status contract of the Soundpipe helper API
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include <QtTest>

extern "C"
{
#include "base.h"
}

namespace
{
void fillFrame(sp_data* sp, void* userData)
{
	auto* frames = static_cast<int*>(userData);
	++(*frames);
	sp->out[0] = 0.25f;
}
}

class ReverbSCBaseTest : public QObject
{
	Q_OBJECT

private slots:
	void createReportsSpOk()
	{
		sp_data* multiChannel = nullptr;
		const auto multiCreateStatus = sp_createn(&multiChannel, 2);
		QVERIFY(multiChannel != nullptr);
		QCOMPARE(multiChannel->nchan, 2);
		QVERIFY(multiChannel->out != nullptr);
		QCOMPARE(multiCreateStatus, SP_OK);
		QCOMPARE(sp_destroy(&multiChannel), SP_OK);

		sp_data* singleChannel = nullptr;
		const auto singleCreateStatus = sp_create(&singleChannel);
		QVERIFY(singleChannel != nullptr);
		QCOMPARE(singleChannel->nchan, 1);
		QCOMPARE(singleCreateStatus, SP_OK);
		QCOMPARE(sp_destroy(&singleChannel), SP_OK);
	}

	void createRefusesInvalidArguments()
	{
		QCOMPARE(sp_createn(nullptr, 1), SP_NOT_OK);

		sp_data* data = nullptr;
		QCOMPARE(sp_createn(&data, 0), SP_NOT_OK);
		QVERIFY(data == nullptr);
		QCOMPARE(sp_createn(&data, -1), SP_NOT_OK);
		QVERIFY(data == nullptr);
	}

	void destroyReportsSpOk()
	{
		sp_data* data = nullptr;
		QCOMPARE(sp_createn(&data, 1), SP_OK);
		QVERIFY(data != nullptr);
		QCOMPARE(sp_destroy(&data), SP_OK);
	}

	void outReportsChannelStatus()
	{
		sp_data* data = nullptr;
		QCOMPARE(sp_createn(&data, 2), SP_OK);
		QVERIFY(data != nullptr);
		QCOMPARE(sp_out(data, 0, 0.5f), SP_OK);
		QCOMPARE(sp_out(data, 1, 0.5f), SP_OK);
		QCOMPARE(sp_out(data, 2, 0.5f), SP_NOT_OK);
		QCOMPARE(sp_destroy(&data), SP_OK);
	}

	void setReportsSpOk()
	{
		sp_param param{};
		QCOMPARE(sp_set(&param, 0.5f), SP_OK);
		QCOMPARE(param.val, 0.5f);
	}

	void auxDataReportsSpOk()
	{
		sp_auxdata aux{};
		QCOMPARE(sp_auxdata_alloc(&aux, 16), SP_OK);
		QVERIFY(aux.ptr != nullptr);
		QCOMPARE(aux.size, static_cast<size_t>(16));
		QCOMPARE(sp_auxdata_free(&aux), SP_OK);
	}

	void rawProcessingReportsSpOk()
	{
		sp_data* data = nullptr;
		QCOMPARE(sp_createn(&data, 1), SP_OK);
		QVERIFY(data != nullptr);
		data->len = 2;
		int frames = 0;
		QCOMPARE(sp_process_raw(data, &frames, fillFrame), SP_OK);
		QCOMPARE(frames, 2);
		QCOMPARE(sp_destroy(&data), SP_OK);
	}

	void plotProcessingReportsSpOk()
	{
		sp_data* data = nullptr;
		QCOMPARE(sp_createn(&data, 1), SP_OK);
		QVERIFY(data != nullptr);
		data->len = 2;
		int frames = 0;
		QCOMPARE(sp_process_plot(data, &frames, fillFrame), SP_OK);
		QCOMPARE(frames, 2);
		QCOMPARE(sp_destroy(&data), SP_OK);
	}
};

QTEST_GUILESS_MAIN(ReverbSCBaseTest)
#include "ReverbSCBaseTest.moc"
