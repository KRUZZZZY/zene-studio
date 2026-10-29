/*
 * ClipEffectsDialogTest.cpp - the clip menu's "Clip effects..." window (Q050-111)
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
 */

/*! The audio clip's menu item opens the clip's effects window, and every control in it is CLICKED here: Add
 *  effect... (the device picker's test chooser answers), Move up, Bypass / Turn on, a parameter row double-clicked
 *  to a new value, and Remove. Each lands on the clip's own chain - the track's chain stays empty - through the
 *  registered command the agent sends, so one control.undo takes each back; the window shows the chain as it is
 *  (order, on/off, parameter values), as do the menu item's count and the clip view's badge. Runs over the build's
 *  own amplifier module (LMMS_TEST_PLUGIN_DIR). */

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>

#include "AudioBusHandle.h"
#include "ClipEffectsDialog.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePicker.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& command, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(command, args);
}

QString amplifierDevice()
{
	const QJsonArray devices = run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
		.result.value(QStringLiteral("devices")).toArray();
	for (const QJsonValue& device : devices)
	{
		if (device.toObject().value(QStringLiteral("name")).toString() == QLatin1String("amplifier"))
		{
			return device.toObject().value(QStringLiteral("id")).toString();
		}
	}
	return {};
}

QAction* item(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

QPushButton* button(QWidget* dialog, const QString& text)
{
	for (QPushButton* b : dialog->findChildren<QPushButton*>())
	{
		if (b->text() == text) { return b; }
	}
	return nullptr;
}

ClipEffectsDialog* openDialog()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (auto* dialog = qobject_cast<ClipEffectsDialog*>(widget); dialog != nullptr && dialog->isVisible()) { return dialog; }
	}
	return nullptr;
}

//! Clicks the row's own place in the list: a double click opens a parameter, a single click selects a device.
void clickRow(QTreeWidget* list, QTreeWidgetItem* row, bool twice)
{
	list->scrollToItem(row);
	const QPoint at = list->visualItemRect(row).center();
	// An item view reports a double click on the index its press landed on, so the press comes first.
	QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, at);
	if (twice) { QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, at); }
}

QTreeWidgetItem* parameterRow(QTreeWidgetItem* device, const QString& name)
{
	for (int i = 0; i < device->childCount(); ++i)
	{
		if (device->child(i)->text(0) == name) { return device->child(i); }
	}
	return nullptr;
}

float volumeOf(Effect* effect)
{
	for (AutomatableModel* model : effect->findChildren<AutomatableModel*>())
	{
		if (model->displayName() == QLatin1String("Volume")) { return model->value<float>(); }
	}
	return -1.0f;
}

} // namespace

class ClipEffectsDialogTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_amplifier = amplifierDevice();
		QVERIFY2(!m_amplifier.isEmpty(), "this build lists no amplifier module (build the 'amplifier' target)");
		DevicePicker::setChooser([this](DevicePicker::Kind, const QString&) { return m_amplifier; });
	}

	void cleanupTestCase()
	{
		DevicePicker::setChooser({});
		ClipEffectsDialog::setValuePrompt({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theClipMenuOpensAWindowWhoseControlsEditTheClipsChain()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		EffectChain* chain = clip->clipEffects();

		QMenu menu;
		addClipEffectsAction(&menu, clip);
		QAction* open = item(&menu, QStringLiteral("Clip effects..."));
		QVERIFY2(open != nullptr, "the audio clip's menu has no Clip effects... item");
		QVERIFY(clipEffectsBadgeText(*clip).isEmpty());
		open->trigger();
		ClipEffectsDialog* dialog = openDialog();
		QVERIFY2(dialog != nullptr, "Clip effects... opened no window");
		QTreeWidget* list = dialog->deviceList();
		QCOMPARE(list->topLevelItemCount(), 0);

		// Add effect... twice: two amplifiers on the CLIP's chain, none on the track's.
		button(dialog, QStringLiteral("Add effect..."))->click();
		button(dialog, QStringLiteral("Add effect..."))->click();
		QCOMPARE(chain->effects().size(), std::size_t{2});
		QCOMPARE(track->audioBusHandle()->effects()->effects().size(), std::size_t{0});
		QCOMPARE(list->topLevelItemCount(), 2);
		QCOMPARE(list->topLevelItem(0)->text(0), QStringLiteral("1. Amplifier"));
		QCOMPARE(list->topLevelItem(0)->text(1), QStringLiteral("On"));
		Effect* firstAdded = chain->effects()[0];
		Effect* secondAdded = chain->effects()[1];

		// The menu and the clip view's badge say the clip has effects now.
		menu.clear();
		addClipEffectsAction(&menu, clip);
		QVERIFY(item(&menu, QStringLiteral("Clip effects (2)...")) != nullptr);
		QCOMPARE(clipEffectsBadgeText(*clip), QStringLiteral("FX 2"));

		// Move up: the second device first; one undo puts the order back.
		clickRow(list, list->topLevelItem(1), false);
		button(dialog, QStringLiteral("Move up"))->click();
		QCOMPARE(chain->effects()[0], secondAdded);
		QCOMPARE(list->topLevelItem(0)->data(0, Qt::UserRole).toString(), control::effectIdOf(secondAdded));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(chain->effects()[0], firstAdded);
		dialog->refresh();

		// Bypass, then Turn on: the device's own on/off, shown in the row.
		clickRow(list, list->topLevelItem(0), false);
		button(dialog, QStringLiteral("Bypass"))->click();
		QVERIFY(!firstAdded->isEnabled());
		QCOMPARE(list->topLevelItem(0)->text(1), QStringLiteral("Bypassed"));
		QVERIFY(button(dialog, QStringLiteral("Turn on")) != nullptr);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(firstAdded->isEnabled());
		dialog->refresh();

		// A parameter row double-clicked to a new value.
		ClipEffectsDialog::setValuePrompt([](const QString&, double value, double, double) {
			return value == 100.0 ? std::optional<double>(25.0) : std::nullopt;
		});
		QTreeWidgetItem* volume = parameterRow(list->topLevelItem(0), QStringLiteral("Volume"));
		QVERIFY(volume != nullptr);
		QCOMPARE(volume->text(1), QStringLiteral("100"));
		clickRow(list, volume, true);
		QCOMPARE(volumeOf(firstAdded), 25.0f);
		QCOMPARE(parameterRow(list->topLevelItem(0), QStringLiteral("Volume"))->text(1), QStringLiteral("25"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(volumeOf(firstAdded), 100.0f);
		dialog->refresh();

		// Remove: the device leaves the clip's chain; one undo brings it back.
		clickRow(list, list->topLevelItem(1), false);
		button(dialog, QStringLiteral("Remove"))->click();
		QCOMPARE(chain->effects().size(), std::size_t{1});
		QCOMPARE(list->topLevelItemCount(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(chain->effects().size(), std::size_t{2});

		// The menu raises the open window instead of opening a second one.
		menu.clear();
		addClipEffectsAction(&menu, clip);
		item(&menu, QStringLiteral("Clip effects (2)..."))->trigger();
		QCOMPARE(openDialog(), dialog);
		dialog->close();
	}

	//! A MIDI clip has no clip effects: no menu item.
	void aMidiClipHasNoItem()
	{
		Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(track != nullptr);
		QMenu menu;
		addClipEffectsAction(&menu, track->createClip(TimePos(0)));
		QVERIFY(menu.actions().isEmpty());
	}

private:
	QTemporaryDir m_home;
	QString m_amplifier;
};

QTEST_MAIN(ClipEffectsDialogTest)
#include "ClipEffectsDialogTest.moc"
