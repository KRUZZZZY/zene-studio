/*
 * VersionedSaveDialogTest.cpp - version changes stay in the selected basename
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

#include "VersionedSaveDialog.h"

using lmms::gui::VersionedSaveDialog;

class VersionedSaveDialogTest : public QObject
{
	Q_OBJECT

private slots:
	void aDottedParentDoesNotAffectExtensionlessVersionInsertion()
	{
		QString path = QStringLiteral("/home/stems.take/Idea");
		QVERIFY(VersionedSaveDialog::changeFileNameVersion(path, true));
		QCOMPARE(path, QStringLiteral("/home/stems.take/Idea-01"));
	}
};

QTEST_APPLESS_MAIN(VersionedSaveDialogTest)
#include "VersionedSaveDialogTest.moc"
