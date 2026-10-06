/*
 * PinConnectorTest.cpp - the pin matrix's hover cursor, over a real audio ports model
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

/*! Moving the pointer over a pin offers the pointing hand, and moving it while a button is held
 *  (a drag) does not: a mouse-move event reports the held buttons, not a button of its own, so a
 *  drag that keeps the hand cursor over the matrix reads as a hover. PinConnector's constructor
 *  needs the real application (it windows itself in the main window), hence the GuiTestApplication
 *  boot, and its matrices are a private nested class, so they are reached as the connector's own
 *  children rather than by name. */

#include <QTemporaryDir>

#include <QtTest>

#include <QMouseEvent>

#include "AudioPortsModel.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "PinConnector.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! AudioPortsModel is abstract (bufferPropertiesChanging)
class TestAudioPortsModel : public AudioPortsModel
{
public:
	using AudioPortsModel::AudioPortsModel;

protected:
	void bufferPropertiesChanging(ch_cnt_t, ch_cnt_t, f_cnt_t) override {}
};

//! The centre of pin (0, 0), in a matrix widget's own coordinates: the cells start at
//! BorderWidth + line thickness (1 + 1) and are cellSize() (13 + 2 + 1) wide, both from PinConnector.h.
QPoint firstPin()
{
	return QPoint{6, 6};
}

//! The connector's own pin matrices, as the widgets they are (the class is private to PinConnector,
//! and moc names it with its namespace). sizeHint() is virtual, so it answers for the matrix too.
QList<QWidget*> matrices(const PinConnector& connector)
{
	QList<QWidget*> found;
	for (QWidget* child : connector.findChildren<QWidget*>())
	{
		if (child->inherits("lmms::gui::PinConnector::MatrixView"))
		{
			child->resize(child->sizeHint());
			found.append(child);
		}
	}
	return found;
}

} // namespace


class PinConnectorTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! A hover offers the hand; the same move with a button held (a drag) must not.
	void aDragDoesNotOfferTheHoverCursor()
	{
		TestAudioPortsModel model{2, 2, false};
		PinConnector connector{&model};
		const QList<QWidget*> views = matrices(connector);
		QVERIFY2(!views.isEmpty(), "the connector's matrices are missing, so this proves nothing");

		for (QWidget* view : views)
		{
			QMouseEvent hover{QEvent::MouseMove, firstPin(), firstPin(), Qt::MouseButton::NoButton,
				Qt::MouseButton::NoButton, Qt::KeyboardModifier::NoModifier};
			QApplication::sendEvent(view, &hover);
			QCOMPARE(view->cursor().shape(), Qt::CursorShape::PointingHandCursor);

			QMouseEvent drag{QEvent::MouseMove, firstPin(), firstPin(), Qt::MouseButton::NoButton,
				Qt::MouseButton::LeftButton, Qt::KeyboardModifier::NoModifier};
			QApplication::sendEvent(view, &drag);
			QVERIFY2(view->cursor().shape() != Qt::CursorShape::PointingHandCursor,
				"a move with a button held still shows the hover cursor, so a drag reads as a hover");
		}
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(PinConnectorTest)
#include "PinConnectorTest.moc"
