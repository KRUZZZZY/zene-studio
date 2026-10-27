/*
 * PluginWindowSystem.h - the native window a hosted plug-in's own editor embeds in
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

#ifndef LMMS_PLUGIN_WINDOW_SYSTEM_H
#define LMMS_PLUGIN_WINDOW_SYSTEM_H

#include <QGuiApplication>
#include <QString>

namespace lmms
{

/*! 0.5.0 L10 (Q050-490): the window system a VST3 or CLAP editor is embedded in. Each format names
 *  the same three with its own strings (VST3 kPlatformType*, CLAP_WINDOW_API_*), and QWidget::winId()
 *  is the matching handle on each: an X11 window id, an HWND, an NSView pointer. Header-only, so the
 *  host tests that compile the host sources themselves need nothing more. */
enum class PluginWindowSystem
{
	X11,
	Win32,
	Cocoa,
	//! A session with no window an editor can embed in: native Wayland (no X11 window to give).
	None
};

//! The window system under the Qt platform plug-in @a qpaPlatform (QGuiApplication::platformName())
//! on this build's operating system. Windows embeds in an HWND and macOS in an NSView; elsewhere it is
//! X11 - xcb, and offscreen/minimal, whose stand-in ids the tests use - unless the session is native
//! Wayland, which is refused rather than handed an id that is not an X11 window.
inline PluginWindowSystem pluginWindowSystemFor(const QString& qpaPlatform)
{
#if defined(_WIN32)
	Q_UNUSED(qpaPlatform)
	return PluginWindowSystem::Win32;
#elif defined(__APPLE__)
	Q_UNUSED(qpaPlatform)
	return PluginWindowSystem::Cocoa;
#else
	return qpaPlatform.startsWith(QLatin1String("wayland")) ? PluginWindowSystem::None : PluginWindowSystem::X11;
#endif
}

//! The window system of the running application, read when an editor opens.
inline PluginWindowSystem currentPluginWindowSystem()
{
	return pluginWindowSystemFor(QGuiApplication::platformName());
}

//! The platform a refusal names: "X11", "Windows", "macOS", or the Qt platform that has none.
inline QString pluginWindowSystemName(PluginWindowSystem system)
{
	switch (system)
	{
	case PluginWindowSystem::X11: return QStringLiteral("X11");
	case PluginWindowSystem::Win32: return QStringLiteral("Windows");
	case PluginWindowSystem::Cocoa: return QStringLiteral("macOS");
	case PluginWindowSystem::None: break;
	}
	return QStringLiteral("Wayland");
}

//! The refusal for a session whose windows no plug-in editor can embed in.
inline QString noPluginWindowSystemError()
{
	return QStringLiteral("this %1 session has no native window a plug-in editor can embed in (start Zene Studio "
		"with QT_QPA_PLATFORM=xcb to embed in X11 through XWayland)").arg(QGuiApplication::platformName());
}

} // namespace lmms

#endif // LMMS_PLUGIN_WINDOW_SYSTEM_H
