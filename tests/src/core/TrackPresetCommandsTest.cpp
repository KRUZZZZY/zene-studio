/*
 * TrackPresetCommandsTest.cpp - focused regressions for track preset commands
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <QJsonArray>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

class TrackPresetCommandsTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;

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

	void saveRefusalExplainsTheAbsolutePathRequirement()
	{
		Track* sample = Track::create(Track::Type::Sample, Engine::getSong());
		const ControlResult refused = run(QStringLiteral("track.preset_save"),
			{{QStringLiteral("track"), control::trackIdOf(sample)}, {QStringLiteral("path"), QStringLiteral("relative.xpf")}});
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(refused.errorMessage, QStringLiteral("'path' must be an absolute file path"));
	}

	void loadReturnsItsTrackCheckpointTransaction()
	{
		Track* source = Track::create(Track::Type::Sample, Engine::getSong());
		Track* target = Track::create(Track::Type::Sample, Engine::getSong());
		target->setMuted(true);
		const QString targetId = control::trackIdOf(target);
		const QString path = m_dir.filePath(QStringLiteral("checkpoint.xpf"));
		QVERIFY(run(QStringLiteral("track.preset_save"), {{QStringLiteral("track"), control::trackIdOf(source)},
			{QStringLiteral("path"), path}}).ok);

		const ControlResult loaded = run(QStringLiteral("track.preset_load"),
			{{QStringLiteral("track"), targetId}, {QStringLiteral("path"), path}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		QVERIFY(!loaded.result.contains(QStringLiteral("__transaction")));  // the registry keeps this descriptor private
		const QJsonArray records = run(QStringLiteral("control.transactions")).result
			.value(QStringLiteral("transactions")).toArray();
		QVERIFY(!records.isEmpty());
		const QJsonObject transaction = records.last().toObject();
		QCOMPARE(transaction.value(QStringLiteral("command")).toString(), QStringLiteral("track.preset_load"));
		QCOMPARE(transaction.value(QStringLiteral("before")).toObject().value(QStringLiteral("track")).toString(), targetId);
		const QJsonObject inverse = transaction.value(QStringLiteral("inverse")).toObject();
		QCOMPARE(inverse.value(QStringLiteral("op")).toString(), QStringLiteral("control.undo"));
		QCOMPARE(inverse.value(QStringLiteral("args")).toObject(), QJsonObject());
		QCOMPARE(transaction.value(QStringLiteral("reversible")).toBool(), true);
		QVERIFY(transaction.value(QStringLiteral("mechanism")).toString().contains(QStringLiteral("Track checkpoint")));
		QCOMPARE(target->isMuted(), false);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(target->isMuted(), true);  // the one track checkpoint restores the previous track state
	}
};

QTEST_MAIN(TrackPresetCommandsTest)
#include "TrackPresetCommandsTest.moc"
