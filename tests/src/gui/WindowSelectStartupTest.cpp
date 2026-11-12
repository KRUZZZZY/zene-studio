/*
 * WindowSelectStartupTest.cpp - window.select during GuiApplication's own startup is a typed refusal, not a read of
 *                               an unassigned main window
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

#include <QtTest>

#include <QCoreApplication>
#include <QJsonObject>
#include <QLabel>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

/*! Watches the loading label GuiApplication shows while it builds the interface. The label reads "Preparing UI" only
 *  after GuiApplication has published itself through getGUI() and before it assigns the main window, so the first event
 *  delivered to the label is inside that window: the control socket is already listening and a window.select that
 *  arrives then must be refused with a typed Requires, not answered from an unassigned main window. The probe runs the
 *  command synchronously from that point - the UI thread runs a command in place - so no timing is involved. */
class StartupProbe : public QObject
{
public:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		Q_UNUSED(event);
		auto* label = qobject_cast<QLabel*>(watched);
		if (m_fired || label == nullptr || label->text() != QStringLiteral("Preparing UI")) { return false; }
		m_fired = true;
		m_reply = ControlRegistry::instance()->invoke(QStringLiteral("window.select"), QJsonObject());
		return false;
	}

	bool fired() const { return m_fired; }
	const ControlResult& reply() const { return m_reply; }

private:
	bool m_fired = false;
	ControlResult m_reply;
};

} // namespace

class WindowSelectStartupTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		QCoreApplication::instance()->installEventFilter(&m_probe);
		guitest::startGui(m_dir);
		QCoreApplication::instance()->removeEventFilter(&m_probe);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A window.select delivered while GuiApplication is between publishing itself and assigning its main window is
	//! refused with Requires, the same answer as in a process with no interface.
	void selectDuringStartupIsRefusedWithRequires()
	{
		QVERIFY(m_probe.fired());
		QVERIFY(!m_probe.reply().ok);
		QCOMPARE(m_probe.reply().errorKind, ControlErrorKind::Requires);
	}

private:
	QTemporaryDir m_dir;
	StartupProbe m_probe;
};

QTEST_MAIN(WindowSelectStartupTest)
#include "WindowSelectStartupTest.moc"
