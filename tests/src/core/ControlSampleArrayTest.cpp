/*
 * ControlSampleArrayTest.cpp - the rules a caller's sample array has to satisfy
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
 * WHY THIS TEST EXISTS. A caller may hand a command an array of samples (the
 * stimulus a render or a process is driven with). The array used to be
 * converted with a file-local loop that called QJsonValue::toDouble() on every
 * element, so a string became a 0, a double outside the float range became an
 * infinity, and an array longer than the render was reserved and copied whole.
 * The rules now live in ONE reader (include/ControlSampleArray.h), and this
 * test is what holds them to that.
 *
 * The reader is pure Qt, so this test runs in every configuration - including
 * a build without the wasmtime C API, where the wasm.* commands themselves do
 * not exist. The command-level coverage of the same rules is
 * tests/control-wasm-sandbox.py, which needs the sandbox compiled in.
 */

#include <QtTest>

#include <QJsonArray>
#include <QJsonValue>

#include "ControlSampleArray.h"

#include <cstdint>
#include <limits>
#include <vector>

class ControlSampleArrayTest : public QObject
{
	Q_OBJECT

private:
	//! What one read of a caller's array produced.
	struct Read
	{
		bool ok = false;
		QString error;
		lmms::ControlErrorKind errorKind = lmms::ControlErrorKind::None;
		std::vector<float> samples;
	};

	//! Read \p values as the samples of a call that asks for \p frames.
	Read read(const QJsonArray& values, std::uint32_t frames) const
	{
		Read read;
		// A sentinel, so a refused read can be shown to have written nothing.
		read.samples.assign(1, -12345.0f);
		const lmms::ControlResult result =
			lmms::control::readSampleArray(values, frames, &read.samples);
		read.ok = result.ok;
		read.error = result.errorMessage;
		read.errorKind = result.errorKind;
		return read;
	}

private slots:
	//! A string is not a sample: toDouble() would turn it into a 0.
	void refusesAStringSample()
	{
		const Read read = this->read(QJsonArray{QStringLiteral("bad"), 0.25}, 2);
		QVERIFY2(!read.ok, "a string sample was accepted");
		QVERIFY(!read.error.isEmpty());
		QVERIFY(read.errorKind == lmms::ControlErrorKind::InvalidArgs);
		QCOMPARE(read.samples.size(), static_cast<std::size_t>(1));
		QCOMPARE(read.samples[0], -12345.0f);
	}

	//! A bool is not a sample either.
	void refusesABooleanSample()
	{
		const Read read = this->read(QJsonArray{true, 0.25}, 2);
		QVERIFY2(!read.ok, "a boolean sample was accepted");
		QVERIFY(read.errorKind == lmms::ControlErrorKind::InvalidArgs);
	}

	//! 1e300 is finite, but it is not a float: the conversion is undefined.
	void refusesAValueOutsideTheFloatRange()
	{
		const Read read = this->read(QJsonArray{1e300}, 1);
		QVERIFY2(!read.ok, "a sample outside the float range was accepted");
		QVERIFY(read.errorKind == lmms::ControlErrorKind::InvalidArgs);
	}

	//! An infinity is not a sample at all.
	void refusesANonFiniteValue()
	{
		const Read read = this->read(
			QJsonArray{std::numeric_limits<double>::infinity()}, 1);
		QVERIFY2(!read.ok, "an infinite sample was accepted");
		QVERIFY(read.errorKind == lmms::ControlErrorKind::InvalidArgs);
	}

	//! A render or a process consumes at most `frames` samples.
	void refusesAnArrayLongerThanTheFrames()
	{
		const Read read = this->read(QJsonArray{0.25, 0.5}, 1);
		QVERIFY2(!read.ok, "more samples than frames were accepted");
		QVERIFY(read.errorKind == lmms::ControlErrorKind::InvalidArgs);
	}

	//! An array of exactly `frames` samples is the longest one that is accepted.
	void acceptsAnArrayOfExactlyTheFrameCount()
	{
		const Read read = this->read(QJsonArray{0.25, 0.5}, 2);
		QVERIFY2(read.ok, qPrintable(read.error));
		QCOMPARE(read.samples.size(), static_cast<std::size_t>(2));
		QCOMPARE(read.samples[0], 0.25f);
		QCOMPARE(read.samples[1], 0.5f);
	}

