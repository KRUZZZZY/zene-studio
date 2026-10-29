/*
 * PluginWindowPlatform.h - Q050-502: the Qt platform a Linux session runs on, for plug-in editors
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

#ifndef LMMS_PLUGIN_WINDOW_PLATFORM_H
#define LMMS_PLUGIN_WINDOW_PLATFORM_H

#include <QByteArray>
#include <QString>

#include "lmms_export.h"

/*! A hosted plug-in's own editor (VST3 kPlatformTypeX11EmbedWindowID, CLAP CLAP_WINDOW_API_X11) embeds
 *  only in an X11 window. On Qt's wayland platform a native widget's winId() is not one (measured: a heap
 *  pointer that X11 answers with BadWindow), so on a Wayland desktop the application runs on XWayland
 *  (QT_QPA_PLATFORM=xcb), chosen at start-up unless the user chose a platform - as other Linux DAWs do.
 *  Where that cannot happen, an editor refuses with the reason instead of being handed a bogus window. */
namespace lmms::pluginwindow
{

//! What the start-up decision reads: the environment and whether argv carries Qt's -platform.
struct Session
{
	QByteArray qpaPlatform;     //!< QT_QPA_PLATFORM as the user left it
	QByteArray sessionType;     //!< XDG_SESSION_TYPE
	QByteArray waylandDisplay;  //!< WAYLAND_DISPLAY
	QByteArray x11Display;      //!< DISPLAY - set by KDE and GNOME when XWayland is available
	bool platformArgument = false;  //!< -platform/--platform on the command line
};

enum class Choice
{
	Unchanged,   //!< not a Wayland session: Qt's own choice (xcb on X11), nothing set
	XWayland,    //!< a Wayland session with XWayland: QT_QPA_PLATFORM set to xcb
	NoXWayland,  //!< a Wayland session without XWayland: native Wayland, editors refuse
	UserChoice,  //!< QT_QPA_PLATFORM or -platform given: left alone
};

struct Decision
{
	Choice choice = Choice::Unchanged;
	QByteArray qpaPlatform;  //!< the value QT_QPA_PLATFORM is set to (XWayland only), else empty
};

//! The session of this process: its environment and @a argv.
LMMS_EXPORT Session currentSession(int argc, const char* const* argv);

//! The platform decision for @a session; pure.
LMMS_EXPORT Decision decide(const Session& session);

/*! Decides for this process, sets QT_QPA_PLATFORM when the decision is XWayland, remembers the
 *  decision for editorRefusal() and returns it. Before the QApplication exists; a no-op on Windows and macOS. */
LMMS_EXPORT Decision chooseStartupPlatform(int argc, const char* const* argv);

//! One line for the log saying what @a decision did and how to override it; empty for Unchanged and UserChoice.
LMMS_EXPORT QString describe(const Decision& decision);

//! Why an editor cannot embed on Qt platform @a qtPlatform after @a startup; empty when it can (xcb,
//! offscreen, windows, cocoa... - every platform but wayland).
LMMS_EXPORT QString editorRefusal(const QString& qtPlatform, const Decision& startup);

//! The same for this process: the running Qt platform and the remembered start-up decision.
LMMS_EXPORT QString editorRefusal();

/*! Tests only: editorRefusal() answers as if the application ran on @a qtPlatform after @a startup.
 *  An empty @a qtPlatform restores the real platform and decision. */
LMMS_EXPORT void setPlatformForTesting(const QString& qtPlatform, const Decision& startup);

} // namespace lmms::pluginwindow

#endif // LMMS_PLUGIN_WINDOW_PLATFORM_H
