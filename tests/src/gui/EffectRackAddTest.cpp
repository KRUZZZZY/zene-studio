/*
 * EffectRackAddTest.cpp - the effect rack's own "Add effect" picks from the device picker
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

/*! The rack (a mixer channel's or an instrument's effect chain, EffectRackView) adds an effect through
 *  the searchable device picker the Device panel uses (DevicePicker.h), not the older select dialog.
 *  What it did before is kept: the chosen device is appended to THIS rack's chain, the rack draws it,
 *  and its controls open; a cancelled picker adds nothing. Runs the real GuiApplication (an effect's
 *  view needs the main window) over the build's own plug-in modules (LMMS_TEST_PLUGIN_DIR). */

#include <QtTest>

#include <QApplication>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "DevicePicker.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "EffectRackView.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Mixer.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QPushButton* addButton(QWidget* rack)
{
	for (QPushButton* button : rack->findChildren<QPushButton*>())
	{
		if (button->text() == EffectRackView::tr("Add effect")) { return button; }
	}
	return nullptr;
}

//! Runs @a handle on the modal dialog the next click opens, once it is up (it is shown inside exec()).
void onceModal(const std::function<void(QWidget*)>& handle, int triesLeft = 50)
{
	QTimer::singleShot(10, [handle, triesLeft] {
		QWidget* modal = QApplication::activeModalWidget();
		if (modal == nullptr && triesLeft > 0) { onceModal(handle, triesLeft - 1); return; }
		handle(modal);
	});
}

} // namespace

class EffectRackAddTest : public QObject
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
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void addEffectPicksFromTheDevicePicker()
	{
		MixerChannel* channel = Engine::mixer()->mixerChannel(Engine::mixer()->createChannel());
		EffectChain* chain = &channel->m_fxChain;
		EffectRackView rack(chain);
		rack.show();
		QPushButton* add = addButton(&rack);
		QVERIFY(add != nullptr);

		QString modalClass;
		QString picked;
		onceModal([&modalClass, &picked](QWidget* modal) {
			modalClass = modal != nullptr ? QString::fromLatin1(modal->metaObject()->className()) : QString();
			auto* picker = qobject_cast<DevicePicker*>(modal);
			if (picker == nullptr)
			{
				if (auto* dialog = qobject_cast<QDialog*>(modal)) { dialog->reject(); }
				return;
			}
			picker->setQuery(QStringLiteral("Amplifier"));
			picked = picker->selectedDevice();
			if (picked.isEmpty()) { picker->reject(); return; }
			static_cast<QDialog*>(picker)->accept();  // the picker's own accept(), as Enter does
		});
		QTest::mouseClick(add, Qt::LeftButton);
		QCOMPARE(modalClass, QStringLiteral("lmms::gui::DevicePicker"));
		if (picked.isEmpty()) { QSKIP("this host catalogues no effect called Amplifier"); }

		// The picked device, appended to this rack's chain, with its controls open - as the old dialog did.
		ControlDeviceEntry entry;
		int index = -1;
		ControlResult error;
		QVERIFY(controlDeviceById(picked, &entry, &index, &error));
		QString pluginName;
		Plugin::Descriptor::SubPluginFeatures::Key key;
		bool useKey = false;
		QVERIFY(controlDeviceModule(entry, &pluginName, &key, &useKey, &error));
		QCOMPARE(static_cast<int>(chain->effects().size()), 1);
		Effect* effect = chain->effects().front();
		QCOMPARE(QString::fromUtf8(effect->descriptor()->name), pluginName);
		QVERIFY2(effect->controls()->isViewVisible(), "the new effect's controls did not open");

		// Cancelled: nothing is added.
		onceModal([](QWidget* modal) {
			if (auto* dialog = qobject_cast<QDialog*>(modal)) { dialog->reject(); }
		});
		QTest::mouseClick(add, Qt::LeftButton);
		QCOMPARE(static_cast<int>(chain->effects().size()), 1);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(EffectRackAddTest)
#include "EffectRackAddTest.moc"
