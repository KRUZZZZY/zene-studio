/*
 * ControlRegistryDisplayTest.cpp - the registry's "display" requirement asks whether a
 *                                  display exists, not whether a human is attending
 *                                  (cluster gui-1105-menu-help-command-reference).
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
 * The test is GUI-less (QTEST_GUILESS_MAIN): with no QGuiApplication, QT_QPA_PLATFORM
 * is the only platform source UnattendedRun consults, so unsetting it stands in for a
 * visible desktop deterministically.
 */

#include <QtTest>

#include "ControlRegistry.h"
#include "UnattendedRun.h"

using namespace lmms;

namespace
{

//! Registers a command that declares requires: display and invokes it on a fresh
//! registry built for an agent instance (--control-socket).
ControlResult invokeDisplayCommandAsAgent(bool* headless)
{
	setAgentInstance(true);
	ControlRegistry::destroy();
	ControlRegistry* registry = ControlRegistry::instance();
	ControlRegistry::setReady(true);
	ControlCommand cmd;
	cmd.id = QStringLiteral("test.needs_display");
	cmd.group = QStringLiteral("test");
	cmd.verb = QStringLiteral("needs_display");
	cmd.requiresEngine = false;  // the requirement check alone is under test
	cmd.requiresDecl = QStringList{QStringLiteral("display")};
	cmd.handler = [](const QJsonObject&) { return ControlResult::success(QJsonObject{{QStringLiteral("ran"), true}}); };
	registry->registerCommand(cmd);
	*headless = registry->isHeadless();
	const ControlResult result = registry->invoke(QStringLiteral("test.needs_display"));
	setAgentInstance(false);
	ControlRegistry::destroy();
	return result;
}

} // namespace

class ControlRegistryDisplayTest : public QObject
{
	Q_OBJECT
private slots:
	void init() { m_platform = qgetenv("QT_QPA_PLATFORM"); }
	void cleanup() { qputenv("QT_QPA_PLATFORM", m_platform); }

	//! gui-1105: an agent instance on a real display is unattended - no modal may be raised
	//! for it - but it still has a display, so a command that declares requires: display
	//! (app.help, Help > Command Reference) runs instead of being refused.
	void agentInstanceOnDisplayRunsDisplayCommands()
	{
		qunsetenv("QT_QPA_PLATFORM");
		bool headless = false;
		const ControlResult result = invokeDisplayCommandAsAgent(&headless);
		QVERIFY2(headless, "an agent instance is unattended: GUI feedback stays suppressed");
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QVERIFY(result.result.value(QStringLiteral("ran")).toBool());
	}

	//! An agent instance on a displayless platform still refuses a display command.
	void agentInstanceWithoutDisplayRefusesDisplayCommands()
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
		bool headless = false;
		const ControlResult result = invokeDisplayCommandAsAgent(&headless);
		QVERIFY(headless);
		QVERIFY(!result.ok);
		QCOMPARE(result.errorKind, ControlErrorKind::Requires);
	}

private:
	QByteArray m_platform;
};

QTEST_GUILESS_MAIN(ControlRegistryDisplayTest)
#include "ControlRegistryDisplayTest.moc"
