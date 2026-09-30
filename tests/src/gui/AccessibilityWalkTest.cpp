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
 *  The windows that open on demand are walked too - an instrument track's window and a sample
 *  track's (every page of every nested tab), the settings and export dialogs - and since knobs
 *  take Tab focus (KnobKeyboardTest) they are walked with the rest; the second walk found 13
 *  more, all named. NOT walked: controls that still take no keyboard focus - the faders (the
 *  mixer drives its current channel's fader from the keyboard instead) and the LED buttons -
 *  and the dialogs not listed here (docs/KNOWN-LIMITATIONS.md). */

#include <QtTest>

#include <QAccessible>
#include <QMap>
#include <QMenuBar>
#include <QSet>
#include <QTemporaryDir>

#include <functional>
#include <memory>

#include "AutomationEditor.h"
#include "ControllerRackView.h"
#include "Engine.h"
#include "ExportProjectDialog.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "MainWindow.h"
#include "MicrotunerConfig.h"
#include "MixerView.h"
#include "PatternEditor.h"
#include "PianoRoll.h"
#include "ProjectNotes.h"
#include "SampleTrack.h"
#include "SampleTrackView.h"
#include "SampleTrackWindow.h"
#include "SessionGridView.h"
#include "SetupDialog.h"
#include "ShortcutsPage.h"
#include "SongEditor.h"
#include "StartHub.h"
#include "TabWidget.h"

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

//! One surface's walk: every Tab-reachable widget seen (each once), and the unnamed ones.
struct Walk
{
	QString surface;
	QSet<QWidget*> seen;
	QMap<QString, int>* found;
	QStringList* detail;
};

//! Collects what is visible in @a root now.
void collect(QWidget* root, Walk* walk)
{
	for (QWidget* widget : root->findChildren<QWidget*>())
	{
		if ((widget->focusPolicy() & Qt::TabFocus) == 0 || !widget->isVisibleTo(root)) { continue; }
		if (walk->seen.contains(widget)) { continue; }
		walk->seen.insert(widget);
		if (!accessibleNameOf(widget).isEmpty()) { continue; }
		const QString key = walk->surface + QLatin1Char('/') + QString::fromLatin1(widget->metaObject()->className());
		(*walk->found)[key] += 1;
		*walk->detail << key + QStringLiteral(" (objectName '%1', tooltip '%2', parent %3)")
			.arg(widget->objectName(), widget->toolTip(),
				QString::fromLatin1(widget->parentWidget()->metaObject()->className()));
	}
}

//! The TabWidgets under @a scope whose nearest TabWidget ancestor below @a scope is none.
QList<TabWidget*> outermostTabs(QWidget* scope)
{
	QList<TabWidget*> tabs;
	for (TabWidget* candidate : scope->findChildren<TabWidget*>())
	{
		QWidget* up = candidate->parentWidget();
		while (up != nullptr && up != scope && qobject_cast<TabWidget*>(up) == nullptr) { up = up->parentWidget(); }
		if (up == scope || up == nullptr) { tabs << candidate; }
	}
	return tabs;
}

//! Collects @a root with every page of every (nested) TabWidget under @a scope shown in turn,
//! so a tabbed window's hidden pages are walked too.
void collectAllPages(QWidget* root, QWidget* scope, Walk* walk)
{
	collect(root, walk);
	for (TabWidget* tabs : outermostTabs(scope))
	{
		const int active = tabs->activeTab();
		for (int page = 0; page < 16; ++page)
		{
			tabs->setActiveTab(page);
			if (tabs->activeTab() == page) { collectAllPages(root, tabs, walk); }
		}
		tabs->setActiveTab(active);
	}
}

} // namespace

class AccessibilityWalkTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		m_app = guitest::startGui(m_home);
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
			Walk one{surface, {}, &found, &detail};
			collectAllPages(root, root, &one);
			std::printf("A11Y_WALKED %s: %d focusable widgets\n", qPrintable(surface), static_cast<int>(one.seen.size()));
			// A surface with nothing to walk would pass for the wrong reason.
			if (one.seen.isEmpty()) { empty << surface; }
		};
		for (const auto& [surface, get] : editors) { walk(surface, get()); }
		// The windows that open on demand: a track's own window (every tab page) and the two
		// largest dialogs.
		QWidget* instrument = instrumentWindow();
		QVERIFY2(instrument != nullptr, "no instrument track window");
		walk(QStringLiteral("InstrumentWindow"), instrument);
		QWidget* sample = sampleTrackWindow();
		QVERIFY2(sample != nullptr, "no sample track window");
		walk(QStringLiteral("SampleTrackWindow"), sample);
		SetupDialog settings;
		walk(QStringLiteral("Settings"), &settings);
		ExportProjectDialog exporter(m_home.filePath(QStringLiteral("out.wav")), ExportProjectDialog::Mode::ExportProject);
		walk(QStringLiteral("Export"), &exporter);
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
	//! The track view the song editor made for a new track of type @a T.
	template<typename View>
	View* newTrackView(Track::Type type)
	{
		Track::create(type, Engine::getSong());
		QCoreApplication::processEvents();
		const QList<TrackView*>& views = getGUI()->songEditor()->m_editor->trackViews();
		return views.isEmpty() ? nullptr : dynamic_cast<View*>(views.last());
	}

	QWidget* instrumentWindow()
	{
		auto* view = newTrackView<InstrumentTrackView>(Track::Type::Instrument);
		return view != nullptr ? view->getInstrumentTrackWindow() : nullptr;
	}

	QWidget* sampleTrackWindow()
	{
		auto* view = newTrackView<SampleTrackView>(Track::Type::Sample);
		return view != nullptr ? view->getSampleTrackWindow() : nullptr;
	}

	QTemporaryDir m_home;
	GuiApplication* m_app = nullptr;
};

QTEST_MAIN(AccessibilityWalkTest)
#include "AccessibilityWalkTest.moc"
