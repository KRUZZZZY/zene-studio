/*
 * CompressorVisualizerTest.cpp - the compressor's visualizer state is published, not shared
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

//! Cluster new-p15-300: the compressor's control dialog and its audio thread used the same
//! plain-float fields. The dialog's timer sampled the peak and the gain hold and reset them,
//! while the audio thread raised them to the block's peak and gain - two threads writing the
//! same object, a data race (undefined behaviour), and a graph that could show values from
//! two different blocks at once.
//!
//! The fix publishes the state instead of sharing it: the holds the two threads touch are
//! atomic, and what the dialog resets them to is published by the audio thread rather than
//! read out of its own working state (m_yL, m_gainResult).
//!
//! The plugin is built as a module, so its header is read here directly. The access
//! specifiers are opened for this translation unit only; they affect neither the layout nor
//! the name mangling, so the plugin's own translation units and the link are unaffected (the
//! SlicerTZoomTest precedent).
//!
//! The assertions are on types, not on timing: the test needs no clock, no sleep and no
//! thread, so it says the same thing on every run.
//!
//! Cluster new-p9-19 (the same compressor header): KeyFilterMailbox::publish() retried
//! `while (m_dirty)` and a writer could be pushed around that loop for as long as other
//! threads kept moving the key-filter knobs. Automation calls calcKeyFilter() on the audio
//! thread, so that was an unbounded amount of work in the audio callback. The regression
//! below forces the same interleaving from inside make() - a nested publish() is exactly a
//! writer that finds the version odd and leaves - and counts the designs the outer publish()
//! runs. It is deterministic and thread-free.

#include <QtTest>

#include <atomic>
#include <type_traits>

// The compressor's header pulls in the control dialog's and Qt's headers; they are included
// first so that the access-specifier macro below does not reach them.
#include "AudioPlugin.h"
#include "CompressorControls.h"
#include "CompressorKeyFilter.h"

#define private public
#include "Compressor.h"
#undef private

namespace
{

//! Whether the effect publishes the values the control dialog resets the visualizer's holds
//! to. Before the fix the dialog read the audio thread's own working state (m_yL and
//! m_gainResult) while the audio thread wrote it - the race. The detection is written so it
//! compiles against the old header as well: the member does not exist there, so the
//! substitution fails and the trait is false.
template <typename T, typename = void>
struct PublishesTheResetValues : std::false_type
{
};

template <typename T>
struct PublishesTheResetValues<T, std::void_t<
	decltype(std::declval<T&>().m_publishedEnvelope),
	decltype(std::declval<T&>().m_publishedGain)>> : std::true_type
{
};

//! The type of one element of the visualizer's holds.
template <typename T>
using HoldElement = std::remove_cvref_t<decltype(std::declval<T&>().m_displayPeak[0])>;

constexpr float kKeyFilterSampleRate = 48000.f;

//! A nested publish() from inside make(): another thread moving a knob while this one holds
//! the version. It marks the design dirty and returns without designing, exactly as a real
//! writer that finds the version odd does.
void markKeyFilterDesignDirty(lmms::KeyFilterMailbox& mailbox)
{
	mailbox.publish([] { return lmms::KeyFilterDesign{}; });
}

} // namespace

class CompressorVisualizerTest : public QObject
{
	Q_OBJECT

private slots:
	//! The peak hold and the gain hold are written by the audio thread (it raises them to the
	//! block's peak and gain) and by the control dialog (it samples them and resets them).
	//! Plain floats make that a data race; atomic elements make it two well-defined stores.
	void theVisualizerHoldsAreAtomic()
	{
		QVERIFY2((std::is_same_v<HoldElement<lmms::CompressorEffect>, std::atomic<float>>),
			"m_displayPeak is written by the audio thread and by the control dialog: it must be atomic");
		QVERIFY2((std::is_same_v<std::remove_cvref_t<decltype(std::declval<lmms::CompressorEffect&>().m_displayGain[0])>,
					  std::atomic<float>>),
			"m_displayGain is written by the audio thread and by the control dialog: it must be atomic");
	}

	//! What the dialog resets the holds to comes from the audio thread's published values, not
	//! from the working state the audio thread writes while it processes.
	void theResetValuesArePublished()
	{
		QVERIFY2(PublishesTheResetValues<lmms::CompressorEffect>::value,
			"the control dialog must reset the holds to values the audio thread published, "
			"not to the audio thread's own working state");
	}

	//! Cluster new-p9-19: publish() must run a bounded number of designs while other
	//! publishers keep marking the design dirty. The old `while (m_dirty)` loop ran one
	//! design per mark (eight here); the audio thread runs calcKeyFilter() when automation
	//! moves the knobs, so that loop is audio-callback time with no bound.
	void theKeyFilterPublishIsBounded()
	{
		lmms::KeyFilterMailbox mailbox;
		const lmms::KeyFilterDesign design =
			lmms::designKeyFilter(200.f, 6000.f, kKeyFilterSampleRate);
		QVERIFY(design.hpOn);
		QVERIFY(design.lpOn);

		// Eight writers arrive, one during each design.
		int designsRun = 0;
		int writersLeft = 8;
		mailbox.publish([&]
		{
			++designsRun;
			if (writersLeft > 0)
			{
				--writersLeft;
				markKeyFilterDesignDirty(mailbox);
			}
			return design;
		});

		QVERIFY2(designsRun <= 2,
			qPrintable(QStringLiteral("publish() ran %1 designs while writers kept arriving; "
				"the audio thread is unbounded").arg(designsRun)));

		// The bound did not corrupt the hand-off: the published design is complete, and a
		// design left pending is finished by the next publish from the current knob values
		// (processImpl does that at the top of a later block).
		lmms::KeyFilterDesign taken{};
		QVERIFY(mailbox.take(taken));
		QCOMPARE(taken.hp.b0, design.hp.b0);
		QCOMPARE(taken.hp.a1, design.hp.a1);
		QCOMPARE(taken.lp.b0, design.lp.b0);

		// The latest knob values are not lost: the next block designs from them (processImpl
		// calls calcKeyFilter() when the mailbox still has a design pending).
		const lmms::KeyFilterDesign latest =
			lmms::designKeyFilter(800.f, 4000.f, kKeyFilterSampleRate);
		mailbox.publish([&] { return latest; });
		QVERIFY(mailbox.take(taken));
		QCOMPARE(taken.hp.b0, latest.hp.b0);
		QCOMPARE(taken.lp.b0, latest.lp.b0);
	}
};

QTEST_APPLESS_MAIN(CompressorVisualizerTest)

#include "CompressorVisualizerTest.moc"
