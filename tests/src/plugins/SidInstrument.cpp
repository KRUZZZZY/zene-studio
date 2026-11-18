/*
 * SidInstrument.cpp - cluster new-p10-380-port: a short sid_fillbuffer fill must
 *                      leave the un-written tail of the stack buffer silent
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
/*! cluster new-p10-380-port (BUG 8): SidInstrument::playNoteImpl discarded
 *  sid_fillbuffer's produced count and converted every entry of the
 *  un-initialised stack VLA `short buf[frames]`.  The emulator only writes the
 *  frames its cycle budget covers (clockrate * frames / samplerate cycles), so
 *  when the output rate rises under a living note (export at 96 kHz while the
 *  note's SID was created at 44.1 kHz - ProjectRenderer changes the device rate
 *  and Song::stop()'s early return keeps the live note handle) the tail of the
 *  buffer is never written and stale stack bytes become audio.
 *  - the short fill itself is pinned by the 44.1 -> 96 kHz step below;
 *  - the un-written tail must be exactly silence: the caller's stack is poisoned
 *    first, so on the unfixed code the tail reads 0xA5A5 (-0.70598 as a sample)
 *    and the test goes red; the product's zero-fill makes it green. */

#include <QtTest>

#include <cstddef>
#include <span>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

#if defined(_MSC_VER)
#define SID_TEST_NOINLINE __declspec(noinline)
#else
#define SID_TEST_NOINLINE __attribute__((noinline))
#endif

//! Stale bytes must read non-zero when the short fill runs, or the bug would
//! convert zero and pass by accident.  Written and returned immediately before
//! the call under test: SidInstrument::playNoteImpl's frame (the VLA first of
//! all) reuses this thread's stack region, which this noinline caller - rooted
//! at the same stack position as the playNote call - has filled with 0xA5.
SID_TEST_NOINLINE void poisonStack()
{
	volatile unsigned char scratch[128 * 1024];
	for (std::size_t i = 0; i < sizeof(scratch); ++i) { scratch[i] = 0xA5; }
	if (scratch[sizeof(scratch) - 1] != 0xA5) { qFatal("stack poison did not run"); }
}

} // namespace

namespace lmms
{

class SidInstrumentTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		// PluginFactory reads LMMS_PLUGIN_DIR when it is first instantiated: point
		// it at the build's modules first (NotePlaybackTest's pattern).
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		Engine::getSong()->clearProject();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A rate rise under a living note short-fills the SID buffer; the frames the
	//! emulator had no cycles for must convert as silence, not as stack garbage.
	void rateRiseShortFillLeavesTailSilent()
	{
		auto* device = Engine::audioEngine()->audioDev();
		QVERIFY(device != nullptr);
		const auto originalRate = device->sampleRate();
		struct RestoreSampleRate
		{
			AudioDevice* device;
			sample_rate_t rate;
			~RestoreSampleRate() { device->setSampleRateForTesting(rate); }
		} restoreRate{device, originalRate};

		// The note is born and its SID fixed to the live rate...
		device->setSampleRateForTesting(44100);

		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		Instrument* instrument = track->loadInstrument(QStringLiteral("sid"));
		QVERIFY(instrument != nullptr);
		QCOMPARE(instrument->nodeName(), QStringLiteral("sid"));

		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY2(frames >= 64, "the period must hold a produced head plus a checkable tail");
		std::vector<SampleFrame> first(static_cast<std::size_t>(frames));

		const Note note(TimePos(384), TimePos(0), 60);
		NotePlayHandle* nph = NotePlayHandleManager::acquire(track, 0, frames * 8, note);
		instrument->playNote(nph, std::span<SampleFrame>{first});

		// ...then the device rate rises under the SAME living note: the SID's
		// cycles-per-sample floor stays fixed at creation while the period's cycle
		// budget (clockrate * frames / samplerate) shrinks, so sid_fillbuffer
		// returns fewer than `frames` samples for this period.
		device->setSampleRateForTesting(96000);
		QCOMPARE(device->sampleRate(), 96000);

		std::vector<SampleFrame> second(static_cast<std::size_t>(frames));
		auto secondOut = std::span<SampleFrame>{second};
		poisonStack();
		instrument->playNote(nph, secondOut);

		// Produced at 96 kHz from a SID created at 44.1 kHz: the budget is
		// 985248 * frames / 96000 (+4) cycles against ~22.34 cycles per sample, so
		// about 46% of the period is filled - the last 32 frames are always in the
		// un-written tail and must be exact zeros.
		const auto head = static_cast<std::size_t>(frames);
		for (std::size_t frame = head - 32; frame < head; ++frame)
		{
			for (ch_cnt_t ch = 0; ch < DEFAULT_CHANNELS; ++ch)
			{
				QVERIFY2(second[frame][ch] == 0.0f,
					qPrintable(QString{"un-written tail frame %1 channel %2 is %3, not silence"}
						.arg(frame).arg(ch).arg(second[frame][ch])));
			}
		}

		NotePlayHandleManager::release(nph);
		Engine::getSong()->clearProject();
	}
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::SidInstrumentTest)

#include "SidInstrument.moc"
