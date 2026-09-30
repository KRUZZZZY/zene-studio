/*
 * ScriptBudgetAction.cpp - File > Script Memory Budget...: the cap a Lua run may hold
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

#include "ScriptBudgetAction.h"

#include <cmath>

#include <QAction>
#include <QApplication>
#include <QInputDialog>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>

#include "ControlRegistry.h"
#include "ScriptEngine.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

constexpr double BytesPerMiB = 1024.0 * 1024.0;

std::function<std::optional<double>(double)> s_prompt;

std::optional<double> promptMiB(double current)
{
	if (s_prompt) { return s_prompt(current); }
	bool ok = false;
	const double mib = QInputDialog::getDouble(QApplication::activeWindow(), QMenu::tr("Script Memory Budget"),
		QMenu::tr("Memory a script run may hold (MiB):"), current,
		ScriptEngine::MinMemoryBudgetBytes / BytesPerMiB, ScriptEngine::MaxMemoryBudgetBytes / BytesPerMiB, 1, &ok);
	return ok ? std::optional<double>(mib) : std::nullopt;
}

quint64 currentBudget() { return ScriptEngine::instance()->memoryBudget(); }

} // namespace

QString scriptBudgetText(quint64 bytes)
{
	return QMenu::tr("Script Memory Budget (%1 MiB)...").arg(bytes / BytesPerMiB, 0, 'f', 1);
}

void setScriptBudgetPrompt(std::function<std::optional<double>(double)> prompt) { s_prompt = std::move(prompt); }

QAction* addScriptBudgetAction(QMenu* menu)
{
	QAction* action = menu->addAction(scriptBudgetText(currentBudget()));
	action->setData(QStringLiteral("script.set_memory_budget"));
	action->setToolTip(QMenu::tr("The Lua memory a script run may hold; a run past it is stopped"));
	QObject::connect(action, &QAction::triggered, action, [action] {
		const auto mib = promptMiB(currentBudget() / BytesPerMiB);
		if (!mib) { return; }
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("script.set_memory_budget"),
			{{QStringLiteral("bytes"), static_cast<double>(std::llround(*mib * BytesPerMiB))}});
		action->setText(scriptBudgetText(currentBudget()));
		if (result.ok) { return; }
		if (isUnattendedRun()) { qWarning("script.set_memory_budget refused: %s", qPrintable(result.errorMessage)); }
		else { QMessageBox::warning(QApplication::activeWindow(), QMenu::tr("Script Memory Budget"), result.errorMessage); }
	});
	QObject::connect(menu, &QMenu::aboutToShow, action, [action] { action->setText(scriptBudgetText(currentBudget())); });
	return action;
}

} // namespace lmms::gui
