/*
 * FeedbackModeAction.cpp - a mixer channel menu's Allow feedback sends: the cycle-permitted submode
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

#include "FeedbackModeAction.h"

#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

std::function<bool(const QString&)> s_ask;

bool ask(const QString& question)
{
	if (s_ask) { return s_ask(question); }
	return QMessageBox::question(QApplication::activeWindow(), QMenu::tr("Feedback Sends"), question,
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

QJsonObject feedbackState()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("feedback.get_state"), QJsonObject{}).result;
}

} // namespace

void setFeedbackModeQuestion(std::function<bool(const QString&)> askFn) { s_ask = std::move(askFn); }

QAction* addFeedbackModeAction(QMenu* menu, std::function<void()> refresh)
{
	const QJsonObject state = feedbackState();
	QAction* action = menu->addAction(QMenu::tr("Allow feedback sends"));
	action->setCheckable(true);
	action->setChecked(state.value(QStringLiteral("enabled")).toBool());
	action->setData(QStringLiteral("feedback.enable"));
	action->setToolTip(QMenu::tr("Let a send close a loop - it is delayed by one period - while plugin delay "
		"compensation is suspended. Turning this off deletes every loop-closing send"));
	QObject::connect(action, &QAction::triggered, menu, [refresh](bool on) {
		if (!on)
		{
			const int loops = static_cast<int>(feedbackState().value(QStringLiteral("feedback_routes")).toArray().size());
			if (loops > 0 && !ask(QMenu::tr("Turning feedback sends off deletes %n loop-closing send(s). Continue?", nullptr, loops)))
			{
				return;
			}
		}
		ControlRegistry::instance()->invoke(on ? QStringLiteral("feedback.enable") : QStringLiteral("feedback.disable"),
			QJsonObject{});
		if (refresh) { refresh(); }
	});
	return action;
}

} // namespace lmms::gui
