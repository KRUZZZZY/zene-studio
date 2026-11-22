/*
 * ConsoleStripTest.cpp - Q050-088: the strip's Console section and the Console's glue compressor
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

/*! Q050-088: mixer.console_strip sets and reads ONE channel's trim, polarity, EQ and its Console's drive and
 *  glue compressor; each setting changes its own signal property on its own channel (deterministic renders
 *  through the strip stages and the Console's chain); one undo step restores the section; a project
 *  saved and reopened keeps it; a refused call changes nothing and records no step; an older Console
 *  state (no glue keys) loads with the glue off and renders as a Console without it; the glue stage
 *  allocates nothing while processing. Loads the build's console module. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <vector>

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ConsoleStrip.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerStrip.h"
#include "SampleFrame.h"
#include "Song.h"

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

int undoDepth() { return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(); }

QJsonObject strip(int index, QJsonObject args = {})
{
	args.insert(QStringLiteral("channel"), ch(index));
	const ControlResult result = run(QStringLiteral("mixer.console_strip"), args);
	return result.ok ? result.result : QJsonObject{{QStringLiteral("error"), result.errorMessage}};
}

//! The console module through plugin.load onto channel @a index; its fx id, empty when this build has none.
QString loadConsole(int index)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
		.result.value(QStringLiteral("devices")).toArray())
	{
		if (device.toObject().value(QStringLiteral("name")).toString() != QLatin1String("console")) { continue; }
		return run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), ch(index)},
			{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}}).result.value(QStringLiteral("id")).toString();
	}
	return QString();
}

//! @a blocks periods of a sine (@a hz, peak @a level) through @a index's chain; the RMS of the last block, left.
double chainRms(int index, double hz, float level, int blocks)
{
	const auto fpp = static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod());
	std::vector<SampleFrame> frames(fpp);
	SampleFrame* data[1] = {frames.data()};
	double sum = 0.0;
	long at = 0;
	for (int b = 0; b < blocks; ++b)
	{
		for (SampleFrame& frame : frames)
		{
			const auto x = static_cast<float>(level * std::sin(kTwoPi * hz * static_cast<double>(at++) / rate()));
			frame = SampleFrame(x, x);
		}
		AudioBus bus{data, 1, static_cast<f_cnt_t>(fpp)};
		channel(index)->m_fxChain.processAudioBuffer(bus);
		sum = 0.0;
		for (const SampleFrame& frame : frames) { sum += static_cast<double>(frame[0]) * frame[0]; }
	}
	return std::sqrt(sum / static_cast<double>(fpp));
}

double db(double linear) { return 20.0 * std::log10(std::max(linear, 1e-12)); }

//! The strip's own stages on channel @a index over one block of a sine (@a hz, @a level); the block's RMS, left and right.
std::pair<double, double> stripRms(int index, double hz, float level)
{
	MixerChannel* c = channel(index);
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::fill(c->m_eqState.begin(), c->m_eqState.end(), 0.f);
	double left = 0.0;
	double right = 0.0;
	long at = 0;
	for (int pass = 0; pass < 40; ++pass)  // the filters settle; the last block is measured
	{
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			const auto x = static_cast<float>(level * std::sin(kTwoPi * hz * static_cast<double>(at++) / rate()));
			c->m_buffer[f] = SampleFrame(x, x);
		}
		mixerstrip::applyInput(*c, fpp);
		mixerstrip::applyOutput(*c, fpp);
		left = right = 0.0;
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			left += static_cast<double>(c->m_buffer[f][0]) * c->m_buffer[f][0];
			right += static_cast<double>(c->m_buffer[f][1]) * c->m_buffer[f][1];
		}
	}
	return {std::sqrt(left / fpp), std::sqrt(right / fpp)};
}

//! One period of a full-scale (or @a level) square wave through channel @a index's chain, counted by the allocation
//! probe; true when every output frame is finite and the glue's reduction is bounded.
bool squareBlockStaysBounded(int index, float level, long* at)
{
	static std::vector<SampleFrame> frames;
	frames.resize(static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod()));  // sized before the probe counts
	for (SampleFrame& frame : frames)
	{
		const float x = ((*at)++ % 97 < 48 ? 1.f : -1.f) * level;
		frame = SampleFrame(x, -x);
	}
	SampleFrame* data[1] = {frames.data()};
	AudioBus bus{data, 1, static_cast<f_cnt_t>(frames.size())};
	test::tlCountAllocations = true;
	channel(index)->m_fxChain.processAudioBuffer(bus);
	test::tlCountAllocations = false;
	const bool finite = std::all_of(frames.begin(), frames.end(), [](const SampleFrame& f) { return std::isfinite(f[0]) && std::isfinite(f[1]); });
	const float reduction = channel(index)->m_fxChain.effects().front()->gainReductionDb();
	return finite && std::isfinite(reduction) && reduction >= 0.f && reduction <= 200.f;
}

} // namespace

class ConsoleStripTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test runs the chains and strip stages itself
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
		// Each call below is its own undo step: no coalescing window (the GUI test drives a coalesced drag).
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), 0}}).ok);
	}

	//! Every field is set and read back on its channel, and only there.
	void theSectionSetsAndReadsOneChannel()
	{
		if (loadConsole(1).isEmpty() || loadConsole(2).isEmpty()) { QSKIP("this build has no console module"); }
		const QJsonObject set = strip(1, {{QStringLiteral("trim_db"), 3.5}, {QStringLiteral("polarity_left"), true},
			{QStringLiteral("polarity_right"), false}, {QStringLiteral("eq_low_db"), 4.0}, {QStringLiteral("eq_mid_db"), -3.0},
			{QStringLiteral("eq_mid_freq"), 2500.0}, {QStringLiteral("eq_high_db"), 2.0}, {QStringLiteral("drive"), 70.0},
			{QStringLiteral("glue"), true}, {QStringLiteral("glue_threshold_db"), -18.0}, {QStringLiteral("glue_ratio"), 6.0},
			{QStringLiteral("glue_makeup_db"), 5.0}});
		QVERIFY2(!set.contains(QStringLiteral("error")), qPrintable(set.value(QStringLiteral("error")).toString()));
		const QJsonObject read = strip(1);
		QCOMPARE(read.value(QStringLiteral("console")).toBool(), true);
		QCOMPARE(read.value(QStringLiteral("trim_db")).toDouble(), 3.5);
		QCOMPARE(read.value(QStringLiteral("polarity_left")).toBool(), true);
		QCOMPARE(read.value(QStringLiteral("eq_low_db")).toDouble(), 4.0);
		QCOMPARE(read.value(QStringLiteral("eq_mid_db")).toDouble(), -3.0);
		QCOMPARE(read.value(QStringLiteral("eq_mid_freq")).toDouble(), 2500.0);
		QCOMPARE(read.value(QStringLiteral("eq_high_db")).toDouble(), 2.0);
		QCOMPARE(read.value(QStringLiteral("drive")).toDouble(), 70.0);
		QCOMPARE(read.value(QStringLiteral("glue")).toBool(), true);
		QCOMPARE(read.value(QStringLiteral("glue_threshold_db")).toDouble(), -18.0);
		QCOMPARE(read.value(QStringLiteral("glue_ratio")).toDouble(), 6.0);
		QCOMPARE(read.value(QStringLiteral("glue_makeup_db")).toDouble(), 5.0);
		// The models the strip stages read carry it (mixer.set_strip / mixer.channel_eq see the same values).
		QCOMPARE(channel(1)->m_trimModel.value(), 3.5f);
		QCOMPARE(run(QStringLiteral("mixer.strip_get"), {{QStringLiteral("channel"), ch(1)}}).result.value(QStringLiteral("polarity_left")).toBool(), true);
		// Channel 2 - which has its own Console - is untouched.
		const QJsonObject other = strip(2);
		QCOMPARE(other.value(QStringLiteral("trim_db")).toDouble(), 0.0);
		QCOMPARE(other.value(QStringLiteral("drive")).toDouble(), 30.0);
		QCOMPARE(other.value(QStringLiteral("glue")).toBool(), false);
		QCOMPARE(other.value(QStringLiteral("eq_low_db")).toDouble(), 0.0);
	}

	//! Trim, polarity and each EQ band change their own signal property, on their own channel.
	void theStripControlsChangeOnlyTheirOwnSignal()
	{
		const auto flat = stripRms(1, 1000.0, 0.25f);
		QVERIFY(!strip(1, {{QStringLiteral("trim_db"), 6.0206}}).contains(QStringLiteral("error")));
		const auto trimmed = stripRms(1, 1000.0, 0.25f);
		QVERIFY(std::abs(trimmed.first / flat.first - 2.0) < 1e-3);  // +6 dB doubles both sides
		QVERIFY(std::abs(trimmed.second / flat.second - 2.0) < 1e-3);
		QVERIFY(std::abs(stripRms(2, 1000.0, 0.25f).first - flat.first) < 1e-6);  // channel 2 untouched
		QVERIFY(!strip(1, {{QStringLiteral("trim_db"), 0.0}, {QStringLiteral("polarity_right"), true}}).contains(QStringLiteral("error")));
		MixerChannel* c = channel(1);
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		for (f_cnt_t f = 0; f < fpp; ++f) { c->m_buffer[f] = SampleFrame(0.5f, 0.5f); }
		mixerstrip::applyOutput(*c, fpp);
		QCOMPARE(c->m_buffer[0][0], 0.5f);  // the left side as it was
		QCOMPARE(c->m_buffer[0][1], -0.5f);  // the right side inverted, its level the same
		QVERIFY(!strip(1, {{QStringLiteral("polarity_right"), false}}).contains(QStringLiteral("error")));
		// Each band: +12 dB lifts its own region and leaves the others' (within the shelves' skirts) alone - each
		// measured against the same render with the band flat, so the block's sine phase cancels out.
		struct Band { const char* field; double inside; double outside; };
		for (const Band& band : {Band{"eq_low_db", 40.0, 4000.0}, Band{"eq_mid_db", 1000.0, 40.0}, Band{"eq_high_db", 16000.0, 300.0}})
		{
			const double flatInside = db(stripRms(1, band.inside, 0.1f).first);
			const double flatOutside = db(stripRms(1, band.outside, 0.1f).first);
			QVERIFY(!strip(1, {{QLatin1String(band.field), 12.0}}).contains(QStringLiteral("error")));
			const double lifted = db(stripRms(1, band.inside, 0.1f).first) - flatInside;
			const double spared = db(stripRms(1, band.outside, 0.1f).first) - flatOutside;
			QVERIFY2(lifted > 9.0, qPrintable(QStringLiteral("%1 lifted its band by %2 dB").arg(QLatin1String(band.field)).arg(lifted)));
			QVERIFY2(std::abs(spared) < 1.0, qPrintable(QStringLiteral("%1 moved %2 dB outside its band").arg(QLatin1String(band.field)).arg(spared)));
			QVERIFY(!strip(1, {{QLatin1String(band.field), 0.0}}).contains(QStringLiteral("error")));
		}
	}

	//! Drive rounds a loud signal's peaks; the glue turns a loud signal down by its ratio above the threshold and
	//! the makeup puts gain back; at ratio 1 it does nothing - all on this channel's Console only.
	void theConsoleDriveAndGlueShapeTheLevel()
	{
		if (loadConsole(1).isEmpty() || loadConsole(2).isEmpty()) { QSKIP("this build has no console module"); }
		QVERIFY(!strip(1, {{QStringLiteral("drive"), 0.0}}).contains(QStringLiteral("error")));
		QVERIFY(!strip(2, {{QStringLiteral("drive"), 0.0}}).contains(QStringLiteral("error")));
		const double clean = db(chainRms(1, 1000.0, 0.5f, 200));
		QVERIFY(!strip(1, {{QStringLiteral("drive"), 100.0}}).contains(QStringLiteral("error")));
		const double driven = db(chainRms(1, 1000.0, 0.5f, 200));
		QVERIFY2(driven < clean - 0.3, qPrintable(QStringLiteral("full drive left a -6 dBFS sine at %1 dB (clean %2)").arg(driven).arg(clean)));
		QVERIFY(!strip(1, {{QStringLiteral("drive"), 0.0}, {QStringLiteral("glue"), true}, {QStringLiteral("glue_threshold_db"), -20.0},
			{QStringLiteral("glue_ratio"), 4.0}}).contains(QStringLiteral("error")));
		const double glued = db(chainRms(1, 1000.0, 0.5f, 200));
		// A -6 dBFS peak is 14 dB over -20; at 4:1 it keeps 3.5 of them: about 10.5 dB less (the detector reads peaks).
		QVERIFY2(std::abs((clean - glued) - 10.5) < 1.5, qPrintable(QStringLiteral("the glue took %1 dB off").arg(clean - glued)));
		QVERIFY(Engine::mixer()->mixerChannel(1)->m_fxChain.effects().front()->gainReductionDb() > 9.0);
		QVERIFY(!strip(1, {{QStringLiteral("glue_makeup_db"), 6.0}}).contains(QStringLiteral("error")));
		const double madeUp = db(chainRms(1, 1000.0, 0.5f, 200));
		QVERIFY(std::abs((madeUp - glued) - 6.0) < 0.2);
		QVERIFY(!strip(1, {{QStringLiteral("glue_ratio"), 1.0}, {QStringLiteral("glue_makeup_db"), 0.0}}).contains(QStringLiteral("error")));
		QVERIFY(std::abs(db(chainRms(1, 1000.0, 0.5f, 200)) - clean) < 0.05);
		// A quiet signal under the threshold passes as if the glue were off.
		QVERIFY(!strip(1, {{QStringLiteral("glue_ratio"), 4.0}}).contains(QStringLiteral("error")));
		QVERIFY(std::abs(db(chainRms(1, 1000.0, 0.02f, 200)) - db(chainRms(2, 1000.0, 0.02f, 200))) < 0.05);
		QVERIFY(std::abs(db(chainRms(2, 1000.0, 0.5f, 200)) - clean) < 0.05);  // channel 2's Console never glued
	}

	//! One change is one undo step; the undo restores every field it changed and the redo puts them back.
	void oneUndoStepRestoresTheSection()
	{
		if (loadConsole(1).isEmpty()) { QSKIP("this build has no console module"); }
		const QJsonObject before = strip(1);
		const int depth = undoDepth();
		QVERIFY(!strip(1, {{QStringLiteral("trim_db"), -4.0}, {QStringLiteral("eq_high_db"), 5.0}, {QStringLiteral("glue"), true},
			{QStringLiteral("glue_ratio"), 8.0}, {QStringLiteral("drive"), 55.0}}).contains(QStringLiteral("error")));
		QCOMPARE(undoDepth(), depth + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QJsonObject after = strip(1);
		after.remove(QStringLiteral("channel"));
		QJsonObject expected = before;
		expected.remove(QStringLiteral("channel"));
		QCOMPARE(after, expected);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(strip(1).value(QStringLiteral("glue_ratio")).toDouble(), 8.0);
		QCOMPARE(strip(1).value(QStringLiteral("trim_db")).toDouble(), -4.0);
	}

	//! Saved and reopened, the section comes back - the strip's values and the Console's glue.
	void aSavedProjectKeepsTheSection()
	{
		if (loadConsole(1).isEmpty()) { QSKIP("this build has no console module"); }
		QVERIFY(!strip(1, {{QStringLiteral("trim_db"), 2.0}, {QStringLiteral("polarity_right"), true}, {QStringLiteral("eq_mid_db"), -6.0},
			{QStringLiteral("eq_mid_freq"), 800.0}, {QStringLiteral("drive"), 12.0}, {QStringLiteral("glue"), true},
			{QStringLiteral("glue_threshold_db"), -25.0}, {QStringLiteral("glue_ratio"), 3.0}, {QStringLiteral("glue_makeup_db"), 4.0}})
			.contains(QStringLiteral("error")));
		const QJsonObject saved = strip(1);
		QTemporaryDir dir;
		const QString path = dir.filePath(QStringLiteral("console-strip.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), path}}).ok);
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), path}}).ok);
		QJsonObject reopened = strip(1);
		QCOMPARE(reopened, saved);
	}

	//! A bad value, an unknown channel or a Console field on a channel without a Console is refused - and the
	//! refusal writes nothing (not even the valid fields beside it) and records no undo step.
	void aRefusedCallChangesNothing()
	{
		const int depth = undoDepth();
		const QJsonObject before = strip(3);
		const auto refused = [](const QJsonObject& args) {
			QJsonObject call = args;
			if (!call.contains(QStringLiteral("channel"))) { call.insert(QStringLiteral("channel"), ch(3)); }
			return run(QStringLiteral("mixer.console_strip"), call);
		};
		QCOMPARE(refused({{QStringLiteral("trim_db"), 30.0}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(refused({{QStringLiteral("trim_db"), QStringLiteral("loud")}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(refused({{QStringLiteral("eq_mid_freq"), 20.0}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(refused({{QStringLiteral("polarity_left"), 1}}).errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(!refused({{QStringLiteral("channel"), QStringLiteral("ch-999999")}, {QStringLiteral("trim_db"), 1.0}}).ok);
		// Channel 3 has no Console: a Console field is refused, and the trim beside it is not written either.
		const ControlResult noConsole = refused({{QStringLiteral("trim_db"), 5.0}, {QStringLiteral("glue_ratio"), 4.0}});
		QCOMPARE(noConsole.errorKind, ControlErrorKind::Refused);
		QVERIFY(noConsole.errorMessage.contains(QStringLiteral("mixer.console_apply")));
		QCOMPARE(strip(3), before);
		QCOMPARE(strip(3).value(QStringLiteral("drive")), QJsonValue());  // null: no Console to report
		if (!loadConsole(3).isEmpty())
		{
			QCOMPARE(refused({{QStringLiteral("glue_ratio"), 0.5}}).errorKind, ControlErrorKind::InvalidArgs);
			QCOMPARE(refused({{QStringLiteral("glue_threshold_db"), 3.0}}).errorKind, ControlErrorKind::InvalidArgs);
			QCOMPARE(refused({{QStringLiteral("glue_makeup_db"), -1.0}}).errorKind, ControlErrorKind::InvalidArgs);
			QCOMPARE(refused({{QStringLiteral("drive"), 101.0}}).errorKind, ControlErrorKind::InvalidArgs);
		}
		QCOMPARE(channel(3)->m_trimModel.value(), 0.f);
		QCOMPARE(undoDepth(), depth + (consolestrip::consoleOf(*channel(3)) != nullptr ? 1 : 0));  // only plugin.load's step
	}

	//! A Console saved before the glue existed has no glue keys: it loads with the glue off, and sounds exactly
	//! like a fresh Console at the same settings.
	void anOlderConsoleLoadsWithTheGlueOff()
	{
		const QString one = loadConsole(1);
		if (one.isEmpty() || loadConsole(2).isEmpty()) { QSKIP("this build has no console module"); }
		QVERIFY(!strip(1, {{QStringLiteral("drive"), 60.0}, {QStringLiteral("glue"), true}, {QStringLiteral("glue_ratio"), 10.0}}).contains(QStringLiteral("error")));
		Effect* console = consolestrip::consoleOf(*channel(1));
		QDomDocument doc;
		doc.setContent(controlEffectStateXml(console));
		QDomElement controls = doc.elementsByTagName(QStringLiteral("ConsoleControls")).item(0).toElement();
		QVERIFY(!controls.isNull());
		for (const char* key : {"glue", "gluethreshold", "glueratio", "gluemakeup"})
		{
			controls.removeAttribute(QLatin1String(key));
			QDomElement node = controls.firstChildElement(QLatin1String(key));
			if (!node.isNull()) { controls.removeChild(node); }
		}
		QVERIFY(controlRestoreEffectState(console, doc.toString().toUtf8()).ok);
		const QJsonObject loaded = strip(1);
		QCOMPARE(loaded.value(QStringLiteral("glue")).toBool(), false);
		QCOMPARE(loaded.value(QStringLiteral("glue_ratio")).toDouble(), 4.0);  // the default
		QCOMPARE(loaded.value(QStringLiteral("drive")).toDouble(), 60.0);  // what the file did carry
		QVERIFY(!strip(2, {{QStringLiteral("drive"), 60.0}}).contains(QStringLiteral("error")));
		const double older = chainRms(1, 440.0, 0.7f, 20);
		const double fresh = chainRms(2, 440.0, 0.7f, 20);
		QCOMPARE(older, fresh);  // bit for bit: the glue stage is skipped while off
	}

	//! The glue stage on every test channel, at the largest period, block after block: no allocation on the
	//! processing path, a finite output, and a bounded reduction.
	void theGlueAllocatesNothingWhileProcessing()
	{
		for (int i = 1; i <= 3; ++i)
		{
			if (loadConsole(i).isEmpty()) { QSKIP("this build has no console module"); }
			QVERIFY(!strip(i, {{QStringLiteral("glue"), true}, {QStringLiteral("glue_threshold_db"), -40.0}, {QStringLiteral("glue_ratio"), 20.0},
				{QStringLiteral("glue_makeup_db"), 24.0}, {QStringLiteral("drive"), 100.0}}).contains(QStringLiteral("error")));
		}
		long at = 0;
		test::resetAllocationCount();
		for (int b = 0; b < 200; ++b)
		{
			for (int i = 1; i <= 3; ++i) { QVERIFY(squareBlockStaysBounded(i, b % 2 == 0 ? 1.f : 0.001f, &at)); }
		}
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});
	}
};

QTEST_MAIN(ConsoleStripTest)
#include "ConsoleStripTest.moc"
