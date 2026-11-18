/*
 * ControlEventHubTest.cpp - the bound, the ordering and the sources of project-change events (Q050-334.1).
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

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "ControlEventHub.h"
#include "ControlRegistry.h"
#include "ControlServer.h"
#include "Engine.h"
#include "ProjectJournal.h"

using namespace lmms;
using control::ControlEventHub;
using control::ControlEventQueue;

class ControlEventHubTest : public QObject
{
	Q_OBJECT

	//! One fake connection, subscribed for the test's length: lines are pulled the way a transport pulls.
	struct Subscribed
	{
		ControlEventHub::Connection connection;
		int wakes = 0;
		QString id;

		Subscribed(ControlEventHubTest* owner, int limit = ControlEventQueue::DefaultEventLimit)
		{
			connection.key = ControlEventHub::nextConnectionKey();
			connection.owner = owner;
			connection.wake = [this]() { ++wakes; };
			const ControlEventHub::ConnectionScope scope(connection);
			const ControlEventHub::SubscribeResult result = ControlEventHub::instance().subscribe(limit);
			if (result.ok) { id = result.result.value(QStringLiteral("subscription")).toString(); }
		}
		~Subscribed() { ControlEventHub::instance().connectionClosed(connection.key); }

		QVector<QJsonObject> lines() const
		{
			QVector<QJsonObject> out;
			QByteArray line;
			while (ControlEventHub::instance().nextLine(connection.key, &line))
			{
				out.append(QJsonDocument::fromJson(line).object());
			}
			return out;
		}
	};

	static ControlCommand testCommand(const QString& id, bool mutating, std::function<ControlResult()> body)
	{
		ControlCommand command;
		command.id = id;
		command.group = QStringLiteral("test");
		command.verb = id.mid(QStringLiteral("test.").size());
		command.mutating = mutating;
		command.handler = [body](const QJsonObject&) { return body(); };
		return command;
	}

	//! A real change: one journal step, described the way a mutating handler describes it.
	static ControlResult journalledEdit()
	{
		Engine::projectJournal()->addJournalAction([] {}, [] {});
		const QJsonObject transaction{{QStringLiteral("before"), QJsonObject{}},
			{QStringLiteral("inverse"), QJsonObject{{QStringLiteral("op"), QStringLiteral("control.undo")}}},
			{QStringLiteral("reversible"), true}, {QStringLiteral("mechanism"), QStringLiteral("test")}};
		return ControlResult::success(QJsonObject{{QStringLiteral("__transaction"), transaction}});
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		ControlEventHub::instance();
		ControlRegistry* registry = ControlRegistry::instance();
		registry->registerCommand(testCommand(QStringLiteral("test.edit"), true, journalledEdit));
		registry->registerCommand(testCommand(QStringLiteral("test.failing_edit"), true, [] {
			return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no"));
		}));
		registry->registerCommand(testCommand(QStringLiteral("test.read"), false, [] { return ControlResult::success(); }));
		registry->registerCommand(testCommand(QStringLiteral("test.outer_edit"), true, [registry] {
			registry->invoke(QStringLiteral("test.edit"));
			return journalledEdit();
		}));
		registry->registerCommand(testCommand(QStringLiteral("test.wrapper"), false, [registry] {
			registry->invoke(QStringLiteral("test.edit"));
			return registry->invoke(QStringLiteral("test.edit"));
		}));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aQueueHoldsItsCountBoundThenOnlyCounts()
	{
		ControlEventQueue queue(3);
		for (quint64 seq = 1; seq <= 3; ++seq) { QVERIFY(queue.push(seq, QByteArray::number(seq))); }
		QVERIFY(!queue.overflowed());
		QVERIFY(!queue.push(4, "4"));
		QVERIFY(!queue.push(5, "5"));
		QCOMPARE(queue.size(), 3);
		QVERIFY(queue.overflowed());
		QCOMPARE(queue.firstMissedSeq(), quint64{4});
		QCOMPARE(queue.missedCount(), quint64{2});
		QByteArray line;
		quint64 seq = 0;
		QVERIFY(queue.take(&line, &seq));
		QCOMPARE(line, QByteArray("1"));
		// Room again, yet the overflow stands: a later event after a gap would claim a continuity that is gone.
		QVERIFY(!queue.push(6, "6"));
		QCOMPARE(queue.missedCount(), quint64{3});
		QVERIFY(queue.take(&line, &seq));
		QCOMPARE(seq, quint64{2});
		QVERIFY(queue.take(&line, &seq));
		QCOMPARE(seq, quint64{3});
		QVERIFY(!queue.take(&line, &seq));
		QCOMPARE(queue.bytes(), 0);
	}

	void aQueueHoldsItsByteBound()
	{
		ControlEventQueue queue(ControlEventQueue::MaxEventLimit);
		const QByteArray line(ControlEventHub::MaxEventLineBytes, 'x');
		quint64 seq = 0;
		while (queue.push(++seq, line)) {}
		QVERIFY(queue.bytes() <= ControlEventQueue::MaxQueuedBytes);
		QVERIFY(queue.bytes() + line.size() > ControlEventQueue::MaxQueuedBytes);
		QCOMPARE(queue.firstMissedSeq(), seq);
	}

	void anOversizeEventGoesOutWithoutItsArgs()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ControlCommand big = testCommand(QStringLiteral("test.big_edit"), true, journalledEdit);
		big.argsSchema = QJsonObject();
		registry->registerCommand(big);
		Subscribed client(this);
		const QJsonObject args{{QStringLiteral("blob"), QString(ControlEventHub::MaxEventLineBytes, QLatin1Char('x'))}};
		QVERIFY(registry->invoke(QStringLiteral("test.big_edit"), args).ok);
		const QVector<QJsonObject> lines = client.lines();
		QCOMPARE(lines.size(), 1);
		QVERIFY(lines[0].value(QStringLiteral("args_omitted")).toBool());
		QVERIFY(!lines[0].contains(QStringLiteral("args")));
		QCOMPARE(lines[0].value(QStringLiteral("command")).toString(), QStringLiteral("test.big_edit"));
	}

	void onlySuccessfulChangesAreEvents()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		Subscribed client(this);
		const quint64 start = ControlEventHub::instance().sequence();
		QVERIFY(registry->invoke(QStringLiteral("test.edit")).ok);
		QVERIFY(!registry->invoke(QStringLiteral("test.failing_edit")).ok);
		QVERIFY(registry->invoke(QStringLiteral("test.read")).ok);
		QVERIFY(registry->invoke(QStringLiteral("test.edit")).ok);
		const QVector<QJsonObject> lines = client.lines();
		QCOMPARE(lines.size(), 2);
		QCOMPARE(lines[0].value(QStringLiteral("seq")).toVariant().toLongLong(), static_cast<qint64>(start + 1));
		QCOMPARE(lines[1].value(QStringLiteral("seq")).toVariant().toLongLong(), static_cast<qint64>(start + 2));
		QCOMPARE(lines[0].value(QStringLiteral("source")).toString(), QStringLiteral("command"));
		QCOMPARE(client.wakes, 1 + 0);  // one wake until the transport pulled: edits do not post a wake each
	}

	//! A command's own nested commands are that one change; a wrapper's are one change each, or nothing reports them.
	void nestingDecidesWhatOneChangeIs()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		Subscribed client(this);
		QVERIFY(registry->invoke(QStringLiteral("test.outer_edit")).ok);
		QVector<QJsonObject> lines = client.lines();
		QCOMPARE(lines.size(), 1);
		QCOMPARE(lines[0].value(QStringLiteral("command")).toString(), QStringLiteral("test.outer_edit"));
		QVERIFY(registry->invoke(QStringLiteral("test.wrapper")).ok);
		lines = client.lines();
		QCOMPARE(lines.size(), 2);
		QCOMPARE(lines[0].value(QStringLiteral("command")).toString(), QStringLiteral("test.edit"));
	}

	//! The macro recorder's one observer slot stays the macro recorder's.
	void theCommandObserverSlotIsLeftAlone()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QStringList observed;
		registry->setCommandObserver([&observed](const QString& id, const QJsonObject&) { observed.append(id); });
		Subscribed client(this);
		QVERIFY(registry->invoke(QStringLiteral("test.edit")).ok);
		registry->setCommandObserver(nullptr);
		QCOMPARE(observed, QStringList{QStringLiteral("test.edit")});
		QCOMPARE(client.lines().size(), 1);
	}

	//! An edit the interface makes directly never passes the registry: the journal step is what reports it.
	void aJournalStepOutsideACommandIsAnEvent()
	{
		ProjectJournal* journal = Engine::projectJournal();
		Subscribed client(this);
		journal->addJournalAction([] {}, [] {});
		QVector<QJsonObject> lines = client.lines();
		QCOMPARE(lines.size(), 1);
		QCOMPARE(lines[0].value(QStringLiteral("source")).toString(), QStringLiteral("journal"));
		QCOMPARE(lines[0].value(QStringLiteral("kind")).toString(), QStringLiteral("edit"));
		QVERIFY(!lines[0].contains(QStringLiteral("command")));
		journal->undo();
		journal->redo();
		lines = client.lines();
		QCOMPARE(lines.size(), 2);
		QCOMPARE(lines[0].value(QStringLiteral("kind")).toString(), QStringLiteral("undo"));
		QCOMPARE(lines[1].value(QStringLiteral("kind")).toString(), QStringLiteral("redo"));
		journal->clearJournal();
		lines = client.lines();
		QCOMPARE(lines.size(), 1);
		QCOMPARE(lines[0].value(QStringLiteral("kind")).toString(), QStringLiteral("project_replaced"));
	}

	void anEditInsideACommandIsReportedOnceByItsName()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		Subscribed client(this);
		QVERIFY(registry->invoke(QStringLiteral("test.edit")).ok);
		QCOMPARE(client.lines().size(), 1);
		// control.undo is not a mutating command: the journal moving under it is what makes it an event.
		QVERIFY(registry->invoke(QStringLiteral("control.undo")).ok);
		const QVector<QJsonObject> lines = client.lines();
		QCOMPARE(lines.size(), 1);
		QCOMPARE(lines[0].value(QStringLiteral("command")).toString(), QStringLiteral("control.undo"));
		QCOMPARE(lines[0].value(QStringLiteral("kind")).toString(), QStringLiteral("undo"));
	}

	void anOverflowEndsTheSubscriptionAndNamesWhatWasMissed()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ControlEventHub& hub = ControlEventHub::instance();
		Subscribed client(this, 2);
		const quint64 start = hub.sequence();
		for (int edit = 0; edit < 5; ++edit) { QVERIFY(registry->invoke(QStringLiteral("test.edit")).ok); }
		QVERIFY(hub.isSubscribed(client.connection.key));
		const QVector<QJsonObject> lines = client.lines();
		QCOMPARE(lines.size(), 3);
		QCOMPARE(lines[1].value(QStringLiteral("seq")).toVariant().toLongLong(), static_cast<qint64>(start + 2));
		const QJsonObject notice = lines[2];
		QCOMPARE(notice.value(QStringLiteral("event")).toString(), QStringLiteral("events_overflow"));
		QCOMPARE(notice.value(QStringLiteral("subscription")).toString(), client.id);
		QCOMPARE(notice.value(QStringLiteral("last_delivered_seq")).toVariant().toLongLong(), static_cast<qint64>(start + 2));
		QCOMPARE(notice.value(QStringLiteral("first_missed_seq")).toVariant().toLongLong(), static_cast<qint64>(start + 3));
		QCOMPARE(notice.value(QStringLiteral("missed")).toVariant().toLongLong(), qint64{3});
		QVERIFY(!hub.isSubscribed(client.connection.key));
		QVERIFY(client.lines().isEmpty());
	}

	//! A client away from the stream learns it from seq, which counts with nobody listening.
	void theSequenceCountsWithNoSubscribers()
	{
		ControlEventHub& hub = ControlEventHub::instance();
		const quint64 start = hub.sequence();
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("test.edit")).ok);
		QCOMPARE(hub.sequence(), start + 1);
	}

	void subscribingNeedsAConnectionAndOnlyOneSubscriptionPerConnection()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult none = registry->invoke(QStringLiteral("events.subscribe"));
		QVERIFY(!none.ok);
		QCOMPARE(none.errorKind, ControlErrorKind::Refused);
		Subscribed client(this);
		const ControlEventHub::ConnectionScope scope(client.connection);
		const ControlResult second = registry->invoke(QStringLiteral("events.subscribe"));
		QVERIFY(!second.ok);
		QCOMPARE(second.errorKind, ControlErrorKind::Busy);
		const ControlResult gone = registry->invoke(QStringLiteral("events.unsubscribe"),
			QJsonObject{{QStringLiteral("subscription"), QStringLiteral("sub-unknown")}});
		QCOMPARE(gone.errorKind, ControlErrorKind::NotFound);
		const ControlResult done = registry->invoke(QStringLiteral("events.unsubscribe"),
			QJsonObject{{QStringLiteral("subscription"), client.id}});
		QVERIFY(done.ok);
		QVERIFY(!ControlEventHub::instance().isSubscribed(client.connection.key));
	}

	//! The commands stay out of the undo story: subscribing records nothing and moves no journal step.
	void subscribingIsNotAnEdit()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const int records = registry->transactions().size();
		const quint64 step = Engine::projectJournal()->topStepSerial();
		const quint64 seq = ControlEventHub::instance().sequence();
		{
			ControlEventHub::Connection connection;
			connection.key = ControlEventHub::nextConnectionKey();
			connection.owner = this;
			connection.wake = [] {};
			const ControlEventHub::ConnectionScope scope(connection);
			const ControlResult subscribed = registry->invoke(QStringLiteral("events.subscribe"));
			QVERIFY(subscribed.ok);
			QVERIFY(registry->invoke(QStringLiteral("events.get_state")).ok);
			QVERIFY(registry->invoke(QStringLiteral("events.unsubscribe"), QJsonObject{{QStringLiteral("subscription"),
				subscribed.result.value(QStringLiteral("subscription"))}}).ok);
		}
		QCOMPARE(registry->transactions().size(), records);
		QCOMPARE(Engine::projectJournal()->topStepSerial(), step);
		QCOMPARE(ControlEventHub::instance().sequence(), seq);
	}

	void aClosingServerForgetsItsSubscribers()
	{
		ControlEventHub& hub = ControlEventHub::instance();
		Subscribed client(this);
		QVERIFY(hub.isSubscribed(client.connection.key));
		hub.ownerClosed(this);
		QVERIFY(!hub.isSubscribed(client.connection.key));
	}
};

QTEST_GUILESS_MAIN(ControlEventHubTest)
#include "ControlEventHubTest.moc"
