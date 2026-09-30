/*
 * VcaStripLifetimeTest.cpp - BUGS_FOUND 11.12: a removed VCA group's strip never paints freed models
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

/*! BUGS_FOUND 11.12, found by the gate run's ControlVcaCommands failing once and reproduced at
 *  ~1 in 12: vca.remove frees the group's fader, mute and solo models, the mixer rebuilds its VCA
 *  strips only on its 500 ms sync, and a paint in between read the freed fader model (SIGSEGV in
 *  Fader::calculateKnobPosYFromModel, fault address 0x6c - ModelView's QPointer had gone null).
 *  Here the window is made deterministic: the group is removed and the mixer is repainted AT
 *  ONCE, before any sync can run. Needs the real application (a Fader cannot be built without
 *  it - the reason VcaStripViewTest skips its strip slot). */

#include <QtTest>

#include "ControlRegistry.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MixerView.h"
#include "VcaStripView.h"

using namespace lmms;
using namespace lmms::gui;

class VcaStripLifetimeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
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

	void aRemovedGroupsStripIsNeverPaintedAgain()
	{
		MixerView* mixer = getGUI()->mixerView();
		getGUI()->mainWindow()->show();
		mixer->parentWidget()->show();
		mixer->show();
		const ControlResult created = ControlRegistry::instance()->invoke(QStringLiteral("vca.create"),
			{{QStringLiteral("name"), QStringLiteral("Drums")}});
		QVERIFY2(created.ok, qPrintable(created.errorMessage));
		mixer->syncWithMixer();
		QCOMPARE(mixer->findChildren<VcaStripView*>().size(), 1);
		QVERIFY(mixer->findChildren<VcaStripView*>().first()->isVisible());

		const ControlResult removed = ControlRegistry::instance()->invoke(QStringLiteral("vca.remove"),
			{{QStringLiteral("group"), created.result.value(QStringLiteral("group")).toString()}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		// No event has run: the sync has not rebuilt anything. This paint is the one that crashed
		// (grab() renders synchronously; repaint() skips a window offscreen has not exposed).
		for (VcaStripView* strip : mixer->findChildren<VcaStripView*>()) { strip->grab(); }
		mixer->grab();
		for (VcaStripView* strip : mixer->findChildren<VcaStripView*>()) { QVERIFY(!strip->isVisible()); }
		QTRY_VERIFY(mixer->findChildren<VcaStripView*>().isEmpty());
	}

	void aFaderWhoseModelDiesPaintsNothing()
	{
		auto* model = new FloatModel(1.f, 0.f, 2.f, 0.01f);
		Fader fader(model, QStringLiteral("Doomed"), nullptr);
		fader.grab();
		delete model;
		QVERIFY(fader.model() == nullptr);
		fader.grab();  // must not read through the null
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(VcaStripLifetimeTest)
#include "VcaStripLifetimeTest.moc"
