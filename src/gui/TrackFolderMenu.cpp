/*
 * TrackFolderMenu.cpp - a track's "Folder" menu: membership, collapse, pin, routing
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

#include "TrackFolderMenu.h"

#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "TrackFolder.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

void run(const QString& command, const QJsonObject& args)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (result.ok) { return; }
	if (isUnattendedRun())
	{
		qWarning("track folder menu: %s refused: %s", qPrintable(command), qPrintable(result.errorMessage));
		return;
	}
	QMessageBox::warning(nullptr, QObject::tr("Folder"), result.errorMessage);
}

//! One checkable item running @a command with @a flag set to the item's new state.
void addToggle(QMenu* menu, const QString& label, bool checked, const QString& command,
	const QString& trackId, const QString& flag)
{
	QAction* action = menu->addAction(label);
	action->setCheckable(true);
	action->setChecked(checked);
	action->setData(command);
	QObject::connect(action, &QAction::toggled, menu, [command, trackId, flag](bool on) {
		run(command, {{QStringLiteral("track"), trackId}, {flag, on}});
	});
}

void addMoveTo(QMenu* menu, Track* track, const QString& trackId)
{
	QMenu* moveTo = menu->addMenu(QObject::tr("Move to folder"));
	const auto addTarget = [moveTo, trackId](const QString& label, const QString& folderId, bool current) {
		QAction* action = moveTo->addAction(label);
		action->setCheckable(true);
		action->setChecked(current);
		action->setData(QStringLiteral("track.set_folder"));
		QObject::connect(action, &QAction::triggered, moveTo, [trackId, folderId] {
			run(QStringLiteral("track.set_folder"), {{QStringLiteral("track"), trackId}, {QStringLiteral("folder"), folderId}});
		});
	};
	addTarget(QObject::tr("No folder"), QString(), track->parentFolder() == nullptr);
	moveTo->addSeparator();
	for (Track* candidate : Engine::getSong()->tracks())
	{
		auto* folder = dynamic_cast<TrackFolder*>(candidate);
		if (folder == nullptr || folder == track) { continue; }
		addTarget(folder->name(), control::trackIdOf(folder), track->parentFolder() == folder);
	}
}

} // namespace

void addTrackFolderMenu(QMenu* menu, Track* track)
{
	const QString trackId = control::trackIdOf(track);
	QMenu* folderMenu = menu->addMenu(QObject::tr("Folder"));
	if (auto* folder = dynamic_cast<TrackFolder*>(track))
	{
		addToggle(folderMenu, QObject::tr("Collapsed"), folder->isCollapsed(),
			QStringLiteral("track.folder_set_collapsed"), trackId, QStringLiteral("collapsed"));
		addToggle(folderMenu, QObject::tr("Pinned"), folder->isPinned(),
			QStringLiteral("track.set_pinned"), trackId, QStringLiteral("pinned"));
		addToggle(folderMenu, QObject::tr("Route children through the folder"), folder->isRouting(),
			QStringLiteral("track.set_routing"), trackId, QStringLiteral("routing"));
		folderMenu->addSeparator();
	}
	addMoveTo(folderMenu, track, trackId);
}

} // namespace lmms::gui
