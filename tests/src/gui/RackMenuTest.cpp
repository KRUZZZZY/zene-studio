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
 *  control.undo takes the last change back. The line's text is pinned. Q050-047: the channel menu's Rack submenu
 *  offers Zones... and Add Zone... over registered commands, and an empty parallel chain takes its first device
 *  from its own "Add device..." button on the Device panel's heading, and the panel's Add device fills the
 *  channel's selected parallel chain; both are plugin.load, one undo step each. */

#include <QtTest>

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QToolButton>

#include "CommandForm.h"
#include "CommandLists.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePanel.h"
#include "DevicePicker.h"
#include "Engine.h"
#include "Mixer.h"
#include "RackMenu.h"
#include "ShellCommandMenus.h"
#include "ShellSelection.h"

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


//! A fresh channel with two empty parallel chains, shown by a Device panel whose picker always answers with the
//! first loadable effect. `ready` is false when this host loads no effect module.
struct PanelFixture
{
	QString id;
	QString device;
	bool ready = false;
	DevicePanel panel;

	PanelFixture()
	{
		const int index = Engine::mixer()->createChannel();
		id = control::channelIdOf(Engine::mixer()->mixerChannel(index));
		const QJsonArray devices = invoke("plugin.list", {{QStringLiteral("kind"), QStringLiteral("effect")},
			{QStringLiteral("loadable_only"), true}}).result.value(QStringLiteral("devices")).toArray();
		if (devices.isEmpty()) { return; }
		device = devices.first().toObject().value(QStringLiteral("id")).toString();
		ready = true;
		DevicePicker::setChooser([d = device](DevicePicker::Kind, const QString&) { return d; });
		invoke("rack.add_chain", {{QStringLiteral("channel"), id}});
		invoke("rack.add_chain", {{QStringLiteral("channel"), id}});
		panel.resize(1200, 300);
		panel.show();
		ShellSelection::instance()->setChannel(id);
		panel.rebuild();
		QCoreApplication::processEvents();
	}

	~PanelFixture()
	{
		DevicePicker::setChooser({});
		ShellSelection::instance()->setTrack(nullptr);
	}

	static ControlResult invoke(const char* command, const QJsonObject& args)
	{
		return ControlRegistry::instance()->invoke(QLatin1String(command), args);
	}

	int count(int chain) const
	{
		return invoke("rack.get_state", {{QStringLiteral("channel"), id}}).result.value(QStringLiteral("chains"))
			.toArray().at(chain).toObject().value(QStringLiteral("devices")).toInt();
	}
};

} // namespace

class RackMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
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

	//! Q050-047: the mixer channel menu's Rack submenu shows Zones... and Add Zone..., and both run commands the
	//! registry has (rack.get_state lists the zones, rack.zone_add adds one, rack.zone_remove removes one).
	void theZonesEntriesRunRegisteredCommands()
	{
		const int index = Engine::mixer()->createChannel();
		const QString id = control::channelIdOf(Engine::mixer()->mixerChannel(index));
		QMenu menu;
		ShellCommandMenus::populateChannel(&menu, id);
		QMenu* rack = submenu(&menu, QStringLiteral("Rack"));
		QVERIFY(rack != nullptr);
		QVERIFY(item(rack, QStringLiteral("Zones...")) != nullptr);
		QAction* add = item(rack, QStringLiteral("Add Zone..."));
		QVERIFY(add != nullptr);
		QCOMPARE(add->property("controlCommand").toString(), QStringLiteral("rack.zone_add"));
		for (const char* command : {"rack.zone_add", "rack.zone_remove", "rack.get_state"})
		{
			QVERIFY2(ControlRegistry::instance()->hasCommand(QLatin1String(command)), command);
		}
		const ListSpec zones = CommandLists::rackZones(id);
		QCOMPARE(zones.listId, QStringLiteral("rack.get_state"));
		QCOMPARE(zones.extras.first().call.id, QStringLiteral("rack.zone_add"));
		QCOMPARE(zones.actions.first().id, QStringLiteral("rack.zone_remove"));
	}

	//! Q050-047: the Inspector's "Add Insert..." form (plugin.load with its `chain` field) also fills an empty
	//! parallel chain; the Device panel's own buttons are the cases below.
	void anEmptyParallelChainTakesItsFirstDeviceFromTheInsertForm()
	{
		const int index = Engine::mixer()->createChannel();
		const QString id = control::channelIdOf(Engine::mixer()->mixerChannel(index));
		const auto invoke = [](const char* command, const QJsonObject& args) {
			return ControlRegistry::instance()->invoke(QLatin1String(command), args);
		};
		QVERIFY(invoke("rack.add_chain", {{QStringLiteral("channel"), id}}).ok);
		const QJsonArray devices = invoke("plugin.list", {{QStringLiteral("kind"), QStringLiteral("effect")},
			{QStringLiteral("loadable_only"), true}}).result.value(QStringLiteral("devices")).toArray();
		if (devices.isEmpty()) { QSKIP("this host loads no effect module"); }

		CommandForm form(CommandCall{QStringLiteral("plugin.load"), {{QStringLiteral("target"), id}}, {}, QStringLiteral("Add an insert"), true});
		QVERIFY(form.fields().contains(QStringLiteral("chain")));
		QVERIFY(form.fields().contains(QStringLiteral("device")));
		QVERIFY(form.setField(QStringLiteral("device"), devices.first().toObject().value(QStringLiteral("id")).toString()));
		QVERIFY(form.setField(QStringLiteral("chain"), 1));
		form.accept();
		QVERIFY2(form.result().ok, qPrintable(form.errorText()));
		const QJsonArray chains = invoke("rack.get_state", {{QStringLiteral("channel"), id}}).result.value(QStringLiteral("chains")).toArray();
		QCOMPARE(chains.at(1).toObject().value(QStringLiteral("devices")).toInt(), 1);
		QCOMPARE(chains.at(0).toObject().value(QStringLiteral("devices")).toInt(), 0);
	}

	//! Q050-047: an empty parallel chain has an "Add device..." button of its own on the Device panel; it is the
	//! socket's plugin.load with a chain field, one undo step.
	void anEmptyParallelChainHasItsOwnAddDeviceButton()
	{
		PanelFixture f;
		if (!f.ready) { QSKIP("this host loads no effect module"); }
		// Both chains are empty, so each heading has its own button; clicking chain 2's fills chain 2 alone.
		const QList<QToolButton*> own = f.panel.findChildren<QToolButton*>(QStringLiteral("deviceAddToChain"));
		QCOMPARE(own.size(), 2);
		QToolButton* second = nullptr;
		for (QToolButton* button : own) { if (button->property("chain").toInt() == 2) { second = button; } }
		QVERIFY(second != nullptr);
		second->click();
		QCOMPARE(f.count(2), 1);
		QCOMPARE(f.count(1), 0);
		QCOMPARE(f.count(0), 0);
		QVERIFY(f.invoke("control.undo", {}).ok);  // one step
		QCOMPARE(f.count(2), 0);
		QCoreApplication::processEvents();
		QCOMPARE(f.panel.findChildren<QToolButton*>(QStringLiteral("deviceAddToChain")).size(), 2);
	}

	//! Q050-047: the Device panel's Add device loads into the channel's selected parallel chain (rack.set_selected),
	//! and into the channel's own chain while parallel is selected; one undo step each.
	void addDeviceLoadsIntoTheSelectedChain()
	{
		PanelFixture f;
		if (!f.ready) { QSKIP("this host loads no effect module"); }
		auto* add = f.panel.findChild<QToolButton*>(QStringLiteral("deviceAdd"));
		QVERIFY(add != nullptr);
		add->click();  // a fresh rack's selector is -1, parallel: the channel's own chain
		QCOMPARE(f.count(0), 1);
		QVERIFY(f.invoke("control.undo", {}).ok);
		QCOMPARE(f.count(0), 0);
		QVERIFY(f.invoke("rack.set_selected", {{QStringLiteral("channel"), f.id}, {QStringLiteral("chain"), 1}}).ok);
		f.panel.rebuild();
		add->click();
		QCOMPARE(f.count(1), 1);
		QCOMPARE(f.count(0), 0);
		QVERIFY(f.invoke("control.undo", {}).ok);
		QCOMPARE(f.count(1), 0);
		QCOMPARE(f.invoke("rack.get_state", {{QStringLiteral("channel"), f.id}}).result.value(QStringLiteral("selected")).toInt(), 1);
	}

	//! A macro is bound to, and unbound from, a parameter of a real effect on the channel's own chain.
	void aMacroIsBoundToAParameterFromTheMenu()
	{
		const int index = Engine::mixer()->createChannel();
		const MixerChannel* channel = Engine::mixer()->mixerChannel(index);
		const QString id = control::channelIdOf(channel);
		const auto invoke = [](const char* command, const QJsonObject& args) {
			return ControlRegistry::instance()->invoke(QLatin1String(command), args);
		};
		bool loaded = false;
		for (const QJsonValue& device : invoke("plugin.list", {{QStringLiteral("kind"), QStringLiteral("effect")},
			{QStringLiteral("loadable_only"), true}}).result.value(QStringLiteral("devices")).toArray())
		{
			loaded = invoke("plugin.load", {{QStringLiteral("target"), id},
				{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}}).ok;
			if (loaded) { break; }
		}
		if (!loaded) { QSKIP("this host loads no effect module"); }
		QVERIFY(invoke("rack.macro_add", {{QStringLiteral("channel"), id}, {QStringLiteral("name"), QStringLiteral("Tone")}}).ok);
		const auto macro = [&] {
			return invoke("rack.get_state", {{QStringLiteral("channel"), id}}).result
				.value(QStringLiteral("macros")).toArray().first().toObject();
		};

		QMenu parent;
		addRackMenu(&parent, channel);
		QMenu* bind = submenu(submenu(&parent, QStringLiteral("Rack")), QStringLiteral("Bind Tone to"));
		QVERIFY(bind != nullptr);
		QMenu* firstEffect = bind->actions().first()->menu();
		QVERIFY(firstEffect != nullptr && !firstEffect->actions().isEmpty());
		const QString parameter = firstEffect->actions().first()->text();
		firstEffect->actions().first()->trigger();
		QCOMPARE(macro().value(QStringLiteral("target_count")).toInt(), 1);
		QCOMPARE(macro().value(QStringLiteral("targets")).toArray().first().toObject()
			.value(QStringLiteral("parameter")).toString(), parameter);

		QMenu again;
		addRackMenu(&again, channel);
		QMenu* unbind = submenu(submenu(&again, QStringLiteral("Rack")), QStringLiteral("Unbind Tone"));
		QVERIFY(unbind != nullptr);
		unbind->actions().first()->trigger();
		QCOMPARE(macro().value(QStringLiteral("target_count")).toInt(), 0);
	}

	//! Two targets on different chains, same effect and parameter: the Unbind items must tell them apart.
	void unbindItemsNameTheirChain()
	{
		const auto target = [](int chain) {
			return QJsonObject{{QStringLiteral("chain"), chain}, {QStringLiteral("effect"), 0},
				{QStringLiteral("parameter"), QStringLiteral("Volume")}};
		};
		const QString onFirst = unbindTargetText(target(0));
		const QString onSecond = unbindTargetText(target(1));
		QVERIFY2(onFirst != onSecond,
			"two chains' targets share one label, so the user cannot tell which one is removed");
		QVERIFY(onFirst.contains(QStringLiteral("chain 0")));
		QVERIFY(onSecond.contains(QStringLiteral("chain 1")));
		QVERIFY(onFirst.contains(QStringLiteral("Volume")));
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
