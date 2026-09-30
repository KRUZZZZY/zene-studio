/*
 * CrashReportMenu.cpp - Help > Crash Reports: arm the reporter, see its report, keep or delete it
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

#include "CrashReportMenu.h"

#include <QAction>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QUrl>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

std::function<bool(const QString&)> s_confirm;
std::function<void(const QString&)> s_opener;

bool confirmDelete(const QString& reportPath)
{
	if (s_confirm) { return s_confirm(reportPath); }
	return QMessageBox::question(nullptr, QMenu::tr("Delete Crash Report"),
		QMenu::tr("Delete %1? Its content cannot be recovered.").arg(reportPath),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void openFolder(const QString& directory)
{
	if (s_opener) { s_opener(directory); return; }
	QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

struct CrashItems
{
	QAction* arm;
	QAction* status;
	QAction* folder;
	QAction* acknowledge;
	QAction* discard;
};

ControlResult invoke(const char* id, const QJsonObject& args = QJsonObject{})
{
	return ControlRegistry::instance()->invoke(QLatin1String(id), args);
}

//! Sets every item from crash.list_reports; @a refusal, when non-empty, replaces the line.
void showState(const CrashItems& items, const QString& refusal = QString())
{
	const ControlResult state = invoke("crash.list_reports");
	const QJsonObject r = state.result;
	{
		const QSignalBlocker block(items.arm);
		items.arm->setEnabled(state.ok);
		items.arm->setChecked(r.value(QStringLiteral("armed")).toBool());
	}
	const bool hasReport = r.value(QStringLiteral("report_count")).toInt() > 0;
	items.folder->setEnabled(!r.value(QStringLiteral("report_directory")).toString().isEmpty());
	items.acknowledge->setEnabled(r.value(QStringLiteral("pending")).toBool());
	items.discard->setEnabled(hasReport);
	items.status->setText(!refusal.isEmpty() ? refusal
		: state.ok ? crashReportStatusText(r) : state.errorMessage);
}

QString refusalOf(const ControlResult& result)
{
	return result.ok ? QString() : QMenu::tr("Refused: %1").arg(result.errorMessage);
}

} // namespace

void setCrashReportDeleteConfirm(std::function<bool(const QString&)> confirm) { s_confirm = std::move(confirm); }
void setCrashReportFolderOpener(std::function<void(const QString&)> opener) { s_opener = std::move(opener); }

QString crashReportStatusText(const QJsonObject& state)
{
	const QString armed = state.value(QStringLiteral("armed")).toBool() ? QMenu::tr("Armed") : QMenu::tr("Not armed");
	if (state.value(QStringLiteral("report_count")).toInt() == 0)
	{
		return QMenu::tr("%1, no report").arg(armed);
	}
	return state.value(QStringLiteral("pending")).toBool()
		? QMenu::tr("%1, one report not yet offered").arg(armed)
		: QMenu::tr("%1, one report (already offered)").arg(armed);
}

QMenu* addCrashReportMenu(QMenu* menu)
{
	QMenu* crash = menu->addMenu(QMenu::tr("Crash Reports"));
	QAction* arm = crash->addAction(QMenu::tr("Report Crashes"));
	QAction* status = crash->addAction(QString());
	crash->addSeparator();
	QAction* folder = crash->addAction(QMenu::tr("Show Report Folder"));
	QAction* acknowledge = crash->addAction(QMenu::tr("Stop Offering the Report"));
	QAction* discard = crash->addAction(QMenu::tr("Delete the Report..."));
	const CrashItems items{arm, status, folder, acknowledge, discard};

	status->setEnabled(false);
	// The line shows crash.list_reports; declaring it keeps the item a registry action (A15).
	status->setData(QStringLiteral("crash.list_reports"));
	arm->setCheckable(true);
	arm->setData(QStringLiteral("crash.enable"));
	arm->setToolTip(QMenu::tr("Write a local report file if the program crashes. Nothing is ever sent: "
		"attach the file to a bug report by hand"));
	folder->setData(QStringLiteral("crash.list_reports"));
	acknowledge->setData(QStringLiteral("crash.acknowledge_report"));
	discard->setData(QStringLiteral("crash.discard_report"));

	QObject::connect(arm, &QAction::toggled, crash, [items](bool on) {
		showState(items, refusalOf(invoke(on ? "crash.enable" : "crash.disable")));
	});
	QObject::connect(folder, &QAction::triggered, crash, [] {
		openFolder(invoke("crash.list_reports").result.value(QStringLiteral("report_directory")).toString());
	});
	QObject::connect(acknowledge, &QAction::triggered, crash, [items] {
		showState(items, refusalOf(invoke("crash.acknowledge_report")));
	});
	QObject::connect(discard, &QAction::triggered, crash, [items] {
		const QString path = invoke("crash.list_reports").result.value(QStringLiteral("report_path")).toString();
		if (!confirmDelete(path)) { return; }
		showState(items, refusalOf(invoke("crash.discard_report")));
	});
	QObject::connect(crash, &QMenu::aboutToShow, crash, [items] { showState(items); });
	showState(items);
	return crash;
}

} // namespace lmms::gui
