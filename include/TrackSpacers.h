/*
 * TrackSpacers.h - visual gaps below tracks in the track list (WF-107)
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

#ifndef LMMS_TRACK_SPACERS_H
#define LMMS_TRACK_SPACERS_H

#include <QHash>
#include <QObject>
#include <QString>

#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 F3 (WF-107): a spacer - a gap, with an optional label, drawn below a track in the track list -
 *  to group the list into sections without a track that does nothing. Not a Track type: a spacer has
 *  no clips, no device and no mixer channel, so it is a per-track attribute ("this much room below me")
 *  keyed by the track's persistent id (as EditLocks keys its locks), and the track list draws it as a
 *  band at the bottom of the track's row (TrackView::applySpacer). It moves with its track, and goes
 *  when its track does. Saved as <track-spacers> only when a live track has one, so a project that never
 *  used them re-saves byte for byte. UI-thread state, written through track.set_spacer. */
class LMMS_EXPORT TrackSpacers : public QObject, public ProjectSection
{
	Q_OBJECT
public:
	static constexpr int MaxHeight = 200;  //!< in unscaled pixels
	static constexpr int MaxLabelLength = 120;

	struct Spacer
	{
		int height = 0;  //!< 0: none
		QString label;
		bool operator==(const Spacer&) const = default;
	};

	static TrackSpacers& instance();

	Spacer spacer(int trackId) const { return m_spacers.value(trackId); }
	//! Set (height clamped into 0..MaxHeight; 0 removes it) and announce the change.
	void setSpacer(int trackId, const Spacer& spacer);
	//! The spacers of tracks that still exist, by track id.
	QHash<int, Spacer> live() const;

	QString elementName() const override { return QStringLiteral("track-spacers"); }
	bool shouldPersist() const override { return !live().isEmpty(); }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override;

signals:
	//! Any spacer changed - the track list lays itself out again.
	void changed();

private:
	TrackSpacers() = default;
	QHash<int, Spacer> m_spacers;
};

} // namespace lmms

#endif // LMMS_TRACK_SPACERS_H
