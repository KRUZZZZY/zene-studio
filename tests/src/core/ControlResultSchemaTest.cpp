/*
 * ControlResultSchemaTest.cpp - replies are held to the result schema they declare
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

//! Two things the command table promised and nothing checked:
//!
//!  * a reply satisfies its own resultSchema (include/ControlResultCheck.h) -
//!    here the checker itself, with a positive and a negative case per rule, and
//!    finishResult's two duties (drop `__transaction`; refuse a violation only
//!    when checks are on). The socket drivers run every command they drive with
//!    ZENE_CONTROL_CHECK_RESULTS=1, which is the sweep over real replies;
//!  * every command's id IS "<group>.<verb>" - the three fields are written out
//!    by hand per command, so they can disagree, and the wire publishes all
//!    three.

#include <QtTest>

#include <QJsonArray>

#include "ControlRegistry.h"
#include "ControlResultCheck.h"
#include "Engine.h"

using namespace lmms;

namespace
{

QJsonObject schemaWithCount()
{
	return QJsonObject{
		{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("required"), QJsonArray{QStringLiteral("count")}},
		{QStringLiteral("properties"), QJsonObject{
			{QStringLiteral("count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}};
}

} // namespace

class ControlResultSchemaTest : public QObject
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
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aConformingReplyPasses()
	{
		QVERIFY(control::resultSchemaViolation(schemaWithCount(),
			QJsonObject{{QStringLiteral("count"), 3}, {QStringLiteral("name"), QStringLiteral("x")}}).isEmpty());
		// An empty schema promises nothing, so nothing can break it.
		QVERIFY(control::resultSchemaViolation(QJsonObject(), QJsonObject{{QStringLiteral("x"), 1}}).isEmpty());
	}

	void aMissingRequiredKeyIsNamed()
	{
		const QString why = control::resultSchemaViolation(schemaWithCount(), QJsonObject{});
		QVERIFY2(why.contains(QStringLiteral("count")), qPrintable(why));
	}

	void aWrongTypeIsNamed()
	{
		const QString why = control::resultSchemaViolation(schemaWithCount(),
			QJsonObject{{QStringLiteral("count"), 2.5}});
		QVERIFY2(why.contains(QStringLiteral("result.count")), qPrintable(why));
	}

	void finishResultDropsTheTransactionSideChannel()
	{
		ControlResult result = ControlResult::success(QJsonObject{{QStringLiteral("count"), 1},
			{QStringLiteral("__transaction"), QJsonObject{}}});
		control::finishResult(QStringLiteral("test.cmd"), schemaWithCount(), &result);
		QVERIFY(result.ok);
		QVERIFY(!result.result.contains(QStringLiteral("__transaction")));
	}

	void finishResultRefusesAViolationOnlyWhenChecksAreOn()
	{
		ControlResult result = ControlResult::success(QJsonObject{{QStringLiteral("count"), QStringLiteral("x")}});
		control::finishResult(QStringLiteral("test.cmd"), schemaWithCount(), &result);
		if (control::resultChecksEnabled())
		{
			QVERIFY(!result.ok);
			QCOMPARE(result.errorKind, ControlErrorKind::Refused);
			QVERIFY2(result.errorMessage.contains(QStringLiteral("test.cmd")), qPrintable(result.errorMessage));
		}
		else
		{
			// A shipped instance never changes a reply because of the check.
			QVERIFY(result.ok);
		}
	}

	void everyIdIsItsGroupDotVerb()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList ids = registry->commandIds();
		QVERIFY2(ids.size() >= 300, qPrintable(QStringLiteral("only %1 commands registered").arg(ids.size())));
		QStringList mismatched;
		for (const QString& id : ids)
		{
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			if (cmd->group.isEmpty() || cmd->verb.isEmpty() || cmd->id != cmd->group + QLatin1Char('.') + cmd->verb)
			{
				mismatched << QStringLiteral("%1 (group '%2', verb '%3')").arg(cmd->id, cmd->group, cmd->verb);
			}
		}
		QVERIFY2(mismatched.isEmpty(), qPrintable(mismatched.join(QStringLiteral("; "))));
	}
};

QTEST_GUILESS_MAIN(ControlResultSchemaTest)
#include "ControlResultSchemaTest.moc"
