/*
 * RenderRangeMenuTest.cpp - Navigate > Range: Render Selection and Export Sections
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

/*! The two range renders declare their registered command, and the selection render forwards the
 *  range's EXACT ticks: an injected invoker records render.render's arguments and the test compares
 *  start_ticks/end_ticks with the range that was set. With no range selected the action says so and
 *  invokes no render. Export Sections forwards an absolute output directory to render.sections.
 *  The renders themselves are render.render's and render.sections' own tests' business. */

#include <QtTest>

#include <QAction>
#include <QJsonObject>
#include <QMenu>

#include "ControlRegistry.h"
#include "Engine.h"
#include "RenderRangeMenu.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QAction* itemWithCommand(QMenu& menu, const QString& command)
{
	for (QAction* action : menu.actions())
	{
		if (action->property("controlCommand").toString() == command) { return action; }
	}
	return nullptr;
}

} // namespace

class RenderRangeMenuTest : public QObject
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
		ControlRegistry::instance()->invoke(QStringLiteral("range.clear"), QJsonObject{});
		setRenderRangeOutputPicker({});
		setRenderSectionsOutputPicker({});
		setRenderRangeInvoker({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void renderSelectionForwardsTheExactRangeTicks()
	{
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("range.set"),
			{{QStringLiteral("start"), 384}, {QStringLiteral("end"), 1152}}).ok);

		QMenu menu;
		addRenderRangeActions(&menu);
		QAction* action = itemWithCommand(menu, QStringLiteral("render.render"));
		QVERIFY2(action != nullptr, "the menu has no Render Selection action");
		QCOMPARE(action->text(), QStringLiteral("Render Selection..."));

		QString invoked;
		QJsonObject captured;
		setRenderRangeOutputPicker([](const QString&) { return QStringLiteral("/tmp/zene-range-selection.wav"); });
		setRenderRangeInvoker([&](const QString& command, const QJsonObject& args) {
			if (command == QStringLiteral("range.get"))
			{
				return ControlRegistry::instance()->invoke(command, args);
			}
			invoked = command;
			captured = args;
			return ControlResult::success(QJsonObject{});
		});

		action->trigger();
		QCOMPARE(invoked, QStringLiteral("render.render"));
		QCOMPARE(captured.value(QStringLiteral("start_ticks")).toInt(), 384);
		QCOMPARE(captured.value(QStringLiteral("end_ticks")).toInt(), 1152);
		QCOMPARE(captured.value(QStringLiteral("out")).toString(),
			QStringLiteral("/tmp/zene-range-selection.wav"));
	}

	void renderSelectionSaysSoWhenNoRangeIsSelected()
	{
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("range.clear"), QJsonObject{}).ok);

		QMenu menu;
		addRenderRangeActions(&menu);
		QAction* action = itemWithCommand(menu, QStringLiteral("render.render"));
		QVERIFY(action != nullptr);

		bool picked = false;
		QString invoked;
		setRenderRangeOutputPicker([&picked](const QString&) { picked = true; return QString(); });
		setRenderRangeInvoker([&](const QString& command, const QJsonObject& args) {
			if (command == QStringLiteral("range.get"))
			{
				return ControlRegistry::instance()->invoke(command, args);
			}
			invoked = command;
			return ControlResult::success(QJsonObject{});
		});

		action->trigger();
		QVERIFY2(!picked, "no range selected, yet the file picker was opened");
		QVERIFY2(invoked.isEmpty(), "no range selected, yet a render was invoked");
	}

	void exportSectionsForwardsTheOutputDirectory()
	{
		QMenu menu;
		addRenderRangeActions(&menu);
		QAction* action = itemWithCommand(menu, QStringLiteral("render.sections"));
		QVERIFY2(action != nullptr, "the menu has no Export Sections action");
		QCOMPARE(action->text(), QStringLiteral("Export Sections..."));

		QString invoked;
		QJsonObject captured;
		setRenderSectionsOutputPicker([](const QString&) { return QStringLiteral("/tmp/zene-sections"); });
		setRenderRangeInvoker([&](const QString& command, const QJsonObject& args) {
			invoked = command;
			captured = args;
			return ControlResult::success({{QStringLiteral("count"), 2}});
		});

		action->trigger();
		QCOMPARE(invoked, QStringLiteral("render.sections"));
		QCOMPARE(captured.value(QStringLiteral("out")).toString(), QStringLiteral("/tmp/zene-sections"));
	}
};

QTEST_MAIN(RenderRangeMenuTest)
#include "RenderRangeMenuTest.moc"
