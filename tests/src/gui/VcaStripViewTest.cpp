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
 *  undoable step the socket makes. Both held here on a real Engine. Q050-171 adds the two
 *  remaining surfaces: Project tools ▸ Groups... (the list dialog's spec over vca.*) and the
 *  Inspector's Clip menu entry for vca.edit_move.
 */

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableButton.h"
#include "CommandLists.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Mixer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "ShellCommandMenus.h"
#include "Song.h"
#include "TimePos.h"
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
		// The strip's Fader is a GuiApplication widget, so the real application is started over a throwaway home.
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
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

	// Q050-171: "New VCA group" is TWO command invocations (vca.create, then vca.assign), so it
	// is two undo steps - not the one step every other menu line takes. Held here because the
	// limitation note states it.
	void newVcaGroupIsCreateThenAssignTwoUndoSteps()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clearVcaGroups();
		QMenu menu;
		populateVcaMenu(&menu, 2, [] {});
		menu.actions().last()->trigger();
		QCOMPARE(mixer->vcaGroups().size(), std::size_t{1});
		VcaGroup* made = mixer->vcaGroups().back();
		QVERIFY(made->contains(2));

		// First undo: the assignment (vca.assign) goes, the group stays.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QCOMPARE(mixer->vcaGroups().size(), std::size_t{1});
		QVERIFY2(!made->contains(2), "the first undo removed the group instead of the assignment");
		// Second undo: the creation (vca.create) goes, the group is gone.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QVERIFY2(mixer->vcaGroups().empty(), "the second undo did not remove the created group");
	}

	// Q050-171: Project tools ▸ Groups... is the list dialog over vca.* - rename, remove, the edit
	// set and the phase lock - and its "New Group..." is vca.create, so the dialog reaches the same
	// commands the socket does.
	void theGroupsListOffersCreateEditSetAndPhaseLock()
	{
		const ListSpec spec = CommandLists::vcaGroups();
		QCOMPARE(spec.title, QStringLiteral("Groups"));
		QCOMPARE(spec.listId, QStringLiteral("vca.list"));
		QCOMPARE(spec.arrayKey, QStringLiteral("groups"));
		QCOMPARE(spec.columns, QStringList({QStringLiteral("id"), QStringLiteral("name"),
			QStringLiteral("gain"), QStringLiteral("muted"), QStringLiteral("soloed"),
			QStringLiteral("member_count")}));
		const struct { const char* label; const char* id; } expected[] = {
			{"Rename...", "vca.rename"},
			{"Remove", "vca.remove"},
			{"Add Track to Edit Set...", "vca.track_add"},
			{"Remove Track from Edit Set...", "vca.track_remove"},
			{"Phase Lock On", "vca.set_phase_lock"},
			{"Phase Lock Off", "vca.set_phase_lock"}};
		QCOMPARE(spec.actions.size(), 6);
		for (int i = 0; i < 6; ++i)
		{
			QCOMPARE(spec.actions[i].label, QString::fromLatin1(expected[i].label));
			QCOMPARE(spec.actions[i].id, QString::fromLatin1(expected[i].id));
			QCOMPARE(spec.actions[i].fromRow.value(QStringLiteral("group")), QStringLiteral("id"));
			const ControlCommand* command = ControlRegistry::instance()->command(spec.actions[i].id);
			QVERIFY2(command != nullptr && command->group == QStringLiteral("vca"), expected[i].id);
		}
		// Phase Lock On and Off are ONE command with the argument fixed, so the row says which.
		QVERIFY(spec.actions[4].fixed.value(QStringLiteral("locked")).toBool());
		QVERIFY(!spec.actions[5].fixed.value(QStringLiteral("locked")).toBool());
		// The two edits ask (the track named in a form), Remove confirms, Rename asks.
		QVERIFY(spec.actions[0].ask && spec.actions[2].ask && spec.actions[3].ask);
		QVERIFY(spec.actions[1].confirm);
		QCOMPARE(spec.extras.size(), 1);
		QCOMPARE(spec.extras[0].label, QStringLiteral("New Group..."));
		QCOMPARE(spec.extras[0].call.id, QStringLiteral("vca.create"));
		QVERIFY(ControlRegistry::instance()->hasCommand(QStringLiteral("vca.create")));
	}

	// Q050-171: the Inspector's Clip menu carries Move With Edit Group..., the vca.edit_move form.
	void theClipsMenuCarriesMoveWithEditGroup()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		QMenu menu;
		ShellCommandMenus::populateClip(&menu, clip);
		QAction* move = nullptr;
		for (QAction* action : menu.actions())
		{
			if (action->property("controlCommand").toString() == QStringLiteral("vca.edit_move")) { move = action; }
		}
		QVERIFY2(move != nullptr, "the Clip menu has no Move With Edit Group entry");
		QVERIFY2(move->isEnabled(), "Move With Edit Group is disabled while its command is registered");
		QVERIFY(move->text().contains(QStringLiteral("Move With Edit Group")));
		QVERIFY(ControlRegistry::instance()->hasCommand(QStringLiteral("vca.edit_move")));
	}

	void theStripsMenuChoosesLinksAndNestingThroughTheCommands()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clearVcaGroups();
		VcaGroup* outer = mixer->createVcaGroup(QStringLiteral("Outer"));
		VcaGroup* inner = mixer->createVcaGroup(QStringLiteral("Inner"));
		VcaGroup* other = mixer->createVcaGroup(QStringLiteral("Other"));
		VcaStripView strip(inner);
		QCOMPARE(strip.groupId(), inner->id());
		QVERIFY(strip.fader() != nullptr);
		QVERIFY(strip.muteButton() != nullptr && strip.soloButton() != nullptr);

		QMenu menu;
		strip.fillMenu(&menu);
		QMenu* links = menuNamed(&menu, QStringLiteral("Links"));
		QMenu* putInside = menuNamed(&menu, QStringLiteral("Put Inside"));
		QVERIFY(links != nullptr && putInside != nullptr);
		QCOMPARE(links->actions().size(), 4);
		for (QAction* action : links->actions()) { QVERIFY(action->isCheckable()); }

		// Mute is linked from the start; unticking it is one recorded vca.set_links.
		QAction* mute = actionNamed(links, QStringLiteral("Mute"));
		QVERIFY(mute->isChecked() == inner->linksMute());
		const bool before = inner->linksMute();
		mute->setChecked(!before);
		QCOMPARE(inner->linksMute(), !before);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QCOMPARE(inner->linksMute(), before);

		// "No Group" is checked while the group sits at the top; choosing another nests it there.
		QAction* none = actionNamed(putInside, QStringLiteral("No Group"));
		QVERIFY(none->isChecked());
		QVERIFY(actionNamed(putInside, QStringLiteral("Inner")) == nullptr);  // never offered as its own parent
		actionNamed(putInside, QStringLiteral("Outer"))->trigger();
		QCOMPARE(inner->parentId(), outer->id());

		// Reopened: Outer is checked, and Outer itself - inside Inner's tree now - cannot take Inner as a parent.
		QMenu again;
		strip.fillMenu(&again);
		QMenu* insideAgain = menuNamed(&again, QStringLiteral("Put Inside"));
		QVERIFY(actionNamed(insideAgain, QStringLiteral("Outer"))->isChecked());
		VcaStripView outerStrip(outer);
		QMenu outerMenu;
		outerStrip.fillMenu(&outerMenu);
		QAction* loop = actionNamed(menuNamed(&outerMenu, QStringLiteral("Put Inside")), QStringLiteral("Inner"));
		QVERIFY(loop != nullptr);
		QVERIFY2(!loop->isEnabled(), "a group was offered a parent that sits inside it");
		QVERIFY(!loop->toolTip().isEmpty());
		QVERIFY(actionNamed(menuNamed(&outerMenu, QStringLiteral("Put Inside")), QStringLiteral("Other"))->isEnabled());

		// No Group puts it back at the top.
		actionNamed(menuNamed(&again, QStringLiteral("Put Inside")), QStringLiteral("No Group"))->trigger();
		QCOMPARE(inner->parentId(), VcaGroup::NoParent);
		QVERIFY(mixer->deleteVcaGroup(other->id()));
		mixer->clearVcaGroups();
	}

	void aStripWhoseGroupIsRemovedRetiresAtOnce()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clearVcaGroups();
		VcaGroup* group = mixer->createVcaGroup(QStringLiteral("Doomed"));
		auto* strip = new VcaStripView(group);
		strip->show();
		QVERIFY(strip->isVisible());
		QVERIFY(mixer->deleteVcaGroup(group->id()));
		QVERIFY2(!strip->isVisible(), "the strip stayed up over freed models");
		QTRY_VERIFY(QPointer<VcaStripView>(strip).isNull());  // deleteLater() ran
	}

	void theContextMenuEventOpensTheSameMenu()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clearVcaGroups();
		VcaGroup* group = mixer->createVcaGroup(QStringLiteral("Keys"));
		VcaStripView strip(group);
		strip.show();
		bool sawLinks = false;
		closePopupWhenUp(&sawLinks);
		QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(4, 4), strip.mapToGlobal(QPoint(4, 4)));
		QApplication::sendEvent(&strip, &event);
		QVERIFY2(sawLinks, "the right-click did not open the strip's menu");
		mixer->clearVcaGroups();
	}

private:
	//! Once the strip's menu is up (it is shown inside exec(), so it is polled for, not waited a fixed time), notes
	//! whether it carries the Links submenu and closes it.
	static void closePopupWhenUp(bool* sawLinks, int triesLeft = 200)
	{
		QTimer::singleShot(10, [sawLinks, triesLeft] {
			auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
			if (menu == nullptr)
			{
				if (triesLeft > 0) { closePopupWhenUp(sawLinks, triesLeft - 1); }
				return;
			}
			*sawLinks = menuNamed(menu, QStringLiteral("Links")) != nullptr;
			menu->close();
		});
	}

	static QMenu* menuNamed(QMenu* menu, const QString& name)
	{
		QAction* action = actionNamed(menu, name);
		return action != nullptr ? action->menu() : nullptr;
	}

	static QAction* actionNamed(QMenu* menu, const QString& name)
	{
		for (QAction* action : menu->actions())
		{
			if (action->text() == name) { return action; }
		}
		return nullptr;
	}

	QTemporaryDir m_home;
};

QTEST_MAIN(VcaStripViewTest)
#include "VcaStripViewTest.moc"
