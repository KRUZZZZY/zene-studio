/*
 * ArrangerFileDrop.cpp - files dropped on the arranger become clips
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
#include "ArrangerFileDrop.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QMimeData>
#include <QStatusBar>
#include <QUrl>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "SongEditor.h"
#include "TextFloat.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "TrackView.h"
#include "embed.h"

namespace lmms::gui::arrangerdrop
{

namespace
{

QStringList localFiles(const QMimeData* mime)
{
	QStringList files;
	if (mime == nullptr) { return files; }
	for (const QUrl& url : mime->urls())
	{
		if (url.isLocalFile()) { files.append(url.toLocalFile()); }
	}
	return files;
}

int trackHeadWidth()
{
	return ConfigManager::inst()->value("ui", "compacttrackbuttons").toInt() == 1
		? trackSettingsWidth(true) + trackOperationsWidth(true)
		: trackSettingsWidth(false) + trackOperationsWidth(false);
}

} // namespace

bool carriesFile(const QMimeData* mime) { return !localFiles(mime).isEmpty(); }

TimePos tickAtContainerX(const TrackContainerView* view, int x)
{
	if (view == nullptr || view->fixedClips()) { return TimePos(0); }
	const int head = trackHeadWidth();
	const int xPos = std::max(x, head);
	const float snapSize = getGUI()->songEditor()->m_editor->getSnapSize();
	return TimePos(((xPos - head) / view->pixelsPerBar() * TimePos::ticksPerBar()) + view->currentPosition())
		.quantize(snapSize, true);
}

bool importDroppedFile(const QMimeData* mime, Track* track, TimePos tick, QString* message)
{
	const QStringList files = localFiles(mime);
	if (files.isEmpty()) { return false; }
	QJsonObject args{{QStringLiteral("path"), QFileInfo(files.first()).absoluteFilePath()},
		{QStringLiteral("position"), static_cast<int>(tick.getTicks())}};
	if (track != nullptr) { args.insert(QStringLiteral("track"), control::trackIdOf(track)); }
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("clip.import_file"), args);
	QString text = result.ok
		? QObject::tr("Imported %1 as a clip.").arg(QFileInfo(files.first()).fileName())
		: result.errorMessage;
	if (files.size() > 1) { text += QObject::tr(" Only the first of the %1 dropped files was used.").arg(files.size()); }
	if (message != nullptr) { *message = text; }
	return result.ok;
}

bool dropFileAndReport(const QMimeData* mime, Track* track, TimePos tick)
{
	QString message;
	const bool made = importDroppedFile(mime, track, tick, &message);
	if (message.isEmpty() || getGUI() == nullptr || getGUI()->mainWindow() == nullptr) { return made; }
	getGUI()->mainWindow()->statusBar()->showMessage(message, 8000);
	if (!made)
	{
		TextFloat::displayMessage(QObject::tr("Dropped file not imported"), message,
			embed::getIconPixmap("error", 24, 24), 6000);
	}
	return made;
}

} // namespace lmms::gui::arrangerdrop
