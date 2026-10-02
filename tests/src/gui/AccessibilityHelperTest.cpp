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
#include "ShellChrome.h"
#include "Shell.h"
#include "ShellPanel.h"
#include <QAction>

using namespace lmms::gui;

//! include/Accessibility.h's two calls on their own, then the same calls as
//! the 0.5.0 shell leaves them: every panel switch and view word announced with
//! its title (never a mnemonic marker), every one Tab-focusable, and every panel
//! frame and chrome band named. The shell replaced the Focus Desk (docs/UI-SHELL.md);
//! these three checks are the Focus Desk's chip, strip and promote-button checks
//! restated for the surface that took its place (SPEC-zene-ui-v0 §5 item 1).
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

	void everyPanelSwitchIsAnnouncedWithItsTitle()
	{
		Shell shell;
		const auto buttons = shell.findChildren<ShellButton*>();
		QVERIFY(!buttons.isEmpty());
		int switches = 0;
		for (QAction* action : shell.panelActions() + shell.viewActions())
		{
			for (ShellButton* button : buttons)
			{
				if (button->defaultAction() != action) { continue; }
				++switches;
				QCOMPARE(button->accessibleName(), action->text().remove(QLatin1Char('&')));
				QVERIFY2(!button->accessibleDescription().isEmpty(), qPrintable(action->text()));
				QCOMPARE(button->focusPolicy(), Qt::TabFocus);
			}
		}
		// Every panel but the arranger (a view, not a footer switch) and every view word.
		QCOMPARE(switches, shell.panelActions().size() - 1 + shell.viewActions().size());
	}

	void theShellChromeIsNamed()
	{
		Shell shell;
		for (const char* name : {"shellHeader", "shellFooter", "mainToolbar"})
		{
			auto* band = shell.findChild<QWidget*>(QString::fromLatin1(name));
			QVERIFY2(band != nullptr, name);
			QVERIFY2(!band->accessibleName().isEmpty(), name);
			QVERIFY2(!band->accessibleDescription().isEmpty(), name);
		}
	}

	void everyPanelFrameIsNamedForItsPanel()
	{
		Shell shell;
		for (const ShellPanelSpec& spec : ShellLayout::panels())
		{
			ShellPanel* frame = shell.panel(spec.id);
			QVERIFY2(frame != nullptr, qPrintable(spec.id));
			QCOMPARE(frame->accessibleName(), spec.title);
			QVERIFY(!frame->accessibleDescription().isEmpty());
		}
	}
};

// The platform plugin has to be chosen before the QApplication exists; the gate
// runner exports QT_QPA_PLATFORM=offscreen and CI does not, so default to
// offscreen when nothing else is configured and no display is attached (the
// same guard MidiLearnGuiTest uses).
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
