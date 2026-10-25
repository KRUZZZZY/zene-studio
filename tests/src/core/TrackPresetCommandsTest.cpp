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
};

QTEST_MAIN(TrackPresetCommandsTest)
#include "TrackPresetCommandsTest.moc"
