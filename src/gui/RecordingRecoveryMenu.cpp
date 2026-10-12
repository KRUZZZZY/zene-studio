/*
 * RecordingRecoveryMenu.cpp - File > Recover Recordings: place or dismiss a capture an abnormal exit left
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

#include "RecordingRecoveryMenu.h"

#include <QApplication>
#include <QFileInfo>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>

#include "ControlRegistry.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

void report(const QString& command, const ControlResult& result)
{
	if (result.ok) { return; }
	if (isUnattendedRun()) { qWarning("%s refused: %s", qPrintable(command), qPrintable(result.errorMessage)); }
	else { QMessageBox::warning(QApplication::activeWindow(), QMenu::tr("Recover Recordings"), result.errorMessage); }
}

//! record.recovery_place for \a path: the one command the menu, the startup offer and the agent surface share.
bool placeTake(const QString& path)
{
	const ControlResult placed = ControlRegistry::instance()->invoke(QStringLiteral("record.recovery_place"),
		{{QStringLiteral("take"), path}});
	report(QStringLiteral("record.recovery_place"), placed);
	return placed.ok;
}

//! The startup box: place every interrupted take, or decide later (File > Recover Recordings keeps the offer).
StartupRecoveryChoice askStartupRecovery(QWidget* parent, const QJsonArray& takes)
{
	QString list;
	for (const QJsonValue& take : takes) { list += QStringLiteral("\n  ") + recoveredTakeText(take.toObject()); }
	QMessageBox box(parent);
	box.setWindowTitle(QMenu::tr("Interrupted recordings"));
	box.setIcon(QMessageBox::Question);
	box.setText(QMenu::tr("%n recording(s) were interrupted when Zene Studio last stopped:", nullptr, static_cast<int>(takes.size())) + list);
	box.setInformativeText(QMenu::tr("Placing adds each one on a new sample track; Edit > Undo takes it back. "
		"The audio files are never deleted."));
	QPushButton* place = box.addButton(QMenu::tr("Place on New Tracks"), QMessageBox::AcceptRole);
	QPushButton* later = box.addButton(QMenu::tr("Decide Later"), QMessageBox::RejectRole);
	box.setDefaultButton(place);
	box.setEscapeButton(later);
	box.exec();
	return box.clickedButton() == place ? StartupRecoveryChoice::Place : StartupRecoveryChoice::Later;
}

void rebuild(QMenu* menu)
{
	for (QAction* action : menu->actions())
	{
		menu->removeAction(action);
		if (action->menu() != nullptr && action->menu()->parent() == menu) { action->menu()->deleteLater(); }
		else if (action->parent() == menu) { action->deleteLater(); }
	}
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("record.recovery_get_state"), QJsonObject{});
	const QJsonArray takes = state.result.value(QStringLiteral("takes")).toArray();
	QAction* line = menu->addAction(!state.ok ? state.errorMessage
		: takes.isEmpty() ? QMenu::tr("No interrupted recording")
		: QMenu::tr("%n interrupted recording(s)", nullptr, static_cast<int>(takes.size())));
	line->setEnabled(false);
	line->setData(QStringLiteral("record.recovery_get_state"));
	for (const QJsonValue& value : takes)
	{
		const QJsonObject take = value.toObject();
		const QString path = take.value(QStringLiteral("take")).toString();
		QMenu* sub = new QMenu(recoveredTakeText(take), menu);
		sub->setToolTip(path);
		menu->addMenu(sub);
		QAction* place = sub->addAction(QMenu::tr("Place on a New Sample Track"));
		place->setData(QStringLiteral("record.recovery_place"));
		QObject::connect(place, &QAction::triggered, menu, [path] { placeTake(path); });
		QAction* dismiss = sub->addAction(QMenu::tr("Dismiss (keep the file)"));
		dismiss->setData(QStringLiteral("record.recovery_discard"));
		QObject::connect(dismiss, &QAction::triggered, menu, [path] {
			report(QStringLiteral("record.recovery_discard"), ControlRegistry::instance()->invoke(
				QStringLiteral("record.recovery_discard"), {{QStringLiteral("take"), path}}));
		});
	}
}

} // namespace

QString recoveredTakeText(const QJsonObject& take)
{
	const int rate = take.value(QStringLiteral("sample_rate")).toInt();
	const double frames = take.value(QStringLiteral("frames_recoverable")).toDouble();
	const QString name = QFileInfo(take.value(QStringLiteral("take")).toString()).fileName();
	return rate > 0 ? QMenu::tr("%1 (%2 s recoverable)").arg(name).arg(frames / rate, 0, 'f', 1) : name;
}

QJsonArray interruptedTakes()
{
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("record.recovery_get_state"), QJsonObject{});
	return state.ok ? state.result.value(QStringLiteral("takes")).toArray() : QJsonArray{};
}

StartupRecoveryChoice offerRecordingRecovery(QWidget* parent, const std::function<StartupRecoveryChoice(const QJsonArray&)>& ask)
{
	const QJsonArray takes = interruptedTakes();
	if (takes.isEmpty()) { return StartupRecoveryChoice::NothingToOffer; }
	if (!ask && isUnattendedRun())
	{
		// No dialog without a person: say it on stderr, leave the journals, and let the agent decide
		// through record.recovery_get_state / record.recovery_place.
		qWarning("%d interrupted recording(s) found; record.recovery_place brings one into the project", static_cast<int>(takes.size()));
		return StartupRecoveryChoice::Later;
	}
	const StartupRecoveryChoice choice = ask ? ask(takes) : askStartupRecovery(parent, takes);
	if (choice == StartupRecoveryChoice::Place)
	{
		for (const QJsonValue& take : takes) { placeTake(take.toObject().value(QStringLiteral("take")).toString()); }
	}
	return choice;
}

void offerRecordingRecoveryAtStartup(QWidget* parent)
{
	// After the event loop starts: a modal box before app->exec() would park the start-up path.
	QTimer::singleShot(0, parent, [parent] { offerRecordingRecovery(parent, nullptr); });
}

QMenu* addRecordingRecoveryMenu(QMenu* menu)
{
	QMenu* recovery = menu->addMenu(QMenu::tr("Recover Recordings"));
	QObject::connect(recovery, &QMenu::aboutToShow, recovery, [recovery] { rebuild(recovery); });
	return recovery;
}

} // namespace lmms::gui
