/*
 * MixerZones.h - the mixer's dockable regions and their resizable separators
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

#ifndef LMMS_GUI_MIXER_ZONES_H
#define LMMS_GUI_MIXER_ZONES_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms::gui::mixerzones
{

/*! Q050-227.1 (0.5.0 L08, MX-007): the mixer's four regions and where each one is docked.
 *
 *  The mixer body is one horizontal splitter holding, from left to right, the regions docked LEFT,
 *  the scrolling channel area, then the regions docked RIGHT. Each boundary is a separator the user
 *  drags. `channels` is the middle dock and never moves (it is the part that scrolls); `master`,
 *  `vca` and `rack` dock left or right of it.
 *
 *  The state is PORTABLE, not a Qt splitter blob: a region id -> dock name, and a region id -> width
 *  in logical pixels. A layout saved by one Qt version therefore opens in another. A document that
 *  does not carry the object (a project from before this feature, a personal layout saved by an
 *  older build) loads the default arrangement, which is the one the mixer has always had: master
 *  left, channels in the middle, the VCA strips and the effect rack to the right.
 *
 *  Interface state, kept with the shell layout (`ui/shell.layout` and the project's own layout,
 *  window.layout_project_*): the project's layout is what saves and restores it, through the one
 *  undoable window.layout_project_save step - no separate project section, no second format. */
struct Layout
{
	//! region id ("master", "channels", "vca", "rack") -> dock name ("left", "middle", "right")
	QHash<QString, QString> docks;
	//! region id -> width in logical pixels; a missing or unusable value falls back to defaultSize().
	QHash<QString, int> sizes;
};

//! The four regions, in the canonical order a side's regions are laid out in.
LMMS_EXPORT const QStringList& regionIds();
//! The regions that may dock left or right: master, vca, rack.
LMMS_EXPORT const QStringList& sideRegionIds();
//! "left", "middle", "right".
LMMS_EXPORT const QStringList& dockNames();

LMMS_EXPORT bool isRegion(const QString& id);
//! A region's name for a menu or a tooltip ("Master Strip", "Channels", "VCA Strips", "Effect Rack").
LMMS_EXPORT QString regionTitle(const QString& id);
//! A dock's name for a menu ("Left", "Right").
LMMS_EXPORT QString dockTitle(const QString& dock);

//! What a mixer with no stored layout shows: master left, channels middle, vca and rack right.
LMMS_EXPORT Layout defaultLayout();
//! The width a region gets when no usable size was stored.
LMMS_EXPORT int defaultSize(const QString& id);

//! The portable form a layout is saved as: {"zones": {id: dock}, "sizes": {id: pixels}}.
LMMS_EXPORT QJsonObject toJson(const Layout& layout);
//! Read a stored object tolerantly: unknown ids are ignored, an unusable dock or size falls back to
//! the default for that region, and a missing object is the default layout. Never fails.
LMMS_EXPORT Layout fromJson(const QJsonObject& doc);

/*! Merge a caller's arguments into @a base, strictly: 'zones' must name known regions and their
 *  docks ('channels' only "middle"), 'sizes' must name known regions with a width between
 *  MinZoneSize and MaxZoneSize. False, with @a error, on anything else; @a base is untouched then. */
LMMS_EXPORT bool parse(const QJsonObject& args, Layout* base, QString* error);

//! The width bounds a caller may set, in logical pixels.
constexpr int MinZoneSize = 24;
constexpr int MaxZoneSize = 4000;

/*! The one live layout. The mixer view renders it and writes its last drag back through setSizes();
 *  the shell's layout snapshot saves toJson(layout()) and restores it through set(). A change of DOCKS
 *  emits changed() (the view re-lays its splitter at once); a change of SIZES only emits sizesChanged().
 *  GUI thread only. */
class LMMS_EXPORT MixerZoneState : public QObject
{
	Q_OBJECT
public:
	static MixerZoneState* instance();

	const Layout& layout() const { return m_layout; }
	//! Take @a layout (a caller's change or a restored layout) and tell the view to re-lay itself.
	void set(const Layout& layout);
	//! The widths a separator drag left behind; they are saved, but do not re-lay the tree.
	void setSizes(const QHash<QString, int>& sizes);
	//! Back to defaultLayout().
	void reset();

signals:
	void changed();
	void sizesChanged();

private:
	MixerZoneState();
	Layout m_layout;
};

} // namespace lmms::gui::mixerzones

#endif // LMMS_GUI_MIXER_ZONES_H
