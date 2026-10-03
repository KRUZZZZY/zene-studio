/*
 * TrackKeyNames.h - per-track key names (a drum kit's labels), saved with the project
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

#ifndef LMMS_TRACK_KEY_NAMES_H
#define LMMS_TRACK_KEY_NAMES_H

#include <QHash>
#include <QString>

#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 L07 (key labels, drum lane view): a name per key of a track - "Kick", "Snare" - that the
 *  note editor's keyboard and its drum view show instead of C3. Kept by Track::id(), saved as
 *  <key-names> (only when any is set; a project without the element loads with none). UI
 *  thread; edited through key_names.*, each edit one undo step. */
class LMMS_EXPORT TrackKeyNames : public ProjectSection
{
public:
	static TrackKeyNames& instance();

	//! The name of @a key on track @a trackId, or empty.
	QString name(int trackId, int key) const;
	QHash<int, QString> names(int trackId) const { return m_names.value(trackId); }
	void setName(int trackId, int key, const QString& name);
	void setNames(int trackId, const QHash<int, QString>& names);
	//! The General MIDI drum names (keys 35..81) as a map, for the drum preset.
	static QHash<int, QString> generalMidiDrums();

	QString elementName() const override;
	bool shouldPersist() const override;
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override { m_names.clear(); }

private:
	TrackKeyNames() = default;
	QHash<int, QHash<int, QString>> m_names;
};

} // namespace lmms

#endif // LMMS_TRACK_KEY_NAMES_H
