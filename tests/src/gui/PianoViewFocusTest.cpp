/*
 * PianoViewFocusTest.cpp - focus handling with and without an assigned piano model
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QFocusEvent>

#include "Engine.h"
#include "PianoView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

class FocusTestPianoView : public PianoView
{
public:
	using PianoView::PianoView;

	void sendFocusIn()
	{
		QFocusEvent event(QEvent::FocusIn, Qt::OtherFocusReason);
		focusInEvent(&event);
	}
};

} // namespace

class PianoViewFocusTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void focusInWithNoAssignedPianoIsSafe()
	{
		FocusTestPianoView view(nullptr);
		QVERIFY(view.model() == nullptr);
		view.sendFocusIn();
	}
};

QTEST_MAIN(PianoViewFocusTest)

#include "PianoViewFocusTest.moc"
