/*
 * TouchContacts.h - the fingers of a multi-touch performance surface, each an independent note
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

#ifndef LMMS_GUI_TOUCH_CONTACTS_H
#define LMMS_GUI_TOUCH_CONTACTS_H

#include <functional>
#include <map>
#include <vector>

#include <QEvent>
#include <QPoint>

#include "lmms_export.h"

namespace lmms::gui
{

/*! 0.5.0 L14 (Q050-321.1): the fingers of a multi-touch performance surface, each an independent
 *  note gate. The surface says which key lies under a position (and how hard a press there is);
 *  every contact keeps its own key, and a key sounds until the LAST contact holding it lifts - so
 *  two fingers play two notes, and releasing or cancelling one never releases another's note.
 *
 *  This is the whole touch bookkeeping of the instrument's keyboard and (later) of the drum pads;
 *  a widget feeds it one QTouchEvent at a time and connects the sink to the track's existing MIDI
 *  input path - the same path the mouse and the computer keyboard use.
 *
 *  GUI thread only: it is driven by a QWidget's touch events, so nothing here runs on the audio
 *  thread. The sink is expected to hand each note to the track there and then (as the mouse does),
 *  which is transient MIDI input and needs no project undo. */
class LMMS_EXPORT TouchContacts
{
public:
	//! One finger as the platform reported it in one event.
	struct Contact
	{
		enum class Phase
		{
			Pressed,   //!< a finger went down
			Moved,     //!< a held finger moved
			Stationary,//!< a held finger did not move
			Released   //!< a finger lifted
		};

		int id = -1;
		QPoint position;
		Phase phase = Phase::Stationary;
	};

	//! The key under a surface position, or a negative value where nothing sounds.
	using KeyAt = std::function<int(const QPoint&)>;
	//! The velocity a press at a position gives the key under it.
	using VelocityAt = std::function<int(const QPoint&, int key)>;
	//! A contact starts sounding @a key (called only for the first contact on that key).
	using NoteOn = std::function<void(int contact, int key, int velocity)>;
	//! A contact's key stops sounding (called only when its last contact lifts).
	using NoteOff = std::function<void(int contact, int key)>;

	void setSurface(KeyAt keyAt, VelocityAt velocityAt);
	void setSink(NoteOn noteOn, NoteOff noteOff);

	/*! Handles the contacts of one touch event of @a type (QEvent::TouchBegin, TouchUpdate,
	 *  TouchEnd or TouchCancel). A TouchEnd is the end of the whole sequence, so every contact
	 *  still on the surface is released: a note never hangs when the platform's list is short.
	 *  TouchCancel releases everything the same way. Returns true when @a contacts is not empty. */
	bool handle(QEvent::Type type, const std::vector<Contact>& contacts);

	//! Releases every held contact, reporting each note-off: a cancelled sequence or a lost focus.
	void releaseAll();
	/*! Releases ONE contact and leaves every other contact sounding. Qt has no per-contact cancel
	 *  event (TouchCancel cancels the whole sequence, which releaseAll() handles), so this is the
	 *  seam a surface uses when it decides a single finger is no longer playing - and what makes
	 *  "cancelling one contact leaves the others held" true of the model itself. */
	void cancel(int contact);

	bool isHolding() const { return !m_contacts.empty(); }
	int heldContacts() const { return static_cast<int>(m_contacts.size()); }
	//! The key @a contact is sounding, or -1.
	int keyOf(int contact) const;
	//! How many contacts hold @a key.
	int contactsOn(int key) const;

	//! The note velocity of a press where the surface does not scale it (MIDI is 1..127).
	static constexpr int DefaultVelocity = 100;

private:
	struct Held
	{
		int key = -1;
		int velocity = 0;
	};

	int keyUnder(const QPoint& position) const;
	int velocityAt(const QPoint& position, int key, int fallback) const;
	void hold(int key, int contact, int velocity);
	void unhold(int key, int contact);
	void press(int contact, const QPoint& position);
	void release(int contact);
	void moveTo(int contact, const QPoint& position);

	KeyAt m_keyAt;
	VelocityAt m_velocityAt;
	NoteOn m_noteOn;
	NoteOff m_noteOff;
	std::map<int, Held> m_contacts;	//!< contact id -> the note it sounds
	std::map<int, int> m_holders;	//!< key -> how many contacts hold it
};

} // namespace lmms::gui

#endif // LMMS_GUI_TOUCH_CONTACTS_H
