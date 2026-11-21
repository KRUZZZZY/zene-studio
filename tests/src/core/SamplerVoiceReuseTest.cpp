/*
 * SamplerVoiceReuseTest.cpp - Q050-103: the sampler's voice limit and released-voice reuse policy
 *
 * At the voice limit a released note's voice is taken before a held one, among released voices the one released
 * first goes first, with none released the oldest held voice goes; the new note starts fresh and a reuse does not
 * click; a note start and a reuse allocate nothing.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include "SamplerTestSupport.h"

using namespace lmms;
using namespace lmms::sampler_test;

namespace
{
thread_local bool countAllocations = false;
thread_local std::uint64_t allocationCount = 0;
} // namespace

// Counting allocator: any allocation inside a counted region is a realtime violation.
void* operator new(std::size_t size)
{
	if (countAllocations) { ++allocationCount; }
	if (void* memory = std::malloc(size != 0 ? size : 1)) { return memory; }
	throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

class SamplerVoiceReuseTest : public SamplerFixture
{
	Q_OBJECT

private slots:
	void initTestCase() { initEngine(); }
	void cleanupTestCase() { cleanupEngine(); }

	//! At the voice limit a RELEASED voice is taken before a held one - even when the held voice is older.
	void releasedVoiceIsReusedBeforeAHeldOne()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Voice limit"), 2.f);
		setValue(QStringLiteral("Release"), 200.f); // a long tail: the released note would sound for ~34 periods
		Played held = start(69);       // oldest, held
		renderPeriods({&held}, 1);
		Played released = start(71);   // newer, then released
		renderPeriods({&held, &released}, 1);
		released.nph->noteOff(0);
		renderPeriods({&held, &released}, 2);
		QCOMPARE(voices(), 2);
		QVERIFY2(peakOf(released.left, released.left.size() - static_cast<std::size_t>(m_fpp), released.left.size()) > 0.05f,
			"the released note is still sounding its tail");

		Played incoming = start(73);
		renderPeriods({&held, &released, &incoming}, 3);

		// The released voice was reused: its tail was faded out within a few ms and is silent while the held note goes on.
		const std::size_t last = released.left.size();
		QVERIFY2(peakOf(released.left, last - static_cast<std::size_t>(m_fpp), last) < 1e-5f,
			"the released note's voice was taken: it is silent although its release tail is far from over");
		QVERIFY2(peakOf(held.left, held.left.size() - static_cast<std::size_t>(m_fpp), held.left.size()) > 0.4f,
			"the older held note was not touched");
		QVERIFY2(peakOf(incoming.left, 0, incoming.left.size()) > 0.4f, "the new note sounds");
		QCOMPARE(voices(), 2);

		// The reuse did not click: the stolen note's level falls to zero over a few ms, not in one frame.
		const std::size_t from = released.left.size() - 3 * static_cast<std::size_t>(m_fpp);
		float biggestStep = 0.f;
		for (std::size_t i = from + 1; i < released.left.size(); ++i)
		{
			biggestStep = std::max(biggestStep, std::abs(released.left[i] - released.left[i - 1]));
		}
		QVERIFY2(biggestStep < 0.05f, qPrintable(QStringLiteral("largest frame-to-frame step %1").arg(biggestStep)));
		for (Played* p : {&held, &released, &incoming}) { NotePlayHandleManager::release(p->nph); }
	}

	//! With no released voice the OLDEST held voice is the one reused.
	void oldestHeldVoiceIsReusedWhenNoneIsReleased()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Voice limit"), 2.f);
		Played oldest = start(69);
		renderPeriods({&oldest}, 1);
		Played newer = start(71);
		renderPeriods({&oldest, &newer}, 2);
		Played incoming = start(73);
		renderPeriods({&oldest, &newer, &incoming}, 3);

		const auto lastPeriodPeak = [&](const Played& p) { return peakOf(p.left, p.left.size() - static_cast<std::size_t>(m_fpp), p.left.size()); };
		QVERIFY2(lastPeriodPeak(oldest) < 1e-5f, "the oldest held note was taken");
		QVERIFY2(lastPeriodPeak(newer) > 0.4f, "the newer held note continues");
		QVERIFY2(lastPeriodPeak(incoming) > 0.4f, "the new note sounds");
		for (Played* p : {&oldest, &newer, &incoming}) { NotePlayHandleManager::release(p->nph); }
	}

	//! The order among released voices is fixed: the one released longest ago goes first.
	void releasedVoicesAreTakenInReleaseOrder()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Voice limit"), 3.f);
		setValue(QStringLiteral("Release"), 400.f);
		Played a = start(69);
		Played b = start(71);
		Played c = start(73);
		renderPeriods({&a, &b, &c}, 1);
		// b is released first, then a: b has been released longer, although a started earlier.
		b.nph->noteOff(0);
		renderPeriods({&a, &b, &c}, 1);
		a.nph->noteOff(0);
		renderPeriods({&a, &b, &c}, 1);

		Played d = start(75);
		renderPeriods({&a, &b, &c, &d}, 3);
		const auto lastPeriodPeak = [&](const Played& p) { return peakOf(p.left, p.left.size() - static_cast<std::size_t>(m_fpp), p.left.size()); };
		QVERIFY2(lastPeriodPeak(b) < 1e-5f, "b was released first, so b is reused first");
		QVERIFY2(lastPeriodPeak(a) > 0.05f, "a is still in its release tail");
		QVERIFY2(lastPeriodPeak(c) > 0.4f, "the held note is untouched");
		for (Played* p : {&a, &b, &c, &d}) { NotePlayHandleManager::release(p->nph); }
	}

	//! A note that takes over a voice starts from the beginning of the sample with its own attack, whatever
	//! the previous owner had played.
	void reusedVoiceStartsFresh()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("ramp.wav")), true));
		setValue(QStringLiteral("Voice limit"), 1.f);
		setValue(QStringLiteral("Attack"), 5.f);
		Played first = start(69);
		renderPeriods({&first}, 12); // well into the sample: the ramp is now far above its start
		const float firstLevel = first.left.back();
		QVERIFY(firstLevel > 0.01f);
		Played second = start(69);
		renderPeriods({&first, &second}, 1);
		QVERIFY2(std::abs(second.left[0]) < 1e-4f, "the new note's envelope starts at zero");
		// Sample start is value 0 rising to 0.9 over two seconds: after a period it is still tiny, far below
		// where the first note's playback was.
		QVERIFY2(second.left.back() < firstLevel * 0.2f,
			qPrintable(QStringLiteral("new note level %1 vs old note %2").arg(second.left.back()).arg(firstLevel)));
		NotePlayHandleManager::release(first.nph);
		NotePlayHandleManager::release(second.nph);
	}

	//! Held notes beyond the limit are not silently dropped: every note sounds when it starts.
	void everyNoteSoundsWhenItStarts()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Voice limit"), 3.f);
		std::vector<std::unique_ptr<Played>> notes;
		for (int i = 0; i < 12; ++i)
		{
			notes.push_back(std::make_unique<Played>(start(60 + i)));
			renderPeriod(*notes.back());
			QVERIFY2(peakOf(notes.back()->left, 0, notes.back()->left.size()) > 0.4f,
				qPrintable(QStringLiteral("note %1 sounds on its first period").arg(i)));
			// Older notes keep rendering (and fade out when reused).
			for (std::size_t j = 0; j + 1 < notes.size(); ++j) { renderPeriod(*notes[j]); }
		}
		// Only the limit plus the voices still fading are in use.
		QVERIFY(voices() <= 3 + 1);
		for (auto& p : notes) { NotePlayHandleManager::release(p->nph); }
		QCOMPARE(voices(), 0);
	}

	//! Starting a note, reusing a voice and releasing a note allocate nothing.
	void noteStartAndVoiceReuseDoNotAllocate()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Voice limit"), 2.f);
		setValue(QStringLiteral("Attack"), 4.f);
		std::vector<SampleFrame> buffer(static_cast<std::size_t>(m_fpp));
		std::vector<NotePlayHandle*> handles;
		for (int i = 0; i < 6; ++i) { handles.push_back(NotePlayHandleManager::acquire(m_track, 0, m_fpp * 100, Note(TimePos(384), TimePos(0), 60 + i))); }

		allocationCount = 0;
		countAllocations = true;
		for (std::size_t i = 0; i < handles.size(); ++i)
		{
			// Starts note i (past the limit from the third on, so it reuses a voice), then renders the earlier ones.
			for (std::size_t j = 0; j <= i; ++j) { m_instrument->playNote(handles[j], std::span<SampleFrame>{buffer}); }
			if (i == 2) { handles[0]->noteOff(0); }
		}
		countAllocations = false;
		const std::uint64_t allocations = allocationCount;
		for (auto* h : handles) { NotePlayHandleManager::release(h); }
		QCOMPARE(static_cast<qulonglong>(allocations), 0ull);
	}
};

QTEST_MAIN(SamplerVoiceReuseTest)
#include "SamplerVoiceReuseTest.moc"
