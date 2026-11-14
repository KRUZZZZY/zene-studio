/*
 * CustomAccessTest.cpp - the 0.5.0 W3 L13 / L14 project and engine commands
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

// Complete fixture method definitions; included once after the fixture declaration.

void CustomAccessTest::aStoppedWorkflowSaysWhereAndResumes() {
	const auto step = [](const char* command, const QJsonObject& args) {
		return QJsonObject{{QStringLiteral("command"), QLatin1String(command)}, {QStringLiteral("args"), args}};
	};
	const QJsonArray steps{step("track.list", {}), step("track.remove", {{QStringLiteral("track"), QStringLiteral("no-such-track")}}),
		step("track.list", {})};
	QVERIFY(run(QStringLiteral("workflow.save"), {{QStringLiteral("name"), QStringLiteral("stops")}, {QStringLiteral("steps"), steps}}).ok);
	const auto done = [] { return run(QStringLiteral("workflow.status")).result.value(QStringLiteral("state")).toString() == QStringLiteral("done"); };
	QVERIFY(run(QStringLiteral("workflow.run"), {{QStringLiteral("name"), QStringLiteral("stops")}}).ok);
	QTRY_VERIFY(done());
	QJsonObject status = run(QStringLiteral("workflow.status")).result;
	QCOMPARE(status.value(QStringLiteral("stopped_at")).toInt(), 1);
	QCOMPARE(status.value(QStringLiteral("failed")).toObject().value(QStringLiteral("step")).toInt(), 2);
	QVERIFY(!status.value(QStringLiteral("failed")).toObject().value(QStringLiteral("error")).toString().isEmpty());
	QCOMPARE(status.value(QStringLiteral("applied_steps")).toArray(), QJsonArray{1});
	QCOMPARE(status.value(QStringLiteral("not_run")).toArray(), QJsonArray{3});
	QVERIFY(status.value(QStringLiteral("message")).toString().contains(QStringLiteral("stopped at step 2 of 3")));

	QCOMPARE(run(QStringLiteral("workflow.resume")).result.value(QStringLiteral("retried")).toInt(), 2);
	QTRY_VERIFY(done());
	QCOMPARE(run(QStringLiteral("workflow.status")).result.value(QStringLiteral("stopped_at")).toInt(), 1);  // it fails again
	QCOMPARE(run(QStringLiteral("workflow.resume"), {{QStringLiteral("skip"), true}}).result.value(QStringLiteral("skipped")).toInt(), 2);
	QTRY_VERIFY(done());
	status = run(QStringLiteral("workflow.status")).result;
	QCOMPARE(status.value(QStringLiteral("stopped_at")).toInt(), -1);
	QCOMPARE(status.value(QStringLiteral("applied_steps")).toArray(), (QJsonArray{1, 3}));
	QCOMPARE(status.value(QStringLiteral("skipped_steps")).toArray(), QJsonArray{2});
	QCOMPARE(run(QStringLiteral("workflow.resume")).errorKind, ControlErrorKind::Refused);
	QVERIFY(run(QStringLiteral("workflow.remove"), {{QStringLiteral("name"), QStringLiteral("stops")}}).ok);
}
void CustomAccessTest::retryingADeletedFinalWorkflowStepIsRefused() {
	const auto step = [](const char* command, const QJsonObject& args) {
		return QJsonObject{{QStringLiteral("command"), QLatin1String(command)}, {QStringLiteral("args"), args}};
	};
	const QJsonArray steps{step("track.list", {}), step("track.list", {}),
		step("track.remove", {{QStringLiteral("track"), QStringLiteral("no-such-track")}})};
	const QString name = QStringLiteral("shrinking-final-step");
	QVERIFY2(run(QStringLiteral("workflow.save"), {{QStringLiteral("name"), name}, {QStringLiteral("steps"), steps}}).ok,
		"could not save the workflow under test");
	const auto done = [] { return run(QStringLiteral("workflow.status")).result.value(QStringLiteral("state")).toString() == QStringLiteral("done"); };
	QVERIFY(run(QStringLiteral("workflow.run"), {{QStringLiteral("name"), name}}).ok);
	QTRY_VERIFY(done());
	QCOMPARE(run(QStringLiteral("workflow.status")).result.value(QStringLiteral("stopped_at")).toInt(), 2);

	const QJsonArray shortened{steps.at(0), steps.at(1)};
	QVERIFY(run(QStringLiteral("workflow.save"), {{QStringLiteral("name"), name}, {QStringLiteral("steps"), shortened}}).ok);
	const ControlResult retried = run(QStringLiteral("workflow.resume"));
	QCOMPARE(retried.errorKind, ControlErrorKind::InvalidArgs);
	QCOMPARE(run(QStringLiteral("workflow.status")).result.value(QStringLiteral("stopped_at")).toInt(), 2);

	// A failed step that is still the saved workflow's final step can still be skipped to the end.
	QVERIFY(run(QStringLiteral("workflow.save"), {{QStringLiteral("name"), name}, {QStringLiteral("steps"), steps}}).ok);
	QVERIFY(run(QStringLiteral("workflow.run"), {{QStringLiteral("name"), name}}).ok);
	QTRY_VERIFY(done());
	const ControlResult skipped = run(QStringLiteral("workflow.resume"), {{QStringLiteral("skip"), true}});
	QVERIFY2(skipped.ok, qPrintable(skipped.errorMessage));
	QCOMPARE(skipped.result.value(QStringLiteral("skipped")).toInt(), 3);
	QTRY_VERIFY(done());
	const QJsonObject status = run(QStringLiteral("workflow.status")).result;
	QCOMPARE(status.value(QStringLiteral("stopped_at")).toInt(), -1);
	QCOMPARE(status.value(QStringLiteral("skipped_steps")).toArray(), QJsonArray{3});
	QVERIFY(run(QStringLiteral("workflow.remove"), {{QStringLiteral("name"), name}}).ok);
}
