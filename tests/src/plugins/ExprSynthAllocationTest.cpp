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

// The window records the allocations of one call until the forced failure
// happens, so a test can tell whether one of them was freed while the call was
// running (a free on the calling thread) and whether it was freed at all (a
// leak). The plugin builds the expression front on the audio thread, so the
// failing path must not free anything there.
constexpr int MaxWindowAllocations = 32;

struct WindowAllocation
{
	void* memory{nullptr};
	std::size_t size{0};
	std::atomic_bool deleted{false};
};

WindowAllocation g_window[MaxWindowAllocations];
std::atomic_int g_windowCount{0};
std::atomic_bool g_windowRecording{false};
std::atomic_int g_windowFrees{0};
std::atomic_bool g_failPointerSizedAllocation{false};

void recordAllocation(void* memory, std::size_t size) noexcept
{
	if (!g_windowRecording.load(std::memory_order_relaxed))
	{
		return;
	}
	const int index = g_windowCount.fetch_add(1, std::memory_order_relaxed);
	if (index >= MaxWindowAllocations)
	{
		return;
	}
	g_window[index].memory = memory;
	g_window[index].size = size;
	g_window[index].deleted.store(false, std::memory_order_relaxed);
}

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
	const int count = g_windowCount.load(std::memory_order_relaxed);
	for (int i = 0; i < count && i < MaxWindowAllocations; ++i)
	{
		if (g_window[i].memory == memory)
		{
			g_window[i].deleted.store(true, std::memory_order_relaxed);
			g_windowFrees.fetch_add(1, std::memory_order_relaxed);
			break;
		}
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
	if (g_failPointerSizedAllocation.load(std::memory_order_relaxed)
		&& size == sizeof(void*)
		&& g_failPointerSizedAllocation.exchange(false, std::memory_order_relaxed))
	{
		// The cyclic vector's buffer for a single pointer: failing it makes the
		// vector growth fail. The recording stops here, so the allocations of the
		// error report that follows are not part of the window.
		allocationFailureSeen.store(true, std::memory_order_relaxed);
		g_windowRecording.store(false, std::memory_order_relaxed);
		throw std::bad_alloc{};
	}
	if (void* memory = std::malloc(size == 0 ? 1 : size))
	{
		recordAllocation(memory, size);
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
		int allocationsInWindow{0};
		int freesDuringCall{0};
		int leakedAllocations{0};
	};

	// Adds a cyclic vector while the vector's growth allocation is forced to fail,
	// and reports what happened to the memory the call touched.
	CyclicVectorGrowthResult failCyclicVectorGrowth(bool interp)
	{
		CyclicVectorGrowthResult result;
		{
			lmms::ExprFront front("1", 4);
			const float samples[4] = {0.0f, 0.5f, 1.0f, 0.25f};

			g_windowCount.store(0, std::memory_order_relaxed);
			g_windowFrees.store(0, std::memory_order_relaxed);
			g_failPointerSizedAllocation.store(true, std::memory_order_relaxed);
			allocationFailureSeen.store(false, std::memory_order_relaxed);
			g_windowRecording.store(true, std::memory_order_relaxed);

			result.added = front.add_cyclic_vector("W1", samples, 4, interp);

			result.allocationWasForcedToFail =
				allocationFailureSeen.load(std::memory_order_relaxed);
			result.freesDuringCall = g_windowFrees.load(std::memory_order_relaxed);
			g_windowRecording.store(false, std::memory_order_relaxed);
			g_failPointerSizedAllocation.store(false, std::memory_order_relaxed);
			result.allocationsInWindow = g_windowCount.load(std::memory_order_relaxed);
		}
		// The expression front is gone, so every allocation of the window must
		// have been freed with it. A wave that was not freed was lost.
		const int count = g_windowCount.load(std::memory_order_relaxed);
		for (int i = 0; i < count && i < MaxWindowAllocations; ++i)
		{
			if (!g_window[i].deleted.load(std::memory_order_relaxed))
			{
				++result.leakedAllocations;
			}
		}
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

	void failedCyclicVectorGrowthLeavesNoWaveBehind()
	{
		// The plugin builds the expression front on the audio thread, so the
		// failing path must not free the wave there, and it must not lose it
		// either.
		const auto result = failCyclicVectorGrowth(false);
		QVERIFY(!result.added);
		QVERIFY2(result.allocationWasForcedToFail,
			"the cyclic vector growth allocation was not intercepted");
		QVERIFY2(result.freesDuringCall == 0,
			"the failing path freed memory on the calling thread");
		QVERIFY2(result.leakedAllocations == 0,
			"a wave object leaked when the cyclic vector failed to grow");
	}

	void failedInterpolatedCyclicVectorGrowthLeavesNoWaveBehind()
	{
		const auto result = failCyclicVectorGrowth(true);
		QVERIFY(!result.added);
		QVERIFY2(result.allocationWasForcedToFail,
			"the interpolated cyclic vector growth allocation was not intercepted");
		QVERIFY2(result.freesDuringCall == 0,
			"the failing path freed memory on the calling thread");
		QVERIFY2(result.leakedAllocations == 0,
			"a wave object leaked when the interpolated cyclic vector failed to grow");
	}
};

QTEST_GUILESS_MAIN(ExprSynthAllocationTest)

#include "ExprSynthAllocationTest.moc"
