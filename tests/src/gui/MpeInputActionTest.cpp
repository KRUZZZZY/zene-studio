/*
 * MpeInputActionTest.cpp - Edit > MPE Input throws the switch and shows its real state
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

/*! The MPE Input item: toggling it runs device.mpe_set (device.mpe_get_state follows), and when
 *  the switch is thrown over the socket instead, the item shows the real state the next time its
 *  menu opens. */

#include <QtTest>

#include <QAction>
#include <QJsonArray>
#include <QMenu>
#include <QStringList>

#include <stdexcept>

#include "ControlRegistry.h"
#include "Engine.h"
#include "MpeInputAction.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

bool mpeEnabled()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("device.mpe_get_state"), QJsonObject{})
		.result.value(QStringLiteral("enabled")).toBool();
}

} // namespace

class MpeInputActionTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::instance()->invoke(QStringLiteral("device.mpe_set"), {{QStringLiteral("enabled"), false}});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theItemThrowsTheSwitchAndFollowsIt()
	{
		QMenu menu;
		QAction* mpe = addMpeInputAction(&menu);
		QVERIFY(mpe->isEnabled());
		QVERIFY(!mpe->isChecked());
		QVERIFY(!mpeEnabled());

		mpe->trigger();
		QVERIFY(mpeEnabled());
		QVERIFY(mpe->isChecked());

		// Thrown over the socket: the menu shows it the next time it opens.
		ControlRegistry::instance()->invoke(QStringLiteral("device.mpe_set"), {{QStringLiteral("enabled"), false}});
		QVERIFY(mpe->isChecked());
		emit menu.aboutToShow();
		QVERIFY(!mpe->isChecked());
	}

	//! A throwing handler must not leave the registry's running-handler counts raised:
	//! otherwise the next top-level edit is treated as nested, its observer notification
	//! is suppressed and its record is held forever. (cluster new-p10-83-port)
	void throwingHandlerRestoresDispatchDepth()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ControlCommand throwing;
		throwing.id = QStringLiteral("test.mpe_throw_depth");
		throwing.group = QStringLiteral("test");
		throwing.verb = QStringLiteral("mpe_throw_depth");
		throwing.handler = [](const QJsonObject&) -> ControlResult {
			throw std::runtime_error("expected handler failure");
		};
		registry->registerCommand(throwing);

		ControlCommand later;
		later.id = QStringLiteral("test.mpe_throw_depth_after");
		later.group = QStringLiteral("test");
		later.verb = QStringLiteral("mpe_throw_depth_after");
		later.mutating = true;
		later.handler = [](const QJsonObject&) -> ControlResult {
			return ControlResult::success(QJsonObject{});
		};
		registry->registerCommand(later);

		QStringList observed;
		registry->setCommandObserver([&observed](const QString& command, const QJsonObject&) {
			observed.append(command);
		});
		registry->clearTransactions();

		bool threw = false;
		try
		{
			registry->invoke(throwing.id);
		}
		catch (const std::runtime_error&)
		{
			threw = true;
		}
		const ControlResult afterThrow = registry->invoke(later.id);
		registry->setCommandObserver({});

		QVERIFY(threw);
		QVERIFY(afterThrow.ok);
		QCOMPARE(observed, QStringList{later.id});
		const QJsonArray transactions = registry->transactions();
		QCOMPARE(transactions.size(), 1);
	}
};

QTEST_MAIN(MpeInputActionTest)
#include "MpeInputActionTest.moc"
