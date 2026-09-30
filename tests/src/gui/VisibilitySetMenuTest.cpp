/*
 * VisibilitySetMenuTest.cpp - the Views menu shows a set, all tracks, saves and removes sets
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

/*! In the real application: a set holding one of two tracks, applied from the Views menu, hides
 *  the other track's ROW (the view flag used to reach no view); All tracks brings it back through
 *  track.visibility_show_all, which one control.undo reverses; Save visible tracks as... saves the
 *  tracks shown; Remove deletes a set. */

#include <QtTest>

#include <QJsonArray>
#include <QMenu>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "Song.h"
#include "SongEditor.h"
#include "TrackView.h"
#include "VisibilitySetMenu.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QAction* entry(QMenu& menu, const QString& text)
{
	for (QAction* action : menu.actions())
	{
		if (action->text() == text) { return action; }
		if (action->menu() != nullptr)
		{
			for (QAction* inner : action->menu()->actions())
			{
				if (action->text() == QMenu::tr("Remove") && inner->text() == text) { return inner; }
			}
		}
	}
	return nullptr;
}

TrackView* viewOf(const Track* track)
{
	for (TrackView* view : getGUI()->songEditor()->m_editor->trackViews())
	{
		if (view->getTrack() == track) { return view; }
	}
	return nullptr;
}

QStringList setNames()
{
	QStringList names;
	for (const QJsonValue& value : ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_list"),
			QJsonObject{}).result.value(QStringLiteral("sets")).toArray())
	{
		names << value.toObject().value(QStringLiteral("name")).toString();
	}
	return names;
}

} // namespace

class VisibilitySetMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
	}

	void cleanupTestCase()
	{
		setVisibilitySetPrompt({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aSetHidesRowsAndAllTracksBringsThemBack()
	{
		Track* kept = Track::create(Track::Type::Sample, Engine::getSong());
		Track* other = Track::create(Track::Type::Sample, Engine::getSong());
		QCoreApplication::processEvents();
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("track.visibility_set_save"),
			{{QStringLiteral("name"), QStringLiteral("Kept")},
				{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(kept)}}}).ok);

		QMenu menu;
		populateVisibilitySetMenu(&menu);
		entry(menu, QStringLiteral("Kept"))->trigger();
		QTRY_VERIFY(!viewOf(other)->isVisible());
		QVERIFY(viewOf(kept)->isVisible());

		populateVisibilitySetMenu(&menu);
		QVERIFY(entry(menu, QStringLiteral("Kept"))->isChecked());
		entry(menu, QMenu::tr("All tracks"))->trigger();
		QTRY_VERIFY(viewOf(other)->isVisible());
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QTRY_VERIFY(!viewOf(other)->isVisible());  // undo of show-all: the set's view again
		entry(menu, QMenu::tr("All tracks"))->trigger();
		QTRY_VERIFY(viewOf(other)->isVisible());

		setVisibilitySetPrompt([] { return std::optional<QString>(QStringLiteral("Everything")); });
		populateVisibilitySetMenu(&menu);
		entry(menu, QMenu::tr("Save visible tracks as..."))->trigger();
		QVERIFY(setNames().contains(QStringLiteral("Everything")));

		populateVisibilitySetMenu(&menu);
		entry(menu, QStringLiteral("Kept"))->trigger();  // the Remove submenu's entry comes after
		populateVisibilitySetMenu(&menu);
		QAction* removeKept = nullptr;
		for (QAction* action : menu.actions())
		{
			if (action->menu() != nullptr)
			{
				for (QAction* inner : action->menu()->actions())
				{
					if (inner->text() == QLatin1String("Kept")) { removeKept = inner; }
				}
			}
		}
		QVERIFY(removeKept != nullptr);
		removeKept->trigger();
		QVERIFY(!setNames().contains(QStringLiteral("Kept")));
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(VisibilitySetMenuTest)
#include "VisibilitySetMenuTest.moc"
