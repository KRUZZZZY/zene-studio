/*
 * ArrangerKeysNavigatorTest.cpp - the arranger's clip keys and the navigator strip under it
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

/*! The arranger's own keys: Alt+Shift+Up/Down choose the clip above or below (the nearest on the next track with
 *  clips, the track's first from none) and say so, Ctrl+Alt+Left/Right shorten or lengthen the selected clip by one snap
 *  step through clip.resize, and Ctrl+Shift+L opens the clip list; every other chord is left alone. The navigator is the
 *  whole song in a thin strip with the shown part outlined: a click centres the arranger there, a drag scrolls, the wheel
 *  zooms around the pointer, Alt-click or a double-click on a landmark zooms to it, the keys scroll and zoom, and it
 *  is a slider a screen reader can set. Each is read back off the arranger itself. */

#include <QtTest>

#include <QAbstractSlider>
#include <QJsonObject>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolTip>
#include <QWheelEvent>

#include "AccessibleWidgets.h"
#include "ArrangementSections.h"
#include "ArrangerClipKeys.h"
#include "ArrangerNavigator.h"
#include "ArrangerOverlays.h"
#include "ArrangerZoom.h"
#include "Clip.h"
#include "ClipView.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "ItemList.h"
#include "MainWindow.h"
#include "ProjectMarkers.h"
#include "ShellSelection.h"
#include "Song.h"
#include "SongEditor.h"
#include "Timeline.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr tick_t Bar = 192;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

Clip* clipAt(Track* track, tick_t start, tick_t length)
{
	Clip* clip = track->createClip(TimePos(start));
	clip->changeLength(TimePos(length));
	clip->setAutoResize(false);
	return clip;
}

ControlResult scrollTo(int position)
{
	return run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), position}});
}

//! Where the arranger starts and how much it shows, read off the arranger itself (not through a command that moves it).
int positionNow() { return static_cast<int>(getGUI()->songEditor()->m_editor->currentPosition().getTicks()); }

int visibleNow() { return static_cast<int>(arrangerzoom::visibleTicks(getGUI()->songEditor()->m_editor)); }

} // namespace

