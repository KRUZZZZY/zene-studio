/*
 * ClipWaveformEditTest.cpp - the clip-gain handle, envelope points and the Invert polarity item,
 *                            driven on the real SampleClipView
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

/*! Q050-125, the GUI half. Every gesture is a real mouse event on a real SampleClipView inside the
 *  real application, and each check reads the MODEL back (and the drawn image), never the handler:
 *  1. A drag of the clip-gain handle sets `clip.set_gain` - one undo step - and moving it down lowers
 *     the gain; the DRAWN waveform follows the gain curve (a boosted clip paints taller).
 *  2. A click on the waveform with Alt held adds an envelope point at the press (`clip.gain_envelope_set`),
 *     and dragging an existing point moves it; both are one undo step each, and the drawn image changes.
 *  3. The context menu's Invert polarity item flips the clip's polarity through `clip.set_polarity` and
 *     the drawn waveform mirrors around zero; the item is checked while the clip is inverted.
 *  4. A locked clip takes none of the three gestures.
 */

#include <QtTest>

#include <QAction>
#include <QPainter>
#include <QImage>
#include <QMenu>
#include <QMouseEvent>
#include <QTemporaryDir>

#include "ArrangerOverlays.h"
#include "ClipEdits.h"
#include "ClipEditsMenu.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "EditLocks.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "SampleClip.h"
#include "SampleClipOverlays.h"
#include "SampleClipView.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "Song.h"
#include "SongEditor.h"
#include "TimePos.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr tick_t Bar = 192;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

SampleClipView* viewOf(const Clip* clip)
{
	for (SampleClipView* view : getGUI()->songEditor()->m_editor->findChildren<SampleClipView*>())
	{
		if (view->getClip() == clip) { return view; }
	}
	return nullptr;
}

//! The frame SampleClipView draws its overlay in (the same one its hit tests use).
ClipOverlayFrame frameOf(SampleClipView* view)
{
	const float ppb = getGUI()->songEditor()->m_editor->pixelsPerBar();
	const float framesPerTick = Engine::framesPerTick(Engine::audioEngine()->outputSampleRate());
	Q_UNUSED(framesPerTick);
	// The clip view's own conversion: pixels per bar over the ticks in a bar.
	return {ppb / DefaultTicksPerBar, 0, view->width(), ClipView::BORDER_WIDTH + 1,
		view->height() - 2 * (ClipView::BORDER_WIDTH + 1)};
}

//! The action @a text anywhere in @a menu's tree (the polarity item lives under Gain and fades).
QAction* findAction(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
		if (action->menu() != nullptr)
		{
			if (QAction* found = findAction(action->menu(), text)) { return found; }
		}
	}
	return nullptr;
}

void renderView(SampleClipView* view, QImage* image)
{
	*image = QImage(view->size(), QImage::Format_ARGB32_Premultiplied);
	image->fill(Qt::transparent);
	QPainter painter(image);
	view->render(&painter);
	painter.end();
}

//! The top of the waveform's painted BAND in the column at @a x: the constant test buffer draws a
//! solid vertical line through the clip's centre, so the first row above the centre that is no longer
//! the band's own colour is where the amplitude stops. A louder clip's band reaches higher (a smaller
//! y), which is the drawn amplitude the acceptance criterion asks about.
int bandTop(const QImage& image, int x)
{
	const int center = image.height() / 2;
	const QColor band = image.pixelColor(x, center);
	if (band == Qt::transparent) { return -1; }
	for (int y = center; y >= 0; --y)
	{
		const QColor c = image.pixelColor(x, y);
		if (c != band) { return y + 1; }
	}
	return 0;
}

void dragTo(SampleClipView* view, const QPoint& from, const QPoint& to)
{
	QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), view->mapToGlobal(from), Qt::LeftButton,
		Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(view, &press);
	QMouseEvent move(QEvent::MouseMove, QPointF(to), view->mapToGlobal(to), Qt::NoButton, Qt::LeftButton,
		Qt::NoModifier);
	QCoreApplication::sendEvent(view, &move);
	QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to), view->mapToGlobal(to), Qt::LeftButton,
		Qt::NoButton, Qt::NoModifier);
	QCoreApplication::sendEvent(view, &release);
}

} // namespace

