/*
 * VcaStripViewTest.cpp - M3.7: the mixer's VCA strip and the channel's VCA menu
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

/*! The VCA model had no interface (plan R8.6). The strip's controls must be the GROUP's own
 *  models - so the strip, the file, undo and vca.* move one state - and the channel menu
 *  must edit membership through the vca.* commands, so a menu edit is the same recorded,
 *  undoable step the socket makes. Both held here on a real Engine.
 */

#include <QtTest>

#include <QAction>
#include <QMenu>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableButton.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "Mixer.h"
#include "VcaGroup.h"
#include "VcaStripView.h"

using namespace lmms;
using namespace lmms::gui;

class VcaStripViewTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
		while (Engine::mixer()->numChannels() < 3) { Engine::mixer()->createChannel(); }
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theStripsControlsAreTheGroupsOwnModels()
	{
		// A Fader's value float is a GuiApplication widget (SimpleTextFloat reads
		// getGUI()), and a unit test has no GuiApplication. The strip is proved
		// live instead: tests/control-success-paths.py builds a group on the GUI
		// instance and renders the mixer (window.screenshot).
		if (getGUI() == nullptr) { QSKIP("no GuiApplication: a Fader cannot be built in this process"); }
		VcaGroup* group = Engine::mixer()->createVcaGroup(QStringLiteral("Keys"));
		group->addMember(1);
		VcaStripView strip(group);
		QCOMPARE(strip.caption(), QStringLiteral("Keys"));
		QCOMPARE(strip.fader()->model(), group->vcaModel());
		QCOMPARE(strip.muteButton()->model(), group->muteModel());
		QCOMPARE(strip.soloButton()->model(), group->soloModel());
		// Moving the strip's model is moving the group: the member reads the gain.
		group->vcaModel()->setValue(0.5f);
		Engine::mixer()->refreshGroups();
		QVERIFY(group->gain() < 1.0f);
		group->vcaModel()->setValue(1.0f);
		QVERIFY(Engine::mixer()->deleteVcaGroup(group->id()));
	}

	void theChannelMenuEditsMembershipThroughTheCommands()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clearVcaGroups();
		const std::size_t before = mixer->vcaGroups().size();
		int changes = 0;
		QMenu menu;
		populateVcaMenu(&menu, 2, [&changes]() { ++changes; });
		QList<QAction*> entries;
		for (QAction* action : menu.actions()) { if (!action->isSeparator()) { entries.append(action); } }
		QCOMPARE(entries.size(), 1); // no group yet: only "New VCA group"
		entries.last()->trigger();
		QCOMPARE(mixer->vcaGroups().size(), before + 1);
		VcaGroup* made = mixer->vcaGroups().back();
		QVERIFY2(made->contains(2), "the new group does not hold the channel it was made from");
		QCOMPARE(changes, 1);

		// Reopened, the menu shows that group CHECKED; toggling it unassigns.
		QMenu again;
		populateVcaMenu(&again, 2, [&changes]() { ++changes; });
		QAction* member = again.actions().first();
		QVERIFY(member->isCheckable() && member->isChecked());
		member->trigger();
		QVERIFY2(!made->contains(2), "unticking the group did not unassign the channel");

		// And it was ONE recorded step: control.undo puts the member back.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QVERIFY2(made->contains(2), "control.undo did not restore the membership the menu removed");
	}
};

QTEST_MAIN(VcaStripViewTest)
#include "VcaStripViewTest.moc"
