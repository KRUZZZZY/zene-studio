/*
 * ArrangerTimelineTest.cpp - the arranger's ruler lanes, anchored zoom, drag preview and lock badges
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

// Complete fixture method definitions; included once after the fixture declaration.

void ArrangerTimelineTest::aReversedTrimmedClipDrawsTheTransientsOfTheAudioItPlays() {
	constexpr int BurstTicks[] = {10, 58, 106, 154};
	const auto data = test::burstAudio(200, {BurstTicks[0], BurstTicks[1], BurstTicks[2], BurstTicks[3]});
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
	QVERIFY(track != nullptr);
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
	QVERIFY(clip != nullptr);
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	// the window covers the first half of the source only
	const auto framesPerTick = static_cast<f_cnt_t>(Engine::framesPerTick(rate));
	clip->setSamplePlayLength(100 * framesPerTick);
	QCoreApplication::processEvents();

	SampleClipView* view = nullptr;
	for (SampleClipView* candidate : m_editor->findChildren<SampleClipView*>())
	{
		if (candidate->getClip() == clip) { view = candidate; }
	}
	QVERIFY2(view != nullptr, "no clip view for the sample clip");

	// the overlay analyses the range the clip plays: the window, the first half
	const std::vector<int> forward = view->transientTicks();
	QCOMPARE(forward.size(), std::size_t{2});
	QVERIFY2(std::abs(forward[0] - BurstTicks[0]) <= 2, qPrintable(QStringLiteral("the first transient sits at %1").arg(forward[0])));
	QVERIFY2(std::abs(forward[1] - BurstTicks[1]) <= 2, qPrintable(QStringLiteral("the second transient sits at %1").arg(forward[1])));

	// the user's own reverse: the clip now plays the MIRROR of its window
	view->reverseSample();
	QCoreApplication::processEvents();
	QVERIFY(clip->sample().reversed());

	// ... so the overlay has to show the transients of the second half, heard backwards: the
	// burst at 154 ticks is played at 199 - 154, the one at 106 ticks at 199 - 106. The ticks
	// come in the order the frames were found (the overlay draws one mark per tick, so the
	// order does not matter - the set does).
	const std::vector<int> reversed = view->transientTicks();
	QCOMPARE(reversed.size(), std::size_t{2});
	const int expected[] = {199 - BurstTicks[3], 199 - BurstTicks[2]};
	const bool first = std::abs(reversed[0] - expected[0]) <= 2 && std::abs(reversed[1] - expected[1]) <= 2;
	const bool swapped = std::abs(reversed[0] - expected[1]) <= 2 && std::abs(reversed[1] - expected[0]) <= 2;
	QVERIFY2(first || swapped, qPrintable(QStringLiteral("the transients sit at %1 and %2, expected %3 and %4")
		.arg(reversed[0]).arg(reversed[1]).arg(expected[0]).arg(expected[1])));
}
void ArrangerTimelineTest::dragChorus(int bars, Qt::KeyboardModifiers mods) {
	auto* lanes = m_editor->findChild<ArrangerRulerLanes*>();
	const int y = lanes->laneRect(ArrangerRulerLanes::Lane::Sections).center().y();
	const QPoint from(m_editor->timeLine()->markerX(TimePos(5 * Bar)), y);
	const QPoint to(m_editor->timeLine()->markerX(TimePos((5 + bars) * Bar)), y);
	QTest::mousePress(lanes, Qt::LeftButton, Qt::NoModifier, from);
	QMouseEvent held(QEvent::MouseMove, to, lanes->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, mods);
	QApplication::sendEvent(lanes, &held);
	QCOMPARE(lanes->cursor().shape(), Qt::ClosedHandCursor);
	QTest::mouseRelease(lanes, Qt::LeftButton, mods, to);
}
