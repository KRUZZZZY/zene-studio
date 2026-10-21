/*
 * TunerTapTest.cpp - the tuner tap changes source in one generation
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

/*! The tuner tap publishes the chosen source and the ring reset as ONE generation (TunerTap.h). A push of
 *  the new source that lands on that instant takes the generation change first and resets the ring, so a
 *  reader never gets a window made of the old source's frames and the new source's frames. The test drives
 *  that push through the LMMS_TESTING seam, synchronously on this thread: no timing, sleep or thread race. */

#include <QtTest>

#include <algorithm>
#include <vector>

#include "SampleFrame.h"
#include "TunerTap.h"

using namespace lmms;

namespace
{

TunerTap& tap() { return TunerTap::instance(); }

std::vector<SampleFrame> framesOf(float value, std::size_t count)
{
	return std::vector<SampleFrame>(count, SampleFrame(value, value));
}

//! What a push made at the instant the new source is published sees.
struct ChangeProbe
{
	bool read = false;
	bool mixed = false;
	std::vector<float> window;
};

//! The audio thread's push landing in the change: the old source's frames are still in the ring.
void pushWhileTheSourceIsPublished(void* data)
{
	auto& probe = *static_cast<ChangeProbe*>(data);
	const auto fresh = framesOf(2.f, 64);
	tap().push(TunerTap::Source::Input, fresh.data(), static_cast<f_cnt_t>(fresh.size()));
	probe.window.assign(128, 0.f);
	probe.read = tap().latest(probe.window.data(), probe.window.size());
	probe.mixed = probe.read && std::any_of(probe.window.begin(), probe.window.end(),
		[](float value) { return value != 2.f; });
}

} // namespace

class TunerTapTest : public QObject
{
	Q_OBJECT

private slots:
	void init() { tap().setSource(TunerTap::Source::Off); }

	//! Switching sources publishes the source and the ring reset as one generation: a push that lands on
	//! that instant resets the old source's frames before it writes, so a reader never sees a window with
	//! frames from both sources (and, until the producer takes the reset, reports nothing).
	void aSourceChangeCannotMixTheOldSourcesFrames()
	{
		tap().setSource(TunerTap::Source::Master);
		const auto master = framesOf(1.f, 64);
		tap().push(TunerTap::Source::Master, master.data(), static_cast<f_cnt_t>(master.size()));

		ChangeProbe probe;
		tap().setSourceWithTestHook(TunerTap::Source::Input, &pushWhileTheSourceIsPublished, &probe);
		QVERIFY2(!probe.read || !probe.mixed,
			"a window read at the source change mixed the old source's frames with the new one's");

		// The tap still reads normally once the change has settled: only the new source's frames.
		const auto input = framesOf(3.f, 128);
		tap().push(TunerTap::Source::Input, input.data(), static_cast<f_cnt_t>(input.size()));
		std::vector<float> window(128);
		QVERIFY(tap().latest(window.data(), window.size()));
		for (const float value : window) { QCOMPARE(value, 3.f); }
	}

	//! A reset requested without a source change is the same one-generation rule.
	void aResetBeforeAPushReportsNothing()
	{
		tap().setSource(TunerTap::Source::Input);
		const auto frames = framesOf(1.f, 32);
		tap().push(TunerTap::Source::Input, frames.data(), static_cast<f_cnt_t>(frames.size()));
		std::vector<float> window(32);
		QVERIFY(tap().latest(window.data(), window.size()));

		tap().reset();
		QVERIFY(!tap().latest(window.data(), window.size()));  // the reset has not been taken yet
		tap().push(TunerTap::Source::Input, frames.data(), static_cast<f_cnt_t>(frames.size()));
		QVERIFY(tap().latest(window.data(), window.size()));
		for (const float value : window) { QCOMPARE(value, 1.f); }
	}
};

QTEST_GUILESS_MAIN(TunerTapTest)
#include "TunerTapTest.moc"
