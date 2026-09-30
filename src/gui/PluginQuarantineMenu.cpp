/*
 * PluginQuarantineMenu.cpp - Edit > Plugin Quarantine: hide a plugin file from the scan, or release it
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

#include "PluginQuarantineMenu.h"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

std::function<QString()> s_picker;

QString pickFile()
{
	if (s_picker) { return s_picker(); }
	return QFileDialog::getOpenFileName(nullptr, QMenu::tr("Quarantine a Plugin File"));
}

ControlResult invoke(const char* id, const QJsonObject& args = QJsonObject{})
{
	return ControlRegistry::instance()->invoke(QLatin1String(id), args);
}

void rebuild(QMenu* menu, const QString& refusal = QString());

void addReleaseItem(QMenu* menu, const QJsonObject& entry)
{
	const QString path = entry.value(QStringLiteral("path")).toString();
	const bool exists = entry.value(QStringLiteral("file_exists")).toBool();
	QAction* release = menu->addAction(exists ? QMenu::tr("Release %1").arg(QFileInfo(path).fileName())
		: QMenu::tr("Release %1 (file gone)").arg(QFileInfo(path).fileName()));
	release->setData(QStringLiteral("plugin.scan_cache_quarantine_remove"));
	const QString reason = entry.value(QStringLiteral("reason")).toString();
	release->setToolTip(reason.isEmpty() ? path : QMenu::tr("%1 - %2").arg(path, reason));
	QObject::connect(release, &QAction::triggered, menu, [menu, path] {
		const ControlResult removed = invoke("plugin.scan_cache_quarantine_remove", {{QStringLiteral("path"), path}});
		rebuild(menu, removed.ok ? QString() : QMenu::tr("Refused: %1").arg(removed.errorMessage));
	});
}

void rebuild(QMenu* menu, const QString& refusal)
{
	// Not QMenu::clear(): a rebuild runs inside one of these actions' own triggered signal, and
	// clear() deletes them on the spot.
	for (QAction* action : menu->actions())
	{
		menu->removeAction(action);
		if (action->parent() == menu) { action->deleteLater(); }
	}
	const ControlResult state = invoke("plugin.scan_cache_get_state");
	QAction* status = menu->addAction(!refusal.isEmpty() ? refusal
		: state.ok ? pluginQuarantineStatusText(state.result) : state.errorMessage);
	status->setEnabled(false);
	menu->addSeparator();
	for (const QJsonValue& entry : state.result.value(QStringLiteral("quarantine")).toArray())
	{
		addReleaseItem(menu, entry.toObject());
	}
	menu->addSeparator();
	QAction* add = menu->addAction(QMenu::tr("Quarantine a Plugin File..."));
	add->setData(QStringLiteral("plugin.scan_cache_quarantine_add"));
	add->setEnabled(state.ok);
	QObject::connect(add, &QAction::triggered, menu, [menu] {
		const QString path = pickFile();
		if (path.isEmpty()) { return; }
		const ControlResult added = invoke("plugin.scan_cache_quarantine_add",
			{{QStringLiteral("path"), path}, {QStringLiteral("reason"), QMenu::tr("quarantined from the menu")}});
		rebuild(menu, added.ok ? QString() : QMenu::tr("Refused: %1").arg(added.errorMessage));
	});
	QAction* rescan = menu->addAction(QMenu::tr("Rescan Plugins"));
	rescan->setData(QStringLiteral("plugin.rescan"));
	rescan->setToolTip(QMenu::tr("Run a plugin scan now, so a quarantine edit takes effect"));
	QObject::connect(rescan, &QAction::triggered, menu, [menu] {
		const ControlResult scanned = invoke("plugin.rescan");
		rebuild(menu, scanned.ok ? QString() : QMenu::tr("Refused: %1").arg(scanned.errorMessage));
	});
}

} // namespace

void setPluginQuarantinePicker(std::function<QString()> picker) { s_picker = std::move(picker); }

QString pluginQuarantineStatusText(const QJsonObject& state)
{
	return QMenu::tr("%1 files in the scan cache, %2 quarantined")
		.arg(state.value(QStringLiteral("entry_count")).toInt())
		.arg(state.value(QStringLiteral("quarantine_count")).toInt());
}

QMenu* addPluginQuarantineMenu(QMenu* menu)
{
	QMenu* quarantine = menu->addMenu(QMenu::tr("Plugin Quarantine"));
	// Built on open, not here: reading the scan cache may construct the plugin factory, and that
	// runs the first scan (the note plugin.scan_cache_get_state carries).
	QObject::connect(quarantine, &QMenu::aboutToShow, quarantine, [quarantine] { rebuild(quarantine); });
	return quarantine;
}

} // namespace lmms::gui
