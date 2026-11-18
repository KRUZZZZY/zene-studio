/*
 * RecordToolsNoEngineTest.cpp - the record tool commands on an instance with no audio engine
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
/*! The recorder is a member of the AudioEngine (include/AudioEngine.h), so an instance with no
 *  engine has no recorder at all. Every command here is registered with requiresEngine left at its
 *  true default, and ControlRegistry::invoke() refuses those with a typed, explained failure before
 *  any handler runs (src/core/ControlRegistry.cpp:257) - so none of them can reach the recorder
 *  through the surface. This file is that claim as an executable test: the calls below must fail,
 *  say why, and not take the process down.
 *
 *  The assertion is deliberately NOT pinned to one error kind: the contract a client depends on is
 *  "typed, explained, no crash". Whether the refusal comes from the registry's engine gate or from
 *  a handler's own noEngine() guard, the sentence below is what holds. RecordToolsTest covers
 *  these commands WITH an engine. */

#include <QtTest>

#include "ControlRegistry.h"
#include "Engine.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! @a result must be a typed, explained refusal that names the missing engine - and the process
//! must still be here to say so, which is the whole point of the case.
void refusesTypedAndAlive(const ControlResult& result, const QString& id)
{
	QVERIFY2(!result.ok, qPrintable(QStringLiteral("%1 answered without an engine").arg(id)));
	QVERIFY2(!result.errorMessage.isEmpty(),
		qPrintable(QStringLiteral("%1 refused with no explanation").arg(id)));
	QVERIFY2(result.errorMessage.contains(QStringLiteral("engine"), Qt::CaseInsensitive),
		qPrintable(QStringLiteral("%1's refusal does not name the engine: %2")
			.arg(id, result.errorMessage)));
}

} // namespace

class RecordToolsNoEngineTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY2(Engine::audioEngine() == nullptr,
			"premise: this executable never calls Engine::init(), so there is no engine at all");
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
	}

	//! The sibling that guards inside its own handler (liveRecorder()/noEngine() in
	//! ControlCommandsRecordingRoutes.cpp) is the reference answer: refused, typed, alive.
	void theGuardedSiblingRefusesTypedAndAlive()
	{
		refusesTypedAndAlive(run(QStringLiteral("record.get_state")), QStringLiteral("record.get_state"));
	}

	//! Luna 3946 reported a crash here: record.remaining dereferenced Engine::audioEngine()
	//! for the recorder and the sample rate with no check. The registry's engine gate stops the
	//! call first (ControlRegistry.cpp:257, requiresEngine defaulting to true at
	//! ControlRegistry.h:100), so the dereference is unreachable from the surface.
	void remainingRefusesTypedAndAlive()
	{
		refusesTypedAndAlive(run(QStringLiteral("record.remaining")), QStringLiteral("record.remaining"));
	}

	//! Luna 3950 reported the same crash for record.route_source, which reads
	//! Engine::audioEngine()->recorder().trackCount() to bound 'route'. Same gate.
	void routeSourceRefusesTypedAndAlive()
	{
		refusesTypedAndAlive(run(QStringLiteral("record.route_source"), {{QStringLiteral("route"), 0}}),
			QStringLiteral("record.route_source"));
	}
};

QTEST_MAIN(RecordToolsNoEngineTest)
#include "RecordToolsNoEngineTest.moc"