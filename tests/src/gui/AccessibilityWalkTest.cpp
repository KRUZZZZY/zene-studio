/*
 * AccessibilityWalkTest.cpp - R8.5: no unnamed focusable widget on the product's surfaces
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

/*! R8.5's acceptance test: "an automated walk of the widget tree finds no unnamed focusable
 *  widgets". The REAL application is built (GuiApplication: the main window and every editor),
 *  plus the surfaces that stand alone (the clip-launch grid, the start hub, the shortcuts page),
 *  and every widget a keyboard user can Tab to is asked for its name the way assistive
 *  technology asks - QAccessible::queryAccessibleInterface(w)->text(Name), which includes a
 *  button's own text, not only setAccessibleName(). A focusable widget with an empty name is
 *  announced as nothing. Each surface must also have something focusable to walk, so an empty
 *  or unbuilt surface cannot pass for the wrong reason.
 *
 *  The first walk found 30 (the toolbar's two sliders, the timeline's icon-only buttons, the
 *  editors' canvases and scroll areas, the mixer strip, the notes and microtuner fields); this
 *  change named all of them. THE RATCHET for what comes next: knownUnnamed() lists allowed
 *  surface/class counts - empty now. A count may not rise and a new pair fails; a listed count
 *  that falls fails until it is lowered, so the list only shrinks (ThemeContrastTest's rule).
 *
 *  NOT walked: controls that take no keyboard focus at all - the knobs, faders and LEDs of the
 *  track rows and instrument windows. They are unreachable by keyboard, which is a larger gap
 *  than a missing name (docs/KNOWN-LIMITATIONS.md). */

#include <QtTest>

#include <QAccessible>
#include <QMap>
#include <QMenuBar>
#include <QTemporaryDir>

#include <functional>
#include <memory>

#include "AutomationEditor.h"
#include "ConfigManager.h"
#include "ControllerRackView.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "MicrotunerConfig.h"
#include "MixerView.h"
#include "PatternEditor.h"
#include "PianoRoll.h"
#include "ProjectNotes.h"
#include "SessionGridView.h"
#include "ShortcutsPage.h"
#include "SongEditor.h"
#include "StartHub.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! "surface/Class" -> the number of unnamed focusable widgets of that class still allowed.
const QMap<QString, int>& knownUnnamed()
{
	static const QMap<QString, int> known{};
	return known;
}

//! What a screen reader would announce for @a widget.
QString accessibleNameOf(QWidget* widget)
{
	QAccessibleInterface* face = QAccessible::queryAccessibleInterface(widget);
	return face != nullptr ? face->text(QAccessible::Name).trimmed() : widget->accessibleName().trimmed();
}

//! "surface/Class" -> unnamed focusable widgets in @a root; @a focusable counts every one walked.
QMap<QString, int> unnamedIn(const QString& surface, QWidget* root, QStringList* detail, int* focusable)
{
	QMap<QString, int> found;
	for (QWidget* widget : root->findChildren<QWidget*>())
	{
		if ((widget->focusPolicy() & Qt::TabFocus) == 0 || !widget->isVisibleTo(root)) { continue; }
		++*focusable;
		if (!accessibleNameOf(widget).isEmpty()) { continue; }
		const QString key = surface + QLatin1Char('/') + QString::fromLatin1(widget->metaObject()->className());
		found[key] += 1;
		*detail << key + QStringLiteral(" (objectName '%1', tooltip '%2', parent %3)")
			.arg(widget->objectName(), widget->toolTip(),
				QString::fromLatin1(widget->parentWidget()->metaObject()->className()));
	}
	return found;
}

} // namespace

class AccessibilityWalkTest : public QObject
{
	Q_OBJECT

private slots:
	//! The REAL application - GuiApplication builds the main window and every editor the way
	//! the product does (most of them cannot be built without it: they add themselves to the
	//! main window's workspace or toolbar) - over a throwaway home, config and working
	//! directory and the Dummy audio device, so nothing the person running it owns is touched.
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		for (const char* name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"})
		{
			qputenv(name, m_home.path().toLocal8Bit());
		}
		ConfigManager* config = ConfigManager::inst();
		config->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		config->setValue(QStringLiteral("audioengine"), QStringLiteral("audiodev"), QStringLiteral("Dummy (no sound output)"));
		config->setWorkingDir(m_home.filePath(QStringLiteral("work")));
		config->createWorkingDir();
		m_app = new GuiApplication();
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
	}

