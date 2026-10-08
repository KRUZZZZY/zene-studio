/*
 * EffectPickerOrganiserTest.cpp - the effect picker's organiser: favourites, hidden, folders, preferred format
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

/*! The organiser marks each picker row with the plug-in's favourite star, hidden flag and folder (item roles on the
 *  name column), a filter reads them, and a row's menu changes them through plugin.organise - so a click and a script
 *  take one path. Held on a real effect's key, and a throwaway key for the menu. */

#include <QtTest>

#include <QApplication>
#include <QInputDialog>
#include <QMenu>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

#include "ControlRegistry.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectPickerOrganiser.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MenuTestSupport.h"
#include "Mixer.h"
#include "PluginOrganiser.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

//! Runs @a handle on the modal widget the next action opens, once it is up (it is shown inside exec()).
void onceModal(const std::function<void(QWidget*)>& handle, int triesLeft = 100)
{
	QTimer::singleShot(10, [handle, triesLeft] {
		QWidget* modal = QApplication::activeModalWidget();
		if (modal == nullptr && triesLeft > 0) { onceModal(handle, triesLeft - 1); return; }
		handle(modal);
	});
}

QStringList commandsOf(QMenu* menu)
{
	QStringList out;
	for (QAction* action : menu->actions())
	{
		if (!action->isSeparator() && action->menu() == nullptr) { out << action->property("controlCommand").toString(); }
	}
	return out;
}

} // namespace

class EffectPickerOrganiserTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		pluginorganiser::setPreferredFormat(QString());
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theOrganiserMarksRowsWithTheirStarAndFilters()
	{
		EffectChain& chain = Engine::mixer()->mixerChannel(0)->m_fxChain;
		Effect* effect = Effect::instantiate(QStringLiteral("amplifier"), &chain, nullptr);
		if (effect == nullptr || !effect->isOkay()) { delete effect; QSKIP("this host loads no effect module"); }
		chain.appendEffect(effect);
		const EffectKey key(effect->descriptor(), QStringLiteral("Amplifier"));
		const QString organiserKey = pickerorganiser::keyOf(key);
		QCOMPARE(organiserKey, pluginorganiser::keyOf(QStringLiteral("amplifier"), {}));
		QVERIFY(pickerorganiser::keyOf(EffectKey()).isEmpty());

		QStandardItemModel model;
		for (const char* name : {"Amplifier", "Reverb", "Reverb"})
		{
			model.appendRow({new QStandardItem(QLatin1String(name)), new QStandardItem(QStringLiteral("Effect"))});
		}
		model.item(2, 1)->setText(QStringLiteral("CLAP effect"));
		pickerorganiser::markRow(model, 5, key);  // no such row: nothing happens
		pluginorganiser::Entry entry;
		entry.favourite = true;
		entry.folder = QStringLiteral("Dynamics");
		pluginorganiser::setEntry(organiserKey, entry);
		pickerorganiser::markRow(model, 0, key);
		QCOMPARE(model.item(0, 0)->text(), QString::fromUtf16(u"★ ") + QStringLiteral("Amplifier"));
		QCOMPARE(model.item(0, 0)->data(pickerorganiser::KeyRole).toString(), organiserKey);
		QVERIFY(model.item(0, 0)->data(pickerorganiser::FavouriteRole).toBool());
		QCOMPARE(model.item(0, 0)->data(pickerorganiser::FolderRole).toString(), QStringLiteral("Dynamics"));
		QCOMPARE(model.item(0, 0)->toolTip(), QStringLiteral("Folder: Dynamics"));
		pickerorganiser::markRow(model, 0, key);  // marking twice does not stack stars
		QCOMPARE(model.item(0, 0)->text(), QString::fromUtf16(u"★ ") + QStringLiteral("Amplifier"));

		const QModelIndex row = model.index(0, 0);
		pickerorganiser::Filter filter;
		QVERIFY(pickerorganiser::accepts(row, filter));
		filter.favouritesOnly = true;
		QVERIFY(pickerorganiser::accepts(row, filter));
		QVERIFY(!pickerorganiser::accepts(model.index(1, 0), filter));  // not a favourite
		filter.favouritesOnly = false;
		filter.folder = QStringLiteral("Dynamics");
		QVERIFY(pickerorganiser::accepts(row, filter));
		filter.folder = QStringLiteral("Elsewhere");
		QVERIFY(!pickerorganiser::accepts(row, filter));
		filter.folder.clear();

		// Hidden rows are left out unless the picker shows them.
		entry.hidden = true;
		pluginorganiser::setEntry(organiserKey, entry);
		pickerorganiser::markRow(model, 0, key);
		QVERIFY(!pickerorganiser::accepts(row, filter));
		filter.showHidden = true;
		QVERIFY(pickerorganiser::accepts(row, filter));

		// The preferred format hides the same plug-in in another format.
		filter.showHidden = false;
		QVERIFY(pickerorganiser::accepts(model.index(1, 0), filter));
		pluginorganiser::setPreferredFormat(QStringLiteral("clap"));
		QVERIFY2(!pickerorganiser::accepts(model.index(1, 0), filter), "a plug-in also listed in the preferred format was not left out");
		QVERIFY(pickerorganiser::accepts(model.index(2, 0), filter));  // the preferred one itself stays
		filter.showHidden = true;
		QVERIFY(pickerorganiser::accepts(model.index(1, 0), filter));
		pluginorganiser::setPreferredFormat(QString());
		pluginorganiser::setEntry(organiserKey, pluginorganiser::Entry());
	}

	void theRowMenuChangesTheOrganiserThroughPluginOrganise()
	{
		const QString key = QStringLiteral("zz-organiser-test-key");
		QWidget host;
		QMenu none(&host);
		pickerorganiser::fillKeyMenu(&none, QString(), &host, [] {});
		QVERIFY(none.actions().isEmpty());

		int changes = 0;
		QMenu menu(&host);
		pickerorganiser::fillKeyMenu(&menu, key, &host, [&changes] { ++changes; });
		QCOMPARE(commandsOf(&menu), (QStringList{QStringLiteral("plugin.organise"), QStringLiteral("plugin.organise"), QStringLiteral("plugin.organise")}));
		menuAction(&menu, {QStringLiteral("Add to Favourites")})->trigger();
		QVERIFY(pluginorganiser::entry(key).favourite);
		QCOMPARE(changes, 1);
		menuAction(&menu, {QStringLiteral("Hide")})->trigger();
		QVERIFY(pluginorganiser::entry(key).hidden);

		QMenu again(&host);
		pickerorganiser::fillKeyMenu(&again, key, &host, [] {});
		QVERIFY(menuAction(&again, {QStringLiteral("Remove from Favourites")}) != nullptr);
		QVERIFY(menuAction(&again, {QStringLiteral("Show in the List")}) != nullptr);
		menuAction(&again, {QStringLiteral("Show in the List")})->trigger();
		menuAction(&again, {QStringLiteral("Remove from Favourites")})->trigger();
		QVERIFY(pluginorganiser::entry(key).isDefault());

		// Move to Folder asks for the folder name in an editable list, and Return alone moves nothing.
		onceModal([](QWidget* modal) {
			auto* input = qobject_cast<QInputDialog*>(modal);
			input->setTextValue(QStringLiteral("  Dynamics "));
			input->accept();
		});
		menuAction(&menu, {QStringLiteral("Move to Folder...")})->trigger();
		QCOMPARE(pluginorganiser::entry(key).folder, QStringLiteral("Dynamics"));
		QVERIFY(pluginorganiser::folders().contains(QStringLiteral("Dynamics")));
		const int afterMove = changes;
		QMenu inFolder(&host);
		pickerorganiser::fillKeyMenu(&inFolder, key, &host, [&changes] { ++changes; });
		onceModal([](QWidget* modal) { qobject_cast<QInputDialog*>(modal)->reject(); });
		menuAction(&inFolder, {QStringLiteral("Move to Folder...")})->trigger();
		QCOMPARE(changes, afterMove);  // cancelled: nothing changed
		QCOMPARE(pluginorganiser::entry(key).folder, QStringLiteral("Dynamics"));
		menuAction(&inFolder, {QStringLiteral("Remove from Folder")})->trigger();
		QVERIFY(pluginorganiser::entry(key).folder.isEmpty());
		QVERIFY(changes > afterMove);

		// fillRowMenu reads the key off the row.
		QStandardItemModel model;
		model.appendRow(new QStandardItem(QStringLiteral("Row")));
		model.item(0)->setData(key, pickerorganiser::KeyRole);
		QMenu rowMenu(&host);
		pickerorganiser::fillRowMenu(&rowMenu, model.index(0, 0), &host, [] {});
		QCOMPARE(rowMenu.actions().size(), 3);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(EffectPickerOrganiserTest)
#include "EffectPickerOrganiserTest.moc"