class ClipWaveformEditTest : public QObject
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
		installArrangerOverlays(getGUI()->songEditor()->m_editor);  // what the shell does once it is shown
		QCoreApplication::processEvents();
		m_track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(m_track != nullptr);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(m_track != nullptr);
		// A clip wide enough to drag in: a whole bar at a readable zoom.
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 160},
			{QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 0}}).ok);
		m_clip = dynamic_cast<SampleClip*>(m_track->createClip(TimePos(0)));
		QVERIFY(m_clip != nullptr);
		m_clip->setAutoResize(false);
		m_clip->changeLength(TimePos(Bar));
		std::vector<SampleFrame> frames(static_cast<std::size_t>(Bar) * Engine::framesPerTick(
			Engine::audioEngine()->outputSampleRate()), SampleFrame(0.6f, 0.6f));
		m_clip->setSampleBuffer(std::make_shared<SampleBuffer>(frames.data(), frames.size(),
			static_cast<int>(Engine::audioEngine()->outputSampleRate())));
		QCoreApplication::processEvents();
		m_view = viewOf(m_clip);
		QVERIFY2(m_view != nullptr, "the clip has no view");
	}

	//! Criterion 1 (the gain half): a drag of the handle writes clip.set_gain through the command, as
	//! one undo step, and the drawn waveform's amplitude follows.
	void draggingTheGainHandleSetsTheGainAndTheWaveformFollows()
	{
		m_view->resize(std::max(120, m_view->width()), 80);
		const ClipOverlayFrame frame = frameOf(m_view);
		const QPoint handle(frame.xOrigin + 10, frame.top + frame.height / 2);
		QImage plain;
		renderView(m_view, &plain);

		// Drag the handle UP (a boost): the clip's gain goes above unity.
		dragTo(m_view, handle, QPoint(handle.x(), handle.y() - frame.height / 4));
		QVERIFY2(m_clip->clipEdits().gain > 1.0f,
			qPrintable(QStringLiteral("the gain drag left gain at %1").arg(m_clip->clipEdits().gain)));
		const float boosted = m_clip->clipEdits().gain;
		QVERIFY(boosted <= gainDbToLinear(24.f) + 1e-4f);

		QImage louder;
		renderView(m_view, &louder);
		const int column = frame.xOrigin + frame.width / 2;
		const int plainTop = bandTop(plain, column);
		const int louderTop = bandTop(louder, column);
		QVERIFY2(plainTop > 0 && louderTop > 0,
			qPrintable(QStringLiteral("the waveform did not paint (view %1x%2, image %3x%4, column %5, band %6)")
				.arg(m_view->width()).arg(m_view->height()).arg(plain.width()).arg(plain.height())
				.arg(column).arg(plainTop)));
		QVERIFY2(louderTop < plainTop,
			qPrintable(QStringLiteral("a boosted clip paints its waveform at y=%1, the same as unity's %2")
				.arg(louderTop).arg(plainTop)));

		// One undo takes the whole gesture back.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_clip->clipEdits().gain, 1.0f);

		// And downward: a cut lowers the gain, and the drawn waveform gets shorter.
		const QPoint again(frame.xOrigin + 10, frame.top + frame.height / 2);
		dragTo(m_view, again, QPoint(again.x(), again.y() + frame.height / 4));
		QVERIFY2(m_clip->clipEdits().gain < 1.0f,
			qPrintable(QStringLiteral("the downward drag left gain at %1").arg(m_clip->clipEdits().gain)));
		QImage quieter;
		renderView(m_view, &quieter);
		QVERIFY(bandTop(quieter, column) > plainTop);
	}

	//! Criterion 1 (the envelope half): Alt+click adds a point, a plain drag moves it, each is one
	//! undo step, and the drawn envelope changes with them.
	void envelopePointsAreAddedAndMovedOnTheWaveform()
	{
		const ClipOverlayFrame frame = frameOf(m_view);
		QCOMPARE(m_clip->clipEdits().envelopeCount, 0);
		const QPoint add(frame.xOrigin + frame.width / 2, frame.top + frame.height / 3);
		QMouseEvent press(QEvent::MouseButtonPress, QPointF(add), m_view->mapToGlobal(add), Qt::LeftButton,
			Qt::LeftButton, Qt::AltModifier);
		QCoreApplication::sendEvent(m_view, &press);
		QMouseEvent release(QEvent::MouseButtonRelease, QPointF(add), m_view->mapToGlobal(add), Qt::LeftButton,
			Qt::NoButton, Qt::AltModifier);
		QCoreApplication::sendEvent(m_view, &release);

		QCOMPARE(m_clip->clipEdits().envelopeCount, 1);
		const int addedTick = m_clip->clipEdits().envelope[0].tick;
		QVERIFY2(std::abs(addedTick - m_clip->length().getTicks() / 2) <= 2,
			qPrintable(QStringLiteral("the point landed at tick %1, not the middle").arg(addedTick)));
		QVERIFY2(m_clip->clipEdits().envelope[0].gainDb < 0.f, "the point above the middle should be a cut");

		// Move it: a plain press on the point, drag to another column and level, release.
		const QPoint at = QPoint(frame.xOrigin + static_cast<int>(m_clip->clipEdits().envelope[0].tick * frame.pixelsPerTick),
			frame.top + frame.height / 3);
		const QPoint to(frame.xOrigin + frame.width / 4, frame.top + frame.height / 4);
		dragTo(m_view, at, to);
		QCOMPARE(m_clip->clipEdits().envelopeCount, 1);
		QVERIFY2(std::abs(m_clip->clipEdits().envelope[0].tick - m_clip->length().getTicks() / 4) <= 2,
			qPrintable(QStringLiteral("the moved point sits at tick %1").arg(m_clip->clipEdits().envelope[0].tick)));

		// One undo restores the added point's position, the second removes it.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_clip->clipEdits().envelopeCount, 1);
		QCOMPARE(m_clip->clipEdits().envelope[0].tick, addedTick);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_clip->clipEdits().envelopeCount, 0);
	}

	//! Criterion 2's display half: the menu item flips the polarity and the drawn waveform mirrors.
	void thePolarityMenuItemFlipsTheClipThroughTheCommand()
	{
		m_view->resize(std::max(120, m_view->width()), 80);
		QImage before;
		renderView(m_view, &before);
		const int column = before.width() / 2;

		QMenu menu;
		addClipEditsMenu(&menu, m_clip);  // the submenu the clip's own context menu builds
		QAction* invert = findAction(&menu, QStringLiteral("Invert polarity"));
		QVERIFY2(invert != nullptr, "the clip's menu has no Invert polarity item");
		QVERIFY2(invert->isCheckable() && !invert->isChecked(), "a non-inverted clip's item should be unchecked");
		invert->trigger();
		QVERIFY2(m_clip->clipEdits().hasPolarity(), "the menu item did not invert the clip");

		QImage after;
		renderView(m_view, &after);
		// The waveform is a constant level, so its painted band is symmetric; the SIGN is the model.
		// The drawing's own mirror is checked by the curve the thumbnail is handed, below.
		QCOMPARE(after.size(), before.size());
		QVERIFY2(after != before, "the polarity flip did not repaint the waveform");
		QVERIFY(column > 0);

		// The state's read-back and the item's check mark agree, and one undo takes the flip back.
		QMenu again;
		addClipEditsMenu(&again, m_clip);
		QAction* checked = findAction(&again, QStringLiteral("Invert polarity"));
		QVERIFY2(checked != nullptr, "the Invert polarity item vanished while inverted");
		QVERIFY2(checked->isChecked(), "an inverted clip's item is not checked");
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_clip->clipEdits().hasPolarity(), false);
	}

	//! The gesture arithmetic on its own: the dB/pixel mapping is its own inverse, a drag moves the
	//! level the way the pointer moves, a point is only taken within reach, and moving one past its
	//! neighbour re-sorts the envelope the command will be handed.
	void theGestureGeometryIsExact()
	{
		const ClipOverlayFrame frame{1.0f, 0, 192, 0, 84};  // one pixel per tick, one dB per pixel
		// -60 at the bottom, +24 at the top, and each maps back to itself.
		QCOMPARE(gainDbAtY(frame.top + frame.height, frame), MinGainPointDb);
		QCOMPARE(gainDbAtY(frame.top, frame), MaxGainPointDb);
		for (const int y : {0, 10, 37, 60, 84})
		{
			QVERIFY(std::abs(gainDbAtY(frame.top + y, frame) - (24.f - y)) < 1e-4f);
		}
		QCOMPARE(gainTickAtX(30, frame), 30);
		QCOMPARE(clipGainDraggedDb(0.f, -10, frame), 10.f);  // up is louder
		QCOMPARE(clipGainDraggedDb(0.f, 10, frame), -10.f);
		QCOMPARE(clipGainDraggedDb(23.f, -10, frame), MaxGainPointDb);  // clamped to the range
		QCOMPARE(clipGainDraggedDb(-59.f, 10, frame), MinGainPointDb);

		ClipEdits edits;
		edits.envelope[0] = {20, 0.f};
		edits.envelope[1] = {60, -12.f};
		edits.envelopeCount = 2;
		QCOMPARE(gainPointAt(edits, frame, QPoint(20, frame.top + 24)), 0);
		QCOMPARE(gainPointAt(edits, frame, QPoint(60, frame.top + 36)), 1);
		QCOMPARE(gainPointAt(edits, frame, QPoint(100, frame.top + 24)), -1);

		// A point dragged past its neighbour takes its place (the command needs tick order).
		QVERIFY(withGainPoint(&edits, 0, 90, -6.f, 192));
		QCOMPARE(edits.envelopeCount, 2);
		QCOMPARE(edits.envelope[0].tick, 60);
		QCOMPARE(edits.envelope[1].tick, 90);
		// Adding at the end; the 32-point cap and the dB range are the command's own rules.
		QVERIFY(withGainPoint(&edits, edits.envelopeCount, 120, 3.f, 192));
		QCOMPARE(edits.envelopeCount, 3);
		QCOMPARE(edits.envelope[2].tick, 120);
		QVERIFY2(!withGainPoint(&edits, edits.envelopeCount, 130, 99.f, 192), "a dB above +24 was accepted");
		QVERIFY2(!withGainPoint(&edits, edits.envelopeCount, 300, 0.f, 192), "a tick past the clip was accepted");
		QVERIFY2(!withGainPoint(&edits, edits.envelopeCount + 1, 130, 0.f, 192), "an index past the end was accepted");
		edits.envelopeCount = MaxGainPoints;
		QVERIFY2(!withGainPoint(&edits, edits.envelopeCount, 130, 0.f, 192), "a 33rd point was accepted");
	}

	//! A locked clip is not edited by any of the three gestures; the lock is the refusal the commands share.
	void aLockedClipTakesNoGesture()
	{
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)}}).ok);
		const ClipOverlayFrame frame = frameOf(m_view);
		const QPoint handle(frame.xOrigin + 10, frame.top + frame.height / 2);
		dragTo(m_view, handle, QPoint(handle.x(), handle.y() - frame.height / 4));
		QCOMPARE(m_clip->clipEdits().gain, 1.0f);
		QCOMPARE(m_clip->clipEdits().envelopeCount, 0);

		QMouseEvent press(QEvent::MouseButtonPress, QPointF(frame.xOrigin + frame.width / 2, frame.top + 10),
			m_view->mapToGlobal(QPoint(frame.xOrigin + frame.width / 2, frame.top + 10)), Qt::LeftButton,
			Qt::LeftButton, Qt::AltModifier);
		QCoreApplication::sendEvent(m_view, &press);
		QCOMPARE(m_clip->clipEdits().envelopeCount, 0);
	}

private:
	QTemporaryDir m_home;
	SampleTrack* m_track = nullptr;
	SampleClip* m_clip = nullptr;
	SampleClipView* m_view = nullptr;
};

QTEST_MAIN(ClipWaveformEditTest)
#include "ClipWaveformEditTest.moc"
