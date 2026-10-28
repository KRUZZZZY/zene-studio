/*
 * AbletonLinkAllocatorTest.cpp - the vendored asio allocators refuse a count whose
 *                                byte size wraps.
 *
 * THE CLAIM: allocate(n) on the three asio allocators that Link's asio build
 * compiles - detail::recycling_allocator, execution_context::allocator and
 * any_completion_handler_allocator - throws std::bad_array_new_length for the
 * count SIZE_MAX / sizeof(T) + 1. There sizeof(T) * n wraps to a small number,
 * and an unchecked allocator would hand back a block far too small for the
 * request. A count that fits still allocates, is usable and frees.
 *
 * Seams: none needed. Each test computes its overflowing count from the element
 * type and calls the allocator directly. There are no threads, timers, clocks or
 * sleeps, so the result does not depend on the machine's load.
 */

#include <QtTest>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>

#include "asio/any_completion_handler.hpp"
#include "asio/detail/recycling_allocator.hpp"
#include "asio/execution_context.hpp"
#include "asio/io_context.hpp"

namespace {

// The smallest count whose byte size sizeof(T) * n wraps around std::size_t.
template <typename T>
constexpr std::size_t overflowingCount()
{
	return std::numeric_limits<std::size_t>::max() / sizeof(T) + 1;
}

// True only when `call` throws std::bad_array_new_length, the contract std::allocator keeps.
template <typename Call>
bool throwsArrayLength(Call call)
{
	try {
		call();
	} catch (const std::bad_array_new_length&) {
		return true;
	} catch (...) {
		return false;
	}
	return false;
}

} // namespace

class AbletonLinkAllocatorTest : public QObject
{
	Q_OBJECT

private slots:
	void recyclingAllocatorRejectsWrappingCount();
	void recyclingAllocatorStillAllocatesAndFrees();
	void executionContextAllocatorRejectsWrappingCount();
	void anyCompletionHandlerAllocatorRejectsWrappingCount();
};

void AbletonLinkAllocatorTest::recyclingAllocatorRejectsWrappingCount()
{
	using Alloc = asio::detail::recycling_allocator<std::uint64_t>;
	const std::size_t n = overflowingCount<std::uint64_t>();
	QVERIFY2(throwsArrayLength([&] { (void)Alloc().allocate(n); }),
	         "allocate(SIZE_MAX / sizeof(T) + 1) must throw std::bad_array_new_length");
}

void AbletonLinkAllocatorTest::recyclingAllocatorStillAllocatesAndFrees()
{
	asio::detail::recycling_allocator<std::uint64_t> alloc;
	const std::size_t n = 4;
	std::uint64_t* p = alloc.allocate(n);
	QVERIFY(p != nullptr);
	for (std::size_t i = 0; i < n; ++i) {
		p[i] = i + 1;
	}
	QCOMPARE(p[n - 1], std::uint64_t{ 4 });
	alloc.deallocate(p, n);
}

void AbletonLinkAllocatorTest::executionContextAllocatorRejectsWrappingCount()
{
	asio::io_context context;
	asio::execution_context::allocator<std::uint64_t> alloc(context);
	const std::size_t n = overflowingCount<std::uint64_t>();
	QVERIFY2(throwsArrayLength([&] { (void)alloc.allocate(n); }),
	         "execution_context::allocator must refuse a wrapping count");
}

void AbletonLinkAllocatorTest::anyCompletionHandlerAllocatorRejectsWrappingCount()
{
	asio::any_completion_handler<void()> handler([] {});
	asio::any_completion_handler_allocator<std::uint64_t, void()> alloc(handler.get_allocator());
	const std::size_t n = overflowingCount<std::uint64_t>();
	QVERIFY2(throwsArrayLength([&] { (void)alloc.allocate(n); }),
	         "any_completion_handler_allocator must refuse a wrapping count");
}

QTEST_GUILESS_MAIN(AbletonLinkAllocatorTest)
#include "AbletonLinkAllocatorTest.moc"
