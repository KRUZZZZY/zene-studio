/*
 * ControlCommandsSessionRecord.cpp - Arrangement Record's TAP and the
 *                                    Back-to-Arrangement switch
 *                                    (SPEC-zene-studio A3 and §4.1; board task
 *                                    #641, the #596 half).
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

/* WHAT THIS GROUP IS. SPEC §4.1 asks for "Arrangement Record: session
 * performance (launches, moves) recorded into the Arrangement as clips" and a
 * "Back-to-Arrangement switch". The ENGINE half is
 * include/SessionArrangementRecorder.h (the audio thread's ring) plus the feed
 * from the launch state machine in src/core/SessionFollow.cpp; these ids are the
 * whole of the surface - this release has no Arrangement Record button, no take
 * lane and no Back-to-Arrangement light, so if it cannot be driven from here it
 * cannot be used at all (docs/KNOWN-LIMITATIONS.md).
 *
 * THE PASS lives in its own translation unit,
 * ControlCommandsSessionRecordLand.cpp: this fork's file-length ratchet measures
 * a file as a unit and the group measured 518 lines against the 500-line limit
 * when the four ids were one file. The pairing the two halves share is in
 * ControlCommandsSessionRecordInternal.h.
 *
 * THE INTENDED SHAPE, stated once, because the pass depends on it: arm, perform,
 * end the session (session.stop_all or session.back_to_arrangement), then land.
 * A land that ran mid-performance would find launches that have not stopped yet,
 * and it refuses rather than invent their ends.
 */

#include <QJsonObject>
#include <QString>

#include "ControlCommandsSessionRecordInternal.h"
#include "ControlCommandsSessionShared.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SessionArrangementRecorder.h"
#include "SessionModel.h"
#include "SessionScheduler.h"
#include "Song.h"

namespace lmms
{

using namespace control;
using namespace sessioncontrol;
using namespace sessionrecord;

namespace
{

void registerRecordArm(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.arrangement_record_arm");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("arrangement_record_arm");
	cmd.description = QStringLiteral("Arm (or disarm) Arrangement Record: while it is armed the "
		"audio thread pushes one event per session launch and per stop into the engine's ring, at "
		"the tick the transition fired on. Disarming KEEPS what the ring already carries - the "
		"performance is landed, not dropped - and a reset (session.stop_all, "
		"session.back_to_arrangement) records the stop of every slot it ends, so a stopped "
		"performance has no launches left open. Mode state, not project state: no transaction is "
		"recorded.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("armed"), booleanProperty()},
	}, {QStringLiteral("armed")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("armed"), booleanProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		const bool armed = args.value(QStringLiteral("armed")).toBool();
		Engine::getSong()->sessionScheduler().arrangementRecorder().setArmed(armed);
		QJsonObject result;
		result.insert(QStringLiteral("armed"), armed);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerRecordStatus(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.arrangement_record_status");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("arrangement_record_status");
	cmd.description = QStringLiteral("Read Arrangement Record back: whether the tap is armed, how "
		"many events it has recorded, how many it dropped because the ring was full, how many are "
		"waiting, how many clips a land pass would write, and how many launches are still playing "
		"(which is what a land pass refuses on). 'pending' is a bounded estimate - the two ring "
		"indices are read separately, so a push landing between them is not counted. Read-only.");
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("armed"), booleanProperty()},
		{QStringLiteral("recorded"), integerProperty()},
		{QStringLiteral("dropped"), integerProperty()},
		{QStringLiteral("pending"), integerProperty()},
		{QStringLiteral("capacity"), integerProperty()},
		{QStringLiteral("landable_pairs"), integerProperty()},
		{QStringLiteral("open_starts"), integerProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		const SessionArrangementRecorder& recorder =
			Engine::getSong()->sessionScheduler().arrangementRecorder();
		SessionArrangementRecorder::Event events[MaxLandEvents];
		const std::size_t count = recorder.snapshot(events, MaxLandEvents);
		LandPair pairs[MaxLandEvents];
		OpenStart open[MaxLandEvents];
		int openCount = 0;
		int unmatchedStops = 0;
		const int pairCount = landPairEvents(events, count, pairs, MaxLandEvents,
			open, &openCount, MaxLandEvents, &unmatchedStops);
		QJsonObject result;
		result.insert(QStringLiteral("armed"), recorder.armed());
		result.insert(QStringLiteral("recorded"), static_cast<double>(recorder.recorded()));
		result.insert(QStringLiteral("dropped"), static_cast<double>(recorder.dropped()));
		result.insert(QStringLiteral("pending"), static_cast<int>(count));
		result.insert(QStringLiteral("capacity"),
			static_cast<int>(SessionArrangementRecorder::Capacity));
		result.insert(QStringLiteral("landable_pairs"), pairCount);
		result.insert(QStringLiteral("open_starts"), openCount);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerBackToArrangement(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.back_to_arrangement");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("back_to_arrangement");
	cmd.description = QStringLiteral("The Back-to-Arrangement switch: end the session playback and "
		"hand every track back to its arrangement content (SPEC A1 mutual exclusivity - a track "
		"plays its session content or its arrangement content, never both). It is one atomic "
		"request, the same engine operation session.stop_all makes, and it exists as its own id "
		"because it is the ARRANGEMENT's verb: the reply reports the recorded performance, which "
		"this does NOT drop - the ring is landed afterwards by "
		"session.arrangement_record_land. Not a project edit: no transaction is recorded.");
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("handed_back"), booleanProperty()},
		{QStringLiteral("recorded"), integerProperty()},
		{QStringLiteral("pending"), integerProperty()},
		{QStringLiteral("dropped"), integerProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		SessionArrangementRecorder& recorder =
			Engine::getSong()->sessionScheduler().arrangementRecorder();
		// The tap goes with the performance it is taping: disarming here means
		// the ring holds exactly the session that was just handed back, and the
		// next pass arms explicitly. The reset that follows records the stop of
		// every slot it ends (SessionScheduler::consumeResetRequest), so the
		// performance this ends is landable immediately - which is the whole
		// contract of this id.
		recorder.setArmed(false);
		Engine::getSong()->sessionScheduler().reset();
		QJsonObject result;
		result.insert(QStringLiteral("handed_back"), true);
		result.insert(QStringLiteral("recorded"), static_cast<double>(recorder.recorded()));
		result.insert(QStringLiteral("pending"), static_cast<double>(recorder.pending()));
		result.insert(QStringLiteral("dropped"), static_cast<double>(recorder.dropped()));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerSessionRecordCommands(ControlRegistry& registry)
{
	registerRecordArm(registry);
	registerRecordStatus(registry);
	registerBackToArrangement(registry);
	// The PASS, in its own translation unit (the read/edit split the automation,
	// warp, vca and chain-preset groups follow, and the file-length ratchet's
	// reason here): the registry still has exactly one session.* registration
	// point for this half, and it is this function.
	registerSessionRecordLandCommands(registry);
}

} // namespace lmms
