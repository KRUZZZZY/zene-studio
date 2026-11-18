/*
 * FileRevealerTest.cpp - file manager discovery fallback
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
 */

#include <QtTest>

#include <QTemporaryDir>

#include "FileRevealer.h"
#include "lmmsconfig.h"

class FileRevealerTest : public QObject
{
	Q_OBJECT

private slots:
	void missingMimeHandlerFallsBackToXdgOpen()
	{
#if defined(LMMS_BUILD_WIN32)
		QCOMPARE(lmms::gui::FileRevealer::getDefaultFileManager(), QStringLiteral("explorer"));
#elif defined(LMMS_BUILD_APPLE)
		QCOMPARE(lmms::gui::FileRevealer::getDefaultFileManager(), QStringLiteral("open"));
#else
		QTemporaryDir emptyPath;
		QVERIFY(emptyPath.isValid());

		const bool hadPath = qEnvironmentVariableIsSet("PATH");
		const QByteArray previousPath = qgetenv("PATH");
		const bool hadDesktop = qEnvironmentVariableIsSet("XDG_CURRENT_DESKTOP");
		const QByteArray previousDesktop = qgetenv("XDG_CURRENT_DESKTOP");

		qputenv("PATH", emptyPath.path().toLocal8Bit());
		qputenv("XDG_CURRENT_DESKTOP", QByteArrayLiteral("zene-test"));
		const QString fileManager = lmms::gui::FileRevealer::getDefaultFileManager();

		if (hadPath) { qputenv("PATH", previousPath); }
		else { qunsetenv("PATH"); }
		if (hadDesktop) { qputenv("XDG_CURRENT_DESKTOP", previousDesktop); }
		else { qunsetenv("XDG_CURRENT_DESKTOP"); }

		QCOMPARE(fileManager, QStringLiteral("xdg-open"));
#endif
	}
};

QTEST_GUILESS_MAIN(FileRevealerTest)

#include "FileRevealerTest.moc"
