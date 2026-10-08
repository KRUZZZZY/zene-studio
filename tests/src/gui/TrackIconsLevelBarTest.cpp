/*
 * TrackIconsLevelBarTest.cpp - the track header's chosen icon, and the thin level meter
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

/*! A track's chosen icon (track.set_icon) replaces its type's glyph on the header button, and an unknown name falls back
 *  to the glyph instead of a blank; clearing it puts the glyph back. The LevelBar polls its source only while shown,
 *  falls back gently from a peak, paints in the meter colours, reads its level in dB in the tooltip, and the track
 *  variant follows its track's mixer channel. */

#include <QtTest>

#include <memory>
#include <vector>

#include <QApplication>
#include <QHelpEvent>
#include <QPointer>
#include <QTemporaryDir>
#include <QToolButton>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "LevelBar.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "ShellIcons.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"
#include "TrackIcons.h"
#include "TrackLabelButton.h"
#include "TrackLooks.h"
#include "TrackView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! A counter on Paint events, to see a header repaint.
class PaintCounter : public QObject
{
public:
	int paints = 0;

protected:
	bool eventFilter(QObject*, QEvent* event) override
	{
		if (event->type() == QEvent::Paint) { ++paints; }
		return false;
	}
};

QColor pixelOf(LevelBar& bar, int x, int y)
{
	return bar.grab().toImage().pixelColor(x, y);
}

} // namespace

class TrackIconsLevelBarTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the mixer must not overwrite the readings set here
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		TrackLooks::instance().clear();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aChosenIconReplacesTheTypeGlyphAndClearingPutsItBack()
	{
		Track* track = Track::create(Track::Type::Sample, Engine::getSong());
		QCoreApplication::processEvents();
		QToolButton button;
		std::string shown;
		QVERIFY(!applyTrackIcon(&button, track, &shown));  // no icon chosen: the type's own applies
		QVERIFY(shown.empty());

		const QString name = shellicons::ids().value(0);
		QVERIFY(!name.isEmpty());
		TrackLooks::instance().setIcon(track->id(), name);
		QVERIFY(applyTrackIcon(&button, track, &shown));
		QCOMPARE(shown, "zene:" + name.toStdString());
		QVERIFY(!button.icon().isNull());
		const qint64 key = button.icon().cacheKey();
		QVERIFY(applyTrackIcon(&button, track, &shown));  // already shown: the button is left alone
		QCOMPARE(button.icon().cacheKey(), key);

		// A name this version cannot draw shows the type glyph, never a blank header.
		TrackLooks::instance().setIcon(track->id(), QStringLiteral("no-such-icon"));
		QVERIFY(!applyTrackIcon(&button, track, &shown));
		QVERIFY(shown.empty());
		QVERIFY(!button.icon().isNull());

		// Back to the first, then removed: the glyph returns.
		TrackLooks::instance().setIcon(track->id(), name);
		QVERIFY(applyTrackIcon(&button, track, &shown));
		TrackLooks::instance().setIcon(track->id(), QString());
		QVERIFY(!applyTrackIcon(&button, track, &shown));
		QVERIFY(shown.empty());
		QVERIFY(!button.icon().isNull());
		QCoreApplication::processEvents();
	}

	void aTypeWithNoGlyphOfItsOwnShowsNoneOnceTheChosenIconIsRemoved()
	{
		Track* folder = Track::create(Track::Type::Instrument, Engine::getSong());
		QCoreApplication::processEvents();
		QToolButton button;
		std::string shown;
		TrackLooks::instance().setIcon(folder->id(), shellicons::ids().value(0));
		QVERIFY(applyTrackIcon(&button, folder, &shown));
		TrackLooks::instance().setIcon(folder->id(), QString());
		QVERIFY(!applyTrackIcon(&button, folder, &shown));
		QVERIFY2(button.icon().isNull(), "the icon just removed is still on the button");
	}

	void repaintingTheHeadersTouchesEveryTrackLabelButton()
	{
		for (int i = 0; i < 3; ++i)
		{
			Track::create(i == 1 ? Track::Type::Instrument : Track::Type::Sample, Engine::getSong());
			QCoreApplication::processEvents();
		}
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
		QCoreApplication::processEvents();
		QList<TrackLabelButton*> headers;
		for (QWidget* widget : QApplication::allWidgets())
		{
			auto* candidate = qobject_cast<TrackLabelButton*>(widget);
			if (candidate != nullptr && candidate->isVisibleTo(candidate->window())) { headers.append(candidate); }
		}
		QVERIFY2(headers.size() >= 3, qPrintable(QStringLiteral("only %1 track headers are on screen").arg(headers.size())));
		std::vector<std::unique_ptr<PaintCounter>> counters;
		for (TrackLabelButton* header : headers)
		{
			counters.push_back(std::make_unique<PaintCounter>());
			header->installEventFilter(counters.back().get());
		}
		repaintTrackHeaders();
		for (const auto& counter : counters) { QTRY_VERIFY(counter->paints > 0); }  // every one of them was painted again
		for (int i = 0; i < headers.size(); ++i) { headers[i]->removeEventFilter(counters[static_cast<std::size_t>(i)].get()); }
	}

	void theLevelBarPollsOnlyWhileShownAndPaintsInTheMeterColours()
	{
		int polls = 0;
		float level = 0.f;
		LevelBar bar([&polls, &level] { ++polls; return level; }, QStringLiteral("Test meter"), nullptr);
		QCOMPARE(bar.size(), QSize(6, 22));
		QCOMPARE(bar.accessibleName(), QStringLiteral("Test meter"));
		QCOMPARE(bar.toolTip(), QStringLiteral("Test meter"));
		QTest::qWait(150);
		QCOMPARE(polls, 0);  // hidden: nothing is polled

		const tokens::Palette& pal = tokens::palette();
		bar.show();
		QTRY_VERIFY(polls > 0);
		QCOMPARE(pixelOf(bar, 3, 0), pal.control);   // silent: the whole bar is the empty track
		QCOMPARE(pixelOf(bar, 3, 21), pal.control);

		level = 0.5f;  // -6.02 dB: the middle colour, filled from the bottom to about 90 %
		QTRY_COMPARE(pixelOf(bar, 3, 21), pal.meterMid);
		QCOMPARE(pixelOf(bar, 3, 0), pal.control);
		level = 1.0f;  // 0 dB: the high colour
		QTRY_COMPARE(pixelOf(bar, 3, 21), pal.meterHigh);
		level = 0.02f;  // -34 dB: the low colour, short
		QTRY_COMPARE(pixelOf(bar, 3, 21), pal.meterLow);

		// It falls back gently from a peak rather than dropping at once, then to nothing.
		level = 1.0f;
		QTRY_COMPARE(pixelOf(bar, 3, 21), pal.meterHigh);
		level = 0.f;
		QTRY_COMPARE(pixelOf(bar, 3, 21), pal.control);

		// Hidden again, polling stops.
		bar.hide();
		const int before = polls;
		QTest::qWait(150);
		QCOMPARE(polls, before);
	}

	void theTooltipReadsTheLevelInDecibelsWhenItOpens()
	{
		float level = 0.5f;
		int polls = 0;
		LevelBar bar([&level, &polls] { ++polls; return level; }, QStringLiteral("Send"), nullptr);
		bar.show();
		QTRY_VERIFY(polls >= 2);  // the reading has been taken before the tooltip opens
		QHelpEvent help(QEvent::ToolTip, QPoint(2, 2), bar.mapToGlobal(QPoint(2, 2)));
		QApplication::sendEvent(&bar, &help);
		QVERIFY2(bar.toolTip().startsWith(QStringLiteral("Send: -6.")), qPrintable(bar.toolTip()));
		QVERIFY(bar.toolTip().endsWith(QStringLiteral(" dB")));
		level = 0.f;
		// The held peak falls away (15 % a poll, 20 polls a second) until the reading is under the meter's floor.
		QTRY_VERIFY_WITH_TIMEOUT((QApplication::sendEvent(&bar, &help), bar.toolTip() == QStringLiteral("Send: < -60 dB")), 6000);
	}

	void aTrackLevelBarFollowsItsTracksMixerChannelAndIsSilentWithNone()
	{
		while (Engine::mixer()->numChannels() < 3) { Engine::mixer()->createChannel(); }
		Track* track = Track::create(Track::Type::Sample, Engine::getSong());
		QCoreApplication::processEvents();
		LevelBar* bar = trackLevelBar(track, nullptr);
		QCOMPARE(bar->accessibleName(), QStringLiteral("The track's level"));
		bar->show();
		const tokens::Palette& pal = tokens::palette();
		track->mixerChannelModel()->setValue(2);
		Engine::mixer()->mixerChannel(2)->m_rmsLeft.set(0.f);
		Engine::mixer()->mixerChannel(2)->m_rmsRight.set(0.9f);  // the louder side counts
		QTRY_COMPARE(pixelOf(*bar, 3, 21), pal.meterHigh);
		track->mixerChannelModel()->setValue(-1);  // no channel: silent
		Engine::mixer()->mixerChannel(2)->m_rmsRight.set(0.f);
		QTRY_COMPARE(pixelOf(*bar, 3, 21), pal.control);
		delete bar;

		// A bar whose track is gone reads silence rather than a dead pointer.
		auto* doomed = Track::create(Track::Type::Sample, Engine::getSong());
		QCoreApplication::processEvents();
		LevelBar* orphan = trackLevelBar(doomed, nullptr);
		orphan->show();
		const QPointer<Track> watched(doomed);
		doomed->deleteLater();
		QTRY_VERIFY(watched.isNull());  // the track is gone
		QTRY_COMPARE(pixelOf(*orphan, 3, 21), pal.control);
		delete orphan;
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(TrackIconsLevelBarTest)
#include "TrackIconsLevelBarTest.moc"