	//! An empty array is silence, not a malformed call.
	void acceptsAnEmptyArray()
	{
		const Read read = this->read(QJsonArray{}, 4);
		QVERIFY2(read.ok, qPrintable(read.error));
		QVERIFY(read.samples.empty());
	}

	//! A negative sample is a sample.
	void acceptsANegativeSample()
	{
		const Read read = this->read(QJsonArray{-0.5}, 1);
		QVERIFY2(read.ok, qPrintable(read.error));
		QCOMPARE(read.samples.size(), static_cast<std::size_t>(1));
		QCOMPARE(read.samples[0], -0.5f);
	}

	//! An ABSENT 'input' argument leaves the caller's own stimulus alone.
	void anAbsentArgumentLeavesTheRampAlone()
	{
		std::vector<float> samples{0.25f, 0.5f};
		const lmms::ControlResult result =
			lmms::control::readInputArgument(QJsonObject{}, 2, &samples);
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(samples.size(), static_cast<std::size_t>(2));
		QCOMPARE(samples[0], 0.25f);
	}

	//! A malformed 'input' argument is refused and changes nothing.
	void aMalformedArgumentIsRefusedAndChangesNothing()
	{
		std::vector<float> samples{0.25f, 0.5f};
		const QJsonObject args{{QStringLiteral("input"), QJsonArray{QStringLiteral("bad")}}};
		const lmms::ControlResult result =
			lmms::control::readInputArgument(args, 2, &samples);
		QVERIFY(!result.ok);
		QVERIFY(result.errorKind == lmms::ControlErrorKind::InvalidArgs);
		QCOMPARE(samples.size(), static_cast<std::size_t>(2));
		QCOMPARE(samples[0], 0.25f);
	}

	//! An ABSENT 'sample_rate' is the caller's default, and it is what is set.
	void anAbsentSampleRateIsTheFallback()
	{
		float rate = -1.0f;
		const lmms::ControlResult result =
			lmms::control::readSampleRateArgument(QJsonObject{}, 48000.0f, &rate);
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(rate, 48000.0f);
	}

	//! A finite, positive rate is read, up to the largest float.
	void aFinitePositiveSampleRateIsRead()
	{
		const QList<double> rates{44100.0, static_cast<double>(std::numeric_limits<float>::max())};
		for (const double value : rates)
		{
			float rate = 0.0f;
			const QJsonObject args{{QStringLiteral("sample_rate"), value}};
			const lmms::ControlResult result =
				lmms::control::readSampleRateArgument(args, 48000.0f, &rate);
			QVERIFY2(result.ok, qPrintable(result.errorMessage));
			QCOMPARE(static_cast<double>(rate), value);
		}
	}

	//! A rate that is not a finite, positive float is refused and changes nothing:
	//! 1e300 and an over-range double become an infinity, a negative or zero rate
	//! is no rate, 1e-60 rounds to a float zero, and a string is not a number.
	void anInvalidSampleRateIsRefusedAndChangesNothing()
	{
		const QList<QJsonValue> rates{
			QJsonValue(1e300),
			QJsonValue(-1.0),
			QJsonValue(0.0),
			QJsonValue(1e-60),
			QJsonValue(static_cast<double>(std::numeric_limits<float>::max()) * 2.0),
			QJsonValue(QStringLiteral("48000"))};
		for (const QJsonValue& value : rates)
		{
			float rate = 12345.0f;
			const QJsonObject args{{QStringLiteral("sample_rate"), value}};
			const lmms::ControlResult result =
				lmms::control::readSampleRateArgument(args, 48000.0f, &rate);
			QVERIFY(!result.ok);
			QVERIFY(result.errorKind == lmms::ControlErrorKind::InvalidArgs);
			QCOMPARE(rate, 12345.0f);
		}
	}
};

QTEST_GUILESS_MAIN(ControlSampleArrayTest)

#include "ControlSampleArrayTest.moc"
