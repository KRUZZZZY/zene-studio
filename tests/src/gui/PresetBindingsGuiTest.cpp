/*
 * PresetBindingsGuiTest.cpp - the preset menus show the controller bindings a load restored (Q050-331)
 *                                       reversibility contract (0.5.0 F6)
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

/*! Q050-331's interface half, driven through the real menus: a track's Effect chain presets > Apply <name>
 *  runs chain.apply and then shows which controller bindings it restored; a track's Track Preset > Load
 *  Preset... opens track.preset_load's form, and after Run the form's answer names the restored bindings. */

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>

#include "AudioBusHandle.h"
#include "ChainPresetMenu.h"
#include "CommandForm.h"
#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MenuTestSupport.h"
#include "MidiLearn.h"
#include "ShellCommandMenus.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString deviceNamed(const QString& name)
{
	const QJsonArray devices = run(QStringLiteral("plugin.list"), {{QStringLiteral("loadable_only"), true}})
		.result.value(QStringLiteral("devices")).toArray();
	for (const QJsonValue& device : devices)
	{
		if (device.toObject().value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) == 0)
		{
			return device.toObject().value(QStringLiteral("id")).toString();
		}
	}
	return QString();
}

Track* trackOf(const QString& id)
{
	ControlResult ignored;
	return control::resolveTrack(id, &ignored);
}

QString addTrack()
{
	static int made = 0;
	const QString track = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}})
		.result.value(QStringLiteral("track")).toString();
	run(QStringLiteral("track.rename"), {{QStringLiteral("track"), track}, {QStringLiteral("name"),
		QStringLiteral("Preset Gui %1").arg(++made)}});
	return track;
}

//! A track with an Amplifier whose first parameter is bound to channel 5, CC 40.
QString boundTrack(QString* target)
{
	const QString track = addTrack();
	const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), track},
		{QStringLiteral("device"), deviceNamed(QStringLiteral("amplifier"))}});
	if (!loaded.ok) { qWarning("plugin.load: %s", qPrintable(loaded.errorMessage)); return {}; }
	auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(trackOf(track));
	Effect* effect = instrumentTrack->audioBusHandle()->effects()->effects().front();
	const QList<FloatModel*> models = effect->findChildren<FloatModel*>();
	if (models.isEmpty()) { return {}; }
	*target = models.first()->displayName();
	// Bound the way controller.map binds (bindMidiController).
	if (bindMidiController(models.first(), 5, 40) == nullptr) { return {}; }
	return track;
}

CommandForm* visibleForm()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (auto* form = qobject_cast<CommandForm*>(widget); form != nullptr && form->isVisible()) { return form; }
	}
	return nullptr;
}

} // namespace

class PresetBindingsGuiTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_world;

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
		QVERIFY2(!deviceNamed(QStringLiteral("amplifier")).isEmpty(), "the build's amplifier module is needed");
	}

	void cleanupTestCase()
	{
		run(QStringLiteral("chain.remove"), {{QStringLiteral("name"), QStringLiteral("gui-bound")}});
		setChainPresetNamePrompt({});
		setChainPresetNotice({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theChainMenuShowsTheRestoredBindings()
	{
		QString target;
		const QString source = boundTrack(&target);
		QVERIFY(!source.isEmpty());
		QMenu sourceMenu;
		addChainPresetMenu(&sourceMenu, trackOf(source));
		setChainPresetNamePrompt([] { return QStringLiteral("gui-bound"); });
		QAction* save = menuAction(&sourceMenu, {QStringLiteral("Effect chain presets"), QStringLiteral("Save chain as preset...")});
		QVERIFY(save != nullptr);
		save->trigger();

		const QString destination = addTrack();
		QMenu destinationMenu;
		addChainPresetMenu(&destinationMenu, trackOf(destination));
		QString notice;
		setChainPresetNotice([&notice](const QString& text) { notice = text; });
		QAction* apply = menuAction(&destinationMenu, {QStringLiteral("Effect chain presets"), QStringLiteral("Apply gui-bound")});
		QVERIFY(apply != nullptr);
		apply->trigger();
		QVERIFY2(notice.contains(QStringLiteral("1 of 1 controller bindings restored")), qPrintable(notice));
		QVERIFY2(notice.contains(target) && notice.contains(QStringLiteral("channel 5, CC 40")), qPrintable(notice));
	}

	void theTrackPresetFormShowsTheRestoredBindings()
	{
		QString target;
		const QString source = boundTrack(&target);
		QVERIFY(!source.isEmpty());
		const QString path = m_world.filePath(QStringLiteral("gui-bound.xpf"));
		QVERIFY(run(QStringLiteral("track.preset_save"), {{QStringLiteral("track"), source}, {QStringLiteral("path"), path}}).ok);
		const QString destination = addTrack();

		QMenu menu;
		ShellCommandMenus::populateTrack(&menu, trackOf(destination));
		QAction* load = menuAction(&menu, {QStringLiteral("Track Preset"), QStringLiteral("Load Preset...")});
		QVERIFY(load != nullptr);
		QCOMPARE(load->property("controlCommand").toString(), QStringLiteral("track.preset_load"));
		load->trigger();
		CommandForm* form = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((form = visibleForm()) != nullptr, 3000);
		QVERIFY(form->setField(QStringLiteral("path"), path));
		QPushButton* runButton = nullptr;
		for (QPushButton* button : form->findChildren<QPushButton*>())
		{
			if (button->text().remove(QLatin1Char('&')) == QStringLiteral("Run")) { runButton = button; }
		}
		QVERIFY(runButton != nullptr);
		QTest::mouseClick(runButton, Qt::LeftButton);
		auto* answer = form->findChild<QPlainTextEdit*>(QStringLiteral("commandFormResult"));
		QVERIFY(answer != nullptr);
		QTRY_VERIFY_WITH_TIMEOUT(answer->isVisible(), 3000);
		const QString shown = answer->toPlainText();
		QVERIFY2(shown.contains(QStringLiteral("1 of 1 controller bindings restored")), qPrintable(shown));
		QVERIFY2(shown.contains(target), qPrintable(shown));
		form->close();
	}
};

QTEST_MAIN(PresetBindingsGuiTest)
#include "PresetBindingsGuiTest.moc"
