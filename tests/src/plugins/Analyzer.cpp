/*
 * Analyzer.cpp - regression test for cluster new-p10-470
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

#include <QtTest>
#include <array>
#include <chrono>
#include <cstring>
#include <new>

#include "AudioBufferView.h"
#include "Engine.h"
#include "Model.h"

#include "AudioPlugin.h"
#include "DataprocLauncher.h"
#include "LocklessRingBuffer.h"
#include "SaControls.h"
#include "SaProcessor.h"

#define private public
#include "Analyzer.h"
#undef private

using namespace lmms;

class AnalyzerTest : public QObject
{
	Q_OBJECT
private slots:
	void debugMembersAreInitializedBeforeTheFirstAudioCallback();
};

void AnalyzerTest::debugMembersAreInitializedBeforeTheFirstAudioCallback()
{
	Engine::init( true );
	QVERIFY2( Engine::audioEngine() != nullptr, "engine failed to initialise" );
	Engine::audioEngine()->audioDev()->stopProcessing();

	alignas(Analyzer) unsigned char storage[sizeof(Analyzer)];
	std::memset(storage, 0xAB, sizeof(storage));

	Analyzer * analyzer = new (storage) Analyzer(nullptr, nullptr);

	QVERIFY(analyzer->m_last_dump_time == std::chrono::steady_clock::time_point{});
	QCOMPARE(analyzer->m_dump_count, 0);
	QCOMPARE(analyzer->m_sum_execution, 0.0f);
	QCOMPARE(analyzer->m_max_execution, 0.0f);

	std::array<float, 2 * 64> buf{};
	InterleavedBufferView<float, 2> view(buf.data(), 64);
	QCOMPARE(analyzer->processImpl(view), ProcessStatus::Continue);

	analyzer->~Analyzer();
}

QTEST_MAIN(AnalyzerTest)

#include "Analyzer.moc"
