/*
 * TouchInstrumentSurfaceTest.cpp - the keyboard as a multi-touch performance surface (Q050-321.1)
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*! Q050-321.1 (touch keyboard and drum-pad surfaces, stage 1/3): more than one finger plays the
 *  instrument window's keyboard at once, each finger its own note.
 *
 *  Everything is measured where the instrument hears it - the track's live note handles
 *  (InstrumentTrack::playingNote), which is the same MIDI input path the mouse and the computer
 *  keyboard feed - and on the drawn key state (Piano::isKeyPressed) the surface shows. The touch
 *  events are real QTouchEvent sequences delivered by QTest to the real PianoView, not a call
 *  into the surface's own model.
 *
 *  The acceptance this unit owns:
 *   - two or more contacts sound distinct simultaneous notes;
 *   - releasing or cancelling one contact leaves the others held;
 *   - the existing mouse and computer-keyboard input still works.
 */

#include <QtTest>

#include <QApplication>
#include <QKeyEvent>
#include <QTouchEvent>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "NotePlayHandle.h"
#include "Piano.h"
#include "PianoView.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! Qt 5.15's touch sequence commit() returns void; Qt 6 returns whether delivery accepted it.
bool commitTouchSequence(QTest::QTouchEventSequence& sequence)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return sequence.commit();
#else
	sequence.commit();
	return true;
#endif
}

//! The native scan code the piano uses for the "Z" key (C on the computer keyboard), per platform.
quint32 scanCodeForZ()
{
#if defined(LMMS_BUILD_WIN32)
	return 44;
#elif defined(LMMS_BUILD_APPLE)
	return 6;
#else
	return 52;  // Linux, the BSDs and Cygwin
#endif
}

} // namespace

class TouchInstrumentSurfaceTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
	}

	void cleanupTestCase()
	{
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track != nullptr);
		m_view = new PianoView(nullptr);
		m_view->setModel(m_track->pianoModel());
		m_view->resize(760, 100);
		m_view->show();
		QVERIFY(QTest::qWaitForWindowExposed(m_view));
		m_device = QTest::createTouchDevice();
	}

	void cleanup()
	{
		delete m_view;
		m_view = nullptr;
		m_track = nullptr;
		m_device = nullptr;
		Engine::getSong()->clearProject();
	}

	//! Two contacts down together: two distinct notes, each held by its own contact.
	void twoContactsSoundTwoDistinctNotes()
	{
		const int first = 60;
		const int second = 64;
		const QPoint atFirst = pointOfKey(first, true);
		const QPoint atSecond = pointOfKey(second, true);

		auto sequence = QTest::touchEvent(m_view, m_device, false);
		sequence.press(0, atFirst, m_view).press(1, atSecond, m_view);
		QVERIFY2(commitTouchSequence(sequence), "the two-finger touch was not delivered");

		QVERIFY2(m_track->playingNote(first) != nullptr, "the first contact did not sound its note");
		QVERIFY2(m_track->playingNote(second) != nullptr, "the second contact did not sound its note");
		QVERIFY(m_track->pianoModel()->isKeyPressed(first));
		QVERIFY(m_track->pianoModel()->isKeyPressed(second));
		QCOMPARE(m_view->touchHolders(first), 1);
		QCOMPARE(m_view->touchHolders(second), 1);
	}

	//! Three contacts, so "two or more" is more than the pair: every one is its own note.
	void threeContactsSoundThreeDistinctNotes()
	{
		const int keys[] = {55, 60, 64};
		const QPoint points[] = {pointOfKey(keys[0], true), pointOfKey(keys[1], true), pointOfKey(keys[2], true)};

		auto sequence = QTest::touchEvent(m_view, m_device, false);
		sequence.press(0, points[0], m_view).press(1, points[1], m_view).press(2, points[2], m_view);
		QVERIFY(commitTouchSequence(sequence));

		for (int key : keys) { QVERIFY2(m_track->playingNote(key) != nullptr, qPrintable(QStringLiteral("key %1 silent").arg(key))); }

		auto last = QTest::touchEvent(m_view, m_device, false);
		last.release(0, points[0], m_view).release(1, points[1], m_view).release(2, points[2], m_view);
		QVERIFY(commitTouchSequence(last));
		for (int key : keys) { QVERIFY2(m_track->playingNote(key) == nullptr, qPrintable(QStringLiteral("key %1 stuck").arg(key))); }
	}

	//! Lifting one contact releases only its note; the other contact's note keeps sounding.
	void releasingOneContactLeavesTheOtherHeld()
	{
		const int released = 60;
		const int held = 64;
		const QPoint atReleased = pointOfKey(released, true);
		const QPoint atHeld = pointOfKey(held, true);

		auto pressed = QTest::touchEvent(m_view, m_device, false);
		pressed.press(0, atReleased, m_view).press(1, atHeld, m_view);
		QVERIFY(commitTouchSequence(pressed));
		QVERIFY(m_track->playingNote(released) != nullptr);
		QVERIFY(m_track->playingNote(held) != nullptr);

		// The first finger lifts; the second stays where it is (Qt reports it as stationary).
		auto oneLifts = QTest::touchEvent(m_view, m_device, false);
		oneLifts.release(0, atReleased, m_view).stationary(1);
		QVERIFY(commitTouchSequence(oneLifts));

		QVERIFY2(m_track->playingNote(released) == nullptr, "the lifted contact's note did not end");
		QVERIFY2(m_track->playingNote(held) != nullptr, "the held contact's note was released with the other");
		QVERIFY(!m_track->pianoModel()->isKeyPressed(released));
		QVERIFY(m_track->pianoModel()->isKeyPressed(held));
		QCOMPARE(m_view->touchHolders(held), 1);

		// The second finger lifts on its own, and then its note ends.
		auto lastLifts = QTest::touchEvent(m_view, m_device, false);
		lastLifts.release(1, atHeld, m_view);
		QVERIFY(commitTouchSequence(lastLifts));
		QVERIFY(m_track->playingNote(held) == nullptr);
		QVERIFY(!m_track->pianoModel()->isKeyPressed(held));
	}

	//! Cancelling one contact - not the whole sequence - leaves the other contact's note sounding.
	void cancellingOneContactLeavesTheOtherHeld()
	{
		const int cancelled = 60;
		const int held = 64;

		auto pressed = QTest::touchEvent(m_view, m_device, false);
		pressed.press(0, pointOfKey(cancelled, true), m_view).press(1, pointOfKey(held, true), m_view);
		QVERIFY(commitTouchSequence(pressed));
		QVERIFY(m_track->playingNote(cancelled) != nullptr);
		QVERIFY(m_track->playingNote(held) != nullptr);

		m_view->touchContacts().cancel(0);

		QVERIFY2(m_track->playingNote(cancelled) == nullptr, "the cancelled contact's note did not end");
		QVERIFY2(m_track->playingNote(held) != nullptr, "cancelling one contact released another");
		QCOMPARE(m_view->touchHolders(cancelled), 0);
		QCOMPARE(m_view->touchHolders(held), 1);
	}

	//! A platform-level TouchCancel ends every contact, so no note is left hanging.
	void aSequenceCancelReleasesEveryContact()
	{
		const int a = 60;
		const int b = 64;

		auto pressed = QTest::touchEvent(m_view, m_device, false);
		pressed.press(0, pointOfKey(a, true), m_view).press(1, pointOfKey(b, true), m_view);
		QVERIFY(commitTouchSequence(pressed));
		QVERIFY(m_track->playingNote(a) != nullptr);
		QVERIFY(m_track->playingNote(b) != nullptr);

		QTouchEvent cancelEvent(QEvent::TouchCancel);
		QCoreApplication::sendEvent(m_view, &cancelEvent);

		QVERIFY(m_track->playingNote(a) == nullptr);
		QVERIFY(m_track->playingNote(b) == nullptr);
		QVERIFY(!m_view->touchContacts().isHolding());
	}

	//! A finger sliding from one key to the next moves its own note, not the other contact's.
	void slidingOneContactMovesOnlyItsOwnNote()
	{
		const int from = 60;
		const int to = 62;
		const int other = 64;

		auto pressed = QTest::touchEvent(m_view, m_device, false);
		pressed.press(0, pointOfKey(from, true), m_view).press(1, pointOfKey(other, true), m_view);
		QVERIFY(commitTouchSequence(pressed));
		QVERIFY(m_track->playingNote(from) != nullptr);
		QVERIFY(m_track->playingNote(other) != nullptr);

		auto slid = QTest::touchEvent(m_view, m_device, false);
		slid.move(0, pointOfKey(to, true), m_view).stationary(1);
		QVERIFY(commitTouchSequence(slid));

		QVERIFY2(m_track->playingNote(from) == nullptr, "the key the finger left kept sounding");
		QVERIFY2(m_track->playingNote(to) != nullptr, "the key the finger reached did not sound");
		QVERIFY2(m_track->playingNote(other) != nullptr, "the other contact's note moved with the sliding one");
	}

	//! Two fingers on the same key are one sounding note, held until the last of them lifts.
	void twoContactsOnOneKeyHoldOneNote()
	{
		const int key = 60;
		const QPoint at = pointOfKey(key, true);

		auto both = QTest::touchEvent(m_view, m_device, false);
		both.press(0, at, m_view).press(1, at + QPoint(1, 0), m_view);
		QVERIFY(commitTouchSequence(both));
		QVERIFY(m_track->playingNote(key) != nullptr);
		QCOMPARE(m_view->touchHolders(key), 2);

		auto one = QTest::touchEvent(m_view, m_device, false);
		one.release(0, at, m_view).stationary(1);
		QVERIFY(commitTouchSequence(one));
		QVERIFY2(m_track->playingNote(key) != nullptr, "one of two fingers on a key released the note");
		QCOMPARE(m_view->touchHolders(key), 1);

		auto none = QTest::touchEvent(m_view, m_device, false);
		none.release(1, at + QPoint(1, 0), m_view);
		QVERIFY(commitTouchSequence(none));
		QVERIFY(m_track->playingNote(key) == nullptr);
	}

	//! The existing mouse input still plays and releases notes.
	void theMouseStillPlaysTheKeyboard()
	{
		const int key = 67;
		const QPoint at = pointOfKey(key, true);

		QTest::mousePress(m_view, Qt::LeftButton, Qt::NoModifier, at);
		QVERIFY2(m_track->playingNote(key) != nullptr, "a mouse press no longer plays a key");
		QVERIFY(m_track->pianoModel()->isKeyPressed(key));

		QTest::mouseRelease(m_view, Qt::LeftButton, Qt::NoModifier, at);
		QVERIFY2(m_track->playingNote(key) == nullptr, "a mouse release no longer ends the note");
		QVERIFY(!m_track->pianoModel()->isKeyPressed(key));
	}

	//! The existing computer-keyboard input still plays and releases notes.
	void theComputerKeyboardStillPlaysTheKeyboard()
	{
		// Z is C at the default octave: the same mapping the instrument window always had.
		const int key = DefaultOctave + Key::C - KeysPerOctave;

		QKeyEvent press(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier, scanCodeForZ(), 0, 0);
		QCoreApplication::sendEvent(m_view, &press);
		QVERIFY2(m_track->playingNote(key) != nullptr, "a computer-keyboard press no longer plays a key");

		QKeyEvent release(QEvent::KeyRelease, Qt::Key_Z, Qt::NoModifier, scanCodeForZ(), 0, 0);
		QCoreApplication::sendEvent(m_view, &release);
		QVERIFY2(m_track->playingNote(key) == nullptr, "a computer-keyboard release no longer ends the note");
	}

	//! Keys stay separately hit-testable, which is what lets two contacts land on different notes.
	void keysAreSeparatelyHitTestable()
	{
		for (const int key : {36, 48, 60, 64, 67, 72})
		{
			const QPoint at = pointOfKey(key, true);
			QCOMPARE(m_view->keyAt(at), key);
			QVERIFY(!m_view->touchContacts().isHolding());
		}
		// Two neighbouring white keys have distinct hit positions and distinct notes.
		const QPoint c = pointOfKey(60, true);
		const QPoint d = pointOfKey(62, true);
		QVERIFY(c != d);
		QCOMPARE(m_view->keyAt(c), 60);
		QCOMPARE(m_view->keyAt(d), 62);
	}

