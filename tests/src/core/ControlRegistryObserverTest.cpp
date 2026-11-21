/*
 * ControlRegistryObserverTest.cpp - the command observer is a best-effort recorder: its exceptions
 *                                  must not change a committed control reply (ED-056).
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

#include <memory>
#include <stdexcept>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectJournal.h"

using namespace lmms;

class ControlRegistryObserverTest : public QObject
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

	void cleanup()
	{
		ControlRegistry::setReady(true);
	}

	//! A mutating test command whose handler describes \a op as its inverse; with \a flag it also
	//! pushes one journal action that sets *flag false on undo and true on redo.
	static ControlCommand describedCommand(const QString& id, bool mutating, const QString& op, bool* flag = nullptr)
	{
		ControlCommand command;
		command.id = id;
		command.group = QStringLiteral("test");
		command.verb = id.mid(QStringLiteral("test.").size());
		command.mutating = mutating;
		command.handler = [op, flag](const QJsonObject&) {
			if (flag != nullptr)
			{
				Engine::projectJournal()->addJournalAction([flag] { *flag = false; }, [flag] { *flag = true; });
			}
			QJsonObject transaction{{QStringLiteral("before"), QJsonObject{{QStringLiteral("value"), 1}}},
				{QStringLiteral("inverse"), QJsonObject{{QStringLiteral("op"), op}}},
				{QStringLiteral("reversible"), true}, {QStringLiteral("mechanism"), QStringLiteral("test")}};
			return ControlResult::success(QJsonObject{{QStringLiteral("__transaction"), transaction}});
		};
		return command;
	}

	//! An observer that throws after it has heard a successful top-level edit must not change the
	//! reply to that edit: the edit is committed and its one record is kept. The record is written
	//! before the notification, so a "refused" reply would invite a retry of an edit that already happened.
	void observerExceptionDoesNotChangeACommittedReply()
	{
		// Clears the observer on scope exit, so a failed QVERIFY cannot leak the throwing observer into later slots.
		struct ClearObserverOnExit
		{
			~ClearObserverOnExit() { ControlRegistry::instance()->setCommandObserver({}); }
		};
		ControlRegistry* registry = ControlRegistry::instance();
		registry->clearTransactions();
		auto edited = std::make_shared<bool>(false);
		ControlCommand command = describedCommand(QStringLiteral("test.observer_throw"), true,
			QStringLiteral("test.observer_throw"));
		const auto base = command.handler;
		command.handler = [edited, base](const QJsonObject& args) {
			*edited = true;
			return base(args);
		};
		registry->registerCommand(command);

		bool heard = false;
		registry->setCommandObserver([&heard](const QString&, const QJsonObject&) {
			heard = true;
			throw std::runtime_error("expected observer failure");
		});
		ClearObserverOnExit clearObserver;
		ControlResult answer;
		bool escaped = false;
		try
		{
			answer = registry->invoke(command.id);
		}
		catch (const std::exception&)
		{
			escaped = true;
		}

		QVERIFY2(!escaped, "an observer's exception must not escape the control surface");
		QVERIFY(heard);
		QVERIFY2(answer.ok, "a committed edit must still be answered ok");
		QVERIFY(*edited);
		const QJsonArray transactions = registry->transactions();
		QCOMPARE(transactions.size(), 1);
		QCOMPARE(transactions.last().toObject().value(QStringLiteral("command")).toString(), command.id);

		// Reconfiguration: a replaced observer hears the next edit, and the registry still works.
		QStringList observed;
		registry->setCommandObserver([&observed](const QString& id, const QJsonObject&) {
			observed.append(id);
		});
		const ControlCommand next = describedCommand(QStringLiteral("test.observer_throw_next"), true,
			QStringLiteral("test.observer_throw_next"));
		registry->registerCommand(next);
		const ControlResult nextAnswer = registry->invoke(next.id);
		registry->setCommandObserver({});

		QVERIFY(nextAnswer.ok);
		QCOMPARE(observed, QStringList{next.id});
	}

	//! The async sibling: a completed job's edit is recorded by recordCompletedEdit, which then notifies
	//! the observer. A throwing observer must not escape from there, and the record already written stays.
	void observerExceptionDoesNotChangeACompletedAsyncEdit()
	{
		// Clears the observer on scope exit, so a failed QVERIFY cannot leak the throwing observer into later slots.
		struct ClearObserverOnExit
		{
			~ClearObserverOnExit() { ControlRegistry::instance()->setCommandObserver({}); }
		};
		ControlRegistry* registry = ControlRegistry::instance();
		registry->clearTransactions();
		bool heard = false;
		registry->setCommandObserver([&heard](const QString&, const QJsonObject&) {
			heard = true;
			throw std::runtime_error("expected observer failure");
		});
		ClearObserverOnExit clearObserver;
		const QJsonObject args{{QStringLiteral("track"), 1}};
		const QJsonObject before{{QStringLiteral("value"), 1}};
		bool escaped = false;
		try
		{
			registry->recordCompletedEdit(QStringLiteral("freeze.tracks"), args, before, QJsonObject{}, 1);
		}
		catch (const std::exception&)
		{
			escaped = true;
		}

		QVERIFY2(!escaped, "an observer's exception must not escape from a completed async edit");
		QVERIFY(heard);
		const QJsonArray transactions = registry->transactions();
		QCOMPARE(transactions.size(), 1);
		QCOMPARE(transactions.last().toObject().value(QStringLiteral("command")).toString(),
			QStringLiteral("freeze.tracks"));
	}
};

QTEST_GUILESS_MAIN(ControlRegistryObserverTest)
#include "ControlRegistryObserverTest.moc"
