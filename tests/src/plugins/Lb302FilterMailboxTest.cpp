/*
 * Lb302FilterMailboxTest.cpp - the LB302 filter settings must cross from the
 * control thread to the audio thread through a published snapshot.
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

/*
 * The test is written against a small, self-contained mailbox:
 *
 *   struct Lb302FilterSettings { float cutoff, reso, envmod, envdecay, dist;
 *                                bool db24; };
 *   class Lb302FilterMailbox {
 *       template <typename Make> void publish(Make make);
 *       bool take(Lb302FilterSettings& out);
 *   };
 *
 * take() returns true exactly once per new publication; a publication in
 * progress (odd version) makes take() return false and leaves the audio-side
 * copy untouched. No thread, sleep or timing is used: the interleaving is
 * forced by calling take() from the make() hook that runs inside publish().
 */

#include <QtTest>

#include <atomic>

#include <QString>

#if __has_include("Lb302FilterMailbox.h")
#include "Lb302FilterMailbox.h"
#define LB302_HAS_FILTER_MAILBOX 1
#else
#define LB302_HAS_FILTER_MAILBOX 0
#endif

namespace
{

#if LB302_HAS_FILTER_MAILBOX

// Each settings field is a lock-free atomic word published to the audio thread
// under a seqlock version, so the payload it is built from must itself be
// lock-free.
static_assert(std::atomic<float>::is_always_lock_free,
	"the LB302 filter settings payload must be lock-free");

constexpr float CutoffA = 0.4f;
constexpr float CutoffC = 0.2f;

#endif

} // namespace

class Lb302FilterMailboxTest : public QObject
{
	Q_OBJECT

private slots:
	void lb302PublishesFilterSettingsToTheAudioThread();
};

void Lb302FilterMailboxTest::lb302PublishesFilterSettingsToTheAudioThread()
{
#if LB302_HAS_FILTER_MAILBOX
	using namespace lmms;

	Lb302FilterMailbox mailbox;

	// A full snapshot is published and taken exactly once.
	Lb302FilterSettings first{0.4f, 0.5f, 0.6f, 0.7f, 0.8f, true};
	mailbox.publish([&] { return first; });

	Lb302FilterSettings taken{};
	QVERIFY(mailbox.take(taken));
	QCOMPARE(taken.cutoff, first.cutoff);
	QCOMPARE(taken.reso, first.reso);
	QCOMPARE(taken.envmod, first.envmod);
	QCOMPARE(taken.envdecay, first.envdecay);
	QCOMPARE(taken.dist, first.dist);
	QCOMPARE(taken.db24, first.db24);

	// A second take without a new publication is a no-op.
	Lb302FilterSettings untouched = taken;
	QVERIFY(!mailbox.take(untouched));
	QCOMPARE(untouched.cutoff, taken.cutoff);
	QCOMPARE(untouched.reso, taken.reso);
	QCOMPARE(untouched.envmod, taken.envmod);
	QCOMPARE(untouched.envdecay, taken.envdecay);
	QCOMPARE(untouched.dist, taken.dist);
	QCOMPARE(untouched.db24, taken.db24);

	// Force the mid-publication interleaving: make() runs inside publish()
	// while the version is odd, so a take() from there must fail. The
	// audio-side copy therefore keeps the previous snapshot.
	Lb302FilterSettings audioSide = taken;
	const Lb302FilterSettings complete{0.1f, 0.2f, 0.3f, 0.4f, 0.5f, false};
	bool tookDuringPublication = true;
	mailbox.publish([&mailbox, &tookDuringPublication, &audioSide, &complete]()
	{
		Lb302FilterSettings candidate{};
		tookDuringPublication = mailbox.take(candidate);
		if (tookDuringPublication) { audioSide = candidate; }
		return complete;
	});

	QVERIFY2(!tookDuringPublication,
		"take() returned a snapshot while a publication was in progress");
	QCOMPARE(audioSide.cutoff, taken.cutoff);
	QCOMPARE(audioSide.reso, taken.reso);
	QCOMPARE(audioSide.envmod, taken.envmod);
	QCOMPARE(audioSide.envdecay, taken.envdecay);
	QCOMPARE(audioSide.dist, taken.dist);
	QCOMPARE(audioSide.db24, taken.db24);

	// The interrupted publication is complete afterwards and is taken.
	Lb302FilterSettings afterPublication{};
	QVERIFY(mailbox.take(afterPublication));
	QCOMPARE(afterPublication.cutoff, complete.cutoff);
	QCOMPARE(afterPublication.reso, complete.reso);
	QCOMPARE(afterPublication.envmod, complete.envmod);
	QCOMPARE(afterPublication.envdecay, complete.envdecay);
	QCOMPARE(afterPublication.dist, complete.dist);
	QCOMPARE(afterPublication.db24, complete.db24);

	// The last of several publications wins.
	const Lb302FilterSettings a{CutoffA, 0.11f, 0.12f, 0.13f, 0.14f, false};
	const Lb302FilterSettings b{0.2f, 0.21f, 0.22f, 0.23f, 0.24f, true};
	const Lb302FilterSettings c{CutoffC, 0.31f, 0.32f, 0.33f, 0.34f, false};
	mailbox.publish([&] { return a; });
	mailbox.publish([&] { return b; });
	mailbox.publish([&] { return c; });

	Lb302FilterSettings last{};
	QVERIFY(mailbox.take(last));
	QCOMPARE(last.cutoff, c.cutoff);
	QCOMPARE(last.reso, c.reso);
	QCOMPARE(last.envmod, c.envmod);
	QCOMPARE(last.envdecay, c.envdecay);
	QCOMPARE(last.dist, c.dist);
	QCOMPARE(last.db24, c.db24);
	QVERIFY(!mailbox.take(last));
#else
	QFAIL("the LB302 does not publish filter settings to the audio thread");
#endif
}

QTEST_MAIN(Lb302FilterMailboxTest)

#include "Lb302FilterMailboxTest.moc"
