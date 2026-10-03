/*
 * ProjectMarkers.h - the project's markers: named, coloured positions on the timeline
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

#ifndef LMMS_PROJECT_MARKERS_H
#define LMMS_PROJECT_MARKERS_H

#include <QObject>
#include <QString>
#include <QVector>

#include "LmmsTypes.h"
#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

//! One marker: a stable id ("mk-<id>" on the wire), a tick, a name and an optional colour.
struct Marker
{
	int id = 0;
	tick_t tick = 0;
	QString name;
	QString color;  //!< "#rrggbb", or empty
	//! 0.5.0 L02 (WF-073): a command run when playback crosses the marker (empty: none), with its
	//! arguments as compact JSON. Run on the main thread (MarkerActions.cpp), never the audio thread.
	QString action;
	QString args;
};

/*! ED-048 (0.5.0): the project's markers - drawn in the arranger's ruler, jumped to, snapped to,
 *  saved with the project as <markers> (only when there is one, so a project that never used
 *  them re-saves byte for byte; an absent element loads as none). UI-thread state, edited only
 *  through the marker.* commands, which record each edit as one undo step. */
class LMMS_EXPORT ProjectMarkers : public QObject, public ProjectSection
{
	Q_OBJECT
public:
	static constexpr int MaxMarkers = 512;

	//! The one instance; registered as a ProjectSection the first time it is asked for.
	static ProjectMarkers& instance();

	//! Every marker, ordered by tick (then id).
	const QVector<Marker>& markers() const { return m_markers; }
	const Marker* find(int id) const;
	//! The first marker after / the last before @a tick, or nullptr.
	const Marker* next(tick_t tick) const;
	const Marker* previous(tick_t tick) const;

	//! Add a marker; its id, or -1 when MaxMarkers are there already.
	int add(tick_t tick, const QString& name, const QString& color);
	bool remove(int id);
	//! Replace marker @a id with @a marker (its id kept).
	bool replace(int id, const Marker& marker);
	//! Put a whole list back (undo/redo).
	void restore(const QVector<Marker>& markers);

	QString elementName() const override;
	bool shouldPersist() const override { return !m_markers.isEmpty(); }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override;

signals:
	void changed();

private:
	ProjectMarkers() = default;
	void sortAndNotify();

	QVector<Marker> m_markers;
	int m_nextId = 1;
};

} // namespace lmms

#endif // LMMS_PROJECT_MARKERS_H
