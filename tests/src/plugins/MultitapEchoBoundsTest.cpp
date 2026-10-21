/*
 * MultitapEchoBoundsTest.cpp - regression for cluster new-p12-162-port:
 * the constructor must not read the low-pass entries past the graph length
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
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

//! The finding (cluster new-p12-162-port) was filed against the historical
//! line, where the constructor asked the filter update for twenty taps
//! (`updateFilters( 0, 19 )`) while the graph writes only the sixteen entries
//! it covers - so four coefficient sets came from memory nobody wrote, and an
//! indeterminate float turned them into NaN coefficients.
//!
//! On the newest line the defect is already gone: the constructor applies the
//! pending controls of the graph's own length
//! (`applyPendingControls( m_controls.m_steps.value() )`,
//! plugins/MultitapEcho/MultitapEcho.cpp:68) and the low-pass array is
//! value-initialised (`float m_lpFreq [32]{};`,
//! plugins/MultitapEcho/MultitapEcho.h:70), so no entry is indeterminate.
//! This test is the guard for that state: it fails on the historical code and
//! passes on the current line.
//!
//! The plugin is built as a module, so its sources are compiled into this test
//! binary (the SlicerTZoomTest / DelayControlsTest pattern). That is what
//! makes the regression deterministic and portable: the effect is constructed
//! in a byte-poisoned buffer, so the bytes the constructor reads are the
//! poison, and the outcome does not depend on the heap, the allocator, the
//! loader or the platform. No runtime module is loaded and no allocator
//! interposition is involved.
//!
//! The access specifiers are opened for this TU only; they affect neither the
//! layout nor the name mangling, so the plugin's own translation units and the
//! link are unaffected (the SlicerTZoomTest precedent).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include <QtTest>

#include "AudioPlugin.h"
#include "BasicFilters.h"
#include "RingBuffer.h"
#include "MultitapEchoControls.h"

#define private public
#include "MultitapEcho.h"
#undef private

#include "Engine.h"
#include "AudioEngine.h"

namespace
{

//! The graph length the controls start with (the "Steps" model's default).
constexpr int GraphLength = 16;

//! The number of taps the historical constructor asked the filter update for.
constexpr int ConstructorTapCount = 20;

//! The number of low-pass entries the effect keeps.
constexpr int LowPassEntryCount = 32;

//! The byte the poisoned storage is filled with; as a float it is a NaN.
constexpr unsigned char PoisonByte = 0xFF;

//! The same pattern seen as the object representation of a float.
constexpr std::uint32_t PoisonPattern = 0xFFFFFFFFu;

//! Storage an effect can be built in, with the alignment the effect needs.
struct PoisonedStorage
{
	alignas(lmms::MultitapEchoEffect) unsigned char bytes[sizeof(lmms::MultitapEchoEffect)];
};

//! An effect built in storage the test owns and poisons first.
class PoisonedEffect
{
public:
	PoisonedEffect()
	{
		// The poison is written through a volatile pointer: a plain memset
		// ahead of a placement new is dead as far as the optimizer is
		// concerned, and a dropped poison would turn the regression into a
		// coin toss.
		auto* const poisoned = reinterpret_cast<volatile unsigned char*>(m_storage.bytes);
		for (std::size_t i = 0; i < sizeof(m_storage.bytes); ++i)
		{
			poisoned[i] = PoisonByte;
		}
		m_effect = new (m_storage.bytes) lmms::MultitapEchoEffect(nullptr, nullptr);
	}

	~PoisonedEffect()
	{
		m_effect->~MultitapEchoEffect();
	}

	PoisonedEffect(const PoisonedEffect&) = delete;
	PoisonedEffect& operator=(const PoisonedEffect&) = delete;

	lmms::MultitapEchoEffect* operator->() const { return m_effect; }
	lmms::MultitapEchoEffect& operator*() const { return *m_effect; }

private:
	PoisonedStorage m_storage;
	lmms::MultitapEchoEffect* m_effect = nullptr;
};

} // namespace

class MultitapEchoBoundsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void constructorLeavesTheTapsPastTheGraphLengthUntouched();
	void everyLowPassEntryIsInitialised();
};

void MultitapEchoBoundsTest::initTestCase()
{
	// The plugin's constructor reads the engine (sample rate, period size).
	lmms::Engine::init(true);
	QVERIFY2(lmms::Engine::audioEngine() != nullptr, "engine failed to initialise");
}

void MultitapEchoBoundsTest::cleanupTestCase()
{
	lmms::Engine::destroy();
}

void MultitapEchoBoundsTest::constructorLeavesTheTapsPastTheGraphLengthUntouched()
{
	const PoisonedEffect effect;

	// The taps past the graph length must keep the untouched filter state:
	// the OnePole default is a pass-through, so an impulse comes back as it
	// went in. A constructor that computed their coefficients from entries
	// past the graph length leaves other coefficients there instead - NaN
	// when the entry was indeterminate, a zero-gain filter when it was zero.
	for (int i = GraphLength; i < ConstructorTapCount; ++i)
	{
		const float output = effect->m_filter[i][0].update(1.0f, 0);
		QVERIFY2(std::isfinite(output),
			"the constructor read a low-pass entry past the graph length");
		QCOMPARE(output, 1.0f);
	}

	// The taps inside the graph length must still carry their low-pass, so
	// the check above cannot pass by leaving every filter untouched.
	const float configured = effect->m_filter[0][0].update(1.0f, 0);
	QVERIFY(std::isfinite(configured));
	QVERIFY2(configured != 1.0f, "the tap inside the graph length lost its low-pass");
}

void MultitapEchoBoundsTest::everyLowPassEntryIsInitialised()
{
	const PoisonedEffect effect;

	// Every low-pass entry is initialised before it is used: the entries are
	// checked by their object representation, not by evaluating them as
	// floats, so an entry that stayed indeterminate cannot be read as a value
	// here. On the current line the whole array is value-initialised.
	for (int i = 0; i < LowPassEntryCount; ++i)
	{
		std::uint32_t representation = 0;
		std::memcpy(&representation, &effect->m_lpFreq[i], sizeof(representation));
		QVERIFY2(representation != PoisonPattern,
			"a low-pass entry is still indeterminate after construction");
	}
}

QTEST_MAIN( MultitapEchoBoundsTest )

#include "MultitapEchoBoundsTest.moc"