class ArrangerKeysNavigatorTest : public QObject
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
		installArrangerOverlays(m_editor);
		installArrangerNavigator(getGUI()->songEditor());
		QCoreApplication::processEvents();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		ShellSelection::instance()->setClip(nullptr);
		ShellSelection::instance()->setTrack(nullptr);
		QCoreApplication::processEvents();
		Engine::getSong()->clearProject();
		QCoreApplication::processEvents();
		m_tracks.clear();
	}

	void onlyTheArrangersChordsAreClaimed()
	{
		const auto claimed = [](int key, Qt::KeyboardModifiers mods) {
			QKeyEvent event(QEvent::KeyPress, key, mods);
			return arrangerkeys::claims(&event);
		};
		const Qt::KeyboardModifiers across = Qt::ShiftModifier | Qt::AltModifier;
		const Qt::KeyboardModifiers resize = Qt::ControlModifier | Qt::AltModifier;
		QVERIFY(claimed(Qt::Key_Up, across));
		QVERIFY(claimed(Qt::Key_Down, across));
		QVERIFY(claimed(Qt::Key_Left, resize));
		QVERIFY(claimed(Qt::Key_Right, resize));
		QVERIFY(claimed(Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier));
		QVERIFY(claimed(Qt::Key_Down, across | Qt::KeypadModifier));  // arrow keys on a keypad are the same chord
		QVERIFY(!claimed(Qt::Key_Down, Qt::AltModifier));              // the Edit menu's Next Clip
		QVERIFY(!claimed(Qt::Key_Up, resize));
		QVERIFY(!claimed(Qt::Key_Right, across));
		QVERIFY(!claimed(Qt::Key_L, Qt::ControlModifier));
		QVERIFY(!claimed(Qt::Key_A, Qt::NoModifier));
		QKeyEvent unclaimed(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
		QVERIFY(!arrangerkeys::handle(m_editor, &unclaimed));
	}

	void altShiftArrowsChooseTheClipAboveOrBelow()
	{
		Clip* first = clipAt(track(), 0, Bar);
		track();                                  // an empty track between: skipped
		Clip* second = clipAt(track(), 2 * Bar, Bar);
		Clip* third = clipAt(m_tracks.at(0), 5 * Bar, Bar);
		QCoreApplication::processEvents();
		Q_UNUSED(third);
		const Qt::KeyboardModifiers across = Qt::ShiftModifier | Qt::AltModifier;

		// From nothing, Down takes the first track's clip nearest the play position.
		QVERIFY(press(Qt::Key_Down, across));
		QCOMPARE(ShellSelection::instance()->clip(), first);
		QVERIFY(a11yText().contains(QStringLiteral(", on ")));
		// ... and the arranger's own selection follows, so Delete and the drag tools act on it.
		bool viewSelected = false;
		for (ClipView* view : m_editor->findChildren<ClipView*>()) { viewSelected = viewSelected || (view->getClip() == first && view->isSelected()); }
		QVERIFY2(viewSelected, "the arranger's selection did not follow the keys");
		// The next track with clips: the empty one is skipped.
		QVERIFY(press(Qt::Key_Down, across));
		QCOMPARE(ShellSelection::instance()->clip(), second);
		// Nothing below: said so, and the selection stays.
		QVERIFY(press(Qt::Key_Down, across));
		QCOMPARE(ShellSelection::instance()->clip(), second);
		QCOMPARE(a11yText(), QStringLiteral("No clip below"));
		QVERIFY(press(Qt::Key_Up, across));
		QCOMPARE(ShellSelection::instance()->clip(), first);
		QVERIFY(press(Qt::Key_Up, across));
		QCOMPARE(ShellSelection::instance()->clip(), first);
		QCOMPARE(a11yText(), QStringLiteral("No clip above"));
		// From the bottom with nothing selected, Up takes the last track's clip.
		ShellSelection::instance()->setClip(nullptr);
		ShellSelection::instance()->setTrack(nullptr);
		QVERIFY(press(Qt::Key_Up, across));
		QCOMPARE(ShellSelection::instance()->clip(), second);
	}

	void ctrlAltArrowsResizeTheSelectedClipBySnapSteps()
	{
		Clip* clip = clipAt(track(), 0, 4 * Bar);
		QCoreApplication::processEvents();
		const Qt::KeyboardModifiers resize = Qt::ControlModifier | Qt::AltModifier;
		const int snap = std::max(1, static_cast<int>(std::lround(m_editor->getSnapSize() * TimePos::ticksPerBar())));
		// Nothing selected: refused, said so, nothing changes.
		QVERIFY(press(Qt::Key_Right, resize));
		QVERIFY2(a11yText().contains(QStringLiteral("no clip is selected")), qPrintable(a11yText()));
		QCOMPARE(clip->length().getTicks(), 4 * Bar);

		ShellSelection::instance()->setClip(clip);
		QVERIFY(press(Qt::Key_Right, resize));
		QCOMPARE(clip->length().getTicks(), 4 * Bar + snap);
		QVERIFY(a11yText().contains(QStringLiteral("long")));
		QVERIFY(press(Qt::Key_Left, resize));
		QVERIFY(press(Qt::Key_Left, resize));
		QCOMPARE(clip->length().getTicks(), 4 * Bar - snap);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // each key is one recorded step
		QCOMPARE(clip->length().getTicks(), 4 * Bar);
		// It never shrinks away: the length bottoms out at one tick.
		clip->changeLength(TimePos(1));
		QVERIFY(press(Qt::Key_Left, resize));
		QCOMPARE(clip->length().getTicks(), 1);
	}

	void ctrlShiftLOpensTheClipList()
	{
		clipAt(track(), 0, Bar);
		QCoreApplication::processEvents();
		QVERIFY(press(Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier));
		QCoreApplication::processEvents();
		bool listed = false;
		for (QWidget* widget : QApplication::allWidgets())
		{
			if (qobject_cast<ItemListPanel*>(widget) != nullptr && widget->isVisible()) { listed = true; }
		}
		QVERIFY2(listed, "Ctrl+Shift+L opened no list of clips");
	}

	void describeClipNamesTheClipItsTrackAndItsSpan()
	{
		Track* t = track();
		t->setName(QStringLiteral("Bass"));
		Clip* clip = clipAt(t, 2 * Bar, 2 * Bar);
		clip->setName(QString());
		QVERIFY(arrangerkeys::describeClip(clip).startsWith(QStringLiteral("Unnamed clip, on Bass, ")));
		clip->setName(QStringLiteral("Riff"));
		QVERIFY(arrangerkeys::describeClip(clip).startsWith(QStringLiteral("Riff, on Bass, ")));
		QVERIFY(!arrangerkeys::selectClip(m_editor, nullptr));
	}

	void theNavigatorShowsTheSongAndScrollsToAClick()
	{
		prepareSong();
		QAbstractSlider* nav = navigator();
		QVERIFY(nav != nullptr);
		QVERIFY(nav->isVisible());
		QVERIFY(!nav->grab().isNull());  // clips, sections, a marker, the loop, the play position and the viewport

		const int width = nav->width();
		QTest::mouseClick(nav, Qt::LeftButton, Qt::NoModifier, QPoint(width / 10, 10));
		const int near = positionNow();
		QTest::mouseClick(nav, Qt::LeftButton, Qt::NoModifier, QPoint(width * 8 / 10, 10));
		const int far = positionNow();
		QVERIFY2(far > near, "a click further along did not scroll further");
		QTest::mouseClick(nav, Qt::LeftButton, Qt::NoModifier, QPoint(0, 10));
		QCOMPARE(positionNow(), 0);  // left of the middle of the view: clamped at the start

		// A drag follows the pointer; the right button does nothing.
		QTest::mousePress(nav, Qt::LeftButton, Qt::NoModifier, QPoint(width / 4, 10));
		const int pressed = positionNow();
		QTest::mouseMove(nav, QPoint(width * 3 / 4, 10));
		QVERIFY(positionNow() > pressed);
		QTest::mouseRelease(nav, Qt::LeftButton, Qt::NoModifier, QPoint(width * 3 / 4, 10));
		const int before = positionNow();
		QTest::mouseClick(nav, Qt::RightButton, Qt::NoModifier, QPoint(width / 10, 10));
		QCOMPARE(positionNow(), before);
	}

	void theNavigatorsKeysScrollAndZoom()
	{
		prepareSong();
		QAbstractSlider* nav = navigator();
		QVERIFY(nav != nullptr);
		nav->setFocus();
		scrollTo(0);
		const int view = visibleNow();
		QVERIFY(view > 0);
		QTest::keyClick(nav, Qt::Key_Right);
		QCOMPARE(positionNow(), view / 8);
		QTest::keyClick(nav, Qt::Key_PageDown);
		QCOMPARE(positionNow(), view / 8 + view);
		QTest::keyClick(nav, Qt::Key_PageUp);
		QCOMPARE(positionNow(), view / 8);
		QTest::keyClick(nav, Qt::Key_Left);
		QCOMPARE(positionNow(), 0);
		QTest::keyClick(nav, Qt::Key_End);
		QVERIFY(positionNow() > 10 * Bar);
		QTest::keyClick(nav, Qt::Key_Home);
		QCOMPARE(positionNow(), 0);

		QTest::keyClick(nav, Qt::Key_Plus);
		const int zoomedIn = visibleNow();
		QVERIFY2(zoomedIn < view, "+ did not zoom in");
		QTest::keyClick(nav, Qt::Key_Minus);
		QVERIFY2(visibleNow() > zoomedIn, "- did not zoom out");

		// A key the strip does not use goes on to the parent.
		const int stays = positionNow();
		QTest::keyClick(nav, Qt::Key_Q);
		QCOMPARE(positionNow(), stays);
	}

	void theWheelZoomsAroundThePointerAndASidewaysTurnGoesOn()
	{
		prepareSong();
		QAbstractSlider* nav = navigator();
		scrollTo(0);
		const int view = visibleNow();
		const QPointF at(nav->width() / 2.0, 10.0);
		QWheelEvent in(at, nav->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		QApplication::sendEvent(nav, &in);
		QVERIFY(in.isAccepted());
		QVERIFY2(visibleNow() < view, "the wheel up did not zoom in");
		QWheelEvent sideways(at, nav->mapToGlobal(at), QPoint(), QPoint(240, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		sideways.setAccepted(true);
		QApplication::sendEvent(nav, &sideways);
		QVERIFY(!sideways.isAccepted());
		QWheelEvent none(at, nav->mapToGlobal(at), QPoint(), QPoint(0, 0), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		none.setAccepted(true);
		QApplication::sendEvent(nav, &none);
		QVERIFY(!none.isAccepted());
	}

	void altClickAndDoubleClickZoomToTheLandmarkUnderThePointer()
	{
		prepareSong();
		QAbstractSlider* nav = navigator();
		scrollTo(0);
		const int view = visibleNow();
		const double perTick = nav->width() / static_cast<double>(std::max<tick_t>((Engine::getSong()->length() + 1) * Bar, view));
		const int inSection = static_cast<int>(6 * Bar * perTick);  // inside the section spanning bars 5..9
		QTest::mouseClick(nav, Qt::LeftButton, Qt::AltModifier, QPoint(inSection, 10));
		QVERIFY2(std::abs(visibleNow() - 4 * Bar) < Bar, qPrintable(QStringLiteral("zoomed to %1 ticks").arg(visibleNow())));
		QVERIFY(std::abs(positionNow() - 4 * Bar) < Bar);

		scrollTo(0);
		run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 40}, {QStringLiteral("anchor"), QStringLiteral("start")}});
		QTest::mouseDClick(nav, Qt::LeftButton, Qt::NoModifier, QPoint(inSection, 10));
		QVERIFY(std::abs(visibleNow() - 4 * Bar) < Bar);
		// Where there is no landmark, the click zooms nothing.
		run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 40}, {QStringLiteral("anchor"), QStringLiteral("start")}});
		const int unzoomed = visibleNow();
		QTest::mouseClick(nav, Qt::LeftButton, Qt::AltModifier, QPoint(static_cast<int>(30 * Bar * perTick), 10));
		QCOMPARE(visibleNow(), unzoomed);
	}

	void theTooltipNamesTheLandmarkAndTheSliderCanBeSet()
	{
		prepareSong();
		QAbstractSlider* nav = navigator();
		const double perTick = nav->width() / static_cast<double>(std::max<tick_t>((Engine::getSong()->length() + 1) * Bar, visibleNow()));
		QHelpEvent help(QEvent::ToolTip, QPoint(static_cast<int>(6 * Bar * perTick), 10), nav->mapToGlobal(QPoint(static_cast<int>(6 * Bar * perTick), 10)));
		QApplication::sendEvent(nav, &help);
		QVERIFY2(QToolTip::text().contains(QStringLiteral("Chorus")), qPrintable(QToolTip::text()));
		QVERIFY(QToolTip::text().contains(QStringLiteral("Click to centre")));
		QHelpEvent away(QEvent::ToolTip, QPoint(static_cast<int>(30 * Bar * perTick), 10), nav->mapToGlobal(QPoint(static_cast<int>(30 * Bar * perTick), 10)));
		QApplication::sendEvent(nav, &away);
		QVERIFY(QToolTip::text().startsWith(QStringLiteral("Click to centre")));
		QToolTip::hideText();

		// A screen reader (or the slider's own key) setting the value scrolls the arranger there.
		QTRY_VERIFY2(nav->maximum() >= 3 * Bar, "the strip never took the song's length as its range");
		nav->setValue(3 * Bar);
		QCOMPARE(positionNow(), 3 * Bar);
		QVERIFY(nav->accessibleDescription().startsWith(QStringLiteral("Showing bars ")));
		// ... and the other way: the arranger moving moves the slider.
		scrollTo(7 * Bar);
		QTRY_COMPARE(nav->value(), 7 * Bar);
	}

	void theNavigatorCommandShowsAndHidesTheStrip()
	{
		QVERIFY(run(QStringLiteral("arrangement.navigator"), {{QStringLiteral("visible"), false}}).ok);
		QVERIFY(!run(QStringLiteral("arrangement.navigator")).result.value(QStringLiteral("visible")).toBool());
		QVERIFY(!getGUI()->songEditor()->findChild<QToolBar*>(QStringLiteral("arrangerNavigator"))->isVisible());
		QVERIFY(run(QStringLiteral("arrangement.navigator"), {{QStringLiteral("visible"), true}}).ok);
		QVERIFY(run(QStringLiteral("arrangement.navigator")).result.value(QStringLiteral("visible")).toBool());
		QVERIFY(getGUI()->songEditor()->findChild<QToolBar*>(QStringLiteral("arrangerNavigator"))->isVisible());
		installArrangerNavigator(getGUI()->songEditor());  // installs once
		QCOMPARE(getGUI()->songEditor()->findChildren<QToolBar*>(QStringLiteral("arrangerNavigator")).size(), 1);
		installArrangerNavigator(nullptr);
	}

