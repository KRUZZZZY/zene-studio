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
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <fftw3.h>
#include <new>
#include <QMutex>
#include <QRgb>
#include <type_traits>
#include <vector>

#include "AudioBufferView.h"
#include "Engine.h"
#include "Model.h"

#include "AudioPlugin.h"
#include "LocklessRingBuffer.h"
#include "SaControls.h"

// The plugin's debug members are private, but the regression has to read the
// very members the finding is about. The plugin headers are therefore read
// with 'private' turned into 'public'; every header they pull in is included
// above with the normal access, so only the two plugin classes are affected.
#define private public
#include "SaProcessor.h"
#undef private

#include "DataprocLauncher.h"

#define private public
#include "Analyzer.h"
#undef private

using namespace lmms;

namespace
{

/**
 * The byte pattern the test writes over the object's storage before it is
 * constructed. A member that neither the declaration nor the constructor
 * initialises keeps these bytes, so comparing a member against this pattern
 * detects an indeterminate read instead of hoping for a lucky stack value.
 */
template<typename T>
T uninitialisedPattern()
{
	static_assert(std::is_trivially_copyable_v<T>, "the pattern needs a trivially copyable type");
	std::array<unsigned char, sizeof(T)> bytes{};
	std::memset(bytes.data(), 0xAB, bytes.size());
	return std::bit_cast<T>(bytes);
}

/**
 * Destroys a placement-constructed object even when a failed assertion aborts
 * the test function, so the plugin's worker thread is always joined.
 */
template<typename T>
class PlacementGuard
{
public:
	explicit PlacementGuard(T * object) : m_object(object) {}
	~PlacementGuard() {m_object->~T();}
	PlacementGuard(const PlacementGuard &) = delete;
	PlacementGuard & operator=(const PlacementGuard &) = delete;
private:
	T * m_object;
};

} // namespace

class AnalyzerTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase();
	void debugMembersAreInitializedBeforeTheFirstAudioCallback();
	void processorDebugMembersAreInitializedBeforeTheFirstAnalysis();
};

void AnalyzerTest::initTestCase()
{
	// Render-only engine so the plugin's SaProcessor ctor can query
	// Engine::audioEngine()->outputSampleRate() like in every other plugin test.
	Engine::init( true );
	QVERIFY2( Engine::audioEngine() != nullptr, "engine failed to initialise" );
	Engine::audioEngine()->audioDev()->stopProcessing();
}

void AnalyzerTest::debugMembersAreInitializedBeforeTheFirstAudioCallback()
{
	// Poison the exact storage the Analyzer will occupy so an uninitialised
	// member reads as a deterministic garbage value rather than luck.
	alignas(Analyzer) unsigned char storage[sizeof(Analyzer)];
	std::memset(storage, 0xAB, sizeof(storage));

	Analyzer * analyzer = new (storage) Analyzer(nullptr, nullptr);
	PlacementGuard<Analyzer> guard(analyzer);

	// The finding: the SA_DEBUG members had no initializer and the constructor
	// never touched them, so the first processImpl() audio callback read
	// uninitialised memory (m_last_dump_time) on every period.
	using Timer = decltype(analyzer->m_last_dump_time);
	const Timer uninitialised = uninitialisedPattern<Timer>();
	QVERIFY2(analyzer->m_last_dump_time != uninitialised,
		"m_last_dump_time still holds the pre-construction storage pattern");
	QCOMPARE(analyzer->m_dump_count, 0);
	QCOMPARE(analyzer->m_sum_execution, 0.0f);
	QCOMPARE(analyzer->m_max_execution, 0.0f);
	if constexpr (std::is_same_v<Timer, std::chrono::steady_clock::time_point>)
	{
		// The debug window is anchored when the plugin is constructed, so the
		// stored timestamp is a moment in the past, never in the future.
		QVERIFY2(analyzer->m_last_dump_time <= std::chrono::steady_clock::now(),
			"m_last_dump_time is not a timestamp of the past");
	}

	// One audio callback through the path that reads m_last_dump_time.
	std::array<float, 2 * 64> buf{};
	InterleavedBufferView<float, 2> view(buf.data(), 64);
	QCOMPARE(analyzer->processImpl(view), ProcessStatus::Continue);

	// The debug counters count the callback exactly once.
	QCOMPARE(analyzer->m_dump_count, 1);
	QVERIFY(analyzer->m_sum_execution >= 0.0f);
	QVERIFY(analyzer->m_max_execution >= 0.0f);
}

void AnalyzerTest::processorDebugMembersAreInitializedBeforeTheFirstAnalysis()
{
	// The same defect in the sibling class: the data processor reads its own
	// SA_DEBUG members on every analysed block and divides the accumulated
	// time by the dump counter, so an indeterminate counter is read and used.
	alignas(SaProcessor) unsigned char storage[sizeof(SaProcessor)];
	std::memset(storage, 0xAB, sizeof(storage));

	SaProcessor * processor = new (storage) SaProcessor(nullptr);
	PlacementGuard<SaProcessor> guard(processor);

	QVERIFY2(processor->m_last_dump_time != uninitialisedPattern<unsigned int>(),
		"m_last_dump_time still holds the pre-construction storage pattern");
	QCOMPARE(processor->m_dump_count, 0);
	QCOMPARE(processor->m_sum_execution, 0.0f);
	QCOMPARE(processor->m_max_execution, 0.0f);
}

QTEST_MAIN(AnalyzerTest)

#include "Analyzer.moc"
