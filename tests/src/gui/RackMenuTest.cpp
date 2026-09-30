/*
 * RackMenuTest.cpp - a mixer channel's Rack menu adds chains, routes, and adds, sets and removes a macro
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

/*! On a fresh mixer channel, every change through the submenu and read back from rack.get_state:
 *  Add parallel chain grows the rack, Route to moves the selector (and Parallel is -1), Remove
 *  chain drops the added chain, Add macro... adds one (an empty name adds nothing), the macro's own
 *  item sets its value (a cancelled prompt changes nothing), Remove macro drops it, and one
 *  control.undo takes the last change back. The line's text is pinned. */

#include <QtTest>

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "RackMenu.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QAction* item(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

QMenu* submenu(QMenu* menu, const QString& text)
{
	QAction* action = item(menu, text);
	return action != nullptr ? action->menu() : nullptr;
}

} // namespace

class RackMenuTest : public QObject
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
		setRackMacroNamePrompt({});
		setRackMacroValuePrompt({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theLineNamesTheRouting()
	{
		QCOMPARE(rackStatusText({{QStringLiteral("chain_count"), 1}, {QStringLiteral("selected"), -1}}),
			QStringLiteral("1 chain (the channel's own)"));
		QCOMPARE(rackStatusText({{QStringLiteral("chain_count"), 2}, {QStringLiteral("selected"), 1}}),
			QStringLiteral("2 chains, routed to chain 1"));
		QCOMPARE(rackStatusText({{QStringLiteral("chain_count"), 3}, {QStringLiteral("selected"), -1}}),
			QStringLiteral("3 chains, routed to parallel"));
	}

	void theRackIsEditedFromTheMenu()
	{
		const int index = Engine::mixer()->createChannel();
		const MixerChannel* channel = Engine::mixer()->mixerChannel(index);
		QVERIFY(channel != nullptr);
		const QString id = control::channelIdOf(channel);
		const auto state = [id] {
			return ControlRegistry::instance()->invoke(QStringLiteral("rack.get_state"), {{QStringLiteral("channel"), id}}).result;
		};
		const auto rack = [channel](QMenu& parent) {
			parent.clear();
			addRackMenu(&parent, channel);
			return submenu(&parent, QStringLiteral("Rack"));
		};
		QCOMPARE(state().value(QStringLiteral("chain_count")).toInt(), 1);

		QMenu parent;
		QMenu* menu = rack(parent);
		QVERIFY(menu != nullptr);
		QCOMPARE(menu->actions().first()->text(), QStringLiteral("1 chain (the channel's own)"));
		QVERIFY(submenu(menu, QStringLiteral("Remove chain")) == nullptr);
		item(menu, QStringLiteral("Add parallel chain"))->trigger();
		QCOMPARE(state().value(QStringLiteral("chain_count")).toInt(), 2);

		menu = rack(parent);
		QMenu* route = submenu(menu, QStringLiteral("Route to"));
		// A fresh rack's selector is -1, parallel.
		QVERIFY(item(route, QStringLiteral("Parallel (every chain, summed)"))->isChecked());
		item(route, QStringLiteral("Chain 1"))->trigger();
		QCOMPARE(state().value(QStringLiteral("selected")).toInt(), 1);
		item(route, QStringLiteral("Parallel (every chain, summed)"))->trigger();
		QCOMPARE(state().value(QStringLiteral("selected")).toInt(), -1);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QCOMPARE(state().value(QStringLiteral("selected")).toInt(), 1);

		setRackMacroNamePrompt([] { return QString(); });
		item(menu, QStringLiteral("Add macro..."))->trigger();
		QCOMPARE(state().value(QStringLiteral("macro_count")).toInt(), 0);
		setRackMacroNamePrompt([] { return QStringLiteral("Brightness"); });
		item(menu, QStringLiteral("Add macro..."))->trigger();
		QCOMPARE(state().value(QStringLiteral("macro_count")).toInt(), 1);

		menu = rack(parent);
		QAction* macro = nullptr;
		for (QAction* action : menu->actions())
		{
			if (action->text().startsWith(QStringLiteral("Macro Brightness: "))) { macro = action; }
		}
		QVERIFY(macro != nullptr);
		setRackMacroValuePrompt([](double) { return std::nullopt; });
		const double before = state().value(QStringLiteral("macros")).toArray().first().toObject().value(QStringLiteral("value")).toDouble();
		macro->trigger();
		QCOMPARE(state().value(QStringLiteral("macros")).toArray().first().toObject().value(QStringLiteral("value")).toDouble(), before);
		setRackMacroValuePrompt([](double) { return 0.75; });
		macro->trigger();
		QCOMPARE(state().value(QStringLiteral("macros")).toArray().first().toObject().value(QStringLiteral("value")).toDouble(), 0.75);

		item(submenu(menu, QStringLiteral("Remove macro")), QStringLiteral("Brightness"))->trigger();
		QCOMPARE(state().value(QStringLiteral("macro_count")).toInt(), 0);
		item(submenu(menu, QStringLiteral("Remove chain")), QStringLiteral("Chain 1"))->trigger();
		QCOMPARE(state().value(QStringLiteral("chain_count")).toInt(), 1);
	}

	void noChannelNoMenu()
	{
		QMenu menu;
		addRackMenu(&menu, nullptr);
		QVERIFY(menu.actions().isEmpty());
	}
};

QTEST_MAIN(RackMenuTest)
#include "RackMenuTest.moc"
