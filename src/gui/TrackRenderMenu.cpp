/*
 * TrackRenderMenu.cpp - a track's Freeze / Unfreeze / Bounce in place items, and DAWproject files
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

#include "TrackRenderMenu.h"

#include <QApplication>
#include <QFileDialog>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Track.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

//! Runs @a command with a wait cursor; a refusal is shown (or logged when nobody is there).
ControlResult runBusy(const QString& title, const QString& command, const QJsonObject& args)
{
	QApplication::setOverrideCursor(Qt::WaitCursor);
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	QApplication::restoreOverrideCursor();
	if (result.ok) { return result; }
	if (isUnattendedRun()) { qWarning("%s refused: %s", qPrintable(command), qPrintable(result.errorMessage)); }
	else { QMessageBox::warning(QApplication::activeWindow(), title, result.errorMessage); }
	return result;
}

QAction* item(QMenu* menu, const QString& text, const QString& command, const QString& trackId)
{
	QAction* action = menu->addAction(text);
	action->setData(command);
	QObject::connect(action, &QAction::triggered, menu, [text, command, trackId] {
		runBusy(text, command, {{QStringLiteral("track"), trackId}});
	});
	return action;
}

} // namespace

void addTrackRenderActions(QMenu* menu, Track* track)
{
	if (track == nullptr || (track->type() != Track::Type::Instrument && track->type() != Track::Type::Sample)) { return; }
	const QString trackId = control::trackIdOf(track);
	if (track->isFrozen()) { item(menu, QMenu::tr("Unfreeze track"), QStringLiteral("freeze.unfreeze"), trackId); }
	else { item(menu, QMenu::tr("Freeze track"), QStringLiteral("freeze.track"), trackId); }
	item(menu, QMenu::tr("Bounce in place"), QStringLiteral("bounce.in_place"), trackId);
}

void addDawProjectActions(QMenu* fileMenu)
{
	const QString filter = QMenu::tr("DAWproject files (*.dawproject)");
	QAction* import = fileMenu->addAction(QMenu::tr("Import DAWproject..."));
	import->setData(QStringLiteral("dawproject.import"));
	QObject::connect(import, &QAction::triggered, fileMenu, [filter] {
		const QString path = QFileDialog::getOpenFileName(QApplication::activeWindow(), QMenu::tr("Import DAWproject"),
			ConfigManager::inst()->userProjectsDir(), filter);
		if (!path.isEmpty()) { runBusy(QMenu::tr("Import DAWproject"), QStringLiteral("dawproject.import"), {{QStringLiteral("path"), path}}); }
	});
	QAction* exportAction = fileMenu->addAction(QMenu::tr("Export DAWproject..."));
	exportAction->setData(QStringLiteral("dawproject.export"));
	QObject::connect(exportAction, &QAction::triggered, fileMenu, [filter] {
		QString path = QFileDialog::getSaveFileName(QApplication::activeWindow(), QMenu::tr("Export DAWproject"),
			ConfigManager::inst()->userProjectsDir(), filter);
		if (path.isEmpty()) { return; }
		if (!path.endsWith(QStringLiteral(".dawproject"))) { path += QStringLiteral(".dawproject"); }
		runBusy(QMenu::tr("Export DAWproject"), QStringLiteral("dawproject.export"),
			{{QStringLiteral("path"), path}, {QStringLiteral("overwrite"), true}});
	});
}

} // namespace lmms::gui
