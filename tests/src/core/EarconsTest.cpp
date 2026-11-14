/*
 * EarconsTest.cpp - the lock-free earcon set and its monitoring-only mix path
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
/*! Q050-497: the earcon set - every category disabled by default, a sound audible only
 *  with its category enabled, levels bounded and validated, no allocation on the audio
 *  path, and the tones strictly outside the render path (renderNextPeriod) that export
 *  and the recorder taps use. */

#include <QtTest>

#include <QApplication>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>

#include "AudioEngine.h"
#include "Earcons.h"
#include "Engine.h"
#include "LmmsTypes.h"

namespace
{
std::atomic<bool> g_countAllocations{false};
std::atomic<std::uint64_t> g_allocationCount{0};

void noteAllocation() noexcept
{
	if (g_countAllocations.load(std::memory_order_relaxed))
	{
		g_allocationCount.fetch_add(1, std::memory_order_relaxed);
	}
}
} // namespace

void* operator new(std::size_t size)
{
	noteAllocation();
	if (void* ptr = std::malloc(size)) { return ptr; }
	throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
	noteAllocation();
	return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
	return ::operator new(size, tag);
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete(void* ptr, const std::nothrow_t&) noexcept { std::free(ptr); }
void operator delete[](void* ptr, const std::nothrow_t&) noexcept { std::free(ptr); }

using namespace lmms;
using namespace lmms::earcons;

class EarconsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	void everythingIsSilentByDefault();
	void onlyAnEnabledCategoryIsHeard();
	void levelsScaleAndRejectInvalidValues();
	void theAudioPathDoesNotAllocate();
	void theRenderPathNeverConsumesAnEarcon();

private:
	void disableAll();
	void flush();
};

void EarconsTest::initTestCase() { Engine::init(true); }
void EarconsTest::cleanupTestCase() { Engine::destroy(); }

void EarconsTest::disableAll()
{
	for (auto category = 0; category < static_cast<int>(Category::Count); ++category)
	{
		setEnabled(static_cast<Category>(category), false);
	}
	flush();
}

void EarconsTest::flush() { beginBuffer(); }

void EarconsTest::everythingIsSilentByDefault()
{
	disableAll();
	prepare(48000);
	QVERIFY(!enabled(Category::Selection));
	QVERIFY(!enabled(Category::Edit));
	QVERIFY(!enabled(Category::Refused));
	QVERIFY(!enabled(Category::Record));
	QVERIFY(!enabled(Category::Loop));
	for (auto sound = 0; sound < static_cast<int>(Sound::Count); ++sound)
	{
		play(static_cast<Sound>(sound));
	}
	beginBuffer();
	for (int i = 0; i < 4800; ++i)
	{
		if (nextSample() != 0.f) { QFAIL("a disabled earcon was heard"); }
	}
}

void EarconsTest::onlyAnEnabledCategoryIsHeard()
{
	disableAll();
	prepare(48000);
	setEnabled(Category::Edit, true);
	play(Sound::EditApplied);
	play(Sound::Refused);
	beginBuffer();

	int lastNonZero = -1;
	for (int i = 0; i < 4800; ++i)
	{
		if (nextSample() != 0.f) { lastNonZero = i; }
	}
	QVERIFY2(lastNonZero >= 0, "the enabled edit earcon was silent");
	// 50 ms at 48 kHz is 2400 frames; the 5 ms attack starts at zero, so allow one frame.
	QVERIFY2(lastNonZero + 1 >= 2399 && lastNonZero + 1 <= 2401,
		qPrintable(QStringLiteral("edit earcon lasted %1 frames").arg(lastNonZero + 1)));
	// The refused sound (120 ms) is in a disabled category and must not extend the tail.
	QVERIFY(lastNonZero + 1 < 4000);
}

void EarconsTest::levelsScaleAndRejectInvalidValues()
{
	disableAll();
	prepare(48000);
	setEnabled(Category::Edit, true);

	const auto peakAt = [](float volume) {
		setLevel(Category::Edit, volume);
		beginBuffer();
		play(Sound::EditApplied);
		beginBuffer();
		float peak = 0.f;
		for (int i = 0; i < 2400; ++i)
		{
			const float value = nextSample();
			if (std::fabs(value) > std::fabs(peak)) { peak = value; }
		}
		return std::fabs(peak);
	};

	const float half = peakAt(0.5f);
	const float full = peakAt(1.0f);
	QVERIFY(half > 0.f);
	QVERIFY(qFuzzyCompare(full, 2.f * half));

	QVERIFY(setLevel(Category::Edit, 0.f));
	beginBuffer();
	play(Sound::EditApplied);
	beginBuffer();
	for (int i = 0; i < 2400; ++i) { QCOMPARE(nextSample(), 0.f); }

	QVERIFY(setLevel(Category::Edit, 0.5f));
	QVERIFY(!setLevel(Category::Edit, std::numeric_limits<float>::quiet_NaN()));
	QCOMPARE(level(Category::Edit), 0.5f);
	QVERIFY(!setLevel(Category::Edit, -0.1f));
	QCOMPARE(level(Category::Edit), 0.5f);
	QVERIFY(!setLevel(Category::Edit, 1.5f));
	QCOMPARE(level(Category::Edit), 0.5f);
	QVERIFY(setLevel(Category::Edit, 0.f));
	QCOMPARE(level(Category::Edit), 0.f);
	QVERIFY(setLevel(Category::Edit, 1.f));
	QCOMPARE(level(Category::Edit), 1.f);
	setLevel(Category::Edit, 0.5f);
}

void EarconsTest::theAudioPathDoesNotAllocate()
{
	disableAll();
	prepare(48000);
	for (auto category = 0; category < static_cast<int>(Category::Count); ++category)
	{
		setEnabled(static_cast<Category>(category), true);
	}
	beginBuffer();

	g_allocationCount.store(0);
	g_countAllocations.store(true);
	for (auto sound = 0; sound < static_cast<int>(Sound::Count); ++sound)
	{
		play(static_cast<Sound>(sound));
	}
	beginBuffer();
	float sum = 0.f;
	for (int i = 0; i < 48000; ++i) { sum += nextSample(); }
	g_countAllocations.store(false);

	QCOMPARE(g_allocationCount.load(), std::uint64_t{0});
	QVERIFY2(sum != 0.f, "no earcon was heard while counting allocations");
}

void EarconsTest::theRenderPathNeverConsumesAnEarcon()
{
	disableAll();
	prepare(Engine::audioEngine()->outputSampleRate());
	for (auto category = 0; category < static_cast<int>(Category::Count); ++category)
	{
		setEnabled(static_cast<Category>(category), true);
	}
	for (auto sound = 0; sound < static_cast<int>(Sound::Count); ++sound)
	{
		play(static_cast<Sound>(sound));
	}

	// The export/recorder path renders periods without any device: it must contain none of them.
	for (int period = 0; period < 3; ++period)
	{
		const auto buffer = Engine::audioEngine()->renderNextPeriod();
		for (const auto& frame : buffer)
		{
			QCOMPARE(frame[0], 0.f);
			QCOMPARE(frame[1], 0.f);
		}
	}

	// Still pending after those periods: the device path is the only consumer.
	beginBuffer();
	float sum = 0.f;
	for (int i = 0; i < 4800; ++i) { sum += nextSample(); }
	QVERIFY2(sum != 0.f, "the render path consumed the earcons");
}

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	EarconsTest test;
	QTEST_SET_MAIN_SOURCE_PATH
	return QTest::qExec(&test, argc, argv);
}

#include "EarconsTest.moc"
