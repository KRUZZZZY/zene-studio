/*
 * RenderPresetMenuTest.cpp - File > Render Presets saves, applies and deletes a render preset
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

/*! Over a throwaway preset store (the working directory): Save Preset... adds the form's preset (a
 *  cancelled form adds nothing; re-saving the name asks first), the rebuilt submenu checks Default
 *  Settings until the preset's item applies it - export.preset_list then names it applied - and
 *  Default Settings goes back; Delete removes it after a Yes; a cancelled output pick renders
 *  nothing. The render itself is render.render's own tests' business. */

#include <QtTest>

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "RenderPresetMenu.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

const QString kPreset = QStringLiteral("RenderPresetMenuTest");

QJsonObject listed()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("export.preset_list"), QJsonObject{}).result;
}

bool stored()
{
	for (const QJsonValue& preset : listed().value(QStringLiteral("presets")).toArray())
	{
		if (preset.toObject().value(QStringLiteral("name")).toString() == kPreset) { return true; }
	}
	return false;
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

class RenderPresetMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_world.isValid());
		ConfigManager::inst()->setWorkingDir(m_world.path());
	}

	void cleanupTestCase()
	{
		ControlRegistry::instance()->invoke(QStringLiteral("export.preset_apply"), QJsonObject{});
		ControlRegistry::instance()->invoke(QStringLiteral("export.preset_remove"), {{QStringLiteral("name"), kPreset}});
		setRenderPresetForm({});
		setRenderPresetQuestion({});
		setRenderPresetOutputPicker({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aPresetIsSavedAppliedAndDeletedFromTheMenu()
	{
		QMenu parent;
		QMenu* menu = addRenderPresetMenu(&parent);
		QVERIFY(item(menu, QStringLiteral("Default Settings"))->isChecked());

		setRenderPresetForm([] { return std::nullopt; });
		item(menu, QStringLiteral("Save Preset..."))->trigger();
		QVERIFY(!stored());
		setRenderPresetForm([] { return RenderPresetRequest{kPreset, 48000, QStringLiteral("24"), QStringLiteral("stereo")}; });
		item(menu, QStringLiteral("Save Preset..."))->trigger();
		QVERIFY(stored());
		QString asked;
		setRenderPresetQuestion([&asked](const QString& question) { asked = question; return false; });
		item(menu, QStringLiteral("Save Preset..."))->trigger();
		QVERIFY(asked.contains(kPreset));

		emit menu->aboutToShow();
		QAction* apply = item(menu, kPreset);
		QVERIFY(apply != nullptr);
		QVERIFY(!apply->isChecked());
		apply->trigger();
		QCOMPARE(listed().value(QStringLiteral("applied_preset")).toString(), kPreset);
		QCOMPARE(listed().value(QStringLiteral("settings")).toObject().value(QStringLiteral("sample_rate")).toInt(), 48000);
		emit menu->aboutToShow();
		QVERIFY(item(menu, kPreset)->isChecked());
		QVERIFY(!item(menu, QStringLiteral("Default Settings"))->isChecked());

		item(menu, QStringLiteral("Default Settings"))->trigger();
		QVERIFY(listed().value(QStringLiteral("applied_preset")).toString().isEmpty());

		bool picked = false;
		setRenderPresetOutputPicker([&picked] { picked = true; return QString(); });
		item(menu, QStringLiteral("Render Song with Preset..."))->trigger();
		QVERIFY(picked);

		emit menu->aboutToShow();
		QAction* deleteMenu = item(menu, QStringLiteral("Delete"));
		QVERIFY(deleteMenu != nullptr && deleteMenu->menu() != nullptr);
		setRenderPresetQuestion([](const QString&) { return true; });
		item(deleteMenu->menu(), kPreset)->trigger();
		QVERIFY(!stored());
		emit menu->aboutToShow();
		QVERIFY(item(menu, kPreset) == nullptr);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}

private:
	QTemporaryDir m_world;
};

QTEST_MAIN(RenderPresetMenuTest)
#include "RenderPresetMenuTest.moc"
