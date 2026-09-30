/*
 * MidiControllerMenuTest.cpp - a MIDI-driven control's menu sets soft takeover and LED feedback
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

/*! A track's volume, driven by a MidiController: its knob's context menu has Soft takeover and LED
 *  feedback in the connection submenu, and toggling them runs controller.soft_takeover /
 *  controller.feedback - the controller's flags follow. A control with no MIDI connection gets
 *  neither item. */

#include <QtTest>

#include <QMenu>

#include "ControlRegistry.h"
#include "ControllerConnection.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Knob.h"
#include "MidiController.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QAction* find(QMenu& menu, const QString& text)
{
	for (QAction* action : menu.actions())
	{
		if (action->text() == text) { return action; }
		if (action->menu() != nullptr)
		{
			for (QAction* inner : action->menu()->actions())
			{
				if (inner->text() == text) { return inner; }
			}
		}
	}
	return nullptr;
}

} // namespace

class MidiControllerMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theConnectionMenuTogglesTakeoverAndFeedback()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		FloatModel* volume = track->volumeModel();
		auto* midi = new MidiController(Engine::getSong());
		volume->setControllerConnection(new ControllerConnection(midi));

		Knob knob(KnobType::Bright26);
		knob.setModel(volume);
		QMenu menu;
		knob.addDefaultActions(&menu);
		QAction* takeover = find(menu, QMenu::tr("Soft takeover"));
		QAction* feedback = find(menu, QMenu::tr("LED feedback"));
		QVERIFY2(takeover != nullptr && feedback != nullptr, "no controller toggles in the connection menu");
		QVERIFY(!takeover->isChecked());

		takeover->trigger();
		QVERIFY(midi->softTakeoverEnabled());
		feedback->trigger();
		QVERIFY(midi->feedbackEnabled());
		takeover->trigger();
		QVERIFY(!midi->softTakeoverEnabled());

		FloatModel loose(0.f, 0.f, 1.f, 0.01f);
		Knob plain(KnobType::Bright26);
		plain.setModel(&loose);
		QMenu none;
		plain.addDefaultActions(&none);
		QVERIFY(find(none, QMenu::tr("Soft takeover")) == nullptr);
	}
};

QTEST_MAIN(MidiControllerMenuTest)
#include "MidiControllerMenuTest.moc"