private:
	/*! A widget position a finger can press to sound @a key: the key's own drawn hit rectangle, low
	 *  on a white key (below the black keys, so the hit test is unambiguous) or high on a black one.
	 *  The test then checks that the surface's own hit test agrees before pressing - it presses
	 *  where the surface says the key is, not where a copy of its geometry thinks it should be. */
	QPoint pointOfKey(int key, bool lowOnKey) const
	{
		const QRect rect = m_view->keyRect(key);
		if (!rect.isValid() || rect.width() <= 0)
		{
			QTest::qFail(qPrintable(QStringLiteral("key %1 has no drawn rectangle").arg(key)), __FILE__, __LINE__);
			return {};
		}
		const QPoint at(rect.center().x(), lowOnKey ? rect.bottom() + 1 - 6 : rect.top() + 6);
		if (m_view->keyAt(at) != key)
		{
			QTest::qFail(qPrintable(QStringLiteral("the surface says key %1 is at %2,%3, not key %4")
				.arg(m_view->keyAt(at)).arg(at.x()).arg(at.y()).arg(key)), __FILE__, __LINE__);
			return {};
		}
		return at;
	}

	InstrumentTrack* m_track = nullptr;
	PianoView* m_view = nullptr;
	QPointingDevice* m_device = nullptr;
};

int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && qEnvironmentVariableIsEmpty("DISPLAY")
		&& qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}
	// The feature under test is the surface's own touch handling, not Qt's fallback that turns an
	// unhandled touch into a mouse click; the product accepts its touches, so the test turns the
	// fallback off for the whole run (the touch-layout suite's pattern).
	QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents, false);
	QApplication app(argc, argv);
	TouchInstrumentSurfaceTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "TouchInstrumentSurfaceTest.moc"
