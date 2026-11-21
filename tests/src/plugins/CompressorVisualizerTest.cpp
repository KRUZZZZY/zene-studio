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
};

QTEST_APPLESS_MAIN(CompressorVisualizerTest)

#include "CompressorVisualizerTest.moc"
