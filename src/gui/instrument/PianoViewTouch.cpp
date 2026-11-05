/*
 * PianoViewTouch.cpp - the piano keyboard as a multi-touch performance surface
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

/*! 0.5.0 L14 (Q050-321.1): multiple fingers play the keyboard at once, each its own note.
 *
 *  The bookkeeping is TouchContacts (src/gui/TouchContacts.cpp); this file is the PianoView
 *  half of it: which key a finger is on, how hard its press is, and the note-on / note-off
 *  pairs handed to the track's existing MIDI input path - the same calls the mouse and the
 *  computer keyboard make, so both keep working exactly as before. A finger's note lasts
 *  until that finger lifts; releasing or cancelling one finger never releases another's.
 *
 *  This lives beside PianoView.cpp rather than inside it for two reasons: the inherited
 *  handler file is already over the file-length ratchet in the whole-tree scope (and must not
 *  grow), and the touch path has its own Qt 5.15 / Qt 6 event spellings. Nothing here runs on
 *  the audio thread - it is QWidget event handling on the GUI thread, and the notes go out
 *  through the very same processInEvent / setKeyState calls the mouse handler uses. */

#include <QMouseEvent>
#include <QTouchEvent>

#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "Piano.h"
#include "PianoView.h"

namespace lmms::gui
{

namespace
{

//! The finger's position in this widget, in both Qt's touch-point spellings (5.15 and 6).
QPoint touchPos(const QWidget* widget,
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	const QEventPoint& point
#else
	const QTouchEvent::TouchPoint& point
#endif
)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return widget->mapFromGlobal(point.globalPosition().toPoint());
#else
	return widget->mapFromGlobal(point.screenPos().toPoint());
#endif
}

//! QTouchEvent exposed touchPoints() through Qt 5.15; Qt 6 moved the list to points().
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
const QList<QEventPoint>& touchPoints(const QTouchEvent* event) { return event->points(); }
#else
const QList<QTouchEvent::TouchPoint>& touchPoints(const QTouchEvent* event) { return event->touchPoints(); }
#endif

//! A touch point's state, as the surface's own phases. Qt 5.15 reports Qt::TouchPointState and
//! Qt 6 reports QEventPoint::State; both spell the same values, so the plain enum value works in
//! one switch without a version-dependent type name.
TouchContacts::Contact::Phase phaseOf(int state)
{
	switch (state)
	{
	case Qt::TouchPointPressed: return TouchContacts::Contact::Phase::Pressed;
	case Qt::TouchPointMoved: return TouchContacts::Contact::Phase::Moved;
	case Qt::TouchPointReleased: return TouchContacts::Contact::Phase::Released;
	default: return TouchContacts::Contact::Phase::Stationary;
	}
}

//! The contacts of one event, in the widget's own coordinates.
std::vector<TouchContacts::Contact> contactsOf(const PianoView* view, const QTouchEvent* event)
{
	std::vector<TouchContacts::Contact> contacts;
	contacts.reserve(static_cast<std::size_t>(touchPoints(event).size()));
	for (const auto& point : touchPoints(event))
	{
		contacts.push_back({point.id(), touchPos(view, point), phaseOf(static_cast<int>(point.state()))});
	}
	return contacts;
}

bool isTouch(QEvent::Type type)
{
	return type == QEvent::TouchBegin || type == QEvent::TouchUpdate || type == QEvent::TouchEnd
		|| type == QEvent::TouchCancel;
}

} // namespace

void PianoView::wireTouch()
{
	if (m_touchWired) { return; }
	m_touchWired = true;
	// A widget must accept touch events to be sent them at all. The inherited constructor is left
	// as it was (it is over the whole-tree file-length ratchet): the first event an instance sees
	// is long before the first finger, so turning the attribute on here is early enough.
	setAttribute(Qt::WA_AcceptTouchEvents, true);
	m_touchContacts.setSurface(
		[this](const QPoint& position) { return keyAt(position); },
		[this](const QPoint& position, int key) { return touchVelocity(position, key); });
	m_touchContacts.setSink(
		[this](int, int key, int velocity) {
			if (m_piano == nullptr) { return; }
			// The mouse handler's own note-on path: the track's MIDI input, then the drawn key.
			m_piano->midiEventProcessor()->processInEvent(MidiEvent(MidiNoteOn, -1, key, velocity));
			m_piano->setKeyState(key, true);
			emit keyPressed(key);
			update();
		},
		[this](int, int key) {
			if (m_piano == nullptr) { return; }
			m_piano->midiEventProcessor()->processInEvent(MidiEvent(MidiNoteOff, -1, key, 0));
			m_piano->setKeyState(key, false);
			update();
		});
}

int PianoView::touchVelocity(const QPoint& position, int key) const
{
	if (m_piano == nullptr) { return 0; }
	// The mouse handler's own scaling: lower on the key is louder, within the key's height.
	const int height = getKeyHeight(key);
	const int base = m_piano->instrumentTrack()->midiPort()->baseVelocity();
	const int yDiff = position.y() - KeyAreaTop;
	if (yDiff > height) { return base; }
	// The mouse handler's own clamp order: a press above the key area is silent, one at or below the
	// key's bottom edge is full velocity (the mouse path clamps to 0 first, then to the maximum).
	if (yDiff <= 0) { return 0; }
	return static_cast<int>(static_cast<float>(yDiff) / height * base);
}

QRect PianoView::keyRect(int key) const
{
	return QRect(getKeyX(key), KeyAreaTop, getKeyWidth(key), getKeyHeight(key));
}

void PianoView::releaseTouchNotes()
{
	m_touchContacts.releaseAll();
	update();
}

bool PianoView::event(QEvent* event)
{
	wireTouch();
	if (isTouch(event->type()))
	{
		auto* touch = static_cast<QTouchEvent*>(event);
		m_touchContacts.handle(event->type(), contactsOf(this, touch));
		event->accept();
		return true;
	}
	if (event->type() == QEvent::FocusOut)
	{
		// The inherited handler notes off every key when the view really loses focus; the touch
		// contacts are dropped here as well, so a finger the platform stops reporting cannot leave
		// a note behind. A note-off for a key that is not sounding is ignored by the track.
		releaseTouchNotes();
	}
	else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
		|| event->type() == QEvent::MouseMove)
	{
		// Qt turns a touch sequence no widget accepted into a mouse press. The keyboard accepts its
		// touches, so such an event is never a finger that is not already a contact - playing it
		// again would sound a second note for the same finger.
		if (static_cast<QMouseEvent*>(event)->source() == Qt::MouseEventSynthesizedByQt) { return true; }
	}
	return QWidget::event(event);
}

} // namespace lmms::gui
