/*
 * SampleClipWarpModeMenuTest.cpp - a sample clip's Warp mode menu drives warp.stretch (Q050-036)
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! The context menu's "Warp mode" submenu offers the five spec modes and the
 *  WSOLA engine, checks the clip's current one, and drives the SAME
 *  registered command the agent surface takes (warp.stretch), so a refusal is
 *  the command's own typed one: a pitch-keeping mode on a clip that renders
 *  linearly changes nothing, and one control.undo takes a switch back. A MIDI
 *  clip gets no menu. */

#include <QtTest>

#include <QAction>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>

#include "ClipEditsMenu.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MidiClip.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "WarpMarkers.h"

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

//! A fresh menu for @a clip, as the context menu builds one each time it opens.
QMenu* warpMenu(QMenu& parent, Clip* clip)
{
	parent.clear();
	addClipWarpModeMenu(&parent, clip);
	QAction* entry = item(&parent, QStringLiteral("Warp mode"));
	return entry != nullptr ? entry->menu() : nullptr;
}

} // namespace


class SampleClipWarpModeMenuTest : public QObject
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
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theSpecifiedModesAreOfferedAndDriveWarpStretch()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		// The refusal rules need a rate change: two markers, a 2x warp.
		const std::array<WarpMarker, 2> markers{ WarpMarker{0, 0}, WarpMarker{88200, 96} };
		QVERIFY(clip->setWarpMarkers(std::span<const WarpMarker>(markers.data(), markers.size())));

		QMenu parent;
		QMenu* menu = warpMenu(parent, clip);
		QVERIFY(menu != nullptr);
		for (const QString& text : {QStringLiteral("Beats"), QStringLiteral("Tones"),
			QStringLiteral("Texture"), QStringLiteral("Re-Pitch"), QStringLiteral("Complex"),
			QStringLiteral("Pitch-preserving (WSOLA)")})
		{
			QAction* action = item(menu, text);
			QVERIFY2(action != nullptr, qPrintable(QStringLiteral("no '%1' item").arg(text)));
			QVERIFY2(action->isCheckable(), qPrintable(text));
			QCOMPARE(action->data().toString(), QStringLiteral("warp.stretch"));
		}
		// The default mode is Re-Pitch, and its item is the checked one.
		QCOMPARE(clip->warpStretchMode(), WarpStretchMode::Resample);
		QVERIFY(item(menu, QStringLiteral("Re-Pitch"))->isChecked());
		QVERIFY(!item(menu, QStringLiteral("Beats"))->isChecked());

		// Each specified mode is one menu gesture away, and the gesture lands
		// on the model through the registered command.
		const std::vector<std::pair<QString, WarpStretchMode>> gestures{
			{QStringLiteral("Beats"), WarpStretchMode::Beats},
			{QStringLiteral("Tones"), WarpStretchMode::Tones},
			{QStringLiteral("Texture"), WarpStretchMode::Texture},
			{QStringLiteral("Complex"), WarpStretchMode::Complex},
			{QStringLiteral("Pitch-preserving (WSOLA)"), WarpStretchMode::PreservePitch},
			{QStringLiteral("Re-Pitch"), WarpStretchMode::Resample}};
		for (const auto& [text, mode] : gestures)
		{
			QMenu again;
			menu = warpMenu(again, clip);
			item(menu, text)->trigger();
			QCOMPARE(clip->warpStretchMode(), mode);
			// The clip's current mode is always the checked item.
			QVERIFY(item(menu, text)->isChecked());
		}

		// One control.undo takes the last switch (to Re-Pitch) back.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QCOMPARE(clip->warpStretchMode(), WarpStretchMode::PreservePitch);
	}

	//! A clip that renders linearly has no rate change: the pitch-keeping
	//! items are the command's typed refusal and leave the mode alone.
	void aLinearClipRefusesThePitchKeepingModes()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);

		QMenu parent;
		QMenu* menu = warpMenu(parent, clip);
		QVERIFY(menu != nullptr);
		for (const QString& text : {QStringLiteral("Beats"), QStringLiteral("Tones"),
			QStringLiteral("Texture"), QStringLiteral("Complex"),
			QStringLiteral("Pitch-preserving (WSOLA)")})
		{
			item(menu, text)->trigger();
			QCOMPARE(clip->warpStretchMode(), WarpStretchMode::Resample);
		}
	}

	void aMidiClipGetsNoWarpModeMenu()
	{
		Track* instrument = Track::create(Track::Type::Instrument, Engine::getSong());
		Clip* notes = instrument->createClip(TimePos(0));
		QMenu menu;
		addClipWarpModeMenu(&menu, notes);
		QVERIFY(menu.actions().isEmpty());
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(SampleClipWarpModeMenuTest)
#include "SampleClipWarpModeMenuTest.moc"
