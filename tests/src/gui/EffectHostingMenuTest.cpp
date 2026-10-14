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
#include <QDomDocument>
#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QInputDialog>
#include <QTemporaryDir>
#include <QTimer>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectDeviceMenu.h"
#include "EffectHostingMenu.h"
#include "EffectPickerOrganiser.h"
#include "Engine.h"
#include "Mixer.h"
#include "PluginOrganiser.h"

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
		QVERIFY(m_home.isValid());
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
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

	void aLoadedDummyEffectHasADeviceAddress()
	{
		const int index = Engine::mixer()->createChannel();
		MixerChannel* channel = Engine::mixer()->mixerChannel(index);

		QDomDocument document;
		QDomElement chain = document.createElement(QStringLiteral("fxchain"));
		chain.setAttribute(QStringLiteral("numofeffects"), 1);
		document.appendChild(chain);
		QDomElement missingEffect = document.createElement(QStringLiteral("effect"));
		missingEffect.setAttribute(QStringLiteral("name"), QStringLiteral("missing-effect-for-device-address-test"));
		missingEffect.appendChild(document.createElement(QStringLiteral("key")));
		chain.appendChild(missingEffect);

		channel->m_fxChain.loadSettings(chain);
		QCOMPARE(channel->m_fxChain.effects().size(), size_t{1});
		Effect* effect = channel->m_fxChain.effects().front();
		QVERIFY(effect != nullptr);
		QCOMPARE(effect->effectChain(), &channel->m_fxChain);
		QVERIFY(!deviceAddress(effect).isEmpty());
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

	void returnKeepsAnUnfiledEffectUnfiled()
	{
		const QString key = QStringLiteral("test.return.unfiled.effect");
		pluginorganiser::setEntry(key, {});
		pluginorganiser::Entry folderEntry;
		folderEntry.folder = QStringLiteral("Existing folder");
		pluginorganiser::setEntry(QStringLiteral("test.return.folder.source"), folderEntry);
		QVERIFY(pluginorganiser::folders().contains(folderEntry.folder));

		QMenu menu;
		pickerorganiser::fillKeyMenu(&menu, key, nullptr, [] {});
		QAction* move = item(&menu, QStringLiteral("Move to Folder..."));
		QVERIFY(move != nullptr);
		QTimer::singleShot(0, [] {
			auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
			QVERIFY(dialog != nullptr);
			auto* combo = dialog->findChild<QComboBox*>();
			QVERIFY(combo != nullptr);
			QVERIFY(combo->isEditable());
			QTest::keyClick(combo->lineEdit(), Qt::Key_Return);
		});
		move->trigger();

		QVERIFY(pluginorganiser::entry(key).folder.isEmpty());
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(EffectHostingMenuTest)
#include "EffectHostingMenuTest.moc"
