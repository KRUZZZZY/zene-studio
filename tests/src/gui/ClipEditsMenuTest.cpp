/*
 * ClipEditsMenuTest.cpp - a sample clip's Gain and fades menu drives clip.set_gain / clip.set_fade
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

/*! On a four-bar sample clip: Clip gain... sets the prompted dB (a cancelled prompt changes nothing)
 *  and the item names the current value, Reset gain returns to unity, Fade in / Fade out set the
 *  chosen length and check it, Fade shape sets both ramps, and one control.undo takes a change
 *  back. A fade longer than the clip is refused and leaves the clip alone. */

#include <QtTest>

#include <QAction>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>

#include "ClipEdits.h"
#include "ClipEditsMenu.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QAction* item(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

QMenu* submenu(QMenu* menu, const QString& text)
{
	QAction* action = item(menu, text);
	return action != nullptr ? action->menu() : nullptr;
}

//! A fresh menu for @a clip, as the context menu builds one each time it opens.
QMenu* editsMenu(QMenu& parent, Clip* clip)
{
	parent.clear();
	addClipEditsMenu(&parent, clip);
	return submenu(&parent, QStringLiteral("Gain and fades"));
}

} // namespace

class ClipEditsMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		setClipGainPrompt({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void gainAndFadesAreSetFromTheMenu()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setAutoResize(false);
		clip->changeLength(TimePos(4 * DefaultTicksPerBar));

		QMenu parent;
		QMenu* menu = editsMenu(parent, clip);
		QVERIFY(menu != nullptr);
		setClipGainPrompt([](double) { return std::nullopt; });
		item(menu, QStringLiteral("Clip gain (0.0 dB)..."))->trigger();
		QCOMPARE(clip->clipEdits().gain, 1.0f);
		setClipGainPrompt([](double current) { return current - 6.0; });
		item(menu, QStringLiteral("Clip gain (0.0 dB)..."))->trigger();
		QVERIFY(std::abs(clip->clipEdits().gain - 0.501187f) < 1e-4f);

		menu = editsMenu(parent, clip);
		QVERIFY(item(menu, QStringLiteral("Clip gain (-6.0 dB)...")) != nullptr);
		QAction* reset = item(menu, QStringLiteral("Reset gain"));
		QVERIFY(reset->isEnabled());
		reset->trigger();
		QCOMPARE(clip->clipEdits().gain, 1.0f);

		menu = editsMenu(parent, clip);
		item(submenu(menu, QStringLiteral("Fade in")), QStringLiteral("1 beat"))->trigger();
		item(submenu(menu, QStringLiteral("Fade out")), QStringLiteral("1 bar"))->trigger();
		QCOMPARE(clip->clipEdits().fadeInTicks, DefaultTicksPerBar / 4);
		QCOMPARE(clip->clipEdits().fadeOutTicks, DefaultTicksPerBar);
		item(submenu(menu, QStringLiteral("Fade shape")), QStringLiteral("Equal power"))->trigger();
		QCOMPARE(clip->clipEdits().fadeInShape, FadeShape::EqualPower);
		QCOMPARE(clip->clipEdits().fadeOutShape, FadeShape::EqualPower);

		menu = editsMenu(parent, clip);
		QVERIFY(item(submenu(menu, QStringLiteral("Fade in")), QStringLiteral("1 beat"))->isChecked());
		QVERIFY(item(submenu(menu, QStringLiteral("Fade out")), QStringLiteral("1 bar"))->isChecked());
		QVERIFY(item(submenu(menu, QStringLiteral("Fade shape")), QStringLiteral("Equal power"))->isChecked());

		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QCOMPARE(clip->clipEdits().fadeInShape, FadeShape::Linear);
		QCOMPARE(clip->clipEdits().fadeOutTicks, DefaultTicksPerBar);

		// A clip one beat long cannot hold a one-bar fade out beside its one-beat fade in.
		clip->changeLength(TimePos(DefaultTicksPerBar / 4));
		menu = editsMenu(parent, clip);
		const ClipEdits before = clip->clipEdits();
		item(submenu(menu, QStringLiteral("Fade out")), QStringLiteral("2 beats"))->trigger();
		QVERIFY(clip->clipEdits() == before);
	}

	void noClipNoMenu()
	{
		QMenu menu;
		addClipEditsMenu(&menu, nullptr);
		QVERIFY(menu.actions().isEmpty());
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(ClipEditsMenuTest)
#include "ClipEditsMenuTest.moc"
