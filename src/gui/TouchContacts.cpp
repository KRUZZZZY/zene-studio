/*
 * TouchContacts.cpp - the fingers of a multi-touch performance surface, each an independent note
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

#include "TouchContacts.h"

#include <algorithm>

namespace lmms::gui
{

void TouchContacts::setSurface(KeyAt keyAt, VelocityAt velocityAt)
{
	m_keyAt = std::move(keyAt);
	m_velocityAt = std::move(velocityAt);
}

void TouchContacts::setSink(NoteOn noteOn, NoteOff noteOff)
{
	m_noteOn = std::move(noteOn);
	m_noteOff = std::move(noteOff);
}

bool TouchContacts::handle(QEvent::Type type, const std::vector<Contact>& contacts)
{
	// A TouchCancel has no points of its own: it ends the whole sequence, so it must be handled
	// even when the platform sent an empty list.
	if (type == QEvent::TouchCancel)
	{
		releaseAll();
		return true;
	}
	if (contacts.empty()) { return false; }

	for (const Contact& contact : contacts)
	{
		switch (contact.phase)
		{
		case Contact::Phase::Pressed:
			press(contact.id, contact.position);
			break;
		case Contact::Phase::Moved:
			moveTo(contact.id, contact.position);
			break;
		case Contact::Phase::Stationary:
			break;
		case Contact::Phase::Released:
			release(contact.id);
			break;
		}
	}

	// TouchEnd ends the whole sequence: a held contact the platform left out of the list (or
	// reported as stationary) must not keep its note sounding.
	if (type == QEvent::TouchEnd) { releaseAll(); }
	return true;
}

int TouchContacts::keyUnder(const QPoint& position) const
{
	return m_keyAt ? m_keyAt(position) : -1;
}

int TouchContacts::velocityAt(const QPoint& position, int key, int fallback) const
{
	return m_velocityAt ? m_velocityAt(position, key) : fallback;
}

void TouchContacts::hold(int key, int contact, int velocity)
{
	// The first contact on a key starts its note; the next one joins that note instead of starting
	// it again (the track keeps one voice per pitch, so a second note-on would be dropped anyway),
	// and the note then lasts until that contact lifts too.
	if (m_holders[key]++ == 0 && m_noteOn) { m_noteOn(contact, key, velocity); }
}

void TouchContacts::unhold(int key, int contact)
{
	const auto holders = m_holders.find(key);
	if (holders == m_holders.end()) { return; }
	if (--holders->second > 0) { return; }
	m_holders.erase(holders);
	if (m_noteOff) { m_noteOff(contact, key); }
}

void TouchContacts::press(int contact, const QPoint& position)
{
	if (m_contacts.count(contact) != 0) { return; }
	const int key = keyUnder(position);
	if (key < 0) { return; }
	const int velocity = velocityAt(position, key, DefaultVelocity);
	m_contacts[contact] = Held{key, velocity};
	hold(key, contact, velocity);
}

void TouchContacts::release(int contact)
{
	const auto it = m_contacts.find(contact);
	if (it == m_contacts.end()) { return; }
	const int key = it->second.key;
	m_contacts.erase(it);
	unhold(key, contact);
}

void TouchContacts::moveTo(int contact, const QPoint& position)
{
	const auto it = m_contacts.find(contact);
	if (it == m_contacts.end()) { return; }
	const int key = keyUnder(position);
	if (key < 0 || key == it->second.key) { return; }  // off the keys: sound on until the lift

	// A finger sliding from one key to the next moves ITS note: the old one ends (unless another
	// contact still holds it) and the new one starts (unless another contact already holds it).
	const int previous = it->second.key;
	it->second.key = key;
	it->second.velocity = velocityAt(position, key, it->second.velocity);
	unhold(previous, contact);
	hold(key, contact, it->second.velocity);
}

void TouchContacts::cancel(int contact)
{
	release(contact);
}

void TouchContacts::releaseAll()
{
	const std::map<int, Held> held = std::move(m_contacts);
	m_contacts.clear();
	m_holders.clear();
	if (!m_noteOff) { return; }
	// Every key once, in the order the contacts first held it: two contacts on one key are one
	// note and get one note-off.
	std::vector<int> keys;
	keys.reserve(held.size());
	for (const auto& [contact, note] : held)
	{
		if (std::find(keys.begin(), keys.end(), note.key) == keys.end()) { keys.push_back(note.key); }
	}
	for (int key : keys)
	{
		const auto owner = std::find_if(held.begin(), held.end(), [key](const auto& entry) { return entry.second.key == key; });
		m_noteOff(owner->first, key);
	}
}

int TouchContacts::keyOf(int contact) const
{
	const auto it = m_contacts.find(contact);
	return it == m_contacts.end() ? -1 : it->second.key;
}

int TouchContacts::contactsOn(int key) const
{
	const auto it = m_holders.find(key);
	return it == m_holders.end() ? 0 : it->second;
}

} // namespace lmms::gui
