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
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
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

//! The two rendering actions enter the cancellable render queue; the task area follows the job after this returns.
void queueTrackRender(const QString& title, const QString& command, const QString& trackId)
{
	const QString operation = command == QLatin1String("freeze.track")
		? QStringLiteral("freeze_track") : QStringLiteral("bounce");
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("render.queue_bounce"),
		{{QStringLiteral("operation"), operation}, {QStringLiteral("track"), trackId}});
	if (result.ok) { return; }
	if (isUnattendedRun()) { qWarning("%s refused: %s", qPrintable(command), qPrintable(result.errorMessage)); }
	else { QMessageBox::warning(QApplication::activeWindow(), title, result.errorMessage); }
}

QAction* item(QMenu* menu, const QString& text, const QString& command, const QString& trackId)
{
	QAction* action = menu->addAction(text);
	action->setData(command);
	QObject::connect(action, &QAction::triggered, menu, [text, command, trackId] {
		if (command == QLatin1String("bounce.in_place") || command == QLatin1String("freeze.track"))
		{
			queueTrackRender(text, command, trackId);
		}
		else { runBusy(text, command, {{QStringLiteral("track"), trackId}}); }
	});
	return action;
}

bool canFreeze(const Track* track)
{
	return (track->type() == Track::Type::Instrument || track->type() == Track::Type::Sample) && !track->isFrozen();
}

//! The checked tracks' ids, in list order.
QJsonArray checkedTracks(const QListWidget* list)
{
	QJsonArray ids;
	for (int row = 0; row < list->count(); ++row)
	{
		if (list->item(row)->checkState() == Qt::Checked) { ids.append(list->item(row)->data(Qt::UserRole).toString()); }
	}
	return ids;
}

} // namespace

QDialog* openBatchFreezeDialog(Track* preselected)
{
	auto* dialog = new QDialog(QApplication::activeWindow());
	dialog->setObjectName(QStringLiteral("batchFreezeDialog"));
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(QMenu::tr("Freeze tracks"));
	auto* layout = new QVBoxLayout(dialog);
	layout->addWidget(new QLabel(QMenu::tr("Tick two or more tracks. They render at the same time as one task "
		"(Tasks in the status bar shows its progress and Cancel); one Undo unfreezes them all."), dialog));
	auto* list = new QListWidget(dialog);
	list->setObjectName(QStringLiteral("batchFreezeTracks"));
	for (Track* track : Engine::getSong()->tracks())
	{
		if (!canFreeze(track)) { continue; }
		auto* row = new QListWidgetItem(track->name(), list);
		row->setData(Qt::UserRole, control::trackIdOf(track));
		row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
		row->setCheckState(track == preselected ? Qt::Checked : Qt::Unchecked);
	}
	layout->addWidget(list);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
	QPushButton* freeze = buttons->button(QDialogButtonBox::Ok);
	freeze->setText(QMenu::tr("Freeze"));
	const auto update = [list, freeze] { freeze->setEnabled(checkedTracks(list).size() >= 2); };
	update();
	QObject::connect(list, &QListWidget::itemChanged, dialog, update);
	QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, list] {
		// The same registered command an agent sends; the task area follows the batch after this returns.
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("freeze.tracks"),
			{{QStringLiteral("tracks"), checkedTracks(list)}});
		if (!result.ok)
		{
			if (isUnattendedRun()) { qWarning("freeze.tracks refused: %s", qPrintable(result.errorMessage)); }
			else { QMessageBox::warning(dialog, QMenu::tr("Freeze tracks"), result.errorMessage); }
			return;
		}
		dialog->accept();
	});
	layout->addWidget(buttons);
	dialog->open();
	return dialog;
}

void addTrackRenderActions(QMenu* menu, Track* track)
{
	if (track == nullptr || (track->type() != Track::Type::Instrument && track->type() != Track::Type::Sample)) { return; }
	const QString trackId = control::trackIdOf(track);
	if (track->isFrozen()) { item(menu, QMenu::tr("Unfreeze track"), QStringLiteral("freeze.unfreeze"), trackId); }
	else
	{
		item(menu, QMenu::tr("Freeze track"), QStringLiteral("freeze.track"), trackId);
		QAction* several = menu->addAction(QMenu::tr("Freeze several tracks..."));
		several->setData(QStringLiteral("freeze.tracks"));
		const int id = track->id();
		QObject::connect(several, &QAction::triggered, menu, [id] { openBatchFreezeDialog(control::trackWithId(id)); });
	}
	item(menu, QMenu::tr("Bounce in place"), QStringLiteral("bounce.in_place"), trackId);
}

QString dawProjectImportNotice(const QJsonObject& result)
{
	const QJsonArray deviations = result.value(QStringLiteral("deviations")).toArray();
	if (deviations.isEmpty()) { return QString(); }
	QStringList lines;
	for (const QJsonValue& value : deviations)
	{
		const QJsonObject deviation = value.toObject();
		lines << QStringLiteral("- %1").arg(deviation.value(QStringLiteral("detail")).toString());
	}
	return QMenu::tr("The file was imported, but %n part(s) of it could not be taken as written:", nullptr,
			static_cast<int>(deviations.size()))
		+ QLatin1Char('\n') + lines.join(QLatin1Char('\n'));
}

namespace
{

//! Runs the import, then lists every deviation the container report carries (a refusal is already shown, with its list).
void importDawProject(const QString& path)
{
	const QString title = QMenu::tr("Import DAWproject");
	const ControlResult result = runBusy(title, QStringLiteral("dawproject.import"), {{QStringLiteral("path"), path}});
	const QString notice = result.ok ? dawProjectImportNotice(result.result) : QString();
	if (notice.isEmpty()) { return; }
	if (isUnattendedRun()) { qWarning("dawproject.import: %s", qPrintable(notice)); }
	else { QMessageBox::warning(QApplication::activeWindow(), title, notice); }
}

} // namespace

void addDawProjectActions(QMenu* fileMenu)
{
	const QString filter = QMenu::tr("DAWproject files (*.dawproject)");
	QAction* import = fileMenu->addAction(QMenu::tr("Import DAWproject..."));
	import->setData(QStringLiteral("dawproject.import"));
	import->setToolTip(QMenu::tr("Reads ZIP entries stored or DEFLATE-compressed. Anything in the file that cannot be imported is listed in a report afterwards."));
	QObject::connect(import, &QAction::triggered, fileMenu, [filter] {
		const QString path = QFileDialog::getOpenFileName(QApplication::activeWindow(), QMenu::tr("Import DAWproject"),
			ConfigManager::inst()->userProjectsDir(), filter);
		if (!path.isEmpty()) { importDawProject(path); }
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
