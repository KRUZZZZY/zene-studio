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
#include "ControlVocabulary.h"
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

	//! clip.set_record from the menu: the item arms and disarms the clip, one control.undo takes an
	//! arm back, and a MIDI clip gets no item and a typed refusal from the command itself.
	void aClipIsArmedToRecordFromTheMenu()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		QVERIFY(!clip->isRecord());

		QMenu menu;
		addClipRecordAction(&menu, clip);
		QAction* record = item(&menu, QStringLiteral("Record into this clip"));
		QVERIFY(record != nullptr);
		QVERIFY(record->isCheckable() && !record->isChecked());
		record->trigger();
		QVERIFY(clip->isRecord());
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QVERIFY(!clip->isRecord());

		QMenu again;
		clip->setRecord(true);
		addClipRecordAction(&again, clip);
		QAction* armed = item(&again, QStringLiteral("Record into this clip"));
		QVERIFY(armed->isChecked());
		armed->trigger();
		QVERIFY(!clip->isRecord());

		Track* instrument = Track::create(Track::Type::Instrument, Engine::getSong());
		Clip* notes = instrument->createClip(TimePos(0));
		QMenu none;
		addClipRecordAction(&none, notes);
		QVERIFY(none.actions().isEmpty());
		const ControlResult refused = ControlRegistry::instance()->invoke(QStringLiteral("clip.set_record"),
			{{QStringLiteral("clip"), control::clipIdOf(notes)}, {QStringLiteral("record"), true}});
		QVERIFY(!refused.ok);
		QVERIFY(refused.errorMessage.contains(QStringLiteral("piano roll")));
	}

	//! Trim to the playhead and slip by a beat: each runs its command and one control.undo takes it
	//! back; with the playhead outside the clip the trims are not offered.
	void aClipIsTrimmedAndSlippedFromTheMenu()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setAutoResize(false);
		clip->changeLength(TimePos(4 * DefaultTicksPerBar));
		const auto seek = [](int ticks) {
			QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.seek"),
				{{QStringLiteral("ticks"), ticks}}).ok);
		};

		seek(DefaultTicksPerBar);
		QMenu menu;
		addClipTrimActions(&menu, clip);
		item(&menu, QStringLiteral("Trim start to playhead"))->trigger();
		QCOMPARE(clip->startPosition().getTicks(), DefaultTicksPerBar);
		QCOMPARE(clip->endPosition().getTicks(), 4 * DefaultTicksPerBar);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QCOMPARE(clip->startPosition().getTicks(), 0);

		QMenu again;
		addClipTrimActions(&again, clip);
		item(&again, QStringLiteral("Trim end to playhead"))->trigger();
		QCOMPARE(clip->startPosition().getTicks(), 0);
		QCOMPARE(clip->endPosition().getTicks(), DefaultTicksPerBar);

		const int offset = clip->startTimeOffset().getTicks();
		item(&again, QStringLiteral("Slip content +1 beat"))->trigger();
		QCOMPARE(clip->startTimeOffset().getTicks(), offset + DefaultTicksPerBar / 4);
		QCOMPARE(clip->startPosition().getTicks(), 0);
		QCOMPARE(clip->endPosition().getTicks(), DefaultTicksPerBar);

		seek(8 * DefaultTicksPerBar);
		QMenu outside;
		addClipTrimActions(&outside, clip);
		QVERIFY(!item(&outside, QStringLiteral("Trim start to playhead"))->isEnabled());
		QVERIFY(!item(&outside, QStringLiteral("Trim end to playhead"))->isEnabled());
		seek(0);
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
