/*
 * MonoDelayTest.cpp - verify MonoDelay releases its array buffer correctly
 *
 * This file is part of Zene Studio.
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QFile>
#include <QtTest>

class MonoDelayTest : public QObject
{
	Q_OBJECT

private slots:
	void bothBufferReleaseSitesUseArrayDelete()
	{
		QFile implementation{QStringLiteral("../../plugins/Flanger/MonoDelay.cpp")};
		QVERIFY2(implementation.open(QIODevice::ReadOnly | QIODevice::Text),
			qPrintable(implementation.errorString()));

		const auto source = implementation.readAll();
		QCOMPARE(source.count("delete[] m_buffer;"), 2);
		QVERIFY(!source.contains("delete m_buffer;"));
	}
};

QTEST_APPLESS_MAIN(MonoDelayTest)
#include "MonoDelayTest.moc"
