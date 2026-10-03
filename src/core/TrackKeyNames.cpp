/*
 * TrackKeyNames.cpp - per-track key names (a drum kit's labels), saved with the project
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

#include "TrackKeyNames.h"

#include <QDomDocument>
#include <QDomElement>

#include "Engine.h"
#include "PatternStore.h"
#include "Song.h"

namespace lmms
{

namespace
{

//! A deleted track's names stay in memory (undoing the delete brings the track back with its id),
//! but are not written: the file carries names only for tracks it carries (review L06-L07a #11).
bool trackExists(int id)
{
	return Engine::getSong()->findTrackById(id) != nullptr
		|| (Engine::patternStore() != nullptr && Engine::patternStore()->findTrackById(id) != nullptr);
}

} // namespace

TrackKeyNames& TrackKeyNames::instance()
{
	static TrackKeyNames* names = [] {
		auto* made = new TrackKeyNames();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *names;
}

QString TrackKeyNames::name(int trackId, int key) const
{
	return m_names.value(trackId).value(key);
}

void TrackKeyNames::setName(int trackId, int key, const QString& name)
{
	if (name.isEmpty())
	{
		m_names[trackId].remove(key);
		if (m_names[trackId].isEmpty()) { m_names.remove(trackId); }
		return;
	}
	m_names[trackId][key] = name;
}

void TrackKeyNames::setNames(int trackId, const QHash<int, QString>& names)
{
	if (names.isEmpty()) { m_names.remove(trackId); } else { m_names[trackId] = names; }
}

QHash<int, QString> TrackKeyNames::generalMidiDrums()
{
	static const char* const Names[] = {"Acoustic Bass Drum", "Bass Drum 1", "Side Stick", "Acoustic Snare", "Hand Clap",
		"Electric Snare", "Low Floor Tom", "Closed Hi-Hat", "High Floor Tom", "Pedal Hi-Hat", "Low Tom", "Open Hi-Hat",
		"Low-Mid Tom", "Hi-Mid Tom", "Crash Cymbal 1", "High Tom", "Ride Cymbal 1", "Chinese Cymbal", "Ride Bell",
		"Tambourine", "Splash Cymbal", "Cowbell", "Crash Cymbal 2", "Vibraslap", "Ride Cymbal 2", "Hi Bongo", "Low Bongo",
		"Mute Hi Conga", "Open Hi Conga", "Low Conga", "High Timbale", "Low Timbale", "High Agogo", "Low Agogo", "Cabasa",
		"Maracas", "Short Whistle", "Long Whistle", "Short Guiro", "Long Guiro", "Claves", "Hi Wood Block",
		"Low Wood Block", "Mute Cuica", "Open Cuica", "Mute Triangle", "Open Triangle"};
	// MIDI note 35 is the first; the engine's key 0 is MIDI note 12 (C0), so key = note - 12.
	QHash<int, QString> out;
	int midi = 35;
	for (const char* label : Names) { out.insert(midi++ - 12, QString::fromLatin1(label)); }
	return out;
}

bool TrackKeyNames::shouldPersist() const
{
	for (auto it = m_names.begin(); it != m_names.end(); ++it)
	{
		if (trackExists(it.key())) { return true; }
	}
	return false;
}

QString TrackKeyNames::elementName() const
{
	return QStringLiteral("key-names");
}

void TrackKeyNames::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement root = doc.createElement(elementName());
	for (auto track = m_names.begin(); track != m_names.end(); ++track)
	{
		if (!trackExists(track.key())) { continue; }
		QDomElement trackElement = doc.createElement(QStringLiteral("track"));
		trackElement.setAttribute(QStringLiteral("id"), track.key());
		for (auto key = track.value().begin(); key != track.value().end(); ++key)
		{
			QDomElement keyElement = doc.createElement(QStringLiteral("key"));
			keyElement.setAttribute(QStringLiteral("key"), key.key());
			keyElement.setAttribute(QStringLiteral("name"), key.value());
			trackElement.appendChild(keyElement);
		}
		root.appendChild(trackElement);
	}
	parent.appendChild(root);
}

void TrackKeyNames::load(const QDomElement& element)
{
	m_names.clear();
	for (QDomElement track = element.firstChildElement(QStringLiteral("track")); !track.isNull();
		track = track.nextSiblingElement(QStringLiteral("track")))
	{
		bool ok = false;
		const int id = track.attribute(QStringLiteral("id")).toInt(&ok);
		if (!ok) { continue; }  // no id is no track: never track 0 by default (review L19 #14)
		for (QDomElement key = track.firstChildElement(QStringLiteral("key")); !key.isNull(); key = key.nextSiblingElement(QStringLiteral("key")))
		{
			setName(id, key.attribute(QStringLiteral("key")).toInt(), key.attribute(QStringLiteral("name")));
		}
	}
}

} // namespace lmms
