/*
 * WaveformDataTest.cpp - Q050-486: the shared loader for saved plug-in waveform data
 *
 * waveform::decode() is the one place a plug-in's saved base64 float array becomes samples.
 * These cases feed it every kind of damaged payload a file can carry - empty, not base64,
 * truncated, too long, far too long, a byte count that is not whole floats, NaN and infinities -
 * and require a typed refusal with no out-of-range access, plus exact bits for valid data.
 * Built with AddressSanitizer on Linux (tests/CMakeLists.txt), so an out-of-bounds read fails
 * the run, and the bound on encoded input is measured through the sanitizer's allocator hooks.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <QDomDocument>
#include <QtTest>

#include "Graph.h"
#include "WaveformData.h"

#if defined(__SANITIZE_ADDRESS__)
#define WAVEFORM_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define WAVEFORM_TEST_ASAN 1
#endif
#endif

#ifdef WAVEFORM_TEST_ASAN
#include <atomic>
#include <cstddef>
// Exported by both ASan runtimes; GCC ships no allocator_interface.h to declare it.
extern "C" int __sanitizer_install_malloc_and_free_hooks(
	void (*mallocHook)(const volatile void*, std::size_t), void (*freeHook)(const volatile void*));

namespace
{
std::atomic<bool> g_watching{false};
std::atomic<std::size_t> g_largest{0};

void onMalloc(const volatile void*, std::size_t size)
{
	if (!g_watching.load(std::memory_order_relaxed)) { return; }
	std::size_t seen = g_largest.load(std::memory_order_relaxed);
	while (size > seen && !g_largest.compare_exchange_weak(seen, size, std::memory_order_relaxed)) {}
}

void onFree(const volatile void*) {}
} // namespace
#endif

using namespace lmms;
using waveform::DecodeStatus;

namespace
{

QString encodeFloats(const std::vector<float>& values)
{
	const QByteArray bytes{reinterpret_cast<const char*>(values.data()),
		static_cast<int>(values.size() * sizeof(float))};
	return QString::fromLatin1(bytes.toBase64());
}

QString encodeBytes(int count)
{
	return QString::fromLatin1(QByteArray(count, '\x11').toBase64());
}

std::vector<float> ramp(int count, float offset = 0.0f)
{
	std::vector<float> values(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i) { values[static_cast<std::size_t>(i)] = offset + 0.001f * static_cast<float>(i) - 0.1f; }
	return values;
}

} // namespace

class WaveformDataTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef WAVEFORM_TEST_ASAN
		__sanitizer_install_malloc_and_free_hooks(onMalloc, onFree);
#endif
	}

	//! Valid data comes back bit for bit, as owning float-aligned storage with its count.
	void validDataRoundTripsExactly()
	{
		std::vector<float> values = ramp(220);
		values[3] = -0.0f;
		values[4] = std::numeric_limits<float>::denorm_min();
		values[5] = std::numeric_limits<float>::max();
		const auto decoded = waveform::decode(encodeFloats(values), 220, 220);
		QCOMPARE(decoded.status, DecodeStatus::Ok);
		QCOMPARE(decoded.count, 220);
		QCOMPARE(decoded.bytes, 220 * static_cast<int>(sizeof(float)));
		QVERIFY(decoded.samples != nullptr);
		QCOMPARE(reinterpret_cast<std::uintptr_t>(decoded.samples.get()) % alignof(float), std::uintptr_t{0});
		QVERIFY(std::memcmp(decoded.samples.get(), values.data(), values.size() * sizeof(float)) == 0);
	}

	void refusals_data()
	{
		QTest::addColumn<QString>("encoded");
		QTest::addColumn<int>("status");
		QTest::addColumn<int>("count");

		std::vector<float> nan = ramp(220);
		nan[17] = std::numeric_limits<float>::quiet_NaN();
		std::vector<float> posInf = ramp(220);
		posInf[0] = std::numeric_limits<float>::infinity();
		std::vector<float> negInf = ramp(220);
		negInf[219] = -std::numeric_limits<float>::infinity();

		QTest::newRow("empty") << QString() << int(DecodeStatus::Missing) << 0;
		QTest::newRow("invalid characters") << QStringLiteral("!!!!not*base64@@") << int(DecodeStatus::Invalid) << 0;
		QTest::newRow("bad padding") << QStringLiteral("AAA=A===") << int(DecodeStatus::Invalid) << 0;
		QTest::newRow("whitespace") << QStringLiteral("AAAA AAAA") << int(DecodeStatus::Invalid) << 0;
		QTest::newRow("non-latin text") << QStringLiteral("AAAA€AAA") << int(DecodeStatus::Invalid) << 0;
		QTest::newRow("truncated by one value") << encodeFloats(ramp(219)) << int(DecodeStatus::WrongLength) << 219;
		QTest::newRow("one value") << encodeFloats(ramp(1)) << int(DecodeStatus::WrongLength) << 1;
		QTest::newRow("excessive by one value") << encodeFloats(ramp(221)) << int(DecodeStatus::Oversized) << 0;
		QTest::newRow("misaligned 3 bytes") << encodeBytes(3) << int(DecodeStatus::Misaligned) << 0;
		QTest::newRow("misaligned 878 bytes") << encodeBytes(878) << int(DecodeStatus::Misaligned) << 0;
		QTest::newRow("NaN") << encodeFloats(nan) << int(DecodeStatus::NonFinite) << 220;
		QTest::newRow("+infinity") << encodeFloats(posInf) << int(DecodeStatus::NonFinite) << 220;
		QTest::newRow("-infinity") << encodeFloats(negInf) << int(DecodeStatus::NonFinite) << 220;
	}

	//! Every damaged payload is refused with its own status, and hands back no storage.
	void refusals()
	{
		QFETCH(QString, encoded);
		QFETCH(int, status);
		QFETCH(int, count);
		const auto decoded = waveform::decode(encoded, 220, 220);
		QCOMPARE(int(decoded.status), status);
		QCOMPARE(decoded.count, count);
		QVERIFY(decoded.samples == nullptr);
		QVERIFY(!waveform::describe(decoded, 220, 220).isEmpty());
	}

	//! A range accepts any count inside it (BitInvader's older writers saved fewer values).
	void aRangeAcceptsCountsInsideIt()
	{
		QCOMPARE(waveform::decode(encodeFloats(ramp(8)), 1, 200).count, 8);
		QCOMPARE(waveform::decode(encodeFloats(ramp(200)), 1, 200).status, DecodeStatus::Ok);
		QCOMPARE(waveform::decode(encodeFloats(ramp(7)), 8, 200).status, DecodeStatus::WrongLength);
		// A minimum below one is treated as one: zero values are never "a waveform".
		QCOMPARE(waveform::decode(encodeFloats(ramp(1)), 0, 4).status, DecodeStatus::Ok);
	}

	//! The encoded-length bound is exact: the largest valid payload fits, one more character does not.
	void theEncodedBoundIsExact()
	{
		for (int count : {1, 2, 3, 16, 32, 128, 200, 220, 4096})
		{
			const QString encoded = encodeFloats(ramp(count));
			QCOMPARE(encoded.size(), waveform::maxEncodedLength(count));
			QCOMPARE(waveform::decode(encoded + QStringLiteral("===="), count, count).status, DecodeStatus::Oversized);
		}
		QVERIFY(waveform::maxEncodedLength(std::numeric_limits<int>::max()) > 0);
	}

	//! A huge payload is refused before anything the size of it is allocated.
	void aHugePayloadIsRefusedWithBoundedMemory()
	{
		const QString huge = encodeFloats(std::vector<float>(1 << 21, 0.25f));  // 8 MiB of floats
#ifdef WAVEFORM_TEST_ASAN
		g_largest = 0;
		g_watching = true;
#endif
		const auto decoded = waveform::decode(huge, 220, 220);
#ifdef WAVEFORM_TEST_ASAN
		g_watching = false;
		QVERIFY2(g_largest.load() < 4096, qPrintable(QStringLiteral("refusing a %1-character payload allocated %2 bytes at once")
			.arg(huge.size()).arg(g_largest.load())));
		// The largest valid payload costs about its own size, not a multiple of the text it came in.
		const QString largest = encodeFloats(ramp(4096));
		g_largest = 0;
		g_watching = true;
		const auto accepted = waveform::decode(largest, 4096, 4096);
		g_watching = false;
		QCOMPARE(accepted.status, DecodeStatus::Ok);
		QVERIFY2(g_largest.load() <= 2 * 4096 * sizeof(float) + 1024, qPrintable(QString::number(g_largest.load())));
#endif
		QCOMPARE(decoded.status, DecodeStatus::Oversized);
		QVERIFY(decoded.samples == nullptr);
	}

	//! loadGraph replaces a graph only with exactly graph.length() values and keeps it otherwise.
	void loadGraphKeepsTheGraphOnARefusal()
	{
		graphModel graph(-1.0f, 1.0f, 32, nullptr);
		const std::vector<float> first = ramp(32);
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("probe"));
		element.setAttribute(QStringLiteral("shape"), encodeFloats(first));
		QCOMPARE(waveform::loadGraph(element, QStringLiteral("shape"), graph, QStringLiteral("Probe")), DecodeStatus::Ok);
		QVERIFY(std::memcmp(graph.samples(), first.data(), first.size() * sizeof(float)) == 0);

		for (const QString& damaged : {encodeFloats(ramp(31, 0.5f)), encodeFloats(ramp(33, 0.5f)), encodeBytes(127),
			QStringLiteral("@@@@"), QString()})
		{
			element.setAttribute(QStringLiteral("shape"), damaged);
			QVERIFY(waveform::loadGraph(element, QStringLiteral("shape"), graph, QStringLiteral("Probe")) != DecodeStatus::Ok);
			QVERIFY(std::memcmp(graph.samples(), first.data(), first.size() * sizeof(float)) == 0);
		}
		element.removeAttribute(QStringLiteral("shape"));
		QCOMPARE(waveform::loadGraph(element, QStringLiteral("shape"), graph, QStringLiteral("Probe")), DecodeStatus::Missing);
		QVERIFY(std::memcmp(graph.samples(), first.data(), first.size() * sizeof(float)) == 0);
	}

	//! The length-checked setter copies no more than its source holds (an exact-size heap source,
	//! so AddressSanitizer would report a read past it) and no more than the graph holds.
	void theLengthCheckedSetterStaysInsideBothBuffers()
	{
		graphModel graph(-1.0f, 1.0f, 16, nullptr);
		const auto shortSource = std::make_unique<float[]>(5);
		for (int i = 0; i < 5; ++i) { shortSource[i] = 0.5f; }
		graph.setSamples(shortSource.get(), 5);
		QCOMPARE(graph.samples()[4], 0.5f);
		QCOMPARE(graph.samples()[5], 0.0f);

		const auto longSource = std::make_unique<float[]>(64);
		for (int i = 0; i < 64; ++i) { longSource[i] = -0.25f; }
		graph.setSamples(longSource.get(), 64);
		QCOMPARE(graph.length(), 16);
		QCOMPARE(graph.samples()[15], -0.25f);

		graph.setSamples(nullptr, 16);
		graph.setSamples(longSource.get(), -3);
		QCOMPARE(graph.samples()[0], -0.25f);
	}
};

QTEST_GUILESS_MAIN(WaveformDataTest)

#include "WaveformDataTest.moc"
