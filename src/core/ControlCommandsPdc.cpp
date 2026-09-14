/*
 * ControlCommandsPdc.cpp - the pdc.* command group (SPEC A11-A16).
 *
 * Feature row 27 of docs/FEATURE-LIST-0.3.0.md ("PDC and sidechain"). The
 * engine side already exists and is proven: include/LatencyCompensation.h,
 * Mixer::totalLatencyFrames() / Mixer::channelInputLatency() and
 * Mixer::updateLatencyCompensation() (src/core/Mixer.cpp:1530), announced in
 * its own comment as PDC "#605", plus the registered proofs
 * tests/src/core/PdcMixerTest.cpp and tests/src/core/PhaseDSidechainTest.cpp.
 * What did not exist was a way for a client to READ any of it: the audit's row
 * 27 says "latency compensation is neither readable nor settable through the
 * socket". This file registers the read.
 *
 * DESIGN DECISION (recorded because it is a decision, not an omission): there
 * is NO pdc setter verb. The per-edge compensation is not a value an operator
 * sets - Mixer::updateLatencyCompensation() recomputes every edge's delay from
 * the routing graph once per period and publishes it, so a command that wrote
 * one would be overwritten by the next period and would be a lie on the wire.
 * The settable things that CHANGE PDC are the things that change the topology
 * (mixer.route_to / mixer.send_to / mixer.sidechain_to / bus.create, and the
 * device that reports latency), and those carry their own commands. A caller
 * that wants a different alignment changes the routing, not this number.
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

#include <QJsonArray>
#include <QJsonObject>

#include "ControlMixerSupport.h"
#include "ControlRegistry.h"

#include "AudioBusHandle.h"
#include "AudioEngine.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "LatencyCompensation.h"
#include "Mixer.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The mixer channels, as PDC sees them.
QJsonArray channelReport(Mixer* mixer)
{
	QJsonArray channels;
	for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		channels.append(channelLatencyJson(*mixer->mixerChannel(i)));
	}
	return channels;
}

//! Every regular send, with the delay the mixer applies to it.
QJsonArray routeReport(Mixer* mixer)
{
	QJsonArray routes;
	for (MixerRoute* route : mixer->m_mixerRoutes) { routes.append(routeJson(*route)); }
	return routes;
}

//! Every sidechain send, with its tap point and the delay it applies.
QJsonArray sidechainReport(Mixer* mixer)
{
	QJsonArray routes;
	for (MixerSidechainRoute* route : mixer->m_mixerSidechainRoutes)
	{
		routes.append(sidechainRouteJson(*route));
	}
	return routes;
}

/*! The direct track inputs: the AudioBusHandles the mixer's PDC graph reads for
 *  a channel's alignment point (Mixer::updateLatencyCompensation, the
 *  m_directSourceLatencyScratch pass). Reported from the SAME list the mixer
 *  walks, so this cannot disagree with the compensation it publishes.
 */
QJsonArray trackInputReport()
{
	QJsonArray inputs;
	AudioEngine* engine = Engine::audioEngine();
	if (engine == nullptr) { return inputs; }
	for (const AudioBusHandle* handle : engine->audioBusHandles())
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("name"), handle->name());
		entry.insert(QStringLiteral("channel"), control::channelId(handle->nextMixerChannel()));
		entry.insert(QStringLiteral("latency_frames"), handle->latencyFrames());
		inputs.append(entry);
	}
	return inputs;
}

//! The sidechain half of the report: whether the engine has sidechain routing
//! at all, and every route that exists.
QJsonObject sidechainSection(Mixer* mixer)
{
	const QJsonArray routes = sidechainReport(mixer);
	QJsonObject sidechain;
	sidechain.insert(QStringLiteral("supported"), true);
	sidechain.insert(QStringLiteral("count"), routes.size());
	sidechain.insert(QStringLiteral("routes"), routes);
	sidechain.insert(QStringLiteral("tap_points"), QJsonArray::fromStringList(sidechainTapPointNames()));
	sidechain.insert(QStringLiteral("note"),
		QStringLiteral("sidechain sends ARE in this engine (Mixer::createSidechainSend, "
			"src/core/Mixer.cpp); create or adjust one with mixer.sidechain_to and read its "
			"tap and compensation here"));
	return sidechain;
}

ControlResult handlePdcReport()
{
	Mixer* mixer = Engine::mixer();
	if (mixer == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("this instance has no mixer"));
	}

	const QJsonArray channels = channelReport(mixer);
	const QJsonArray routes = routeReport(mixer);
	const QJsonArray trackInputs = trackInputReport();
	const int total = mixer->totalLatencyFrames();

	QJsonObject result;
	result.insert(QStringLiteral("total_latency_frames"), total);
	// The bound the graph can actually honour (#605 follow-up, audit B-1): a
	// request above it is clamped by the delay line itself.
	result.insert(QStringLiteral("delay_line_capacity_frames"), LatencyCompensation::MaxFrames);
	result.insert(QStringLiteral("delay_line_clamped"), total >= LatencyCompensation::MaxFrames);
	result.insert(QStringLiteral("channels"), channels);
	result.insert(QStringLiteral("channel_count"), channels.size());
	result.insert(QStringLiteral("routes"), routes);
	result.insert(QStringLiteral("route_count"), routes.size());
	result.insert(QStringLiteral("sidechain"), sidechainSection(mixer));
	result.insert(QStringLiteral("track_inputs"), trackInputs);
	result.insert(QStringLiteral("track_input_count"), trackInputs.size());
	result.insert(QStringLiteral("note"),
		QStringLiteral("total_latency_frames is the delay from a source entering the mixer to the "
			"master output (Mixer::totalLatencyFrames); input_latency_frames is the alignment point "
			"the mixer publishes per channel (Mixer::channelInputLatency). Both are recomputed once "
			"per period by Mixer::updateLatencyCompensation - no command sets them, and this "
			"command writes nothing"));
	return ControlResult::success(result);
}

} // namespace

void registerPdcCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("pdc.report");
		cmd.group = QStringLiteral("pdc");
		cmd.verb = QStringLiteral("report");
		cmd.description = QStringLiteral("Plugin delay compensation as the mixer publishes it: the "
			"total latency to the master output, every channel's alignment point and the latency its "
			"own chain adds, the compensation applied at every send, and whether sidechain routing "
			"exists. Read-only.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("total_latency_frames"), integerProperty()},
			{QStringLiteral("delay_line_capacity_frames"), integerProperty()},
			{QStringLiteral("delay_line_clamped"), booleanProperty()},
			{QStringLiteral("channels"), arrayProperty()},
			{QStringLiteral("channel_count"), integerProperty()},
			{QStringLiteral("routes"), arrayProperty()},
			{QStringLiteral("route_count"), integerProperty()},
			{QStringLiteral("sidechain"), objectProperty()},
			{QStringLiteral("track_inputs"), arrayProperty()},
			{QStringLiteral("track_input_count"), integerProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handlePdcReport(); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
