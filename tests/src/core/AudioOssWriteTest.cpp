/*
 * AudioOssWriteTest.cpp - complete writes for OSS audio buffers.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>

#if __has_include("AudioOssWrite.h")
#include "AudioOssWrite.h"
#else
// Fallback used only when the product helper has been removed (e.g. a reversed
// product patch): it models the original single-write behaviour so the
// regression can still compile and demonstrate the bug.
#include <cerrno>
namespace lmms::audio
{
template <typename Writer>
auto writeAll(Writer&& writer, const void* data, std::size_t size) -> bool
{
	if (size == 0) { return true; }
	const auto written = writer(data, size);
	if (written < 0) { return false; }
	return static_cast<std::size_t>(written) == size;
}
} // namespace lmms::audio
#endif

class AudioOssWriteTest : public QObject
{
	Q_OBJECT

private slots:
	void completesShortAndInterruptedWrites()
	{
		const QByteArray input = QByteArrayLiteral("abcdef");
		QByteArray output;
		constexpr std::array<std::ptrdiff_t, 4> results{2, -1, 1, 3};
		std::array<std::size_t, 4> requested{};
		auto call = std::size_t{0};

		const auto complete = lmms::audio::writeAll(
			[&](const void* data, std::size_t remaining) {
				requested[call] = remaining;
				const auto result = results[call++];
				if (result < 0)
				{
					errno = EINTR;
					return result;
				}
				output.append(static_cast<const char*>(data), static_cast<qsizetype>(result));
				return result;
			},
			input.constData(), static_cast<std::size_t>(input.size()));

		QVERIFY(complete);
		QCOMPARE(output, input);
		QCOMPARE(call, results.size());
		QCOMPARE(requested[0], std::size_t{6});
		QCOMPARE(requested[1], std::size_t{4});
		QCOMPARE(requested[2], std::size_t{4});
		QCOMPARE(requested[3], std::size_t{3});
	}

	void stopsOnOtherError()
	{
		const auto complete = lmms::audio::writeAll(
			[](const void*, std::size_t) {
				errno = EIO;
				return std::ptrdiff_t{-1};
			}, "x", 1);

		QVERIFY(!complete);
	}

	void stopsOnZeroProgress()
	{
		const auto complete = lmms::audio::writeAll(
			[](const void*, std::size_t) { return std::ptrdiff_t{0}; }, "x", 1);

		QVERIFY(!complete);
	}
};

QTEST_GUILESS_MAIN(AudioOssWriteTest)

#include "AudioOssWriteTest.moc"