	void cleanupTestCase() { Engine::destroy(); }

	void noSurfaceHasAnUnnamedFocusableWidget()
	{
		GuiApplication* gui = getGUI();
		QMenuBar bar;
		const QList<QPair<QString, std::function<QWidget*()>>> editors{
			{QStringLiteral("Toolbar"), [gui] { return gui->mainWindow()->toolBar(); }},
			{QStringLiteral("SongEditor"), [gui] { return gui->songEditor(); }},
			{QStringLiteral("PianoRoll"), [gui] { return gui->pianoRoll(); }},
			{QStringLiteral("AutomationEditor"), [gui] { return gui->automationEditor(); }},
			{QStringLiteral("PatternEditor"), [gui] { return gui->patternEditor(); }},
			{QStringLiteral("Mixer"), [gui] { return gui->mixerView(); }},
			{QStringLiteral("ControllerRack"), [gui] { return gui->getControllerRackView(); }},
			{QStringLiteral("ProjectNotes"), [gui] { return gui->getProjectNotes(); }},
			{QStringLiteral("Microtuner"), [gui] { return gui->getMicrotunerConfig(); }},
		};
		const QList<QPair<QString, std::function<QWidget*()>>> standalone{
			{QStringLiteral("SessionGrid"), [] { return new SessionGridView(); }},
			{QStringLiteral("StartHub"), [] { return new StartHub(QStringList{QStringLiteral("/tmp/a.mmpz")}, QStringList{}); }},
			{QStringLiteral("Shortcuts"), [gui] { return new ShortcutsPage(gui->mainWindow()->menuBar()); }},
		};
		gui->mainWindow()->show();
		QMap<QString, int> found;
		QStringList detail;
		QStringList empty;
		const auto walk = [&found, &detail, &empty](const QString& surface, QWidget* root) {
			QWidget* window = root->parentWidget() != nullptr ? root->parentWidget() : root;
			window->show();
			root->show();
			int focusable = 0;
			const QMap<QString, int> here = unnamedIn(surface, root, &detail, &focusable);
			std::printf("A11Y_WALKED %s: %d focusable widgets\n", qPrintable(surface), focusable);
			// A surface with nothing to walk would pass for the wrong reason.
			if (focusable == 0) { empty << surface; }
			for (auto it = here.cbegin(); it != here.cend(); ++it) { found[it.key()] += it.value(); }
		};
		for (const auto& [surface, get] : editors) { walk(surface, get()); }
		for (const auto& [surface, make] : standalone)
		{
			std::unique_ptr<QWidget> root(make());
			root->resize(1200, 800);
			walk(surface, root.get());
		}
		for (const QString& line : detail) { std::printf("A11Y_UNNAMED %s\n", qPrintable(line)); }
		QVERIFY2(empty.isEmpty(), qPrintable(QStringLiteral("nothing focusable was walked on: ") + empty.join(QStringLiteral(", "))));

		QStringList worse;
		QStringList better;
		for (auto it = found.cbegin(); it != found.cend(); ++it)
		{
			const int allowed = knownUnnamed().value(it.key(), 0);
			if (it.value() > allowed) { worse << QStringLiteral("%1: %2 (allowed %3)").arg(it.key()).arg(it.value()).arg(allowed); }
			if (it.value() < allowed) { better << QStringLiteral("%1: %2 (listed %3)").arg(it.key()).arg(it.value()).arg(allowed); }
		}
		for (auto it = knownUnnamed().cbegin(); it != knownUnnamed().cend(); ++it)
		{
			if (!found.contains(it.key())) { better << QStringLiteral("%1: 0 (listed %2)").arg(it.key()).arg(it.value()); }
		}
		QVERIFY2(worse.isEmpty(), qPrintable(QStringLiteral("unnamed focusable widgets: ") + worse.join(QStringLiteral("; "))));
		QVERIFY2(better.isEmpty(), qPrintable(QStringLiteral("fixed - lower knownUnnamed(): ") + better.join(QStringLiteral("; "))));
	}

private:
	QTemporaryDir m_home;
	GuiApplication* m_app = nullptr;
};

QTEST_MAIN(AccessibilityWalkTest)
#include "AccessibilityWalkTest.moc"
