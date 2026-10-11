/*
 * SerializingObjectHookTest.cpp - hook ownership when a hook is reassigned.
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include "SerializingObject.h"

using namespace lmms;

namespace
{

class TestSerializingObject final : public SerializingObject
{
public:
	QString nodeName() const override
	{
		return QStringLiteral("serializable");
	}

protected:
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
};


class TestHook final : public SerializingObjectHook
{
public:
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
};

}


class SerializingObjectHookTest : public QObject
{
	Q_OBJECT

private slots:
	void reassigningHookDetachesPreviousObject()
	{
		TestSerializingObject first;
		TestSerializingObject second;
		TestHook hook;

		first.setHook(&hook);
		QVERIFY(first.hook() == &hook);

		second.setHook(&hook);
		QVERIFY(second.hook() == &hook);
		QVERIFY(first.hook() == nullptr);
	}
};

QTEST_GUILESS_MAIN(SerializingObjectHookTest)

#include "SerializingObjectHookTest.moc"
