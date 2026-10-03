/*
 * TrackIcons.cpp - a track's chosen icon on its header
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

#include "TrackIcons.h"

#include <QApplication>
#include <QToolButton>

#include "ShellIcons.h"
#include "Track.h"
#include "TrackLabelButton.h"
#include "TrackLooks.h"
#include "embed.h"

namespace lmms::gui
{

namespace
{

const std::string ChosenPrefix = "zene:";

//! The type glyph the track views give a track that is not an instrument (an instrument's logo is
//! TrackLabelButton's own business).
const char* typeIconName(Track::Type type)
{
	switch (type)
	{
	case Track::Type::Sample: return "sample_track";
	case Track::Type::Pattern: return "pattern_track";
	case Track::Type::Automation: return "automation_track";
	default: return nullptr;
	}
}

} // namespace

bool applyTrackIcon(QToolButton* button, const Track* track, std::string* shown)
{
	const QString chosen = TrackLooks::instance().icon(track->id());
	if (!chosen.isEmpty())
	{
		const std::string name = ChosenPrefix + chosen.toStdString();
		if (*shown != name)
		{
			*shown = name;
			button->setIcon(shellicons::icon(chosen));
		}
		return true;
	}
	if (shown->rfind(ChosenPrefix, 0) == 0)
	{
		shown->clear();  // an instrument's logo is set again by the caller, which compares its name
		if (const char* own = typeIconName(track->type())) { button->setIcon(embed::getIconPixmap(own)); }
	}
	return false;
}

void repaintTrackHeaders()
{
	for (QWidget* widget : QApplication::allWidgets())
	{
		if (auto* header = qobject_cast<TrackLabelButton*>(widget)) { header->update(); }
	}
}

} // namespace lmms::gui
