/*
 * ChainPresetMenuTest.cpp - a track menu saves, applies and deletes an effect chain preset
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

/*! Over a throwaway preset store (the working directory): a sample track with one real effect
 *  (the build's own plugin modules, LMMS_TEST_PLUGIN_DIR) saves its chain from the submenu, a
 *  cancelled name saves nothing, re-saving an existing name asks first (No keeps the old file),
 *  the rebuilt submenu applies the preset to a second, empty track - which then holds the same
 *  device - and Delete removes it after a Yes. Skipped when this host loads no effect module. */

#include <QtTest>

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>

#include "ChainPresetMenu.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

const QString kPreset = QStringLiteral("ChainPresetMenuTest");

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject{})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QStringList presets()
{
	QStringList names;
	for (const QJsonValue& preset : run(QStringLiteral("chain.list")).result.value(QStringLiteral("presets")).toArray())
	{
		names << preset.toObject().value(QStringLiteral("name")).toString();
	}
	return names;
}

QStringList chainOf(const QString& track)
{
	QStringList plugins;
	const QJsonArray chains = run(QStringLiteral("dsp.get_state"), {{QStringLiteral("target"), track}})
		.result.value(QStringLiteral("chains")).toArray();
	for (const QJsonValue& fx : chains.isEmpty() ? QJsonArray() : chains.first().toObject().value(QStringLiteral("devices")).toArray())
	{
		plugins << fx.toObject().value(QStringLiteral("plugin")).toString();
	}
	return plugins;
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

class ChainPresetMenuTest : public QObject
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
		QVERIFY(m_world.isValid());
		ConfigManager::inst()->setWorkingDir(m_world.path());
	}

	void cleanupTestCase()
	{
		run(QStringLiteral("chain.remove"), {{QStringLiteral("name"), kPreset}});
		setChainPresetNamePrompt({});
		setChainPresetQuestion({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aChainIsSavedAppliedAndDeletedFromTheMenu()
	{
		Track* source = Track::create(Track::Type::Sample, Engine::getSong());
		Track* destination = Track::create(Track::Type::Sample, Engine::getSong());
		const QString sourceId = control::trackIdOf(source);
		const QString destinationId = control::trackIdOf(destination);
		const QJsonArray effects = run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")},
			{QStringLiteral("loadable_only"), true}}).result.value(QStringLiteral("devices")).toArray();
		bool loaded = false;
		for (const QJsonValue& device : effects)
		{
			loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), sourceId},
				{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}}).ok;
			if (loaded) { break; }
		}
		if (!loaded) { QSKIP("this host loads no effect module"); }
		const QStringList chain = chainOf(sourceId);
		QCOMPARE(chain.size(), 1);
		QVERIFY(chainOf(destinationId).isEmpty());

		QMenu sourceMenu;
		addChainPresetMenu(&sourceMenu, source);
		QMenu* presetMenu = sourceMenu.actions().first()->menu();
		QVERIFY(presetMenu != nullptr);
		QAction* save = item(presetMenu, QStringLiteral("Save chain as preset..."));
		QVERIFY(save != nullptr);

		setChainPresetNamePrompt([] { return QString(); });
		save->trigger();
		QVERIFY(!presets().contains(kPreset));
		setChainPresetNamePrompt([] { return kPreset; });
		save->trigger();
		QVERIFY(presets().contains(kPreset));

		QString asked;
		setChainPresetQuestion([&asked](const QString& question) { asked = question; return false; });
		save->trigger();
		QVERIFY(asked.contains(kPreset));

		QMenu destinationMenu;
		addChainPresetMenu(&destinationMenu, destination);
		QMenu* destinationPresets = destinationMenu.actions().first()->menu();
		emit destinationPresets->aboutToShow();
		QAction* apply = item(destinationPresets, QStringLiteral("Apply %1").arg(kPreset));
		QVERIFY(apply != nullptr);
		apply->trigger();
		QCOMPARE(chainOf(destinationId), chain);

		QAction* deleteMenu = item(destinationPresets, QStringLiteral("Delete"));
		QVERIFY(deleteMenu != nullptr && deleteMenu->menu() != nullptr);
		QAction* remove = item(deleteMenu->menu(), kPreset);
		QVERIFY(remove != nullptr);
		setChainPresetQuestion([](const QString&) { return true; });
		remove->trigger();
		QVERIFY(!presets().contains(kPreset));

		emit destinationPresets->aboutToShow();
		QVERIFY(item(destinationPresets, QStringLiteral("Apply %1").arg(kPreset)) == nullptr);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}

	void aTrackWithNoChainGetsNoMenu()
	{
		QMenu menu;
		addChainPresetMenu(&menu, Track::create(Track::Type::Automation, Engine::getSong()));
		QVERIFY(menu.actions().isEmpty());
		addChainPresetMenu(&menu, nullptr);
		QVERIFY(menu.actions().isEmpty());
	}

private:
	QTemporaryDir m_world;
};

QTEST_MAIN(ChainPresetMenuTest)
#include "ChainPresetMenuTest.moc"
