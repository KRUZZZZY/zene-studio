/*
 * ControllerTemplateMenuTest.cpp - Edit > Controller Templates saves, lists, applies and deletes
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

/*! Over a throwaway working directory (the template store is under the user's presets, so the test
 *  points ConfigManager elsewhere first and asserts it did): with one MIDI-driven control, "Save
 *  current mappings as..." saves a template, the next menu lists it with its mappings resolvable,
 *  applying it binds, and Delete removes it - each through its controller.template_* command. */

#include <QtTest>

#include <QJsonArray>
#include <QMenu>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControllerConnection.h"
#include "ControllerTemplateMenu.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiController.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QAction* starting(QMenu& menu, const QString& prefix)
{
	for (QAction* action : menu.actions())
	{
		if (action->text().startsWith(prefix)) { return action; }
	}
	return nullptr;
}

QJsonObject listed()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("controller.template_list"), QJsonObject{}).result;
}

} // namespace

class ControllerTemplateMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		ConfigManager::inst()->setWorkingDir(m_home.filePath(QStringLiteral("work")));
		ConfigManager::inst()->createWorkingDir();
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY2(listed().value(QStringLiteral("directory")).toString().startsWith(m_home.path()),
			"the template store is not under the throwaway directory - refusing to touch the real one");
	}

	void cleanupTestCase()
	{
		setControllerTemplatePrompt({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theMenuSavesListsAppliesAndDeletes()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		track->volumeModel()->setControllerConnection(new ControllerConnection(new MidiController(Engine::getSong())));

		setControllerTemplatePrompt([] { return std::optional<QString>(QStringLiteral("Desk")); });
		QMenu menu;
		populateControllerTemplateMenu(&menu);
		starting(menu, QMenu::tr("Save current mappings as..."))->trigger();
		QCOMPARE(listed().value(QStringLiteral("count")).toInt(), 1);

		populateControllerTemplateMenu(&menu);
		QAction* apply = starting(menu, QStringLiteral("Desk ("));
		QVERIFY2(apply != nullptr, "the saved template is not listed");
		QVERIFY2(apply->text().contains(QStringLiteral("1/1")), qPrintable(apply->text()));
		apply->trigger();

		QAction* drop = nullptr;
		for (QAction* action : menu.actions())
		{
			if (action->menu() != nullptr && !action->menu()->actions().isEmpty()) { drop = action->menu()->actions().first(); }
		}
		QVERIFY(drop != nullptr);
		drop->trigger();
		QCOMPARE(listed().value(QStringLiteral("count")).toInt(), 0);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(ControllerTemplateMenuTest)
#include "ControllerTemplateMenuTest.moc"
