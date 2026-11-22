/*
 * PluginWindowPlatformTest.cpp - Q050-502: the Qt platform a Wayland desktop runs on, for plug-in editors
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

/*! The start-up decision without a compositor: a Wayland session with XWayland runs on xcb, one without it stays
 *  on Wayland, the user's QT_QPA_PLATFORM or -platform is never overridden, an X11 session is left alone; and the
 *  editor refusal each outcome leaves - none on xcb/offscreen, a reason naming the way out on wayland. */

#include <QtTest>

#include "PluginWindowPlatform.h"
#include "lmmsconfig.h"

using namespace lmms::pluginwindow;

namespace
{

Session wayland(const QByteArray& display)
{
	Session session;
	session.sessionType = "wayland";
	session.waylandDisplay = "wayland-0";
	session.x11Display = display;
	return session;
}

//! Restores one environment variable when it goes out of scope.
class EnvGuard
{
public:
	explicit EnvGuard(const char* name) : m_name(name), m_had(qEnvironmentVariableIsSet(name)), m_value(qgetenv(name)) {}
	~EnvGuard()
	{
		if (m_had) { qputenv(m_name, m_value); }
		else { qunsetenv(m_name); }
	}
	EnvGuard(const EnvGuard&) = delete;
	EnvGuard& operator=(const EnvGuard&) = delete;

private:
	const char* m_name;
	bool m_had;
	QByteArray m_value;
};

} // namespace

class PluginWindowPlatformTest : public QObject
{
	Q_OBJECT

private slots:
	void aWaylandSessionWithXWaylandRunsOnXcb()
	{
		const Decision decision = decide(wayland(":1"));
		QCOMPARE(decision.choice, Choice::XWayland);
		QVERIFY(decision.qpaPlatform.startsWith("xcb"));
		// XDG_SESSION_TYPE alone (WAYLAND_DISPLAY unset, as some launchers leave it) is a Wayland session too.
		Session typeOnly;
		typeOnly.sessionType = "Wayland";
		typeOnly.x11Display = ":0";
		QCOMPARE(decide(typeOnly).choice, Choice::XWayland);
		QVERIFY(describe(decision).contains(QStringLiteral("XWayland")));
	}

	void aWaylandSessionWithoutXWaylandStaysOnWayland()
	{
		const Decision decision = decide(wayland(""));
		QCOMPARE(decision.choice, Choice::NoXWayland);
		QVERIFY(decision.qpaPlatform.isEmpty());
		QVERIFY(describe(decision).contains(QStringLiteral("DISPLAY is not set")));
	}

	void theUsersChoiceIsNeverOverridden()
	{
		Session byEnvironment = wayland(":1");
		byEnvironment.qpaPlatform = "wayland";
		QCOMPARE(decide(byEnvironment).choice, Choice::UserChoice);
		Session byArgument = wayland(":1");
		byArgument.platformArgument = true;
		QCOMPARE(decide(byArgument).choice, Choice::UserChoice);
		QVERIFY(describe(decide(byArgument)).isEmpty());  // the user's choice is not logged
		const char* argv[] = {"zene", "-platform", "wayland"};
		QVERIFY(currentSession(3, argv).platformArgument);
		const char* plain[] = {"zene", "song.mmpz"};
		QVERIFY(!currentSession(2, plain).platformArgument);
	}

	void anX11SessionIsLeftAlone()
	{
		Session x11;
		x11.sessionType = "x11";
		x11.x11Display = ":0";
		const Decision decision = decide(x11);
		QCOMPARE(decision.choice, Choice::Unchanged);
		QVERIFY(decision.qpaPlatform.isEmpty());
		QVERIFY(describe(decision).isEmpty());
	}

	//! The process-level step: QT_QPA_PLATFORM is set for a Wayland session with XWayland and untouched otherwise.
	void theStartupStepSetsTheEnvironmentOnlyWhenItDecides()
	{
#if defined(LMMS_BUILD_WIN32) || defined(LMMS_BUILD_APPLE)
		QSKIP("Windows and macOS have no Wayland session: the start-up step is a no-op there");
#endif
		const EnvGuard qpa("QT_QPA_PLATFORM");
		const EnvGuard type("XDG_SESSION_TYPE");
		const EnvGuard waylandDisplay("WAYLAND_DISPLAY");
		const EnvGuard display("DISPLAY");
		const char* argv[] = {"zene"};
		qunsetenv("QT_QPA_PLATFORM");
		qputenv("XDG_SESSION_TYPE", "wayland");
		qputenv("WAYLAND_DISPLAY", "wayland-0");
		qputenv("DISPLAY", ":1");
		QCOMPARE(chooseStartupPlatform(1, argv).choice, Choice::XWayland);
		QVERIFY(qgetenv("QT_QPA_PLATFORM").startsWith("xcb"));

		qputenv("QT_QPA_PLATFORM", "wayland");  // the user's override
		QCOMPARE(chooseStartupPlatform(1, argv).choice, Choice::UserChoice);
		QCOMPARE(qgetenv("QT_QPA_PLATFORM"), QByteArray("wayland"));

		qunsetenv("QT_QPA_PLATFORM");
		qunsetenv("DISPLAY");  // no XWayland
		QCOMPARE(chooseStartupPlatform(1, argv).choice, Choice::NoXWayland);
		QVERIFY(!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"));
	}

	void onlyTheWaylandPlatformRefusesAnEditor()
	{
		for (const char* platform : {"xcb", "offscreen", "windows", "cocoa", "minimal"})
		{
			QVERIFY2(editorRefusal(QLatin1String(platform), Decision{Choice::UserChoice, {}}).isEmpty(), platform);
		}
		const QString noXWayland = editorRefusal(QStringLiteral("wayland"), Decision{Choice::NoXWayland, {}});
		QVERIFY(noXWayland.contains(QStringLiteral("X11")));
		QVERIFY(noXWayland.contains(QStringLiteral("no XWayland")));
		const QString user = editorRefusal(QStringLiteral("wayland-egl"), Decision{Choice::UserChoice, {}});
		QVERIFY(user.contains(QStringLiteral("QT_QPA_PLATFORM")));
		const QString noXcb = editorRefusal(QStringLiteral("wayland"), Decision{Choice::XWayland, "xcb;wayland"});
		QVERIFY(noXcb.contains(QStringLiteral("xcb platform plug-in")));
		QVERIFY(!editorRefusal(QStringLiteral("wayland"), Decision{}).isEmpty());
	}

	void theTestingSeamIsTheOnlyWayToFakeThePlatform()
	{
		setPlatformForTesting(QStringLiteral("wayland"), Decision{Choice::NoXWayland, {}});
		QVERIFY(editorRefusal().contains(QStringLiteral("no XWayland")));
		setPlatformForTesting({}, {});
		QVERIFY(editorRefusal().isEmpty());  // a QCoreApplication test: no Qt platform, nothing refused
	}
};

QTEST_GUILESS_MAIN(PluginWindowPlatformTest)
#include "PluginWindowPlatformTest.moc"
