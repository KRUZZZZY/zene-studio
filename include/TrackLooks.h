/*
 * TrackLooks.h - a track's icon, kept with the project
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

#ifndef LMMS_TRACK_LOOKS_H
#define LMMS_TRACK_LOOKS_H

#include <functional>
#include <utility>

#include <QHash>
#include <QString>

#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 W3 L13 (WF-120 track icons): an icon per track - one of the interface's icon names - that the
 *  track's header shows in place of its type's glyph. Kept by Track::id(), saved as <track-looks> only
 *  when a track that exists has one (a project that never set one re-saves byte for byte; a project
 *  without the element loads with none). UI thread; edited through track.set_icon, one undo step. */
class LMMS_EXPORT TrackLooks : public ProjectSection
{
public:
	static TrackLooks& instance();

	//! The icon of track @a trackId, or empty.
	QString icon(int trackId) const { return m_icons.value(trackId); }
	//! Give track @a trackId the icon @a icon (empty removes it) and tell the interface.
	void setIcon(int trackId, const QString& icon);
	//! What runs after every change - the interface repaints the track headers. One hook.
	void setOnChange(std::function<void()> hook) { m_onChange = std::move(hook); }
	//! Review W3-agent-custom #1: the interface's own test of an icon name (whether it can draw it), installed at
	//! start-up; without an interface every name is accepted, since nothing is drawn.
	void setIconCheck(std::function<bool(const QString&)> check) { m_iconCheck = std::move(check); }
	bool knowsIcon(const QString& name) const { return name.isEmpty() || !m_iconCheck || m_iconCheck(name); }

	QString elementName() const override;
	bool shouldPersist() const override;
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override;

private:
	TrackLooks() = default;
	void changed() const;

	QHash<int, QString> m_icons;
	std::function<void()> m_onChange;
	std::function<bool(const QString&)> m_iconCheck;
};

} // namespace lmms

#endif // LMMS_TRACK_LOOKS_H
