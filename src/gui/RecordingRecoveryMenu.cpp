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
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>

#include "ControlRegistry.h"
#include "Engine.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
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

//! A new sample track named after the take, holding the recovered file in a clip at bar 1.
void placeOnNewTrack(const QString& audio)
{
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
	if (track == nullptr) { return; }
	track->setName(QMenu::tr("Recovered: %1").arg(QFileInfo(audio).completeBaseName()));
	if (auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)))) { clip->setSampleFile(audio); }
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
	for (const QJsonValue& value : takes)
	{
		const QJsonObject take = value.toObject();
		const QString path = take.value(QStringLiteral("take")).toString();
		QMenu* sub = new QMenu(recoveredTakeText(take), menu);
		sub->setToolTip(path);
		menu->addMenu(sub);
		QAction* place = sub->addAction(QMenu::tr("Place on a New Sample Track"));
		place->setData(QStringLiteral("record.recovery_restore"));
		QObject::connect(place, &QAction::triggered, menu, [path] {
			const ControlResult restored = ControlRegistry::instance()->invoke(QStringLiteral("record.recovery_restore"),
				{{QStringLiteral("take"), path}});
			report(QStringLiteral("record.recovery_restore"), restored);
			if (restored.ok) { placeOnNewTrack(restored.result.value(QStringLiteral("audio")).toString()); }
		});
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

QMenu* addRecordingRecoveryMenu(QMenu* menu)
{
	QMenu* recovery = menu->addMenu(QMenu::tr("Recover Recordings"));
	QObject::connect(recovery, &QMenu::aboutToShow, recovery, [recovery] { rebuild(recovery); });
	return recovery;
}

} // namespace lmms::gui
