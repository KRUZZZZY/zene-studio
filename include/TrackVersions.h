/*
 * TrackVersions.h - named versions of a track's clips
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
#ifndef LMMS_TRACK_VERSIONS_H
#define LMMS_TRACK_VERSIONS_H

#include <QMap>
#include <QString>
#include <QStringList>

#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

class Track;

/*! 0.5.0 L04 (ED-049): track versions - a track keeps named versions of its clips (a verse with the old bass line, a
 *  scratch idea) and switches between them; the one switched away from is kept as it was left. Saved with the
 *  project as <track-versions> only while some track has versions (a project that never used them re-saves byte
 *  for byte), keyed by the tracks' saved ids. Interface thread. */
class LMMS_EXPORT TrackVersions : public ProjectSection
{
public:
	//! One track's versions: the active one's name and each version's clips as XML.
	struct Set
	{
		QString active;
		QMap<QString, QString> clips;
		bool operator==(const Set& other) const { return active == other.active && clips == other.clips; }
	};

	static TrackVersions& instance();

	//! By the track's saved id (Track::id), so an undo can hold the id instead of the track.
	Set of(int trackId) const;
	void set(int trackId, const Set& versions);
	//! The track's clips as XML, and the track's clips replaced by @a xml's.
	static QString clipsOf(Track* track);
	static void replaceClips(Track* track, const QString& xml);

	QString elementName() const override { return QStringLiteral("track-versions"); }
	bool shouldPersist() const override { return !m_sets.isEmpty(); }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override { m_sets.clear(); }

private:
	TrackVersions() = default;
	QMap<int, Set> m_sets;  //!< by track id
};

} // namespace lmms

#endif // LMMS_TRACK_VERSIONS_H
