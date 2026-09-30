/*
 * ScriptBudgetActionTest.cpp - File > Script Memory Budget... sets the Lua cap
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

/*! The item names the current cap, sets the prompted one through script.set_memory_budget (a
 *  cancelled prompt changes nothing), leaves the cap alone when the value is outside the surface's
 *  range, and one control.undo puts the previous cap back. */

#include <QtTest>

#include <QAction>
#include <QJsonObject>
#include <QMenu>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ScriptBudgetAction.h"
#include "ScriptEngine.h"

using namespace lmms;
using namespace lmms::gui;

class ScriptBudgetActionTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ScriptEngine::instance()->setMemoryBudget(ScriptEngine::DefaultMemoryBudgetBytes);
		setScriptBudgetPrompt({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theItemSetsTheCap()
	{
		QCOMPARE(scriptBudgetText(64ull * 1024 * 1024), QStringLiteral("Script Memory Budget (64.0 MiB)..."));
		const quint64 before = ScriptEngine::instance()->memoryBudget();
		QMenu menu;
		QAction* budget = addScriptBudgetAction(&menu);
		QCOMPARE(budget->text(), scriptBudgetText(before));

		setScriptBudgetPrompt([](double) { return std::nullopt; });
		budget->trigger();
		QCOMPARE(ScriptEngine::instance()->memoryBudget(), before);

		setScriptBudgetPrompt([](double) { return 16.0; });
		budget->trigger();
		QCOMPARE(ScriptEngine::instance()->memoryBudget(), 16ull * 1024 * 1024);
		QCOMPARE(budget->text(), QStringLiteral("Script Memory Budget (16.0 MiB)..."));

		setScriptBudgetPrompt([](double) { return 4096.0; });  // past the 1 GiB ceiling
		budget->trigger();
		QCOMPARE(ScriptEngine::instance()->memoryBudget(), 16ull * 1024 * 1024);

		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QCOMPARE(ScriptEngine::instance()->memoryBudget(), before);
	}
};

QTEST_MAIN(ScriptBudgetActionTest)
#include "ScriptBudgetActionTest.moc"
