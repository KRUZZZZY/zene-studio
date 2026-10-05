/*
 * LmmsStyleTest.cpp - the custom theme's hot reload, over a theme the test owns
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
/*! What the hot reload promises: edit style.css and the theme follows; delete it and recreate it
 *  and the theme follows again. A file watch dies with the file it watches, so the recreate half
 *  is the interesting one - and it is the half that has to keep working. */

#include <QtTest>

#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "Engine.h"
#include "GuiTestApplication.h"
#include "LmmsStyle.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

/*! Style, then un-style, is all a QSS marker needs to be: the whole string is searched. */
void writeTheme(const QString& path, const QString& marker)
{
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
	file.write(QString("QWidget { color: %1; }\n").arg(marker).toUtf8());
}

} // namespace

class LmmsStyleTest : public QObject
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

	void recreatingADeletedStylesheetKeepsHotReloading()
	{
		QVERIFY(m_theme.isValid());
		const QString css = m_theme.filePath(QStringLiteral("style.css"));
		writeTheme(css, QStringLiteral("redmarker"));

		ConfigManager::inst()->setThemeDir(m_theme.path());
		// The watcher belongs to the style, and the stylesheet it writes is the app-wide one,
		// which is what the reload is observable through.
		auto* style = new LmmsStyle();
		QTRY_VERIFY(qApp->styleSheet().contains(QStringLiteral("redmarker")));

		// Delete it: Qt drops the watch along with the file, so nothing is watching any more.
		QVERIFY(QFile::remove(css));
		QCoreApplication::processEvents();
		QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(css), 2000);

		// Recreate it with different contents. A theme the user is still editing comes back
		// this way, and the reload has to pick it up again rather than stay dead.
		writeTheme(css, QStringLiteral("greenmarker"));
		QTRY_VERIFY(qApp->styleSheet().contains(QStringLiteral("greenmarker")));

		delete style;
	}

private:
	QTemporaryDir m_home;
	QTemporaryDir m_theme;
};

QTEST_MAIN(LmmsStyleTest)
#include "LmmsStyleTest.moc"