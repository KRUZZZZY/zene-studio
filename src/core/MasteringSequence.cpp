/*
 * MasteringSequence.cpp - the ordered release sequence of a mastering document
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
/*! 0.5.0 L12 (Q050-317.1): the model behind the mastering release page. The load pass is
 *  defensive: a hand-edited file must not put an id in twice, must not read a missing title as
 *  an error, and must not let a fade or a gap outside the page's bounds through. A song whose
 *  source is not a string at all is dropped - there is nothing to master. */

#include "MasteringSequence.h"

#include <algorithm>

#include <QDomElement>
#include <QFileInfo>

namespace lmms
{

MasteringSequence& MasteringSequence::instance()
{
	static MasteringSequence* sequence = [] {
		auto* made = new MasteringSequence();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *sequence;
}

const MasteringSong* MasteringSequence::find(int id) const
{
	const auto found = std::find_if(m_songs.begin(), m_songs.end(), [id](const MasteringSong& song) { return song.id == id; });
	return found != m_songs.end() ? &*found : nullptr;
}

int MasteringSequence::indexOf(int id) const
{
	for (int index = 0; index < m_songs.size(); ++index)
	{
		if (m_songs.at(index).id == id) { return index; }
	}
	return -1;
}

int MasteringSequence::add(const MasteringSong& song)
{
	if (m_songs.size() >= MaxSongs || song.source.isEmpty()) { return -1; }
	MasteringSong added = song;
	added.id = m_nextId++;
	m_songs.append(added);
	return added.id;
}

bool MasteringSequence::replace(int id, const MasteringSong& song)
{
	for (MasteringSong& each : m_songs)
	{
		if (each.id != id) { continue; }
		each = song;
		each.id = id;  // the address never changes with an edit
		return true;
	}
	return false;
}

bool MasteringSequence::remove(int id)
{
	const qsizetype before = m_songs.size();
	m_songs.erase(std::remove_if(m_songs.begin(), m_songs.end(), [id](const MasteringSong& song) { return song.id == id; }),
		m_songs.end());
	return m_songs.size() != before;
}

bool MasteringSequence::move(int id, int to)
{
	const int from = indexOf(id);
	if (from < 0) { return false; }
	const int target = std::clamp(to, 0, static_cast<int>(m_songs.size()) - 1);
	if (target == from) { return false; }
	const MasteringSong song = m_songs.at(from);
	m_songs.remove(from);
	m_songs.insert(target, song);
	return true;
}

void MasteringSequence::restore(const QVector<MasteringSong>& songs)
{
	m_songs = songs;
	for (const MasteringSong& song : m_songs) { m_nextId = std::max(m_nextId, song.id + 1); }
}

void MasteringSequence::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement list = doc.createElement(elementName());
	for (const MasteringSong& song : m_songs)
	{
		QDomElement element = doc.createElement(QStringLiteral("song"));
		element.setAttribute(QStringLiteral("id"), song.id);
		element.setAttribute(QStringLiteral("source"), song.source);
		if (!song.title.isEmpty()) { element.setAttribute(QStringLiteral("title"), song.title); }
		if (song.processing != QLatin1String(NoProcessing)) { element.setAttribute(QStringLiteral("processing"), song.processing); }
		if (song.fadeInMs != 0) { element.setAttribute(QStringLiteral("fade-in-ms"), song.fadeInMs); }
		if (song.fadeOutMs != 0) { element.setAttribute(QStringLiteral("fade-out-ms"), song.fadeOutMs); }
		if (song.gapMs != 0) { element.setAttribute(QStringLiteral("gap-ms"), song.gapMs); }
		list.appendChild(element);
	}
	parent.appendChild(list);
}

namespace
{

//! A bounded millisecond field: absent is 0, out of range is clamped (a hand-edited file still loads).
int boundedMs(const QDomElement& element, const char* name, int maximum)
{
	const QString text = element.attribute(QLatin1String(name));
	if (text.isEmpty()) { return 0; }
	bool ok = false;
	const int value = text.toInt(&ok);
	return ok ? std::clamp(value, 0, maximum) : 0;
}

} // namespace

void MasteringSequence::load(const QDomElement& element)
{
	m_songs.clear();
	for (QDomElement child = element.firstChildElement(QStringLiteral("song")); !child.isNull();
		child = child.nextSiblingElement(QStringLiteral("song")))
	{
		if (m_songs.size() >= MaxSongs) { break; }
		const QString source = child.attribute(QStringLiteral("source"));
		if (source.isEmpty()) { continue; }  // no audio, no song
		MasteringSong song;
		song.source = source;
		song.title = child.attribute(QStringLiteral("title"));
		const QString processing = child.attribute(QStringLiteral("processing"));
		song.processing = processing.isEmpty() ? QLatin1String(NoProcessing) : processing;
		song.fadeInMs = boundedMs(child, "fade-in-ms", MaxFadeMs);
		song.fadeOutMs = boundedMs(child, "fade-out-ms", MaxFadeMs);
		song.gapMs = boundedMs(child, "gap-ms", MaxGapMs);
		// The id is an ADDRESS: a duplicate, a zero or a malformed one is re-assigned, so a
		// hand-edited file can never make two songs answer to one id.
		bool idOk = false;
		const int id = child.attribute(QStringLiteral("id")).toInt(&idOk);
		song.id = idOk && id > 0 && indexOf(id) < 0 ? id : m_nextId;
		m_songs.append(song);
		m_nextId = std::max(m_nextId, song.id + 1);
	}
}

void MasteringSequence::clear()
{
	m_songs.clear();
	m_nextId = 1;
}

} // namespace lmms
