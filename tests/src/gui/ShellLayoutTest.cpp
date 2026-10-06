/*
 * ShellLayoutTest.cpp - the shell's region, view and refusal rules, without a display
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

#include <QtTest>

#include "ShellLayout.h"

using lmms::gui::ShellLayout;
using lmms::gui::ShellRegion;
using lmms::gui::ShellView;

class ShellLayoutTest : public QObject
{
	Q_OBJECT

private slots:
	void everyPanelHasAUniqueIdAndATitle()
	{
		QSet<QString> seen;
		for (const auto& spec : ShellLayout::panels())
		{
			QVERIFY2(!spec.id.isEmpty() && !spec.title.isEmpty(), qPrintable(spec.id));
			QVERIFY2(!seen.contains(spec.id), qPrintable(spec.id));
			seen.insert(spec.id);
		}
		for (const char* id : {"arranger", "launcher", "mixer", "detail", "devices", "inspector", "browser"})
		{
			QVERIFY2(seen.contains(QString::fromLatin1(id)), id);
		}
	}

	void eachViewHasItsOwnCentre()
	{
		QCOMPARE(ShellLayout::centerOf(ShellView::Arrange), QStringLiteral("arranger"));
		QCOMPARE(ShellLayout::centerOf(ShellView::Mix), QStringLiteral("mixer"));
		QCOMPARE(ShellLayout::centerOf(ShellView::Edit), QStringLiteral("detail"));
		ShellLayout layout;
		for (ShellView v : {ShellView::Arrange, ShellView::Mix, ShellView::Edit})
		{
			layout.setView(v);
			QVERIFY(layout.isVisible(ShellLayout::centerOf(v)));
			QCOMPARE(layout.regionOf(ShellLayout::centerOf(v)), ShellRegion::Center);
		}
	}

	void theCentreCannotBeHidden()
	{
		ShellLayout layout;
		QString reason;
		QVERIFY(!layout.close(QStringLiteral("arranger"), &reason));
		QVERIFY(reason.contains(QStringLiteral("cannot be hidden")));
		QVERIFY(layout.toggle(QStringLiteral("arranger")));
		QVERIFY(layout.isVisible(QStringLiteral("arranger")));
	}

	void aSideRegionHoldsOnePanelAtATime()
	{
		ShellLayout layout;
		QVERIFY(layout.open(QStringLiteral("devices")));
		QCOMPARE(layout.openIn(ShellRegion::Bottom), QStringLiteral("devices"));
		QVERIFY(layout.open(QStringLiteral("mixer")));
		QCOMPARE(layout.openIn(ShellRegion::Bottom), QStringLiteral("mixer"));
		QVERIFY(!layout.isVisible(QStringLiteral("devices")));
		QVERIFY(!layout.toggle(QStringLiteral("mixer")));
		QVERIFY(layout.openIn(ShellRegion::Bottom).isEmpty());
	}

	void onlyOnePanelHoldsAPinInARegion()
	{
		ShellLayout layout;
		QVERIFY(layout.setPinned(QStringLiteral("devices"), true));
		QVERIFY(layout.isPinned(QStringLiteral("devices")));
		// Both live in the Bottom region, which the shell says holds one panel at a time, so the
		// newer pin takes the region rather than the two fighting over it on every view switch.
		QVERIFY(layout.setPinned(QStringLiteral("detail"), true));
		QVERIFY(layout.isPinned(QStringLiteral("detail")));
		QVERIFY(!layout.isPinned(QStringLiteral("devices")));
		layout.setView(ShellView::Mix);
		QCOMPARE(layout.openIn(ShellRegion::Bottom), QStringLiteral("detail"));
		layout.setView(ShellView::Edit);
		QVERIFY(layout.isVisible(QStringLiteral("detail")));  // it is this view's centre ...
		QVERIFY(layout.openIn(ShellRegion::Bottom) != QStringLiteral("detail"));  // ... so it is not also the panel below it
		layout.setView(ShellView::Arrange);
		QCOMPARE(layout.openIn(ShellRegion::Bottom), QStringLiteral("detail"));
	}

	void closingAPinnedPanelTakesItsPinWithIt()
	{
		ShellLayout layout;
		QVERIFY(layout.setPinned(QStringLiteral("devices"), true));
		QVERIFY(layout.close(QStringLiteral("devices")));
		// Closing is the user saying "not this one": a pin left behind reopens the panel on the
		// next view switch, which is not what closing it asked for.
		QVERIFY(!layout.isPinned(QStringLiteral("devices")));
		layout.setView(ShellView::Mix);
		layout.setView(ShellView::Arrange);
		QVERIFY(!layout.isVisible(QStringLiteral("devices")));
	}

	void aViewRefusesWhatItDoesNotOffer()
	{
		ShellLayout layout;
		layout.setView(ShellView::Edit);
		QString reason;
		QVERIFY(!layout.open(QStringLiteral("arranger"), &reason));
		QVERIFY2(reason.contains(QStringLiteral("edit")), qPrintable(reason));
		QVERIFY(!layout.open(QStringLiteral("launcher")));
		QVERIFY(!layout.open(QStringLiteral("no-such-panel"), &reason));
		QVERIFY(reason.contains(QStringLiteral("no-such-panel")));
		layout.setView(ShellView::Mix);
		QVERIFY(!layout.isVisible(QStringLiteral("arranger")));
		QVERIFY(layout.open(QStringLiteral("launcher")));
	}

	void theLauncherSitsBesideTheCentre()
	{
		ShellLayout layout;
		QVERIFY(!layout.launcherBesideCenter());
		QVERIFY(layout.toggle(QStringLiteral("launcher")));
		QVERIFY(layout.launcherBesideCenter());
		QCOMPARE(layout.regionOf(QStringLiteral("launcher")), ShellRegion::Center);
		layout.setView(ShellView::Edit);
		QVERIFY(!layout.launcherBesideCenter());
		layout.setView(ShellView::Arrange);
		QVERIFY(layout.launcherBesideCenter());
	}

	void theCentreNeverAlsoOpensBelow()
	{
		ShellLayout layout;
		QCOMPARE(layout.openIn(ShellRegion::Bottom), QStringLiteral("detail"));
		layout.setView(ShellView::Edit);
		QVERIFY(layout.openIn(ShellRegion::Bottom) != QStringLiteral("detail"));
		QVERIFY(layout.open(QStringLiteral("detail")));
		QVERIFY(layout.openIn(ShellRegion::Bottom) != QStringLiteral("detail"));
	}

	void viewsKeepTheirOwnPanels()
	{
		ShellLayout layout;
		layout.open(QStringLiteral("automation"));
		layout.setView(ShellView::Mix);
		layout.open(QStringLiteral("pattern"));
		layout.setView(ShellView::Arrange);
		QCOMPARE(layout.openIn(ShellRegion::Bottom), QStringLiteral("automation"));
		layout.setView(ShellView::Mix);
		QCOMPARE(layout.openIn(ShellRegion::Bottom), QStringLiteral("pattern"));
	}

	void theLayoutRoundTripsAndIgnoresWhatItCannotHonour()
	{
		ShellLayout layout;
		layout.setView(ShellView::Mix);
		layout.close(QStringLiteral("inspector"));
		layout.open(QStringLiteral("launcher"));
		const QVariantMap saved = layout.save();

		ShellLayout restored;
		restored.restore(saved);
		QCOMPARE(restored.view(), ShellView::Mix);
		QVERIFY(restored.openIn(ShellRegion::Left).isEmpty());
		QVERIFY(restored.launcherBesideCenter());
		QCOMPARE(restored.save(), saved);

		// A hand-edited or future layout: an unknown view, a panel in the wrong region, an
		// unknown panel. None of it may produce a state the rules forbid.
		QVariantMap bad;
		bad.insert(QStringLiteral("view"), QStringLiteral("tablet"));
		QVariantMap arrange;
		arrange.insert(QStringLiteral("left"), QStringLiteral("browser"));
		arrange.insert(QStringLiteral("bottom"), QStringLiteral("no-such-panel"));
		bad.insert(QStringLiteral("arrange"), arrange);
		ShellLayout guarded;
		guarded.restore(bad);
		QCOMPARE(guarded.view(), ShellView::Arrange);
		QCOMPARE(guarded.openIn(ShellRegion::Left), QStringLiteral("inspector"));
		QCOMPARE(guarded.openIn(ShellRegion::Bottom), QStringLiteral("detail"));
	}

	void viewNamesRoundTrip()
	{
		for (const QString& name : ShellLayout::viewNames())
		{
			ShellView v = ShellView::Arrange;
			QVERIFY(ShellLayout::viewFromName(name, v));
			QCOMPARE(ShellLayout::viewName(v), name);
		}
		ShellView untouched = ShellView::Mix;
		QVERIFY(!ShellLayout::viewFromName(QStringLiteral("tablet"), untouched));
		QCOMPARE(untouched, ShellView::Mix);
	}

	//! 0.5.0 G: one spelling of the region words, which window.get_state and the saved layout share.
	void regionWordsAreTheSavedLayoutsKeys()
	{
		QCOMPARE(ShellLayout::regionName(ShellRegion::Left), QStringLiteral("left"));
		QCOMPARE(ShellLayout::regionName(ShellRegion::Center), QStringLiteral("center"));
		QCOMPARE(ShellLayout::regionName(ShellRegion::Bottom), QStringLiteral("bottom"));
		QCOMPARE(ShellLayout::regionName(ShellRegion::Right), QStringLiteral("right"));
		const QVariantMap saved = ShellLayout().save().value(QStringLiteral("arrange")).toMap();
		for (ShellRegion r : {ShellRegion::Left, ShellRegion::Bottom, ShellRegion::Right})
		{
			QVERIFY(saved.contains(ShellLayout::regionName(r)));
		}
	}
};

QTEST_GUILESS_MAIN(ShellLayoutTest)
#include "ShellLayoutTest.moc"
