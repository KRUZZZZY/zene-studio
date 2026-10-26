/*
 * SfzEffectBusViewTest.cpp - Cluster new-p21-142: sfizz's EffectBus::effectView must not read past the effect list
 *
 * effectView(index) returns the effect at index, or nullptr when there is none. An index equal to the number of
 * effects has no effect behind it, so it must return nullptr; it used to be compared with '>' and read one slot past
 * the end of the vector. The sfizz bus is linked straight from the build's sfzplayer_sfizz library, so the test calls
 * the real class without loading a plugin module.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <cstddef>
#include <memory>

#include "sfizz/Effects.h"
#include "sfizz/effects/Nothing.h"

class SfzEffectBusViewTest : public QObject
{
	Q_OBJECT

private slots:
	// An empty bus has no slot 0: the buggy comparison let index 0 through and dereferenced the empty vector's storage.
	void emptyBusHasNoEffectAtIndexZero();

	// The index equal to the effect count is the case the '>' comparison let through.
	void indexEqualToEffectCountIsRejected();

	void indexPastEffectCountIsRejected();

	void inRangeIndexReturnsTheAddedEffect();
};

void SfzEffectBusViewTest::emptyBusHasNoEffectAtIndexZero()
{
	sfz::EffectBus bus;
	QCOMPARE(bus.numEffects(), std::size_t{0});

	QVERIFY(bus.effectView(0) == nullptr);
	QCOMPARE(bus.numEffects(), std::size_t{0});
}

void SfzEffectBusViewTest::indexEqualToEffectCountIsRejected()
{
	sfz::EffectBus bus;
	bus.addEffect(std::make_unique<sfz::fx::Nothing>());
	bus.addEffect(std::make_unique<sfz::fx::Nothing>());
	QCOMPARE(bus.numEffects(), std::size_t{2});

	QVERIFY(bus.effectView(2) == nullptr);
	QCOMPARE(bus.numEffects(), std::size_t{2});
}

void SfzEffectBusViewTest::indexPastEffectCountIsRejected()
{
	sfz::EffectBus bus;
	bus.addEffect(std::make_unique<sfz::fx::Nothing>());

	QVERIFY(bus.effectView(1) == nullptr);
	QVERIFY(bus.effectView(7) == nullptr);
}

void SfzEffectBusViewTest::inRangeIndexReturnsTheAddedEffect()
{
	sfz::EffectBus bus;
	auto first = std::make_unique<sfz::fx::Nothing>();
	auto second = std::make_unique<sfz::fx::Nothing>();
	const sfz::Effect* firstPtr = first.get();
	const sfz::Effect* secondPtr = second.get();
	bus.addEffect(std::move(first));
	bus.addEffect(std::move(second));

	QVERIFY(bus.effectView(0) == firstPtr);
	QVERIFY(bus.effectView(1) == secondPtr);
}

QTEST_GUILESS_MAIN(SfzEffectBusViewTest)
#include "SfzEffectBusViewTest.moc"
