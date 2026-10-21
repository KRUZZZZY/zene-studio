/*
 * ExprSynthAllocationTest.cpp - allocation failure safety for ExprFront.
 */

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

#include <QtTest>

#include "ExprSynth.h"

namespace
{

std::atomic_bool failNextAllocation{false};
std::atomic_bool allocationFailureSeen{false};
std::atomic<void*> watchedAllocation{nullptr};
std::atomic_bool watchedAllocationDeleted{false};

// Counts the allocations since the last reset, so a test can fail the n-th one
// and watch the first one.
std::atomic_int allocationIndex{0};
std::atomic_int failAllocationIndex{-1};
std::atomic<void*> markedAllocation{nullptr};
std::atomic_bool markedAllocationDeleted{false};

void recordDelete(void* memory) noexcept
{
	if (memory == watchedAllocation.load(std::memory_order_relaxed))
	{
		watchedAllocationDeleted.store(true, std::memory_order_relaxed);
	}
	if (memory == markedAllocation.load(std::memory_order_relaxed))
	{
		markedAllocationDeleted.store(true, std::memory_order_relaxed);
	}
	std::free(memory);
}

}

void* operator new(std::size_t size)
{
	if (failNextAllocation.exchange(false, std::memory_order_relaxed))
	{
		allocationFailureSeen.store(true, std::memory_order_relaxed);
		throw std::bad_alloc{};
	}
	const int index = allocationIndex.fetch_add(1, std::memory_order_relaxed);
	if (index == failAllocationIndex.load(std::memory_order_relaxed))
	{
		allocationFailureSeen.store(true, std::memory_order_relaxed);
		throw std::bad_alloc{};
	}
	if (void* memory = std::malloc(size == 0 ? 1 : size))
	{
		if (index == 0)
		{
			markedAllocation.store(memory, std::memory_order_relaxed);
		}
		return memory;
	}
	throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept
{
	recordDelete(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
	recordDelete(memory);
}

class ExprSynthAllocationTest : public QObject
{
	Q_OBJECT

private:
	struct CyclicVectorGrowthResult
	{
		bool added{false};
		bool allocationWasForcedToFail{false};
		bool waveWasAllocated{false};
		bool waveWasDeleted{false};
	};

	// Adds a cyclic vector while the vector's first growth allocation is forced to
	// fail, and reports what happened to the wave object that was created before it.
	CyclicVectorGrowthResult failCyclicVectorGrowth(bool interp)
	{
		lmms::ExprFront front("1", 4);
		const float samples[4] = {0.0f, 0.5f, 1.0f, 0.25f};

		// The wave object is the first allocation of add_cyclic_vector and the growth
		// of the cyclic vector is the second one. Failing the second allocation makes
		// the push_back throw after the wave object was already created.
		allocationIndex.store(0, std::memory_order_relaxed);
		failAllocationIndex.store(1, std::memory_order_relaxed);
		markedAllocation.store(nullptr, std::memory_order_relaxed);
		markedAllocationDeleted.store(false, std::memory_order_relaxed);
		allocationFailureSeen.store(false, std::memory_order_relaxed);

		CyclicVectorGrowthResult result;
		result.added = front.add_cyclic_vector("W1", samples, 4, interp);

		failAllocationIndex.store(-1, std::memory_order_relaxed);
		result.allocationWasForcedToFail = allocationFailureSeen.load(std::memory_order_relaxed);
		result.waveWasAllocated = markedAllocation.load(std::memory_order_relaxed) != nullptr;
		result.waveWasDeleted = markedAllocationDeleted.load(std::memory_order_relaxed);
		markedAllocation.store(nullptr, std::memory_order_relaxed);
		return result;
	}

private slots:
	void failedDataAllocationDoesNotDeleteAnotherFrontsData()
	{
		static_assert(std::is_standard_layout_v<lmms::ExprFront>);

		lmms::ExprFront seed("1", 4);
		QVERIFY(seed.getData() != nullptr);

		auto* seededData = seed.getData();
		alignas(lmms::ExprFront) std::byte failedStorage[sizeof(lmms::ExprFront)]{};
		std::memcpy(failedStorage, &seededData, sizeof(seededData));

		watchedAllocation.store(seededData, std::memory_order_relaxed);
		watchedAllocationDeleted.store(false, std::memory_order_relaxed);
		allocationFailureSeen.store(false, std::memory_order_relaxed);
		failNextAllocation.store(true, std::memory_order_relaxed);
		auto* failed = ::new (failedStorage) lmms::ExprFront("1", 4);
		failNextAllocation.store(false, std::memory_order_relaxed);

		failed->~ExprFront();
		const bool allocationWasForcedToFail =
			allocationFailureSeen.load(std::memory_order_relaxed);
		const bool deletedSeedData = watchedAllocationDeleted.load(std::memory_order_relaxed);
		watchedAllocation.store(nullptr, std::memory_order_relaxed);

		if (deletedSeedData)
		{
			// The pre-fix constructor preserves the seeded bytes when allocation
			// throws. Its destructor then frees seed's data, so detach it before
			// seed's automatic destructor runs.
			lmms::ExprFrontData* nullData = nullptr;
			std::memcpy(reinterpret_cast<std::byte*>(&seed), &nullData, sizeof(nullData));
		}

		QVERIFY(allocationWasForcedToFail);
		QVERIFY(!deletedSeedData);
	}

	void failedCyclicVectorGrowthDoesNotLeakTheWave()
	{
		const auto result = failCyclicVectorGrowth(false);
		QVERIFY(!result.added);
		QVERIFY2(result.allocationWasForcedToFail,
			"the cyclic vector growth allocation was not intercepted");
		QVERIFY2(result.waveWasAllocated,
			"the wave object was not allocated before the cyclic vector grew");
		QVERIFY2(result.waveWasDeleted,
			"the wave object leaked when the cyclic vector failed to grow");
	}

	void failedInterpolatedCyclicVectorGrowthDoesNotLeakTheWave()
	{
		const auto result = failCyclicVectorGrowth(true);
		QVERIFY(!result.added);
		QVERIFY2(result.allocationWasForcedToFail,
			"the interpolated cyclic vector growth allocation was not intercepted");
		QVERIFY2(result.waveWasAllocated,
			"the wave object was not allocated before the cyclic vector grew");
		QVERIFY2(result.waveWasDeleted,
			"the wave object leaked when the interpolated cyclic vector failed to grow");
	}
};

QTEST_GUILESS_MAIN(ExprSynthAllocationTest)

#include "ExprSynthAllocationTest.moc"
