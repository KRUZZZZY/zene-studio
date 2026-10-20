/*
 * AllocationProbeTest.cpp - the allocation probe counts allocations made in
 *                           LIBRARY code, not just in the test's own file
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

// Every "zero allocations on the audio path" test in this directory rests on
// AllocationProbe.h. Its job is to count allocations made by the code UNDER
// test, which lives in lmmsobjs - not in the test's translation unit. Until
// this test existed nothing checked that, and the probe counted nothing there
// (its operator new was `inline`, so the binary had none of its own). These
// slots pin both halves: an allocation inside a library function is counted,
// and counting stays off outside the window.

#include <QtTest>

#include <cstdint>
#include <memory>
#include <new>

#include "RoutingGraph.h"

#include "AllocationProbe.h"

namespace
{

//! The allocations \a body makes on this thread, counted by the probe.
template<typename Body>
auto countAllocations(Body&& body) -> std::uint64_t
{
	lmms::test::resetAllocationCount();
	lmms::test::tlCountAllocations = true;
	body();
	lmms::test::tlCountAllocations = false;
	return lmms::test::tlAllocationCount;
}

} // namespace

class AllocationProbeTest : public QObject
{
	Q_OBJECT

private slots:
	//! RoutingGraph::createNode() is compiled into lmmsobjs and returns a
	//! std::make_unique'd node: one operator new call that happens entirely
	//! inside library code. The probe must see it.
	void countsAnAllocationMadeInLibraryCode()
	{
		const auto typeName = QStringLiteral("gain"); // built outside the window
		std::unique_ptr<lmms::RoutingNode> node;
		const auto counted = countAllocations([&] { node = lmms::RoutingGraph::createNode(typeName); });

		QVERIFY2(node != nullptr, "the library call under the probe did not run");
		QVERIFY2(counted > 0,
			qPrintable(QStringLiteral("an operator new inside lmmsobjs was not counted (counted %1) - "
				"the probe's replacement allocation functions do not cover library code")
				.arg(static_cast<qulonglong>(counted))));
	}

	//! The positive control on the test's own side: a direct call of the
	//! global allocation function (which, unlike a new-expression, the
	//! compiler may not elide) is counted exactly once.
	void countsAnAllocationMadeInTheTestItself()
	{
		void* block = nullptr;
		const auto counted = countAllocations([&] { block = ::operator new(64); });
		::operator delete(block);
		QCOMPARE(counted, std::uint64_t{1});
	}

	//! The over-aligned form is replaced too (a std::vector of an alignas(64)
	//! type reaches it): counted once, and the block honours the alignment.
	void countsAndAlignsAnOverAlignedAllocation()
	{
		constexpr auto alignment = std::align_val_t{64};
		void* block = nullptr;
		const auto counted = countAllocations([&] { block = ::operator new(48, alignment); });
		const auto address = reinterpret_cast<std::uintptr_t>(block);
		::operator delete(block, alignment);
		QCOMPARE(counted, std::uint64_t{1});
		QCOMPARE(address % 64, std::uintptr_t{0});
	}

	//! Frees are counted too (Q050-016 round 3): a direct call of the global
	//! deallocation function is one free, and destroying a node built in
	//! lmmsobjs frees at least its own block.
	void countsFrees()
	{
		void* block = ::operator new(64);
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		::operator delete(block);
		lmms::test::tlCountAllocations = false;
		QCOMPARE(lmms::test::tlFreeCount, std::uint64_t{1});
		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});

		auto node = lmms::RoutingGraph::createNode(QStringLiteral("gain"));
		QVERIFY(node != nullptr);
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		node.reset();
		lmms::test::tlCountAllocations = false;
		QVERIFY2(lmms::test::tlFreeCount >= 1,
			"destroying a library-built node was not counted as a free");
	}

	//! Counting is off outside the window: the same library allocation made
	//! with the probe disarmed leaves the count where it was.
	void doesNotCountWhileDisarmed()
	{
		lmms::test::resetAllocationCount();
		auto node = lmms::RoutingGraph::createNode(QStringLiteral("gain"));
		QVERIFY(node != nullptr);
		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});
	}
};

QTEST_GUILESS_MAIN(AllocationProbeTest)

#include "AllocationProbeTest.moc"
