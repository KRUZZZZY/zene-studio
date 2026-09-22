/*
 * AccessibilityHelperTest.cpp - the a11y helper, then the Focus Desk surface
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
#include <QToolButton>
#include <QtTest>

#include "Accessibility.h"
#include "FocusDesk.h"

using namespace lmms::gui;

//! include/Accessibility.h's two calls on their own, then the same calls as
//! the Focus Desk surface actually leaves them: every chip announced with its
//! module's title (never the glyph), every chip and the density control
//! Tab-focusable, and a mounted card's promote button named for the module it
//! promotes. This is the owner's manual-test surface, so it is where the
//! sweep's pattern is held to its own promise (SPEC-zene-ui-v0 §5 item 1).
class AccessibilityHelperTest : public QObject
{
	Q_OBJECT

private slots:
	void announceSetsNameAndDescription()
	{
		QWidget control;
		lmms::a11y::announce(&control, QStringLiteral("Main fader"),
			QStringLiteral("Track 1 output volume"));
		QCOMPARE(control.accessibleName(), QStringLiteral("Main fader"));
		QCOMPARE(control.accessibleDescription(), QStringLiteral("Track 1 output volume"));
	}

	void describeLeavesTheNameTrackingItsText()
	{
		QWidget control;
		control.setAccessibleName(QStringLiteral("Density: Standard"));
		lmms::a11y::describe(&control, QStringLiteral("Cycle the density preset."));
		QCOMPARE(control.accessibleName(), QStringLiteral("Density: Standard"));
		QCOMPARE(control.accessibleDescription(), QStringLiteral("Cycle the density preset."));
		// An empty description is a no-op, not an eraser of the one already set.
		lmms::a11y::describe(&control, QString());
		QCOMPARE(control.accessibleDescription(), QStringLiteral("Cycle the density preset."));
	}

	void nullControlsAreSafe()
	{
		// No assert below is the assertion: these calls must not crash, and a
		// null dereference fails the test by crashing it.
		lmms::a11y::announce(nullptr, QStringLiteral("name"), QStringLiteral("description"));
		lmms::a11y::describe(nullptr, QStringLiteral("description"));
	}

	void everyChipIsAnnouncedWithItsModuleTitle()
	{
		FocusDesk desk;
		const auto rows = desk.registerRows();
		QVERIFY(!rows.isEmpty());
		for (const FocusModule& row : rows)
		{
			auto* chip = desk.findChild<QToolButton*>(focusActionCommandId(row.id));
			QVERIFY2(chip != nullptr, qPrintable(row.id));
			// The visible text carries the glyph; the announced name must not.
			QCOMPARE(chip->accessibleName(), row.title);
			QVERIFY(!chip->accessibleDescription().isEmpty());
			QCOMPARE(chip->focusPolicy(), Qt::TabFocus);
		}
	}

	void stripControlsAreReachableAndAnnounced()
	{
		FocusDesk desk;
		QCOMPARE(desk.accessibleName(), QStringLiteral("Focus Desk"));

		auto* density = desk.findChild<QToolButton*>(QStringLiteral("focusDeskDensity"));
		QVERIFY(density != nullptr);
		QCOMPARE(density->focusPolicy(), Qt::TabFocus);
		// The name tracks the text: the current preset is the state.
		QCOMPARE(density->accessibleName(), density->text());
		QVERIFY(!density->accessibleDescription().isEmpty());
	}

	void aMountedCardsPromoteButtonNamesTheModule()
	{
		QWidget host;
		FocusDesk desk;
		QVERIFY(desk.mountModule(QStringLiteral("arrangement"), new QWidget(&host)));
		auto* promote = desk.findChild<QToolButton*>(
			QStringLiteral("focusDeskPromote_arrangement"));
		QVERIFY(promote != nullptr);
		QString title;
		for (const FocusModule& row : desk.registerRows())
		{
			if (row.id == QStringLiteral("arrangement")) { title = row.title; }
		}
		QVERIFY(!title.isEmpty());
		QCOMPARE(promote->accessibleName(), QStringLiteral("Show %1 on stage").arg(title));
		QCOMPARE(promote->focusPolicy(), Qt::TabFocus);
		QVERIFY(!promote->accessibleDescription().isEmpty());
	}
};

// The platform plugin has to be chosen before the QApplication exists; the gate
// runner exports QT_QPA_PLATFORM=offscreen and CI does not, so default to
// offscreen when nothing else is configured and no display is attached (the
// same guard FocusDeskTest uses).
int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
		&& qEnvironmentVariableIsEmpty("DISPLAY")
		&& qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}

	QApplication app(argc, argv);
	AccessibilityHelperTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "AccessibilityHelperTest.moc"
