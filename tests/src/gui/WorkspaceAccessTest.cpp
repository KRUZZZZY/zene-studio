/*
 * WorkspaceAccessTest.cpp - the 0.5.0 W3 L13 / L14 interface commands
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
/*! 0.5.0 W3 L13 / L14, the interface side: the light palette holds every declared pair at AA (WF-118);
 *  a theme colour override is one undo step, names the pairs it breaks and round-trips through a file,
 *  and the Theme Colours list runs it (WF-117); the layout density scales the stylesheet's metrics and is
 *  a window.appearance field (WF-134); Help > Ask shows app.ask's answers (WF-142), What's New the shipped notes
 *  (WF-141), the large clock the play position (WF-101); the window verbs refuse, typed, without a main window. */

#include <QtTest>

#include <QJsonArray>
#include <QTemporaryDir>

#include "Appearance.h"
#include "CommandListDialog.h"
#include "CommandLists.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "WcagContrast.h"
#include "WorkspaceWindows.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

class WorkspaceAccessTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		qputenv("HOME", m_home.path().toLocal8Bit());
		qputenv("ZENE_UI_SCALE_FILE", m_home.filePath(QStringLiteral("scale")).toLocal8Bit());
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theLightPaletteIsAA()
	{
		QVERIFY(appearance::paletteNames().contains(QStringLiteral("light")));
		const tokens::Palette light = tokens::paletteNamed(QStringLiteral("light"));
		for (const tokens::ContrastPair& pair : tokens::contrastPairs(light))
		{
			const double ratio = a11y::contrastRatio(pair.foreground, pair.background);
			QVERIFY2(ratio >= pair.minimum, qPrintable(QStringLiteral("%1: %2:1, needs %3:1")
				.arg(QLatin1String(pair.name)).arg(ratio, 0, 'f', 2).arg(pair.minimum)));
		}
		// Light means light: the panel is lighter than its text, the reverse of the dark palette.
		QVERIFY(a11y::relativeLuminance(light.panel) > a11y::relativeLuminance(light.text));
		QVERIFY(tokens::paletteNamed(QStringLiteral("dark")).panel != light.panel);
	}

	void aColourOverrideIsOneUndoStep()
	{
		const ControlResult set = run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), QStringLiteral("accent")},
			{QStringLiteral("color"), QStringLiteral("#ff8800")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(tokens::themeOverrides().value(QStringLiteral("accent")).toString(), QStringLiteral("#ff8800"));
		bool found = false;
		for (const QJsonValue& entry : set.result.value(QStringLiteral("colors")).toArray())
		{
			const QJsonObject role = entry.toObject();
			if (role.value(QStringLiteral("name")).toString() != QLatin1String("accent")) { continue; }
			found = true;
			QCOMPARE(role.value(QStringLiteral("color")).toString(), QStringLiteral("#ff8800"));
			QVERIFY(role.value(QStringLiteral("overridden")).toBool());
		}
		QVERIFY(found);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(tokens::themeOverrides().isEmpty());
	}

	void aBadColourIsRefused()
	{
		const ControlResult bad = run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), QStringLiteral("accent")},
			{QStringLiteral("color"), QStringLiteral("not-a-colour")}});
		QVERIFY(!bad.ok);
		QCOMPARE(bad.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(!run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), QStringLiteral("noSuchRole")},
			{QStringLiteral("color"), QStringLiteral("#000000")}}).ok);
		QVERIFY(tokens::themeOverrides().isEmpty());
	}

	void anUnreadableThemeIsNamed()
	{
		const QString panel = tokens::paletteNamed(appearance::options().palette).panel.name();
		const ControlResult set = run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), QStringLiteral("text")},
			{QStringLiteral("color"), panel}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QVERIFY(!set.result.value(QStringLiteral("contrast_failures")).toArray().isEmpty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("theme.get")).result.value(QStringLiteral("contrast_failures")).toArray().isEmpty());
	}

	void aThemeRoundTripsThroughAFile()
	{
		QVERIFY(run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), QStringLiteral("panel")},
			{QStringLiteral("color"), QStringLiteral("#202830")}}).ok);
		const QString file = m_home.filePath(QStringLiteral("mine.theme.json"));
		const ControlResult exported = run(QStringLiteral("theme.export"), {{QStringLiteral("file"), file}});
		QVERIFY2(exported.ok, qPrintable(exported.errorMessage));
		QCOMPARE(exported.result.value(QStringLiteral("colors")).toInt(), static_cast<int>(tokens::tokenNames().size()));
		QVERIFY(run(QStringLiteral("theme.reset")).ok);
		QVERIFY(tokens::themeOverrides().isEmpty());
		const ControlResult imported = run(QStringLiteral("theme.import"), {{QStringLiteral("file"), file}, {QStringLiteral("replace"), true}});
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));
		QCOMPARE(tokens::themeOverrides().value(QStringLiteral("panel")).toString(), QStringLiteral("#202830"));
		QCOMPARE(tokens::themeOverrides().size(), tokens::tokenNames().size());
		QVERIFY(!run(QStringLiteral("theme.import"), {{QStringLiteral("file"), m_home.filePath(QStringLiteral("missing.json"))}}).ok);
		for (int step = 0; step < 3; ++step) { QVERIFY(run(QStringLiteral("control.undo")).ok); }
		QVERIFY(tokens::themeOverrides().isEmpty());
	}

	void theColourListRunsTheCommands()
	{
		CommandListDialog dialog(CommandLists::themeColors());
		QCOMPARE(dialog.rowCount(), static_cast<int>(tokens::tokenNames().size()));
		QVERIFY(run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), dialog.row(0).value(QStringLiteral("name"))},
			{QStringLiteral("color"), QStringLiteral("#123456")}}).ok);
		QCOMPARE(static_cast<int>(tokens::themeOverrides().size()), 1);
		dialog.refresh();
		const ControlResult cleared = dialog.runAction(0, 1);  // "Use the Palette's"
		QVERIFY2(cleared.ok, qPrintable(cleared.errorMessage));
		QVERIFY(tokens::themeOverrides().isEmpty());
	}

	void theDensityScalesTheStylesheet()
	{
		const QString css = QStringLiteral("QPushButton { padding: 5px 10px; min-height: 18px; color: #ffffff; border-width: 1px; }");
		QCOMPARE(tokens::scaleMetrics(css, 1.0), css);
		const QString touch = tokens::scaleMetrics(css, 2.0);
		QVERIFY(touch.contains(QStringLiteral("padding: 10px 20px;")));
		QVERIFY(touch.contains(QStringLiteral("min-height: 36px;")));
		QVERIFY(touch.contains(QStringLiteral("border-width: 1px;")));
		QCOMPARE(appearance::densityFactor(QStringLiteral("standard")), 1.0);
		QVERIFY(appearance::densityFactor(QStringLiteral("touch")) > appearance::densityFactor(QStringLiteral("comfortable")));
		QVERIFY(tokens::targetSize() >= tokens::size::target);
	}

	void theDensityIsAnAppearanceField()
	{
		const ControlResult set = run(QStringLiteral("window.appearance"), {{QStringLiteral("density"), QStringLiteral("comfortable")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(appearance::options().density, QStringLiteral("comfortable"));
		QCOMPARE(set.result.value(QStringLiteral("density")).toString(), QStringLiteral("comfortable"));
		QVERIFY(!run(QStringLiteral("window.appearance"), {{QStringLiteral("density"), QStringLiteral("cramped")}}).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(appearance::options().density, QStringLiteral("standard"));
	}

	void askShowsTheAnswers()
	{
		AssistantDialog dialog;
		QVERIFY(dialog.ask(QStringLiteral("how do I add a send to a return channel")) > 0);
		QVERIFY(!dialog.answersText().isEmpty());
		QCOMPARE(dialog.ask(QStringLiteral("a")), 0);
	}

	void whatsNewShowsTheNotes()
	{
		const ControlResult notes = run(QStringLiteral("app.whats_new"));
		QVERIFY2(notes.ok, qPrintable(notes.errorMessage));
		WhatsNewDialog dialog(notes.result);
		QVERIFY(dialog.notesText().contains(QStringLiteral("0.5.0")));
	}

	void theClockShowsThePosition()
	{
		LargeClock clock;
		QVERIFY2(clock.refresh().startsWith(QStringLiteral("1 . 1 . 000")), qPrintable(clock.refresh()));
	}

	void theWindowVerbsNeedAMainWindow()
	{
		for (const char* id : {"window.stay_on_top", "window.tidy", "window.large_clock", "window.ask", "window.whats_new",
			"window.focus_panel"})
		{
			const ControlResult refused = run(QLatin1String(id));
			QVERIFY2(!refused.ok, id);
			QCOMPARE(refused.errorKind, ControlErrorKind::Requires);
		}
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(WorkspaceAccessTest)
#include "WorkspaceAccessTest.moc"
