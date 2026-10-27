/*
 * StripMeterModeTest.cpp - Q050-108: each mixer strip's own meter mode - peak, RMS, VU, LUFS and true peak
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

/*! Q050-108: mixer.set_strip 'meter_mode' sets one strip's meter (default - the view's -, peak, rms, pre_fader,
 *  vu, lufs) without touching another; mixer.strip_get reads it back with the strip's own readings. The VU needle
 *  integrates over 300 ms (99 % of a steady sine's reading at 300 ms, well short of it at 150 ms) and reads 0 VU
 *  for a sine at meter.set_reference's level; two strips fed known, different stereo sines read their own
 *  calibrated BS.1770 loudness and true peak, after their faders. The mode is saved with the channel (absent:
 *  default) and one undo step puts it back. The strip meters allocate nothing on the mixer worker. Synthetic
 *  buffers through the strip's meter stage (mixerstrip::updateMeters): no plug-in module. */

#include <QtTest>

#include <QTemporaryDir>

#include <cmath>
#include <vector>

#include "AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerStrip.h"
#include "SampleFrame.h"
#include "StripMeters.h"

using namespace lmms;

namespace
{

constexpr double kTwoPi = 6.283185307179586;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

MixerChannel* channel(int index) { return Engine::mixer()->mixerChannel(index); }

QString ch(int index) { return control::channelIdOf(channel(index)); }

double rate() { return Engine::audioEngine()->outputSampleRate(); }

bool setMode(int index, const char* mode)
{
	return run(QStringLiteral("mixer.set_strip"), {{QStringLiteral("channel"), ch(index)}, {QStringLiteral("meter_mode"), QLatin1String(mode)}}).ok;
}

QJsonObject stripOf(int index)
{
	return run(QStringLiteral("mixer.strip_get"), {{QStringLiteral("channel"), ch(index)}}).result;
}

//! Feeds stereo sines (@a hz, peak dBFS @a leftDb / @a rightDb) through @a index's meter stage at fader @a volume,
//! period by period, from frame *@a at; calls @a each(seconds) after every period.
template<typename Each>
void feed(int index, double hz, double leftDb, double rightDb, float volume, double seconds, long* at, Each each)
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> block(static_cast<std::size_t>(fpp));
	const double left = std::pow(10.0, leftDb / 20.0);
	const double right = std::pow(10.0, rightDb / 20.0);
	const long end = *at + static_cast<long>(seconds * rate());
	while (*at < end)
	{
		for (SampleFrame& frame : block)
		{
			const double s = std::sin(kTwoPi * hz * static_cast<double>((*at)++) / rate());
			frame = SampleFrame(static_cast<float>(left * s), static_cast<float>(right * s));
		}
		mixerstrip::updateMeters(*channel(index), block.data(), volume, fpp);
		each(static_cast<double>(*at) / rate());
	}
}

void feed(int index, double hz, double levelDb, float volume, double seconds)
{
	long at = 0;
	feed(index, hz, levelDb, levelDb, volume, seconds, &at, [](double) {});
}

} // namespace

class StripMeterModeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test feeds the meter stage itself
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		QVERIFY(run(QStringLiteral("project.new")).ok);
		while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), 0}}).ok);
		QVERIFY(run(QStringLiteral("meter.set_reference"), {{QStringLiteral("dbfs"), -18.0}}).ok);
	}

	//! A new channel follows the view; each strip takes its own mode, read back by the agent; a bad name is refused.
	void eachStripKeepsItsOwnMode()
	{
		QCOMPARE(stripOf(1).value(QStringLiteral("meter_mode")).toString(), QStringLiteral("default"));
		QVERIFY(setMode(1, "vu"));
		QVERIFY(setMode(2, "lufs"));
		QCOMPARE(stripOf(1).value(QStringLiteral("meter_mode")).toString(), QStringLiteral("vu"));
		QCOMPARE(stripOf(2).value(QStringLiteral("meter_mode")).toString(), QStringLiteral("lufs"));
		QCOMPARE(stripOf(3).value(QStringLiteral("meter_mode")).toString(), QStringLiteral("default"));
		for (const char* mode : {"peak", "rms", "pre_fader", "default"})
		{
			QVERIFY(setMode(3, mode));
			QCOMPARE(stripOf(3).value(QStringLiteral("meter_mode")).toString(), QLatin1String(mode));
		}
		QCOMPARE(stripOf(1).value(QStringLiteral("meter_mode")).toString(), QStringLiteral("vu"));  // untouched
		QVERIFY(!setMode(1, "ppm"));
		QCOMPARE(stripOf(1).value(QStringLiteral("meter_mode")).toString(), QStringLiteral("vu"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the last change: channel 3 back to pre_fader
		QCOMPARE(stripOf(3).value(QStringLiteral("meter_mode")).toString(), QStringLiteral("pre_fader"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(stripOf(3).value(QStringLiteral("meter_mode")).toString(), QStringLiteral("rms"));
	}

	//! VU: a steady 1 kHz sine at the reference (-18 dBFS peak) reads 0 VU, reaching 99 % of it (-0.09 VU) at 300 ms
	//! and not before 150 ms; another reference moves the reading by the difference. RMS stays its own mode.
	void theVuIntegratesOver300msToTheReference()
	{
		QVERIFY(setMode(1, "vu"));
		double at150 = 0.0;
		double at300 = 0.0;
		double before300 = -100.0;
		long frame = 0;
		feed(1, 1000.0, -18.0, -18.0, 1.f, 1.0, &frame, [&](double t) {
			const double vu = stripmeters::vu(*channel(1), 0, -18.0);
			if (t < 0.15) { at150 = vu; }
			if (t < 0.29) { before300 = vu; }
			if (t >= 0.3 && at300 == 0.0) { at300 = vu; }
		});
		QVERIFY2(at150 < -1.0, qPrintable(QStringLiteral("the needle read %1 VU at 150 ms: no integration").arg(at150)));
		QVERIFY2(before300 < -0.09, qPrintable(QStringLiteral("the needle read %1 VU before 290 ms").arg(before300)));
		QVERIFY2(at300 > -0.15 && at300 < 0.1, qPrintable(QStringLiteral("the needle read %1 VU at 300 ms").arg(at300)));
		const double settled = stripmeters::vu(*channel(1), 0, -18.0);
		QVERIFY2(std::abs(settled) < 0.05, qPrintable(QStringLiteral("a steady reference tone reads %1 VU").arg(settled)));
		const QJsonObject meters = stripOf(1).value(QStringLiteral("meters")).toObject();
		QVERIFY(std::abs(meters.value(QStringLiteral("vu_left")).toDouble() - settled) < 0.05);
		QCOMPARE(meters.value(QStringLiteral("vu_reference_dbfs")).toDouble(), -18.0);
		QVERIFY(run(QStringLiteral("meter.set_reference"), {{QStringLiteral("dbfs"), -12.0}}).ok);
		const double moved = stripOf(1).value(QStringLiteral("meters")).toObject().value(QStringLiteral("vu_left")).toDouble();
		QVERIFY2(std::abs(moved + 6.0) < 0.05, qPrintable(QStringLiteral("at a -12 dBFS reference the -18 dBFS tone reads %1 VU").arg(moved)));
		// RMS is not VU: its reading is this period's RMS after the fader (a -18 dBFS sine: -21 dBFS).
		const double rmsDb = 20.0 * std::log10(stripOf(1).value(QStringLiteral("meters")).toObject().value(QStringLiteral("rms_left")).toDouble());
		QVERIFY(std::abs(rmsDb + 21.01) < 0.1);
		// A strip in another mode has no needle.
		QVERIFY(stripOf(2).value(QStringLiteral("meters")).toObject().value(QStringLiteral("vu_left")).isNull());
	}

	//! LUFS: two strips fed known stereo sines read their own BS.1770 loudness and true peak (EBU Tech 3341's
	//! -23 dBFS 1 kHz tone reads -23 LUFS), after their own fader; a strip not in lufs reads none.
	void eachStripReadsItsOwnLoudness()
	{
		QVERIFY(setMode(1, "lufs"));
		QVERIFY(setMode(2, "lufs"));
		feed(1, 1000.0, -23.0, 1.f, 3.5);
		feed(2, 1000.0, -7.0, 0.5f, 3.5);  // -7 dBFS through a fader at 0.5 (-6.02 dB): -13 after it
		const QJsonObject one = stripOf(1).value(QStringLiteral("meters")).toObject();
		const QJsonObject two = stripOf(2).value(QStringLiteral("meters")).toObject();
		const auto closeTo = [](const QJsonObject& m, const char* key, double want, double within) {
			const double got = m.value(QLatin1String(key)).toDouble(1e9);
			if (std::abs(got - want) > within) { qWarning("%s read %.3f, want %.3f", key, got, want); }
			return std::abs(got - want) <= within;
		};
		QVERIFY(closeTo(one, "lufs_short_term", -23.0, 0.1));
		QVERIFY(closeTo(one, "lufs_momentary", -23.0, 0.1));
		QVERIFY(closeTo(one, "true_peak_dbtp", -23.0, 0.2));
		QVERIFY(closeTo(two, "lufs_short_term", -13.02, 0.1));
		QVERIFY(closeTo(two, "true_peak_dbtp", -13.02, 0.2));
		QVERIFY(stripOf(3).value(QStringLiteral("meters")).toObject().value(QStringLiteral("lufs_short_term")).isNull());
		// Leaving and re-entering the mode starts a fresh measurement: the old true peak is gone.
		QVERIFY(setMode(2, "peak"));
		QVERIFY(stripOf(2).value(QStringLiteral("meters")).toObject().value(QStringLiteral("true_peak_dbtp")).isNull());
		QVERIFY(setMode(2, "lufs"));
		feed(2, 1000.0, -30.0, 1.f, 0.5);
		QVERIFY(closeTo(stripOf(2).value(QStringLiteral("meters")).toObject(), "true_peak_dbtp", -30.0, 0.2));
	}

	//! The mode is saved with the channel - only off its default - and comes back on load; a channel saved before
	//! the mode existed reads 'default'.
	void theModeIsSavedWithTheChannel()
	{
		QVERIFY(setMode(1, "vu"));
		QVERIFY(setMode(2, "lufs"));
		QTemporaryDir dir;
		const QString path = dir.filePath(QStringLiteral("meter-modes.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), path}}).ok);
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), path}}).ok);
		QCOMPARE(stripOf(1).value(QStringLiteral("meter_mode")).toString(), QStringLiteral("vu"));
		QCOMPARE(stripOf(2).value(QStringLiteral("meter_mode")).toString(), QStringLiteral("lufs"));
		QCOMPARE(stripOf(3).value(QStringLiteral("meter_mode")).toString(), QStringLiteral("default"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll().count("metermode=\""), 2);
	}

	//! The VU and loudness meters run on fixed state: a period through each, and a muted period, allocate nothing.
	void theStripMetersAllocateNothing()
	{
		QVERIFY(setMode(1, "vu"));
		QVERIFY(setMode(2, "lufs"));  // the loudness meter is built here, on this (control) thread
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> block(static_cast<std::size_t>(fpp), SampleFrame(0.25f, -0.25f));
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int p = 0; p < 400; ++p)
		{
			mixerstrip::updateMeters(*channel(1), block.data(), 1.f, fpp);
			mixerstrip::updateMeters(*channel(2), block.data(), 1.f, fpp);
			mixerstrip::silenceMeters(*channel(1));
			mixerstrip::silenceMeters(*channel(2));
		}
		test::tlCountAllocations = false;
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});
		QVERIFY(std::isfinite(stripmeters::loudness(*channel(2))->truePeakDbtp()));
	}
};

QTEST_MAIN(StripMeterModeTest)
#include "StripMeterModeTest.moc"
