/*
 * EqPeakHandoffTest.cpp - cluster new-p9-122: the audio thread and the GUI
 * timer hand peak-meter floats through EqFader without synchronization
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

/*! Four bug reports (new-p9-122, new-p15-58, new-p16-57, new-p17-52) are one
 *  root cause: EqFader - the peak-meter fader shared by the Eq, Delay and
 *  Compressor dialogs - receives raw float pointers to fields the audio
 *  thread writes (EqEffect::processImpl, DelayEffect::processImpl,
 *  CompressorEffect::processImpl) while MainWindow::periodicUpdate drives
 *  EqFader::updateVuMeters to read AND clear them (*m_lPeak = 0). Unsynchronized
 *  concurrent read/modify/write of the same plain float is a data race
 *  (undefined behaviour under the C++ memory model), and the GUI's
 *  read-then-clear can lose an audio-thread peak written between the read and
 *  the clear.
 *
 *  The fix: the peak fields become std::atomic<float> (lock-free, per the
 *  DspLoadProbe precedent), the audio thread publishes with relaxed
 *  stores/CAS-max, and the GUI timer takes the value with a single
 *  exchange(0) - read-and-clear as one atomic step.
 *
 *  A concurrent plain-float race has no deterministic failure without a race
 *  detector, so each slot fails deterministically on the original code through
 *  what can be checked without racing: the declared type of the handoff
 *  fields, and - for the widget and the audio-side writes - the access
 *  pattern in the sources themselves (the cluster-new-p8-266 /
 *  cluster-new-p9-370 pattern). The template below additionally exercises the
 *  exchange handoff on a real constructed plugin's fields; it is written so
 *  the test compiles against both the plain-float and the atomic version and
 *  fails at runtime on the plain-float one. */

#include <atomic>
#include <type_traits>

#include <QtTest>

#include "DelayControls.h"
#include "DelayEffect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "EqControls.h"

using namespace lmms;

namespace
{

QByteArray readSource(const QString& relativePath)
{
	QFile file{QCoreApplication::applicationDirPath() + QStringLiteral("/") + relativePath};
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { return {}; }
	return file.readAll();
}

//! The UI-side read the handoff must use: take the peak and leave zero in one
//! atomic step, so a store between two GUI ticks is neither lost nor torn.
//! Instantiated only for std::atomic<float>; for a plain float the else
//! branch fails the test at runtime while the file still compiles.
template<typename Peak>
void verifyExchangeHandoff(Peak& peak, const char* what)
{
	if constexpr (std::is_same_v<std::decay_t<Peak>, std::atomic<float>>)
	{
		peak.store(0.25f, std::memory_order_relaxed);
		peak.store(0.5f, std::memory_order_relaxed);
		QCOMPARE(peak.exchange(0.0f, std::memory_order_relaxed), 0.5f);
		QVERIFY(peak.exchange(0.0f, std::memory_order_relaxed) == 0.0f);
		peak.store(0.125f, std::memory_order_relaxed);
		QCOMPARE(peak.exchange(0.0f, std::memory_order_relaxed), 0.125f);
	}
	else
	{
		Q_UNUSED(peak);
		QFAIL(qPrintable(QStringLiteral("%1: handoff field is a plain float, not std::atomic<float>; "
			"the audio thread and the GUI timer race on it").arg(QLatin1String{what})));
	}
}

} // namespace

class EqPeakHandoffTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! new-p9-122: EqFader::updateVuMeters (plugins/Eq/EqFader.h:71/81) read
	//! *m_lPeak and then wrote *m_lPeak = 0 while the audio thread concurrently
	//! wrote the same float. The widget must take atomic pointers and read with
	//! a single exchange that clears the field in the same atomic step.
	void newP9_122_eqFaderReadsPeaksWithAtomicExchange()
	{
		const auto fader = readSource(QStringLiteral("../../plugins/Eq/EqFader.h"));
		QVERIFY(!fader.isEmpty());
		// The constructor hands the widget the plugin's own peak fields.
		QVERIFY(fader.contains("std::atomic<float>* lPeak"));
		QVERIFY(fader.contains("std::atomic<float>* rPeak"));
		QVERIFY(!fader.contains("float* lPeak"));
		// The members point at atomics.
		QVERIFY(fader.contains("std::atomic<float>* m_lPeak;"));
		QVERIFY(fader.contains("std::atomic<float>* m_rPeak;"));
		// Read-and-clear is one exchange, not a read followed by a plain store
		// (the plain store raced with the audio thread and lost peaks).
		QVERIFY(fader.contains("m_lPeak->exchange(0.0f, std::memory_order_relaxed)"));
		QVERIFY(fader.contains("m_rPeak->exchange(0.0f, std::memory_order_relaxed)"));
		QVERIFY(!fader.contains("*m_lPeak = 0"));
		QVERIFY(!fader.contains("*m_rPeak = 0"));
	}

	//! new-p15-58: EqControls.h:65 declared m_inPeakL..m_highShelfPeakR as
	//! plain floats - the fields EqFader reads on the GUI timer while
	//! EqEffect::processImpl writes them on the audio thread. All fourteen
	//! fields must be std::atomic<float> and lock-free.
	void newP15_58_eqControlsPeakFieldsAreAtomic()
	{
		using C = EqControls;
		QVERIFY((std::is_same_v<decltype(C::m_inPeakL), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_inPeakR), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_outPeakL), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_outPeakR), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_lowShelfPeakL), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_lowShelfPeakR), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_para1PeakL), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_para1PeakR), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_para2PeakL), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_para2PeakR), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_para3PeakL), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_para3PeakR), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_para4PeakL), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_para4PeakR), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_highShelfPeakL), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(C::m_highShelfPeakR), std::atomic<float>>));
		QVERIFY2(std::atomic<float>::is_always_lock_free,
			"peak meters are written on the audio thread and must be lock-free");
	}

	//! new-p16-57: DelayEffect.cpp:138-139 wrote m_delayControls.m_outPeakL/R
	//! (plain floats, per DelayControls.h:58-59) on the audio thread while the
	//! Delay dialog's EqFaders read them on the GUI timer. The sibling
	//! CompressorEffect (Compressor.cpp:561-564) shares the EqFader widget and
	//! the same defect, so the sibling rule brings its four fields and writes
	//! into this slot too.
	void newP16_57_delayAndCompressorPeaksAreAtomic()
	{
		QVERIFY((std::is_same_v<decltype(DelayControls::m_outPeakL), std::atomic<float>>));
		QVERIFY((std::is_same_v<decltype(DelayControls::m_outPeakR), std::atomic<float>>));
		// CompressorControls' peak fields are private (the dialog is a friend),
		// so their type is pinned in the header the way new-p9-122 pins EqFader.
		const auto compressorControls = readSource(QStringLiteral("../../plugins/Compressor/CompressorControls.h"));
		QVERIFY(!compressorControls.isEmpty());
		QVERIFY(compressorControls.contains("std::atomic<float> m_inPeakL;"));
		QVERIFY(compressorControls.contains("std::atomic<float> m_inPeakR;"));
		QVERIFY(compressorControls.contains("std::atomic<float> m_outPeakL;"));
		QVERIFY(compressorControls.contains("std::atomic<float> m_outPeakR;"));
		QVERIFY(!compressorControls.contains("float m_outPeakL;"));

		// The audio-side write pattern on a real plugin's fields: publish with a
		// relaxed store, take with the atomic exchange (the GUI's read).
		EffectChain chain{nullptr};
		auto* effect = new DelayEffect(&chain, nullptr);
		auto* delayControls = static_cast<DelayControls*>(effect->controls());
		verifyExchangeHandoff(delayControls->m_outPeakL, "DelayControls::m_outPeakL");
		verifyExchangeHandoff(delayControls->m_outPeakR, "DelayControls::m_outPeakR");
		delete effect;

		// Audio-thread stores are relaxed, not plain assignment (which would
		// silently become seq_cst RMW and hide the intent).
		const auto delaySource = readSource(QStringLiteral("../../plugins/Delay/DelayEffect.cpp"));
		QVERIFY(!delaySource.isEmpty());
		QVERIFY(delaySource.contains(
			"m_delayControls.m_outPeakL.store(peak.left(), std::memory_order_relaxed)"));
		QVERIFY(delaySource.contains(
			"m_delayControls.m_outPeakR.store(peak.right(), std::memory_order_relaxed)"));

		const auto compressorSource = readSource(QStringLiteral("../../plugins/Compressor/Compressor.cpp"));
		QVERIFY(!compressorSource.isEmpty());
		QVERIFY(compressorSource.contains(
			"m_compressorControls.m_outPeakL.store(lOutPeak, std::memory_order_relaxed)"));
		QVERIFY(compressorSource.contains(
			"m_compressorControls.m_inPeakL.store(lInPeak, std::memory_order_relaxed)"));
		QVERIFY(!compressorSource.contains("m_compressorControls.m_outPeakL = lOutPeak"));
	}

	//! new-p17-52: EqEffect.cpp:180-181 and 277-278 updated the input/output
	//! peak floats with a plain read-modify-write (m = m < v ? v : m) on the
	//! audio thread while the UI timer read and cleared them; the band peaks
	//! (EqEffect.cpp:330-347) were plain chained assignments. The in/out
	//! max-hold must be a lock-free CAS raise and the band peaks relaxed stores.
	void newP17_52_eqEffectWritesPeaksThroughAtomics()
	{
		const auto effectSource = readSource(QStringLiteral("../../plugins/Eq/EqEffect.cpp"));
		QVERIFY(!effectSource.isEmpty());
		// The in/out max-hold is the raisePeak CAS helper, not the old racy
		// load-compare-store on the plain float.
		QVERIFY(effectSource.contains("raisePeak(m_eqControls.m_inPeakL, m_inPeak[0])"));
		QVERIFY(effectSource.contains("raisePeak(m_eqControls.m_inPeakR, m_inPeak[1])"));
		QVERIFY(effectSource.contains("raisePeak(m_eqControls.m_outPeakL, outPeak[0])"));
		QVERIFY(effectSource.contains("raisePeak(m_eqControls.m_outPeakR, outPeak[1])"));
		QVERIFY(effectSource.contains("compare_exchange_weak"));
		QVERIFY(!effectSource.contains("m_eqControls.m_inPeakL = m_eqControls.m_inPeakL"));
		QVERIFY(!effectSource.contains("m_eqControls.m_outPeakL = m_eqControls.m_outPeakL"));
		// The band peaks are relaxed stores, not chained plain assignments.
		QVERIFY(effectSource.contains("m_lowShelfPeakL.store(lowShelf, std::memory_order_relaxed)"));
		QVERIFY(effectSource.contains("m_lowShelfPeakR.store(lowShelf, std::memory_order_relaxed)"));
		QVERIFY(!effectSource.contains("m_lowShelfPeakL = m_lowShelfPeakR"));
		QVERIFY(!effectSource.contains("m_para1PeakL = m_para1PeakR"));
		QVERIFY(!effectSource.contains("m_highShelfPeakL = m_highShelfPeakR"));
	}
};

QTEST_MAIN(EqPeakHandoffTest)

#include "EqPeakHandoffTest.moc"
