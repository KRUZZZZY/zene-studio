/*
 * AafImportMenu.cpp - File > Import AAF... (interchange.aaf_import, WF-038)
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

#include "AafImportMenu.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QJsonObject>
#include <QMenu>

#include "AafImport.h"
#include "CommandForm.h"
#include "ConfigManager.h"

namespace lmms::gui
{

QAction* addAafImportAction(QMenu* fileMenu)
{
	QAction* action = fileMenu->addAction(QMenu::tr("Import AAF..."));
	action->setData(QStringLiteral("interchange.aaf_import"));
	if (!interchange::aafImportAvailable())
	{
		action->setEnabled(false);
		action->setStatusTip(QMenu::tr("This build was made without LibAAF, so it cannot import AAF files"));
		return action;
	}
	QObject::connect(action, &QAction::triggered, fileMenu, [] {
		const QString path = QFileDialog::getOpenFileName(QApplication::activeWindow(), QMenu::tr("Import AAF"),
			ConfigManager::inst()->userProjectsDir(), QMenu::tr("AAF files (*.aaf)"));
		if (path.isEmpty()) { return; }
		CommandCall call;
		call.id = QStringLiteral("interchange.aaf_import");
		call.fixed = {{QStringLiteral("path"), path}, {QStringLiteral("start_at_first_clip"), true}};
		call.title = QMenu::tr("Import AAF");
		call.ask = false;
		call.showResult = true;  // what was built and which media is missing is the point of the reply
		runCommand(QApplication::activeWindow(), call);
	});
	return action;
}

} // namespace lmms::gui
