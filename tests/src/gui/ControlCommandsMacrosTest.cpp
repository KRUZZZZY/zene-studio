/*
 * ControlCommandsMacrosTest.cpp - workflow command names, step validation and recording.
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

#include <QJsonArray>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QJsonObject step(const QString& command, const QJsonValue& args)
{
	return {{QStringLiteral("command"), command}, {QStringLiteral("args"), args}};
}

ControlResult invoke(const QString& command, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(command, args);
}

} // namespace

class ControlCommandsMacrosTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::instance()->setCommandObserver({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void paddedNameWorksForRun()
	{
		const QString name = QStringLiteral("macro run whitespace");
		const QJsonArray steps{step(QStringLiteral("control.ping"), QJsonObject{})};
		const ControlResult saved = invoke(QStringLiteral("workflow.save"),
			{{QStringLiteral("name"), QStringLiteral("  macro run whitespace  ")}, {QStringLiteral("steps"), steps}});
		const ControlResult ran = invoke(QStringLiteral("workflow.run"),
			{{QStringLiteral("name"), QStringLiteral("  macro run whitespace  ")}});

		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QCOMPARE(saved.result.value(QStringLiteral("name")).toString(), name);
		QVERIFY2(ran.ok, qPrintable(ran.errorMessage));
		QCOMPARE(ran.result.value(QStringLiteral("name")).toString(), name);
	}

	void paddedNameWorksForRemove()
	{
		const QString name = QStringLiteral("macro remove whitespace");
		const QJsonArray steps{step(QStringLiteral("control.ping"), QJsonObject{})};
		const ControlResult saved = invoke(QStringLiteral("workflow.save"),
			{{QStringLiteral("name"), QStringLiteral("  macro remove whitespace  ")}, {QStringLiteral("steps"), steps}});
		const ControlResult removed = invoke(QStringLiteral("workflow.remove"),
			{{QStringLiteral("name"), QStringLiteral("  macro remove whitespace  ")}});

		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QCOMPARE(saved.result.value(QStringLiteral("name")).toString(), name);
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(removed.result.value(QStringLiteral("name")).toString(), name);
		QCOMPARE(invoke(QStringLiteral("workflow.remove"), {{QStringLiteral("name"), name}}).errorKind,
			ControlErrorKind::NotFound);
	}

	void stepShapeValidation_data()
	{
		QTest::addColumn<QJsonArray>("steps");
		QTest::addColumn<bool>("accepted");

		QTest::addRow("valid step") << QJsonArray{step(QStringLiteral("transport.get_state"), QJsonObject{})} << true;
		QTest::addRow("missing args") << QJsonArray{QJsonObject{{QStringLiteral("command"), QStringLiteral("transport.get_state")}}} << false;
		QTest::addRow("nonobject args") << QJsonArray{step(QStringLiteral("transport.get_state"), 42)} << false;
		QTest::addRow("args fail command schema") << QJsonArray{step(QStringLiteral("transport.seek"),
			QJsonObject{{QStringLiteral("ticks"), QStringLiteral("start")}})} << false;
		QTest::addRow("missing command") << QJsonArray{QJsonObject{{QStringLiteral("args"), QJsonObject{}}}} << false;
		QTest::addRow("nonstring command") << QJsonArray{QJsonObject{{QStringLiteral("command"), 17},
			{QStringLiteral("args"), QJsonObject{}}}} << false;
		QTest::addRow("nonobject step") << QJsonArray{QStringLiteral("transport.get_state")} << false;
		QJsonArray full;
		for (int i = 0; i < 64; ++i) { full.append(step(QStringLiteral("transport.get_state"), QJsonObject{})); }
		QTest::addRow("64 step limit") << full << true;
		full.append(step(QStringLiteral("transport.get_state"), QJsonObject{}));
		QTest::addRow("over 64 step limit") << full << false;
	}

	void stepShapeValidation()
	{
		QFETCH(QJsonArray, steps);
		QFETCH(bool, accepted);
		const QString name = QStringLiteral("shape-") + QString::fromLatin1(QTest::currentDataTag());
		const ControlResult saved = invoke(QStringLiteral("workflow.save"),
			{{QStringLiteral("name"), name}, {QStringLiteral("steps"), steps}});
		if (saved.ok) { invoke(QStringLiteral("workflow.remove"), {{QStringLiteral("name"), name}}); }

		QCOMPARE(saved.ok, accepted);
		if (!accepted) { QCOMPARE(saved.errorKind, ControlErrorKind::InvalidArgs); }
	}

	void recordingIgnoresReadOnlyCommands()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlCommand* readCommand = registry->command(QStringLiteral("transport.get_state"));
		QVERIFY(readCommand != nullptr);
		QVERIFY(!readCommand->mutating);

		const QString name = QStringLiteral("record-read-only");
		QVERIFY2(invoke(QStringLiteral("workflow.record_start"), {{QStringLiteral("name"), name}}).ok,
			"could not start workflow recording");
		QCOMPARE(invoke(QStringLiteral("workflow.status")).result.value(QStringLiteral("recording")).toObject()
			.value(QStringLiteral("steps")).toInt(), 0);
		QVERIFY2(invoke(QStringLiteral("transport.get_state")).ok, "the read-only transport query failed");
		QCOMPARE(invoke(QStringLiteral("workflow.status")).result.value(QStringLiteral("recording")).toObject()
			.value(QStringLiteral("steps")).toInt(), 0);

		const int tick = invoke(QStringLiteral("transport.get_state")).result.value(QStringLiteral("position_ticks")).toInt();
		const int targetTick = tick < 0x7fffffff ? tick + 1 : tick - 1;
		const ControlResult changed = invoke(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), targetTick}});
		QVERIFY2(changed.ok, qPrintable(changed.errorMessage));
		QCOMPARE(invoke(QStringLiteral("workflow.status")).result.value(QStringLiteral("recording")).toObject()
			.value(QStringLiteral("steps")).toInt(), 1);
		QVERIFY2(invoke(QStringLiteral("control.undo")).ok, "could not restore the playhead after the recording probe");

		const ControlResult stopped = invoke(QStringLiteral("workflow.record_stop"));
		QVERIFY2(stopped.ok, qPrintable(stopped.errorMessage));
		QCOMPARE(stopped.result.value(QStringLiteral("steps")).toInt(), 1);
		QVERIFY2(invoke(QStringLiteral("workflow.remove"), {{QStringLiteral("name"), name}}).ok,
			"could not remove the recording probe workflow");
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(ControlCommandsMacrosTest)
#include "ControlCommandsMacrosTest.moc"
