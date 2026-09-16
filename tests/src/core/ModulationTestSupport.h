/*
 * ModulationTestSupport.h - the helpers the #602 modulation layer's four test
 *                           files share.
 *
 * ONE definition for the four files that use it, the same rule
 * RackTestSupport.h follows for the rack group: a second copy of a helper is
 * the drift these splits exist to prevent. The files are split because this
 * fork's file-length ratchet measures a file as a unit - a single test file
 * covering the engine, the surface and the per-note group is past the 500-line
 * limit on its own.
 *
 *   ModulationLayerValueTest.cpp          the pure layer: the LFO arithmetic and
 *                                         the layer's own bounds (no engine).
 *   ModulationLayerTest.cpp               the engine on a real Mixer: the target
 *                                         resolver, the relative write, the
 *                                         no-op paths, allocations,
 *                                         persistence.
 *   ControlModulatorCommandsTest.cpp      the modulator.* surface.
 *   ControlNoteExpressionCommandsTest.cpp the note.expression.* surface.
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

#ifndef LMMS_MODULATION_TEST_SUPPORT_H
#define LMMS_MODULATION_TEST_SUPPORT_H

#include <cmath>
#include <cstdio>

#include <QString>

#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "ModulationLayer.h"
#include "ProjectJournal.h"
#include "RackTestSupport.h"
#include "Song.h"

namespace modtest
{

using namespace lmms;
using namespace racktest;

//! One evidence line, flushed: the tests print what they measured, not only
//! that they passed.
inline void evidence(const char* label, const QString& detail)
{
	std::fprintf(stdout, "MODULATION_EVIDENCE %s %s\n", label, detail.toUtf8().constData());
	std::fflush(stdout);
}

//! A fixed-point reading, so the LFO assertions compare exact values.
inline double rounded(double value) { return std::round(value * 1000.0) / 1000.0; }

/*! A model's current value. QCOMPARE cannot take `model->value<float>()`
 *  directly: the macro's expansion parses the angle brackets as relational
 *  operators. One accessor keeps every assertion readable. */
inline float valueOf(const AutomatableModel* model) { return model->value<float>(); }

//! A sine source at @a rateHz with @a phase (0..1), bipolar or unipolar.
inline ModulatorSource sineAt(float rateHz, float phase, bool unipolar)
{
	ModulatorSource source;
	source.shape = ModulationShape::Sine;
	source.rateHz = rateHz;
	source.phase = phase;
	source.unipolar = unipolar;
	return source;
}

//! A route on the shared fixture's driven chain (see RackTestSupport.h).
inline ModulationRoute routeOf(const char* parameter, float depth)
{
	ModulationRoute route;
	// The channel under test's own id, not its index: a route stores the
	// persistent ch-<n> id, and the fixture's channel is not ch-<kChannel> (see
	// underTestChannelId()). A route built from the index resolves to whatever
	// channel carries that number - the master in a fresh session - and then
	// fails to find the rack chain the fixture put on the real channel.
	route.channel = underTestChannelId();
	route.chain = kDrivenChain;
	route.effect = 0;
	route.parameter = QString::fromLatin1(parameter);
	route.depth = depth;
	return route;
}

//! Invoke a command through the registry, exactly as the socket does.
inline ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! "ch-<n>" for the fixture's channel under test, in the form the commands
//! parse - the id the ENGINE gave that channel, never "ch-<kChannel>"
//! (RackTestSupport.h's underTestChannelId() says why).
inline QString channelIdOf()
{
	return underTestChannel();
}

//! Every test starts from a layer nobody has edited.
//!
//! NOT Song::clearProject(): that rebuilds the mixer, and the fixture the
//! modulation tests share with the rack group (RackTestSupport.h) is built once
//! in initTestCase - clearing the project would take the rack chain and its two
//! parameters out from under every test after the first.
inline void resetLayer()
{
	Engine::getSong()->modulationLayer().edit([](ModulationLayer& layer,
		ModulationRuntime& runtime) {
		layer.clear();
		runtime = ModulationRuntime{};
		return true;
	});
	Engine::projectJournal()->clearJournal();
}

//! A clip with one note, created through the surface. Both ids are out.
inline bool buildClipWithNote(QString* clip, QString* note)
{
	const ControlResult track = run(QStringLiteral("track.add"),
		{{QStringLiteral("type"), QStringLiteral("instrument")}});
	if (!track.ok) { return false; }
	const ControlResult clipResult = run(QStringLiteral("clip.add"),
		{{QStringLiteral("track"), track.result.value(QStringLiteral("track")).toString()},
			{QStringLiteral("position"), 0}, {QStringLiteral("length"), 384}});
	if (!clipResult.ok) { return false; }
	*clip = clipResult.result.value(QStringLiteral("clip")).toString();
	const ControlResult noteResult = run(QStringLiteral("note.add"),
		{{QStringLiteral("clip"), *clip}, {QStringLiteral("key"), 57},
			{QStringLiteral("position"), 0}, {QStringLiteral("length"), 96},
			{QStringLiteral("velocity"), 100}});
	if (!noteResult.ok) { return false; }
	*note = noteResult.result.value(QStringLiteral("note")).toString();
	return true;
}

} // namespace modtest

#endif // LMMS_MODULATION_TEST_SUPPORT_H
