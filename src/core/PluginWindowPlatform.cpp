/*
 * PluginWindowPlatform.cpp - Q050-502: the Qt platform a Linux session runs on, for plug-in editors
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

#include "PluginWindowPlatform.h"

#include <QGuiApplication>
#include <QtGlobal>

#include "lmmsconfig.h"

namespace lmms::pluginwindow
{

namespace
{

//! The decision chooseStartupPlatform() made; Unchanged until it runs (a test, a core-only run).
Decision s_startup;
//! setPlatformForTesting(): the platform editorRefusal() reads instead of the real one, and its decision.
QString s_testPlatform;
Decision s_testStartup;

} // namespace


Session currentSession(int argc, const char* const* argv)
{
	Session session;
	session.qpaPlatform = qgetenv("QT_QPA_PLATFORM");
	session.sessionType = qgetenv("XDG_SESSION_TYPE");
	session.waylandDisplay = qgetenv("WAYLAND_DISPLAY");
	session.x11Display = qgetenv("DISPLAY");
	for (int i = 1; i < argc; ++i)
	{
		const QByteArray arg(argv[i]);
		if (arg == "-platform" || arg == "--platform") { session.platformArgument = true; }
	}
	return session;
}


Decision decide(const Session& session)
{
	// The user's own choice wins, whatever it is - including native Wayland, where editors then refuse.
	if (!session.qpaPlatform.isEmpty() || session.platformArgument) { return {Choice::UserChoice, {}}; }
	const bool wayland = !session.waylandDisplay.isEmpty() || session.sessionType.toLower() == "wayland";
	if (!wayland) { return {}; }
	if (session.x11Display.isEmpty()) { return {Choice::NoXWayland, {}}; }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	// A fallback list: a Qt without its xcb plug-in still starts, on Wayland, and its editors say why they refuse.
	return {Choice::XWayland, QByteArrayLiteral("xcb;wayland")};
#else
	return {Choice::XWayland, QByteArrayLiteral("xcb")};
#endif
}


Decision chooseStartupPlatform(int argc, const char* const* argv)
{
#if defined(LMMS_BUILD_WIN32) || defined(LMMS_BUILD_APPLE)
	Q_UNUSED(argc);
	Q_UNUSED(argv);
	return {};
#else
	s_startup = decide(currentSession(argc, argv));
	if (s_startup.choice == Choice::XWayland) { qputenv("QT_QPA_PLATFORM", s_startup.qpaPlatform); }
	return s_startup;
#endif
}


QString describe(const Decision& decision)
{
	switch (decision.choice)
	{
	case Choice::XWayland:
		return QStringLiteral("Wayland session: running on XWayland (QT_QPA_PLATFORM=%1) so plug-in editors can open; "
			"set QT_QPA_PLATFORM=wayland to run natively without them").arg(QString::fromLatin1(decision.qpaPlatform));
	case Choice::NoXWayland:
		return QStringLiteral("Wayland session without XWayland (DISPLAY is not set): running natively on Wayland, "
			"plug-in editors cannot open");
	case Choice::UserChoice:  // silent: the user knows what they chose (and every offscreen test run chooses)
	case Choice::Unchanged:
		break;
	}
	return {};
}


QString editorRefusal(const QString& qtPlatform, const Decision& startup)
{
	if (!qtPlatform.startsWith(QLatin1String("wayland"))) { return {}; }
	const QString why = QStringLiteral("plug-in editors embed only in X11 windows and Zene Studio is running natively on "
		"Wayland (Qt platform '%1'): ").arg(qtPlatform);
	switch (startup.choice)
	{
	case Choice::NoXWayland:
		return why + QStringLiteral("this session has no XWayland (DISPLAY is not set) - enable or install XWayland, "
			"then restart Zene Studio");
	case Choice::UserChoice:
		return why + QStringLiteral("QT_QPA_PLATFORM or -platform asked for it - restart Zene Studio without them "
			"(or with QT_QPA_PLATFORM=xcb) to open editors through XWayland");
	case Choice::XWayland:
		return why + QStringLiteral("Qt's xcb platform plug-in could not be loaded - install it (libqxcb), then "
			"restart Zene Studio");
	case Choice::Unchanged:
		break;
	}
	return why + QStringLiteral("restart Zene Studio with QT_QPA_PLATFORM=xcb to open editors through XWayland");
}


QString editorRefusal()
{
	if (!s_testPlatform.isEmpty()) { return editorRefusal(s_testPlatform, s_testStartup); }
	return editorRefusal(QGuiApplication::platformName(), s_startup);
}


void setPlatformForTesting(const QString& qtPlatform, const Decision& startup)
{
	s_testPlatform = qtPlatform;
	s_testStartup = startup;
}

} // namespace lmms::pluginwindow