private:
	Track* track()
	{
		Track* t = Track::create(Track::Type::Instrument, Engine::getSong());
		QCoreApplication::processEvents();
		m_tracks.push_back(t);
		return t;
	}

	QAbstractSlider* navigator() const
	{
		auto* bar = getGUI()->songEditor()->findChild<QToolBar*>(QStringLiteral("arrangerNavigator"));
		return bar != nullptr ? bar->findChild<QAbstractSlider*>() : nullptr;
	}

	//! A song long enough to scroll, with every kind of landmark, and the strip shown.
	void prepareSong()
	{
		Track* t = track();
		clipAt(t, 0, 3 * Bar);
		clipAt(t, 40 * Bar, 2 * Bar);
		ArrangementSections::instance().add(4 * Bar, 8 * Bar, QStringLiteral("Chorus"), QStringLiteral("#d04040"));
		ProjectMarkers::instance().add(12 * Bar, QStringLiteral("Drop"), QString());
		Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		timeline.setLoopPoints(TimePos(16 * Bar), TimePos(20 * Bar));
		timeline.setLoopEnabled(true);
		Engine::getSong()->setPlayPos(2 * Bar, Song::PlayMode::Song);
		run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 40}, {QStringLiteral("anchor"), QStringLiteral("start")}});
		QVERIFY(run(QStringLiteral("arrangement.navigator"), {{QStringLiteral("visible"), true}}).ok);
		scrollTo(0);
		QCoreApplication::processEvents();
		if (navigator() != nullptr) { navigator()->resize(600, navigator()->height()); }
	}

	bool press(int key, Qt::KeyboardModifiers mods)
	{
		QKeyEvent event(QEvent::KeyPress, key, mods);
		return arrangerkeys::handle(m_editor, &event);
	}

	static QString a11yText() { return a11y::lastAnnouncement(); }

	QTemporaryDir m_home;
	SongEditor* m_editor = nullptr;
	std::vector<Track*> m_tracks;
};

QTEST_MAIN(ArrangerKeysNavigatorTest)
#include "ArrangerKeysNavigatorTest.moc"
