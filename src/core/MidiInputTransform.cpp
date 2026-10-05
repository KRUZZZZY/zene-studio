/*
 * MidiInputTransform.cpp - a track's transform of live MIDI input: channel, key range, transpose, scale snap, velocity
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

#include "MidiInputTransform.h"

#include <algorithm>

#include <QDomElement>
#include <QJsonDocument>

#include "MidiEvent.h"

namespace lmms
{

namespace
{

constexpr int NotHeld = -2;
constexpr int Dropped = -1;
constexpr auto Relaxed = std::memory_order_relaxed;

//! The value \a key carries in \a json, or \a fallback when it is not named -
//! so a pair whose other end is already in force is checked against it.
int namedInt(const QJsonObject& json, const char* key, int fallback)
{
	return json.contains(QLatin1String(key)) ? json.value(QLatin1String(key)).toInt() : fallback;
}

} // namespace

MidiInputTransform::MidiInputTransform()
{
	for (auto& held : m_heldAs) { held.store(NotHeld, Relaxed); }
}

bool MidiInputTransform::apply(MidiEvent* event)
{
	switch (event->type())
	{
	case MidiNoteOn: return event->velocity() > 0 ? noteOn(event) : follow(event, true);
	case MidiNoteOff: return follow(event, true);
	case MidiKeyPressure: return follow(event, false);
	default: return passesChannel(*event);
	}
}

bool MidiInputTransform::passesChannel(const MidiEvent& event) const
{
	const int channel = m_channel.load(Relaxed);
	return channel == 0 || event.channel() + 1 == channel;
}

std::atomic<int>* MidiInputTransform::heldFor(const MidiEvent& event)
{
	const int key = event.key();
	const int channel = event.channel();
	if (key < 0 || key > 127 || channel < 0 || channel > 15) { return nullptr; }
	return &m_heldAs[static_cast<std::size_t>(channel * 128 + key)];
}

bool MidiInputTransform::noteOn(MidiEvent* event)
{
	std::atomic<int>* held = heldFor(*event);
	if (held == nullptr) { return passesChannel(*event); }
	const int key = event->key();
	if (!passesChannel(*event) || key < m_keyLow.load(Relaxed) || key > m_keyHigh.load(Relaxed))
	{
		// A repeated note-on that is dropped must not overwrite the key a sounding one was given,
		// or that note's note-off would be dropped with it and the note would hang.
		int notHeld = NotHeld;
		held->compare_exchange_strong(notHeld, Dropped, Relaxed);
		return false;
	}
	const int out = snapped(std::clamp(key + m_transpose.load(Relaxed), 0, 127));
	held->store(out, Relaxed);
	event->setKey(static_cast<int16_t>(out));
	event->setVelocity(static_cast<int16_t>(clampedVelocity(event->velocity())));
	return true;
}

bool MidiInputTransform::follow(MidiEvent* event, bool ends)
{
	// A note-off (or pressure) goes to the key its note-on (on the same channel) was given, whatever
	// the channel filter is now; one whose note-on came before the transform was set passes unchanged
	// when its channel does.
	std::atomic<int>* held = heldFor(*event);
	const int as = held != nullptr ? held->load(Relaxed) : NotHeld;
	if (as == NotHeld) { return passesChannel(*event); }
	if (ends) { held->store(NotHeld, Relaxed); }
	if (as == Dropped) { return false; }
	event->setKey(static_cast<int16_t>(as));
	return true;
}

int MidiInputTransform::clampedVelocity(int velocity) const
{
	const int fixed = m_fixedVelocity.load(Relaxed);
	if (fixed > 0) { return fixed; }
	return std::clamp(velocity * m_velocityScale.load(Relaxed) / 100 + m_velocityOffset.load(Relaxed), 1, 127);
}

int MidiInputTransform::snapped(int key) const
{
	// The nearest key whose pitch class is in the mask, the lower one on a tie.
	const int mask = m_scaleMask.load(Relaxed);
	if (mask == 0) { return key; }
	for (int d = 0; d < 12; ++d)
	{
		if (key - d >= 0 && (mask & (1 << ((key - d) % 12))) != 0) { return key - d; }
		if (key + d <= 127 && (mask & (1 << ((key + d) % 12))) != 0) { return key + d; }
	}
	return key;
}

auto MidiInputTransform::settings() -> const std::array<Setting, 8>&
{
	static const std::array<Setting, 8> table{{
		{"channel", &MidiInputTransform::m_channel, 0, 16, 0},
		{"key_low", &MidiInputTransform::m_keyLow, 0, 127, 0},
		{"key_high", &MidiInputTransform::m_keyHigh, 0, 127, 127},
		{"transpose", &MidiInputTransform::m_transpose, -48, 48, 0},
		{"scale_mask", &MidiInputTransform::m_scaleMask, 0, 4095, 0},
		{"velocity_scale", &MidiInputTransform::m_velocityScale, 1, 400, 100},
		{"velocity_offset", &MidiInputTransform::m_velocityOffset, -127, 127, 0},
		{"fixed_velocity", &MidiInputTransform::m_fixedVelocity, 0, 127, 0},
	}};
	return table;
}

QJsonObject MidiInputTransform::toJson() const
{
	QJsonObject out;
	for (const Setting& s : settings()) { out.insert(QLatin1String(s.key), (this->*s.field).load(Relaxed)); }
	out.insert(QStringLiteral("active"), active());
	return out;
}

bool MidiInputTransform::fromJson(const QJsonObject& json, QString* error)
{
	// Checked whole first: one bad value changes nothing.
	for (const Setting& s : settings())
	{
		const QJsonValue value = json.value(QLatin1String(s.key));
		if (value.isUndefined()) { continue; }
		if (!value.isDouble() || value.toInt() < s.minimum || value.toInt() > s.maximum)
		{
			*error = QStringLiteral("'%1' is an integer in %2..%3").arg(QLatin1String(s.key)).arg(s.minimum).arg(s.maximum);
			return false;
		}
	}
	// The two ends are a RANGE, checked one value at a time: 100 and 20 are each
	// a legal key, and together they hold no key at all, so every note-on fails
	// the range test in noteOn() and the instrument goes silent. The ordering is
	// part of the range, and - like any other bad value - it is checked before
	// anything is stored, against whichever end the request does not name.
	if (namedInt(json, "key_low", m_keyLow.load(Relaxed))
		> namedInt(json, "key_high", m_keyHigh.load(Relaxed)))
	{
		*error = QStringLiteral("'key_low' must not be above 'key_high': the range would "
			"hold no key and every note would be dropped");
		return false;
	}
	for (const Setting& s : settings())
	{
		if (json.contains(QLatin1String(s.key))) { (this->*s.field).store(json.value(QLatin1String(s.key)).toInt(), Relaxed); }
	}
	updateActive();
	return true;
}

void MidiInputTransform::reset()
{
	for (const Setting& s : settings()) { (this->*s.field).store(s.identity, Relaxed); }
	updateActive();
}

void MidiInputTransform::updateActive()
{
	bool any = false;
	for (const Setting& s : settings()) { any = any || (this->*s.field).load(Relaxed) != s.identity; }
	m_active.store(any, Relaxed);
}

void MidiInputTransform::save(QDomElement& trackElement) const
{
	if (!active()) { return; }
	QJsonObject json = toJson();
	json.remove(QStringLiteral("active"));
	trackElement.setAttribute(QStringLiteral("inputtransform"), QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
}

void MidiInputTransform::load(const QDomElement& trackElement)
{
	// Only a project's track element carries it; an instrument preset, loaded through the same path,
	// does not, and must not reset what the user set on the track.
	const QString text = trackElement.attribute(QStringLiteral("inputtransform"));
	if (text.isEmpty()) { return; }
	reset();
	QString ignored;
	fromJson(QJsonDocument::fromJson(text.toUtf8()).object(), &ignored);
}

} // namespace lmms
