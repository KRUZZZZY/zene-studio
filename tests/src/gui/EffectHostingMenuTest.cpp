/*
 * EffectHostingMenuTest.cpp - an effect's Hosting submenu follows its oop.get_state entry
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

/*! One real effect on a mixer channel (LMMS_TEST_PLUGIN_DIR): the Hosting submenu's line names the
 *  state oop.get_state reports for exactly that device, Run in a separate process is offered exactly
 *  when the device can choose its mode, Restart only while it runs in a separate process, and an
 *  effect in no chain gets no submenu. Skipped when this host loads no effect module. */

#include <QtTest>

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "EffectChain.h"
#include "EffectHostingMenu.h"
#include "Engine.h"
#include "Mixer.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject{})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QAction* item(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

} // namespace

class EffectHostingMenuTest : public QObject
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
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theLineNamesTheState()
	{
		QCOMPARE(hostingStatusText({{QStringLiteral("hosting"), QJsonObject{{QStringLiteral("state"), QStringLiteral("in-process")}}}}),
			QStringLiteral("Hosting: in-process"));
		QCOMPARE(hostingStatusText(QJsonObject{}), QStringLiteral("Hosting: unknown"));
	}

	void theSubmenuFollowsTheDevicesEntry()
	{
		const int index = Engine::mixer()->createChannel();
		MixerChannel* channel = Engine::mixer()->mixerChannel(index);
		const QString id = control::channelIdOf(channel);
		bool loaded = false;
		for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")},
			{QStringLiteral("loadable_only"), true}}).result.value(QStringLiteral("devices")).toArray())
		{
			loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), id},
				{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}}).ok;
			if (loaded) { break; }
		}
		if (!loaded) { QSKIP("this host loads no effect module"); }
		Effect* effect = channel->m_fxChain.effects().front();

		QJsonObject entry;
		for (const QJsonValue& chain : run(QStringLiteral("oop.get_state")).result.value(QStringLiteral("chains")).toArray())
		{
			for (const QJsonValue& device : chain.toObject().value(QStringLiteral("devices")).toArray())
			{
				if (device.toObject().value(QStringLiteral("target")).toString() == id) { entry = device.toObject(); }
			}
		}
		QVERIFY(!entry.isEmpty());
		const QJsonObject hosting = entry.value(QStringLiteral("hosting")).toObject();

		QMenu menu;
		addEffectHostingMenu(&menu, effect);
		QAction* sub = item(&menu, QStringLiteral("Hosting"));
		QVERIFY(sub != nullptr && sub->menu() != nullptr);
		QCOMPARE(sub->menu()->actions().first()->text(), hostingStatusText(entry));
		QAction* mode = item(sub->menu(), QStringLiteral("Run in a separate process"));
		QAction* restart = item(sub->menu(), QStringLiteral("Restart its process"));
		QVERIFY(mode != nullptr && restart != nullptr);
		QCOMPARE(mode->isEnabled(), hosting.value(QStringLiteral("can_choose_mode")).toBool());
		const bool separate = hosting.value(QStringLiteral("state")).toString() == QStringLiteral("separate-process");
		QCOMPARE(mode->isChecked(), separate);
		QCOMPARE(restart->isEnabled(), separate && hosting.value(QStringLiteral("can_restart")).toBool());

		QMenu none;
		addEffectHostingMenu(&none, nullptr);
		QVERIFY(none.actions().isEmpty());
	}
};

QTEST_MAIN(EffectHostingMenuTest)
#include "EffectHostingMenuTest.moc"
