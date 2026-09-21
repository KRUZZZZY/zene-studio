/*
 * FocusDeskTest.cpp - the Focus Desk register (UI plan §4.2) and its shell.
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
#include <QFrame>
#include <QLayout>
#include <QPointer>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

#include "FocusDesk.h"

using namespace lmms::gui;

//! Every rule the register encodes, and the shell that reads it. The two halves
//! are separated on purpose: the register is widget-free, so "what may exist"
//! is decidable without a display, and only the placement rules need one.
class FocusDeskTest : public QObject
{
	Q_OBJECT

private slots:
	// ---------------------------------------------------------------- register

	void theRegisterIsValid()
	{
		const auto rows = FocusDeskModules::v1Register();
		QVERIFY2(FocusDeskModules::validate(rows).isEmpty(),
			qPrintable(FocusDeskModules::validate(rows).join("; ")));
	}

	//! A validator that cannot fail is not evidence. Each mutation below breaks
	//! exactly one rule, and every one of them must be refused - otherwise
	//! theRegisterIsValid() would pass on any table at all.
	void theValidatorRejectsEveryWayARegisterCanBeWrong()
	{
		const auto good = FocusDeskModules::v1Register();
		QVERIFY(FocusDeskModules::validate(good).isEmpty());

		{ // 1. an empty register
			QVERIFY(!FocusDeskModules::validate({}).isEmpty());
		}
		{ // 2. a duplicate id
			auto rows = good;
			rows.append(rows.first());
			QVERIFY(!FocusDeskModules::validate(rows).isEmpty());
		}
		{ // 3. a homed-in region the row may not occupy
			auto rows = good;
			rows[1].home = FocusRegion::Right;   // arrangement may not live right
			QVERIFY(!FocusDeskModules::validate(rows).isEmpty());
		}
		{ // 4. focusable, but the stage is out of reach
			auto rows = good;
			rows[1].allowed = static_cast<int>(FocusRegion::Bottom);
			rows[1].home = FocusRegion::Bottom;
			QVERIFY(rows[1].focusable);
			QVERIFY(!FocusDeskModules::validate(rows).isEmpty());
		}
		{ // 5. no glyph, which is what makes the strip scannable
			auto rows = good;
			rows[2].glyph.clear();
			QVERIFY(!FocusDeskModules::validate(rows).isEmpty());
		}
		{ // 6. no owning menu id, so its commands have no route (M1)
			auto rows = good;
			rows[2].menuId.clear();
			QVERIFY(!FocusDeskModules::validate(rows).isEmpty());
		}
		{ // 7. no region at all
			auto rows = good;
			rows[2].allowed = 0;
			QVERIFY(!FocusDeskModules::validate(rows).isEmpty());
		}
	}

	void theRegisterIsTheElevenNamedRowsOfThePlan()
	{
		const QStringList expected{
			"browser", "arrangement", "session", "inspector", "devicechain",
			"modulation", "detaileditor", "mixer", "automation", "project", "learn"};
		QStringList actual;
		for (const auto& row : FocusDeskModules::v1Register()) { actual.append(row.id); }
		QCOMPARE(actual, expected);
		// Staging is recorded, not silent: a row with no mount states why.
		const auto* session = FocusDeskModules::find(FocusDeskModules::v1Register(), "session");
		QVERIFY(session != nullptr);
		QVERIFY(!session->unmounted.isEmpty());
	}

	//! §4.2's table is the authority on what may exist, so the register is read
	//! back against that table rather than against itself - a register compared
	//! only to its own rows would pass whatever it happened to say.
	//!
	//! The mask is the four regions in declaration order, L C R B, then the focus
	//! contract. The column that matters is the last: a row the table keeps out
	//! of the centre region cannot take the stage, because the stage *is* the
	//! centre region (§5.1, §9.4) - so "may occupy" and "can be promoted" are one
	//! decision, and this is where the two are made to agree.
	void theRegionSetsAreThePlanTable()
	{
		const QStringList expected{
			"browser=1001:0", "arrangement=0101:1", "session=0100:1",
			"inspector=0011:0", "devicechain=0011:0", "modulation=0010:0",
			"detaileditor=0101:1", "mixer=0101:1", "automation=0001:0",
			"project=1001:0", "learn=1100:1"};
		const FocusRegion regions[] = {FocusRegion::Left, FocusRegion::Centre,
			FocusRegion::Right, FocusRegion::Bottom};
		QStringList actual;
		for (const auto& row : FocusDeskModules::v1Register())
		{
			QString mask;
			for (const auto region : regions)
			{
				mask += FocusDeskModules::canOccupy(row, region)
					? QStringLiteral("1") : QStringLiteral("0");
			}
			actual.append(QStringLiteral("%1=%2:%3").arg(row.id, mask,
				row.focusable ? QStringLiteral("1") : QStringLiteral("0")));
		}
		QCOMPARE(actual, expected);
	}

	void focusableRowsCanReachTheStage()
	{
		for (const auto& row : FocusDeskModules::focusable(FocusDeskModules::v1Register()))
		{
			QVERIFY2(FocusDeskModules::canOccupy(row, FocusRegion::Centre), qPrintable(row.id));
		}
		QCOMPARE(FocusDeskModules::homedIn(FocusDeskModules::v1Register(), FocusRegion::Left).size(), 3);
		QVERIFY(FocusDeskModules::find(FocusDeskModules::v1Register(), "nope") == nullptr);
		QCOMPARE(FocusDeskModules::indexOf(FocusDeskModules::v1Register(), "mixer"), 7);
	}

	void densityNamesRoundTripAndCycle()
	{
		for (const auto level : {FocusDensity::Minimal, FocusDensity::Standard, FocusDensity::Complete})
		{
			FocusDensity back = FocusDensity::Minimal;
			QVERIFY(FocusDeskModules::densityFromName(FocusDeskModules::densityName(level), &back));
			QCOMPARE(back, level);
		}
		QCOMPARE(FocusDeskModules::densityName(FocusDensity::Minimal), QString("minimal"));
		QCOMPARE(FocusDeskModules::densityName(FocusDensity::Standard), QString("standard"));
		QCOMPARE(FocusDeskModules::densityName(FocusDensity::Complete), QString("complete"));

		FocusDensity level = FocusDensity::Minimal;
		for (int i = 0; i < 3; ++i) { level = FocusDeskModules::nextDensity(level); }
		QCOMPARE(level, FocusDensity::Minimal);

		FocusDensity untouched = FocusDensity::Complete;
		QVERIFY(!FocusDeskModules::densityFromName("dense", &untouched));
		QCOMPARE(untouched, FocusDensity::Complete);
		// Case and surrounding space are tolerated; a wrong word is not.
		QVERIFY(FocusDeskModules::densityFromName("  MiNiMaL ", &untouched));
		QCOMPARE(untouched, FocusDensity::Minimal);
	}

	void theActionIdIsTheModFormOfTheRegisterId()
	{
		QCOMPARE(focusActionCommandId("mixer"), QString("mod:mixer"));
	}

	// ------------------------------------------------------------------- shell

	void theStripShowsEveryRowAndRemovesNone()
	{
		FocusDesk desk;
		QStringList expected;
		for (const auto& row : desk.registerRows()) { expected.append(row.id); }
		QCOMPARE(desk.stripIds(), expected);

		for (const auto& row : desk.registerRows())
		{
			const auto chips = desk.findChildren<QToolButton*>(focusActionCommandId(row.id));
			QCOMPARE(chips.size(), 1);
			// X4: an unavailable item is greyed with a reason, never removed.
			QVERIFY(!desk.chipEnabled(row.id));
			const QString reason = desk.chipReason(row.id);
			QVERIFY2(!reason.isEmpty(), qPrintable(row.id));
			QCOMPARE(desk.chipState(row.id), QString("unavailable"));
		}
		// The reason is the register's own, not a generic fallback.
		QVERIFY(desk.chipReason("session").contains("reserved slot"));
		QVERIFY(desk.chipReason("browser").contains("sidebar"));
	}

	void mountingRefusesRowsOutsideTheRegister()
	{
		QWidget host;
		auto* stray = new QWidget(&host);
		FocusDesk desk;

		QVERIFY(!desk.mountModule("not-a-row", stray));
		QVERIFY(!desk.mountModule("", stray));
		QCOMPARE(desk.mountedCount(), 0);
		QCOMPARE(stray->parentWidget(), &host);   // untouched, not adopted

		QVERIFY(desk.mountModule("mixer", stray));
		QCOMPARE(desk.mountedCount(), 1);
		QVERIFY(!desk.mountModule("mixer", stray));  // one widget per row
		QCOMPARE(desk.mountedCount(), 1);
	}

	void theCentreHomedRowTakesTheStageWhenItIsMounted()
	{
		QWidget host;
		FocusDesk desk;
		QCOMPARE(desk.focused(), QString());

		QSignalSpy changes(&desk, &FocusDesk::focusChanged);
		QVERIFY(desk.mountModule("arrangement", new QWidget(&host)));

		QCOMPARE(desk.focused(), QString("arrangement"));
		QCOMPARE(desk.chipState("arrangement"), QString("stage"));
		QCOMPARE(desk.stagedWidget(), desk.mountedWidget("arrangement"));
		QCOMPARE(desk.stagedWidget()->parentWidget(), desk.regionHost(FocusRegion::Centre));
		QVERIFY(desk.chipEnabled("arrangement"));
		QCOMPARE(changes.count(), 1);
	}

	void focusingMovesThePreviousModuleBackToItsHome()
	{
		QWidget host;
		FocusDesk desk;
		QVERIFY(desk.mountModule("arrangement", new QWidget(&host)));   // centre -> stage
		QVERIFY(desk.mountModule("mixer", new QWidget(&host)));         // bottom -> dock
		QCOMPARE(desk.chipState("mixer"), QString("bottom"));
		QCOMPARE(desk.chipState("arrangement"), QString("stage"));

		QSignalSpy changes(&desk, &FocusDesk::focusChanged);
		QVERIFY(desk.focusModule("mixer"));
		QCOMPARE(desk.chipState("mixer"), QString("stage"));
		// arrangement's home is the centre region, so its home is the park: the
		// stage holds one thing, and the chip says where the other one went.
		QCOMPARE(desk.chipState("arrangement"), QString("parked"));
		QCOMPARE(changes.count(), 1);
		QCOMPARE(changes.at(0).at(0).toString(), QString("mixer"));

		// The module that loses the stage returns to its own region rather than
		// to the park: the bottom dock is the mixer's home, and it goes back.
		QVERIFY(desk.focusModule("arrangement"));
		QCOMPARE(desk.chipState("arrangement"), QString("stage"));
		QCOMPARE(desk.chipState("mixer"), QString("bottom"));
		// Re-focusing the module already on the stage is a no-op, not an event.
		QVERIFY(desk.focusModule("arrangement"));
		QCOMPARE(changes.count(), 2);

		QVERIFY(desk.focusModule("devicechain") == false);   // not mounted
	}

	//! A row the table keeps out of the centre region can be mounted and visible
	//! and still not be promotable - so the refusal names that contract instead
	//! of pretending the module is missing (§4.3(6)).
	void aMountedRowThatMayNotTakeTheStageIsRefusedByName()
	{
		QWidget host;
		FocusDesk desk;
		QVERIFY(desk.mountModule("project", new QWidget(&host)));
		QCOMPARE(desk.chipState("project"), QString("left"));
		QVERIFY(desk.chipEnabled("project"));   // mounted, so not greyed

		QSignalSpy refusals(&desk, &FocusDesk::moduleRefused);
		QVERIFY(!desk.focusModule("project"));
		QCOMPARE(refusals.count(), 1);
		QCOMPARE(refusals.at(0).at(0).toString(), QString("project"));
		QVERIFY(refusals.at(0).at(1).toString().contains("not focusable"));
		// Refused, not moved: the module is still where its row says it lives.
		QCOMPARE(desk.chipState("project"), QString("left"));
		QCOMPARE(desk.focused(), QString());
	}

	void focusingAStagedRowIsRefusedWithTheRegistersReason()
	{
		FocusDesk desk;
		QSignalSpy refusals(&desk, &FocusDesk::moduleRefused);

		QVERIFY(!desk.focusModule("session"));
		QCOMPARE(refusals.count(), 1);
		QCOMPARE(refusals.at(0).at(0).toString(), QString("session"));
		QVERIFY(refusals.at(0).at(1).toString().contains("reserved slot"));

		// A row that is not in the register at all is refused for that reason,
		// which is §4.2's rule stated from the shell's side.
		QVERIFY(!desk.focusModule("nope"));
		QCOMPARE(refusals.count(), 2);
		QVERIFY(refusals.at(1).at(1).toString().contains("not in the module register"));
	}

	void releaseAllHandsEveryWidgetBackWithoutDeletingIt()
	{
		QWidget host;
		FocusDesk desk;
		QPointer<QWidget> arrangement = new QWidget(&host);
		QPointer<QWidget> project = new QWidget(&host);
		QVERIFY(desk.mountModule("arrangement", arrangement));
		QVERIFY(desk.mountModule("project", project));

		const auto released = desk.releaseAll();
		QCOMPARE(released.size(), 2);
		QCOMPARE(desk.mountedCount(), 0);
		QVERIFY(!arrangement.isNull());
		QVERIFY(!project.isNull());
		QVERIFY(desk.stagedWidget() == nullptr);
		QVERIFY(!desk.chipEnabled("arrangement"));
		QCOMPARE(desk.chipState("arrangement"), QString("unavailable"));

		// The desk must survive deleting a card that used to hold a live widget.
		QCoreApplication::processEvents();
		QVERIFY(!arrangement.isNull());
		QVERIFY(!project.isNull());
	}

	void minimalDensityCollapsesRailBodiesAndKeepsTheChips()
	{
		QWidget host;
		FocusDesk desk;
		QVERIFY(desk.mountModule("project", new QWidget(&host)));
		QCOMPARE(desk.density(), FocusDensity::Standard);
		QVERIFY(desk.railBodyVisible());

		auto* body = desk.findChild<QWidget*>(QStringLiteral("focusDeskBody_project"));
		QVERIFY(body != nullptr);
		QVERIFY(body->isVisibleTo(&desk));

		desk.setDensity(FocusDensity::Minimal);
		QVERIFY(!desk.railBodyVisible());
		// Collapsed, never removed: the chip is still there and still enabled,
		// the module is still mounted, and its body is hidden rather than gone.
		QVERIFY(desk.chipEnabled("project"));
		QCOMPARE(desk.stripIds().size(), 11);
		QVERIFY(desk.mountedWidget("project") != nullptr);
		QVERIFY(!body->isVisibleTo(&desk));

		desk.setDensity(FocusDensity::Complete);
		QVERIFY(desk.railBodyVisible());
		QVERIFY(body->isVisibleTo(&desk));
		// And a stage-only module is unaffected by a rail density choice.
		QVERIFY(desk.mountModule("arrangement", new QWidget(&host)));
		desk.setDensity(FocusDensity::Minimal);
		QCOMPARE(desk.chipState("arrangement"), QString("stage"));
		QCOMPARE(desk.stagedWidget()->parentWidget(), desk.regionHost(FocusRegion::Centre));
	}

	void theLayoutRoundTripsAndAPendingFocusIsHeldUntilItsModuleMounts()
	{
		QWidget host;
		FocusDesk desk;
		QVERIFY(desk.mountModule("arrangement", new QWidget(&host)));

		// A focus choice restored before its module exists is held, not dropped.
		QVariantMap saved;
		saved.insert("focus", QString("mixer"));
		saved.insert("density", QString("minimal"));
		QVERIFY(desk.restoreLayout(saved));
		QCOMPARE(desk.density(), FocusDensity::Minimal);
		QCOMPARE(desk.focused(), QString("arrangement"));   // mixer not mounted yet

		QVERIFY(desk.mountModule("mixer", new QWidget(&host)));
		QCOMPARE(desk.focused(), QString("mixer"));
		QCOMPARE(desk.chipState("mixer"), QString("stage"));

		const QVariantMap round = desk.saveLayout();
		QCOMPARE(round.value("focus").toString(), QString("mixer"));
		QCOMPARE(round.value("density").toString(), QString("minimal"));
		QCOMPARE(round.value("placed.mixer").toString(), QString("stage"));
		QCOMPARE(round.value("placed.arrangement").toString(), QString("parked"));

		// A saved layout names modules, so a layout naming a module the register
		// cannot contain is invalid rather than silently pending forever.
		QVariantMap broken;
		broken.insert("focus", QString("no-such-module"));
		QVERIFY(!desk.restoreLayout(broken));
		broken.insert("focus", QString("mixer"));
		broken.insert("density", QString("dense"));
		QVERIFY(!desk.restoreLayout(broken));
		QCOMPARE(desk.density(), FocusDensity::Minimal);
	}

	//! Every register row's home region must be a place the shell can actually
	//! put it, in the region's own host - otherwise a module is told where it
	//! lives and then shown somewhere else, or nowhere.
	void everyHomedRegionHasAVisiblePlace()
	{
		QWidget host;
		FocusDesk desk;
		// One mountable row per home region: left, centre, right, bottom.
		QVERIFY(desk.mountModule("project", new QWidget(&host)));
		QVERIFY(desk.mountModule("arrangement", new QWidget(&host)));
		QVERIFY(desk.mountModule("devicechain", new QWidget(&host)));
		QVERIFY(desk.mountModule("mixer", new QWidget(&host)));
		QCOMPARE(desk.chipState("project"), QString("left"));
		QCOMPARE(desk.chipState("arrangement"), QString("stage"));
		QCOMPARE(desk.chipState("devicechain"), QString("right"));
		QCOMPARE(desk.chipState("mixer"), QString("bottom"));

		// A rail and the dock show a module inside its card, so the card is what
		// sits in the region host. The stage shows the module itself, because a
		// card there would be a frame around the one thing on screen.
		QCOMPARE(desk.findChild<QFrame*>("focusDeskCard_project")->parentWidget(),
			desk.regionHost(FocusRegion::Left));
		QCOMPARE(desk.findChild<QFrame*>("focusDeskCard_devicechain")->parentWidget(),
			desk.regionHost(FocusRegion::Right));
		QCOMPARE(desk.findChild<QFrame*>("focusDeskCard_mixer")->parentWidget(),
			desk.regionHost(FocusRegion::Bottom));
		QCOMPARE(desk.mountedWidget("arrangement")->parentWidget(),
			desk.regionHost(FocusRegion::Centre));
	}

	void everyRegionHasItsOwnHost()
	{
		FocusDesk desk;
		const FocusRegion regions[] = {FocusRegion::Left, FocusRegion::Centre,
			FocusRegion::Right, FocusRegion::Bottom};
		// §5.1's four regions are four different places: a region resolving to
		// another region's host is how a module ends up displayed where its own
		// row says it may not live.
		for (const auto a : regions)
		{
			QVERIFY(desk.regionHost(a) != nullptr);
			for (const auto b : regions)
			{
				if (a != b) { QVERIFY(desk.regionHost(a) != desk.regionHost(b)); }
			}
		}
	}

	//! The stage holds the focused module and nothing else, at full size. The
	//! card around a staged module is parked, not laid out on the stage: its
	//! header is hidden there and lifting the content empties its body, so a
	//! card on the stage is an empty frame dividing the stage with the module
	//! the user is looking at. The card's parent alone does not catch that, so
	//! the stage host is asked for its direct children - and for its size, once
	//! laid out, because a shared stage would still name the right child.
	void theStageHoldsTheFocusedModuleAndNothingElse()
	{
		QWidget host;
		FocusDesk desk;
		QVERIFY(desk.mountModule("arrangement", new QWidget(&host)));
		QVERIFY(desk.mountModule("project", new QWidget(&host)));
		QCOMPARE(desk.chipState("arrangement"), QString("stage"));

		QWidget* stage = desk.regionHost(FocusRegion::Centre);
		const auto onStage = stage->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
		QCOMPARE(onStage.size(), 1);
		QCOMPARE(onStage.first(), desk.mountedWidget("arrangement"));
		QVERIFY(desk.findChild<QFrame*>("focusDeskCard_arrangement")->parentWidget()
			== desk.findChild<QWidget*>("focusDeskPark"));

		desk.resize(1000, 700);
		desk.show();
		QApplication::processEvents();
		QCOMPARE(desk.mountedWidget("arrangement")->size(), stage->size());
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
	FocusDeskTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "FocusDeskTest.moc"
