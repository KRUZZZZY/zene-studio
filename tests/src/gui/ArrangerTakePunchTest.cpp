/*
 * ArrangerTakePunchTest.cpp - the punch band in the ruler and the take-lane rows of the arrangement
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

/*! 0.5.0 L03, in the real application:
 *  - Q050-147: the punch range has its own band under the loop brace, its handles drag it, the
 *    toolbar arm/clear actions drive transport.punch_set/punch_clear, and the loop markers never move;
 *  - Q050-123: a sample track with take lanes shows one row per lane, with each take's clip geometry
 *    and waveform on the bar ruler's mapping at two scroll/zoom positions, and the comp strip keeps
 *    the existing take-lanes panel available.
 *
 *  These two cases share the arrangement fixture; they live together so ArrangerTimelineTest's
 *  file-length ratchet is not grown by another unit's tests. */

#include <QtTest>

#include <cmath>
#include <memory>
#include <vector>

#include <QAction>
#include <QImage>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

#include "CompSwipeStrip.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "SongEditor.h"
#include "TakeLaneRows.h"
#include "TakeLanesPanel.h"
#include "TimeLineWidget.h"
#include "Timeline.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "TrackContentWidget.h"
#include "TrackView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr tick_t Bar = 192;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A take with audible peaks, so the waveform painter has something to draw.
SampleClip* waveTake(Track* track, tick_t start, tick_t length)
{
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(start)));
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	std::vector<SampleFrame> data(static_cast<std::size_t>(std::max(1, rate / 4)));
	for (std::size_t i = 0; i < data.size(); ++i)
	{
		const float value = static_cast<float>(std::sin(static_cast<double>(i) * 0.08)) * 0.7f;
		data[i] = SampleFrame(value, -value);
	}
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	clip->changeLength(TimePos(length));
	clip->movePosition(TimePos(start));
	return clip;
}

//! True when the middle column of @a rect holds a pixel other than the take's fill: the waveform.
bool hasWaveformInk(const QImage& image, const QRect& rect)
{
	const QRect visible = rect.intersected(image.rect()).adjusted(0, 3, 0, -3);
	if (visible.isEmpty()) { return false; }
	const int x = visible.center().x();
	QHash<QRgb, int> counts;
	int total = 0;
	for (int y = visible.top(); y <= visible.bottom(); ++y)
	{
		++counts[image.pixel(x, y)];
		++total;
	}
	int mostCommon = 0;
	for (auto it = counts.cbegin(); it != counts.cend(); ++it) { mostCommon = std::max(mostCommon, it.value()); }
	return total - mostCommon > 0;
}

} // namespace

class ArrangerTakePunchTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
		m_editor = getGUI()->songEditor()->m_editor;
		QCoreApplication::processEvents();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 40},
			{QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 0}}).ok);
		QCoreApplication::processEvents();
	}

	//! 0.5.0 L03 (Q050-147): the punch range has its own band and handles under the loop brace,
	//! and the toolbar's arm control arms/disarms it without ever reading the loop markers. A drag
	//! is one transport.punch_set - one undo step - and leaves the loop exactly as it was.
	void thePunchRangeHasItsOwnHandlesAndArmControl()
	{
		Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		timeline.clearPunch();
		auto* arm = getGUI()->songEditor()->findChild<QAction*>(QStringLiteral("punchAction"));
		auto* clear = getGUI()->songEditor()->findChild<QAction*>(QStringLiteral("punchClearAction"));
		QVERIFY2(arm != nullptr, "the song editor has no punch arm action");
		QVERIFY2(clear != nullptr, "the song editor has no punch clear action");
		QVERIFY(!arm->isChecked());
		QVERIFY(!clear->isEnabled());

		const tick_t loopBegin = timeline.loopBegin().getTicks();
		const tick_t loopEnd = timeline.loopEnd().getTicks();
		const bool loopEnabled = timeline.loopEnabled();

		// Arm at the play position: the range starts there, not at the loop brace.
		Engine::getSong()->setPlayPos(TimePos(3 * Bar), Song::PlayMode::Song);
		arm->trigger();
		QVERIFY2(timeline.punchArmed(), "arming the punch action did not arm the punch range");
		QCOMPARE(timeline.punchBegin(), tick_t{3 * Bar});
		QCOMPARE(timeline.punchEnd(), tick_t{4 * Bar});
		QCOMPARE(timeline.loopBegin().getTicks(), loopBegin);
		QCOMPARE(timeline.loopEnd().getTicks(), loopEnd);
		QCOMPARE(timeline.loopEnabled(), loopEnabled);

		TimeLineWidget* ruler = m_editor->timeLine();
		// The offscreen test window does not always give the ruler its laid-out height; a real one
		// is 32 pixels. Give it that, so the band's own row can be checked against the loop's.
		ruler->resize(std::max(1200, ruler->width()), 32);
		QVERIFY(ruler->hasPunchRange());
		const QRect band = ruler->punchBandRect();
		QVERIFY2(!band.isEmpty(), "the ruler did not reserve a band for the punch range");
		// Its own band, not the whole ruler: the loop brace keeps the room above it.
		QVERIFY(band.height() >= 2);
		QVERIFY(band.height() < ruler->height());
		QCOMPARE(band.bottom(), ruler->height() - 1);
		QCOMPARE(ruler->punchBeginHandleRect().center().x(), ruler->markerX(TimePos(3 * Bar)));
		QCOMPARE(ruler->punchEndHandleRect().center().x(), ruler->markerX(TimePos(4 * Bar)));

		// Drag the out handle one bar to the right. The loop is untouched and one undo puts it back.
		QPoint from = ruler->punchEndHandleRect().center();
		QPoint to(ruler->markerX(TimePos(5 * Bar)), from.y());
		QTest::mousePress(ruler, Qt::LeftButton, Qt::NoModifier, from);
		QTest::mouseMove(ruler, to);
		QTest::mouseRelease(ruler, Qt::LeftButton, Qt::NoModifier, to);
		QCOMPARE(timeline.punchBegin(), tick_t{3 * Bar});
		QCOMPARE(timeline.punchEnd(), tick_t{5 * Bar});
		QCOMPARE(timeline.loopBegin().getTicks(), loopBegin);
		QCOMPARE(timeline.loopEnd().getTicks(), loopEnd);
		QCOMPARE(timeline.loopEnabled(), loopEnabled);
		QVERIFY2(run(QStringLiteral("control.undo")).ok, "the punch drag was not one undo step");
		QCOMPARE(timeline.punchEnd(), tick_t{4 * Bar});

		// The in handle moves just as independently.
		from = ruler->punchBeginHandleRect().center();
		to = QPoint(ruler->markerX(TimePos(2 * Bar)), from.y());
		QTest::mousePress(ruler, Qt::LeftButton, Qt::NoModifier, from);
		QTest::mouseMove(ruler, to);
		QTest::mouseRelease(ruler, Qt::LeftButton, Qt::NoModifier, to);
		QCOMPARE(timeline.punchBegin(), tick_t{2 * Bar});
		QCOMPARE(timeline.punchEnd(), tick_t{4 * Bar});
		QCOMPARE(timeline.loopBegin().getTicks(), loopBegin);
		QCOMPARE(timeline.loopEnd().getTicks(), loopEnd);

		// Disarming keeps the bounds for another pass; clearing forgets them.
		arm->trigger();
		QVERIFY2(!timeline.punchEnabled(), "disarming the punch action left it armed");
		QCOMPARE(timeline.punchBegin(), tick_t{2 * Bar});
		QCOMPARE(timeline.punchEnd(), tick_t{4 * Bar});
		QVERIFY(!timeline.punchArmed());

		// An agent's edit leaves the same range and the GUI reflects its arm state.
		QVERIFY(run(QStringLiteral("transport.punch_set"), {{QStringLiteral("start"), 1 * Bar},
			{QStringLiteral("end"), 2 * Bar}, {QStringLiteral("enabled"), true}}).ok);
		QCOMPARE(arm->isChecked(), true);
		QCOMPARE(timeline.punchBegin(), tick_t{1 * Bar});
		QCOMPARE(timeline.punchEnd(), tick_t{2 * Bar});
		QCOMPARE(timeline.loopBegin().getTicks(), loopBegin);
		QCOMPARE(timeline.loopEnd().getTicks(), loopEnd);

		QVERIFY(clear->isEnabled());
		clear->trigger();
		QCOMPARE(timeline.punchBegin(), tick_t{0});
		QCOMPARE(timeline.punchEnd(), tick_t{0});
		QVERIFY(!timeline.punchEnabled());
		QCOMPARE(arm->isChecked(), false);
		QCOMPARE(timeline.loopBegin().getTicks(), loopBegin);
		QCOMPARE(timeline.loopEnd().getTicks(), loopEnd);
	}


	//! 0.5.0 L03 (Q050-123): a sample track with take lanes shows one row per lane, every take at
	//! its own place on the same mapping as the bar ruler - at two zoom levels - and its waveform
	//! is painted inside that rectangle. The existing take-lanes panel stays available beside it.
	void takeLaneRowsAlignWaveformsWithTheRulerAndKeepTheCompPanel()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(track != nullptr);
		SampleClip* first = waveTake(track, 0, 2 * Bar);
		SampleClip* second = waveTake(track, 3 * Bar, 2 * Bar);
		const QString id = control::trackIdOf(track);
		QVERIFY(run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), id}}).ok);
		QVERIFY(run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), id}}).ok);
		QVERIFY(run(QStringLiteral("comp.assign"), {{QStringLiteral("clip"), control::clipIdOf(first)},
			{QStringLiteral("lane"), 0}}).ok);
		QVERIFY(run(QStringLiteral("comp.assign"), {{QStringLiteral("clip"), control::clipIdOf(second)},
			{QStringLiteral("lane"), 1}}).ok);
		QCoreApplication::processEvents();

		TrackView* view = nullptr;
		for (TrackView* candidate : m_editor->trackViews())
		{
			if (candidate->getTrack() == track) { view = candidate; }
		}
		QVERIFY2(view != nullptr, "the sample track has no row in the arranger");
		auto* rows = view->getTrackContentWidget()->findChild<TakeLaneRows*>(QStringLiteral("takeLaneRows"));
		QVERIFY2(rows != nullptr, "the sample track has no take-lane rows");
		QTRY_VERIFY_WITH_TIMEOUT(rows->isVisible(), 3000);
		rows->refreshNow();
		QCOMPARE(rows->lanes().size(), std::size_t{2});

		const auto contentX = [this](tick_t tick) {
			const TimeLineWidget* ruler = m_editor->timeLine();
			return ruler->markerX(TimePos(tick)) - ruler->markerX(m_editor->currentPosition());
		};
		auto bands = rows->bands();
		QCOMPARE(bands.size(), std::size_t{2});
		QCOMPARE(bands[0].lane, 0);
		QCOMPARE(bands[0].rect.left(), contentX(first->startPosition().getTicks()));
		QCOMPARE(bands[0].rect.right() + 1, contentX(first->endPosition().getTicks()));
		QCOMPARE(bands[1].lane, 1);
		QCOMPARE(bands[1].rect.left(), contentX(second->startPosition().getTicks()));
		QVERIFY(bands[0].rect.top() < bands[1].rect.top());

		const QImage drawn = view->getTrackContentWidget()->grab().toImage();
		QVERIFY2(hasWaveformInk(drawn, bands[0].rect), "the first take's lane holds no waveform");
		QVERIFY2(hasWaveformInk(drawn, bands[1].rect), "the second take's lane holds no waveform");

		// Scrolling moves the rows with the ruler, and a zoom keeps the same tick at the same place.
		QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 2 * Bar}}).ok);
		QCoreApplication::processEvents();
		rows->refreshNow();
		bands = rows->bands();
		QCOMPARE(bands[0].rect.left(), contentX(first->startPosition().getTicks()));
		QCOMPARE(bands[1].rect.left(), contentX(second->startPosition().getTicks()));
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 80},
			{QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		QCoreApplication::processEvents();
		rows->refreshNow();
		bands = rows->bands();
		QCOMPARE(bands[1].rect.left(), contentX(second->startPosition().getTicks()));
		QCOMPARE(bands[1].rect.right() + 1, contentX(second->endPosition().getTicks()));

		// The existing comp panel is still the place takes are auditioned and comped.
		TakeLanesPanel panel(id);
		panel.refresh();
		QVERIFY(panel.compButton() != nullptr);
		QVERIFY(panel.addLaneButton() != nullptr);
		QVERIFY(panel.auditionButton(0) != nullptr);
		QVERIFY(panel.strip() != nullptr);
		QCOMPARE(panel.strip()->takeBands().size(), std::size_t{2});
		for (const CompSwipeStrip::TakeBand& band : panel.strip()->takeBands())
		{
			QCOMPARE(band.rect.left(), panel.strip()->tickToX(band.begin));
			QCOMPARE(band.rect.right() + 1, panel.strip()->tickToX(band.end));
		}
	}


private:
	QTemporaryDir m_home;
	SongEditor* m_editor = nullptr;
};

QTEST_MAIN(ArrangerTakePunchTest)
#include "ArrangerTakePunchTest.moc"
