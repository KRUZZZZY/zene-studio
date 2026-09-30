/*
 * GuiTestApplication.h - the real GuiApplication, in a test, over a throwaway home
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

#ifndef LMMS_TESTS_GUI_TEST_APPLICATION_H
#define LMMS_TESTS_GUI_TEST_APPLICATION_H

#include <QTemporaryDir>

#include "ConfigManager.h"
#include "GuiApplication.h"

namespace guitest
{

/*! Builds the REAL application - GuiApplication makes the main window and every editor the way
 *  the product does (most of them cannot be built without it: they add themselves to the main
 *  window's workspace or toolbar) - over a throwaway home, config and working directory and the
 *  Dummy audio device, so nothing the person running the test owns is touched. Call from
 *  initTestCase(), before anything reads ConfigManager; the caller destroys the Engine. */
inline lmms::gui::GuiApplication* startGui(const QTemporaryDir& home)
{
	for (const char* name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"})
	{
		qputenv(name, home.path().toLocal8Bit());
	}
	lmms::ConfigManager* config = lmms::ConfigManager::inst();
	config->loadConfigFile(home.filePath(QStringLiteral("zene.xml")));
	config->setValue(QStringLiteral("audioengine"), QStringLiteral("audiodev"), QStringLiteral("Dummy (no sound output)"));
	config->setWorkingDir(home.filePath(QStringLiteral("work")));
	config->createWorkingDir();
	return new lmms::gui::GuiApplication();
}

} // namespace guitest

#endif // LMMS_TESTS_GUI_TEST_APPLICATION_H
