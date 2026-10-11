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

void recordDelete(void* memory) noexcept
{
	if (memory == watchedAllocation.load(std::memory_order_relaxed))
	{
		watchedAllocationDeleted.store(true, std::memory_order_relaxed);
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
	if (void* memory = std::malloc(size == 0 ? 1 : size))
	{
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
};

QTEST_GUILESS_MAIN(ExprSynthAllocationTest)

#include "ExprSynthAllocationTest.moc"
