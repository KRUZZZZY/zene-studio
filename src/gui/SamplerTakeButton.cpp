/*
 * SamplerTakeButton.cpp - the "Record take" button of a sampler's own view (0.5.0 L10, Q050-156)
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

#include "SamplerTakeButton.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QPushButton>
#include <QTimer>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

constexpr double DefaultTakeSeconds = 2.0;
constexpr int PollIntervalMs = 50;

QString translated(const char* text)
{
	return QCoreApplication::translate("SamplerTakeButton", text);
}

QString stateOf(const QJsonObject& reply)
{
	return reply.value(QStringLiteral("state")).toString();
}

ControlResult invoke(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QTimer* pollTimer(QPushButton* button)
{
	return button->findChild<QTimer*>(QStringLiteral("zeneSamplerTakePoll"));
}

void stopPolling(QPushButton* button)
{
	if (QTimer* timer = pollTimer(button)) { timer->stop(); }
}

void showProblem(QPushButton* button, const QString& why)
{
	button->setToolTip(why);
}

void resetButton(QPushButton* button)
{
	stopPolling(button);
	button->setText(translated("Record take"));
	button->setToolTip(QCoreApplication::translate("SamplerTakeButton",
		"Record the input into this sampler; click again to stop"));
	button->setProperty("zeneSamplerTakeState", QStringLiteral("idle"));
}

bool landTake(QPushButton* button)
{
	const ControlResult landed = invoke(QStringLiteral("record.sampler_capture_land"));
	if (!landed.ok)
	{
		stopPolling(button);
		button->setText(translated("Land take"));
		button->setProperty("zeneSamplerTakeState", QStringLiteral("done"));
		showProblem(button, landed.errorMessage);
		return false;
	}
	resetButton(button);
	return true;
}

void startTake(QPushButton* button, const QString& trackId)
{
	const double seconds = button->property("zeneSamplerTakeSeconds").toDouble();
	const ControlResult started = invoke(QStringLiteral("record.sampler_capture"),
		{{QStringLiteral("track"), trackId},
			{QStringLiteral("seconds"), seconds > 0.0 ? seconds : DefaultTakeSeconds}});
	if (!started.ok)
	{
		showProblem(button, started.errorMessage);
		return;
	}
	button->setProperty("zeneSamplerTakeState", QStringLiteral("recording"));
	button->setText(translated("Stop take"));
	button->setToolTip(QCoreApplication::translate("SamplerTakeButton",
		"Stop the take and load it into this sampler"));
	QTimer* timer = pollTimer(button);
	if (timer == nullptr)
	{
		timer = new QTimer(button);
		timer->setObjectName(QStringLiteral("zeneSamplerTakePoll"));
		timer->setInterval(PollIntervalMs);
		QObject::connect(timer, &QTimer::timeout, button, [button] { samplerTakeButtonPoll(button); });
	}
	timer->start();
}

void stopAndLand(QPushButton* button)
{
	const ControlResult stopped = invoke(QStringLiteral("record.sampler_capture_stop"));
	if (!stopped.ok)
	{
		showProblem(button, stopped.errorMessage);
		return;
	}
	landTake(button);
}

} // namespace

void samplerTakeButtonStep(QPushButton* button, const QString& trackId)
{
	if (button == nullptr) { return; }
	button->setProperty("zeneSamplerTakeTrack", trackId);
	const ControlResult status = invoke(QStringLiteral("record.sampler_capture_status"));
	if (!status.ok)
	{
		showProblem(button, status.errorMessage);
		return;
	}
	const QString state = stateOf(status.result);
	const QString recordingTrack = status.result.value(QStringLiteral("track")).toString();
	if (state != QLatin1String("idle") && !recordingTrack.isEmpty() && recordingTrack != trackId)
	{
		showProblem(button, QCoreApplication::translate("SamplerTakeButton",
			"Another sampler take is in hand on %1; let its own view land or cancel it").arg(recordingTrack));
		return;
	}
	if (state == QLatin1String("recording"))
	{
		stopAndLand(button);
	}
	else if (state == QLatin1String("done"))
	{
		landTake(button);
	}
	else
	{
		startTake(button, trackId);
	}
}

void samplerTakeButtonPoll(QPushButton* button)
{
	if (button == nullptr) { return; }
	const ControlResult status = invoke(QStringLiteral("record.sampler_capture_status"));
	if (!status.ok)
	{
		stopPolling(button);
		showProblem(button, status.errorMessage);
		return;
	}
	const QString state = stateOf(status.result);
	if (state == QLatin1String("done"))
	{
		const QString track = status.result.value(QStringLiteral("track")).toString();
		const QString mine = button->property("zeneSamplerTakeTrack").toString();
		if (track == mine) { landTake(button); }
		else { resetButton(button); }
		return;
	}
	if (state != QLatin1String("recording")) { resetButton(button); }
}

} // namespace lmms::gui
