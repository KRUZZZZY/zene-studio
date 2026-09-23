/*
 * FocusDeskWorkspacesTest.cpp - §6.1 workspaces as declarative arrangements
 *                               (work-list row 7 + row 6's layout-as-data).
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

#include <QApplication>
#include <QSignalSpy>
#include <QSplitter>
#include <QtTest>

#include "FocusDesk.h"
#include "FocusDeskWorkspaces.h"

using namespace lmms::gui;

//! The v1 workspace data restates the B-desk mockup (membership = `data-in`,
//! rails = `[data-ws]` columns, flagship = the STAGE map), the validator that
//! keeps a list honest against the register, and how the desk APPLIES one:
//! re-measured rails, parked non-members, flagship only when the focus leaves.
class FocusDeskWorkspacesTest : public QObject
{
	Q_OBJECT

private slots:
	//! The declaration itself: five ids, the mockup's exact rail widths and
	//! flagships, every member in the register, scope stated (C2).
	void theV1ListIsTheBDeskDeclaration()
	{
		const auto workspaces = FocusDeskWorkspaces::v1Workspaces();
		const auto rows = FocusDeskModules::v1Register();
		QCOMPARE(workspaces.size(), 5);
		QVERIFY2(FocusDeskWorkspaces::validate(workspaces, rows).isEmpty(),
			qPrintable(FocusDeskWorkspaces::validate(workspaces, rows).join("; ")));

		struct Expected
		{
			const char* id;
			int leftRail;
			int rightRail;
			const char* flagship;
		};
		const Expected expected[] = {
			{"compose", 300, 300, "detaileditor"},
			{"record", 300, 300, "detaileditor"},
			{"mix", 300, 392, "mixer"},
			{"design", 280, 336, "modulation"},
			{"perform", 268, 300, "session"},
		};
		for (int i = 0; i < 5; ++i)
		{
			QCOMPARE(workspaces[i].id, QString::fromLatin1(expected[i].id));
			QCOMPARE(workspaces[i].leftRail, expected[i].leftRail);
			QCOMPARE(workspaces[i].rightRail, expected[i].rightRail);
			QCOMPARE(workspaces[i].flagship, QString::fromLatin1(expected[i].flagship));
			QCOMPARE(workspaces[i].scope, WorkspaceScope::Machine);
			QVERIFY(!workspaces[i].members.isEmpty());
		}

		// Membership from the mockup's data-in: p-arr only in compose/record,
		// p-rack only in compose/design/mix, the six shared panels everywhere.
		const FocusWorkspace* compose = FocusDeskWorkspaces::find(workspaces, QStringLiteral("compose"));
		const FocusWorkspace* mix = FocusDeskWorkspaces::find(workspaces, QStringLiteral("mix"));
		const FocusWorkspace* perform = FocusDeskWorkspaces::find(workspaces, QStringLiteral("perform"));
		QVERIFY(compose != nullptr && mix != nullptr && perform != nullptr);
		QCOMPARE(compose->members.size(), 8);
		QVERIFY(compose->members.contains(QStringLiteral("arrangement")));
		QVERIFY(compose->members.contains(QStringLiteral("devicechain")));
		QCOMPARE(mix->members.size(), 7);
		QVERIFY(!mix->members.contains(QStringLiteral("arrangement")));
		QVERIFY(mix->members.contains(QStringLiteral("devicechain")));
		QCOMPARE(perform->members.size(), 6);
		QVERIFY(!perform->members.contains(QStringLiteral("devicechain")));
		QVERIFY(perform->members.contains(QStringLiteral("detaileditor")));

		// C2: the scope's own words, spelled for the on-screen label.
		QCOMPARE(workspaceScopeName(WorkspaceScope::Machine),
			QStringLiteral("this machine"));
		QCOMPARE(workspaceScopeName(WorkspaceScope::Project),
			QStringLiteral("this project"));

		QVERIFY(FocusDeskWorkspaces::find(workspaces, QStringLiteral("nope")) == nullptr);
	}

	//! A validator that cannot fail is not evidence: each block below breaks
	//! exactly one rule and must name it.
	void theValidatorRejectsEveryWayAListCanBeWrong()
	{
		const auto good = FocusDeskWorkspaces::v1Workspaces();
		const auto rows = FocusDeskModules::v1Register();
		QVERIFY(FocusDeskWorkspaces::validate(good, rows).isEmpty());

		{ // 1. no workspaces at all
			QVERIFY(!FocusDeskWorkspaces::validate({}, rows).isEmpty());
		}
		{ // 2. a duplicate id
			auto workspaces = good;
			workspaces.append(workspaces.first());
			QVERIFY(!FocusDeskWorkspaces::validate(workspaces, rows).isEmpty());
		}
		{ // 3. a member the register does not know
			auto workspaces = good;
			workspaces[0].members.append(QStringLiteral("teleprompter"));
			QVERIFY(!FocusDeskWorkspaces::validate(workspaces, rows).isEmpty());
		}
		{ // 4. a flagship that is not declared a member
			auto workspaces = good;
			// Not `mixer`: it IS a member of compose (p-mix is in all five), so
			// that mutation would break no rule. automation is a register row
			// and a member of no v1 workspace.
			workspaces[0].flagship = QStringLiteral("automation");
			QVERIFY(!FocusDeskWorkspaces::validate(workspaces, rows).isEmpty());
		}
		{ // 5. a rail width that is not a width
			auto workspaces = good;
			workspaces[0].leftRail = 0;
			QVERIFY(!FocusDeskWorkspaces::validate(workspaces, rows).isEmpty());
		}
		{ // 6. an empty id or title
			auto workspaces = good;
			workspaces[0].title.clear();
			QVERIFY(!FocusDeskWorkspaces::validate(workspaces, rows).isEmpty());
		}
		{ // 7. a workspace declaring no members
			auto workspaces = good;
			workspaces[0].members.clear();
			QVERIFY(!FocusDeskWorkspaces::validate(workspaces, rows).isEmpty());
		}
	}

	//! Applying a workspace: rails re-measured from the declaration, mounted
	//! non-members parked (their chips still say so), the flagship promoted
	//! through the `mod:` seam because the focus left the workspace, and the
	//! change announced exactly once per applied id (idempotent).
	void aWorkspaceReMeasuresTheRailsAndParksNonMembers()
	{
		QWidget host;
		FocusDesk desk;
		desk.resize(1200, 800);
		desk.show();
		QVERIFY(desk.mountModule(QStringLiteral("arrangement"), new QWidget(&host)));
		QVERIFY(desk.mountModule(QStringLiteral("automation"), new QWidget(&host)));
		QVERIFY(desk.mountModule(QStringLiteral("project"), new QWidget(&host)));
		QVERIFY(desk.mountModule(QStringLiteral("mixer"), new QWidget(&host)));
		QVERIFY(desk.mountModule(QStringLiteral("devicechain"), new QWidget(&host)));
		QCOMPARE(desk.focused(), QStringLiteral("arrangement"));
		QCOMPARE(desk.workspace(), QString());
		QApplication::processEvents();

		QSignalSpy changed(&desk, &FocusDesk::workspaceChanged);
		QVERIFY(desk.applyWorkspace(QStringLiteral("mix")));
		QCOMPARE(desk.workspace(), QStringLiteral("mix"));
		QCOMPARE(changed.count(), 1);
		QApplication::processEvents();

		// arrangement is not a member of mix, so the flagship takes the stage
		// through dispatchAction; automation and project are members of no v1
		// workspace and park; devicechain is a member and keeps its home.
		QCOMPARE(desk.focused(), QStringLiteral("mixer"));
		QCOMPARE(desk.chipState(QStringLiteral("mixer")), QStringLiteral("stage"));
		QCOMPARE(desk.chipState(QStringLiteral("arrangement")), QStringLiteral("parked"));
		QCOMPARE(desk.chipState(QStringLiteral("automation")), QStringLiteral("parked"));
		QCOMPARE(desk.chipState(QStringLiteral("project")), QStringLiteral("parked"));
		QCOMPARE(desk.chipState(QStringLiteral("devicechain")), QStringLiteral("right"));

		// The rails re-measured to the declaration: right rail = 392px.
		auto* splitter = desk.findChild<QSplitter*>(QStringLiteral("focusDeskSplitter"));
		QVERIFY(splitter != nullptr);
		const QList<int> sizes = splitter->sizes();
		QCOMPARE(sizes.size(), 3);
		QVERIFY2(qAbs(sizes[2] - 392) <= 8,
			qPrintable(QStringLiteral("right rail = %1, wanted 392").arg(sizes[2])));

		// Idempotent per id: the pane's observer answers our own settings.set
		// write with the same id and it must be a no-op, not a second change.
		QVERIFY(desk.applyWorkspace(QStringLiteral("mix")));
		QCOMPARE(changed.count(), 1);

		// An id this build does not have: refused through moduleRefused and
		// nothing changes.
		QSignalSpy refused(&desk, &FocusDesk::moduleRefused);
		QVERIFY(!desk.applyWorkspace(QStringLiteral("studio")));
		QCOMPARE(desk.workspace(), QStringLiteral("mix"));
		QCOMPARE(refused.count(), 1);
	}

	//! Membership is read at mount time too: a module mounted AFTER the
	//! workspace was applied lands by its membership, not by its register
	//! home - never inferred from what happens to be mounted (§6.1).
	void modulesMountedAfterAWorkspaceLandByItsMembership()
	{
		QWidget host;
		FocusDesk desk;
		QVERIFY(desk.applyWorkspace(QStringLiteral("compose")));

		QVERIFY(desk.mountModule(QStringLiteral("project"), new QWidget(&host)));
		QCOMPARE(desk.chipState(QStringLiteral("project")), QStringLiteral("parked"));

		QVERIFY(desk.mountModule(QStringLiteral("devicechain"), new QWidget(&host)));
		QCOMPARE(desk.chipState(QStringLiteral("devicechain")), QStringLiteral("right"));

		// The workspace parks a flagship-pending choice, so the first mount
		// does not steal the stage; the flagship claims it when it arrives.
		QVERIFY(desk.mountModule(QStringLiteral("arrangement"), new QWidget(&host)));
		QCOMPARE(desk.chipState(QStringLiteral("arrangement")), QStringLiteral("parked"));
		QVERIFY(desk.mountModule(QStringLiteral("detaileditor"), new QWidget(&host)));
		QCOMPARE(desk.focused(), QStringLiteral("detaileditor"));
		QCOMPARE(desk.chipState(QStringLiteral("detaileditor")), QStringLiteral("stage"));
	}

	//! Design's flagship (modulation) may not take the stage in this build -
	//! the register wins over the mockup's STAGE map, and the refusal is
	//! SAID (moduleRefused), while the workspace's other work still lands.
	void aFlagshipTheRegisterForbidsIsRefusedNotSilent()
	{
		QWidget host;
		FocusDesk desk;
		QVERIFY(desk.mountModule(QStringLiteral("arrangement"), new QWidget(&host)));
		QVERIFY(desk.mountModule(QStringLiteral("devicechain"), new QWidget(&host)));
		QCOMPARE(desk.focused(), QStringLiteral("arrangement"));
		QApplication::processEvents();

		QSignalSpy refused(&desk, &FocusDesk::moduleRefused);
		QVERIFY(desk.applyWorkspace(QStringLiteral("design")));
		QCOMPARE(desk.workspace(), QStringLiteral("design"));

		// The flagship could not be promoted - said, with a reason; and the
		// stage honestly keeps the module it still shows.
		QCOMPARE(refused.count(), 1);
		QVERIFY(!refused.first().last().toString().isEmpty());
		QCOMPARE(desk.focused(), QStringLiteral("arrangement"));
		QCOMPARE(desk.chipState(QStringLiteral("arrangement")), QStringLiteral("stage"));
	}

	//! Row 6's layout-as-data: the workspace and rail widths travel through
	//! save/restore, and the explicit rail keys win over the workspace's own
	//! widths (they are the user's later resize of those rails).
	void theLayoutRoundTripCarriesWorkspaceAndRails()
	{
		QWidget host;
		FocusDesk desk;
		desk.resize(1200, 800);
		desk.show();
		QVERIFY(desk.mountModule(QStringLiteral("arrangement"), new QWidget(&host)));
		QVERIFY(desk.applyWorkspace(QStringLiteral("design")));
		QApplication::processEvents();

		const QVariantMap saved = desk.saveLayout();
		QCOMPARE(saved.value(QStringLiteral("workspace")).toString(), QStringLiteral("design"));
		QVERIFY(saved.contains(QStringLiteral("leftRail")));
		QVERIFY(saved.contains(QStringLiteral("rightRail")));
		const int savedLeft = saved.value(QStringLiteral("leftRail")).toInt();
		QVERIFY(savedLeft > 0);

		// The restored desk is shown at the same geometry BEFORE the restore,
		// because QSplitter stores pixel widths as ratios of its current size:
		// applying 280px to a never-shown widget would record a ratio against a
		// placeholder width and read back as something else entirely. A restart
		// restores into a live window, which is what this models.
		FocusDesk restored;
		restored.resize(1200, 800);
		restored.show();
		QApplication::processEvents();
		QVERIFY(restored.mountModule(QStringLiteral("arrangement"), new QWidget(&host)));
		QVERIFY(restored.restoreLayout(saved));
		QCOMPARE(restored.workspace(), QStringLiteral("design"));
		auto* splitter = restored.findChild<QSplitter*>(QStringLiteral("focusDeskSplitter"));
		QVERIFY(splitter != nullptr);
		const QList<int> sizes = splitter->sizes();
		QVERIFY2(qAbs(sizes[0] - savedLeft) <= 8,
			qPrintable(QStringLiteral("left rail = %1, saved %2").arg(sizes[0]).arg(savedLeft)));

		// An unknown workspace id in a layout is an invalid layout, not a
		// workspace waiting to be created - refused, and `ok` says so.
		QVariantMap bad = saved;
		bad.insert(QStringLiteral("workspace"), QStringLiteral("studio"));
		FocusDesk other;
		QVERIFY(!other.restoreLayout(bad));
	}
};

// The platform plugin has to be chosen before the QApplication exists; the gate
// runner exports QT_QPA_PLATFORM=offscreen and CI does not, so default to
// offscreen when nothing else is configured and no display is attached.
int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
		&& qEnvironmentVariableIsEmpty("DISPLAY")
		&& qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}

	QApplication app(argc, argv);
	FocusDeskWorkspacesTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "FocusDeskWorkspacesTest.moc"
