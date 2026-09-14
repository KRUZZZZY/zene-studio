/*
 * ControlCommandsLink.cpp - the link.* (session tempo/phase sync) command group
 *                           (SPEC-zene-studio.md A11-A16).
 *
 * The engine half is include/LinkSync.h, include/LinkPeerTransport.h,
 * src/core/LinkSync.cpp and src/core/LinkUdpTransport.cpp; this group is what
 * makes any of it drivable, and it is the ONLY way to reach it - there is no
 * interface for session sync in this release (docs/KNOWN-LIMITATIONS.md).
 *
 * What these commands are NOT: a wrapper around the Ableton Link library. The
 * licence analysis in docs/LINK-SYNC.md section 1 says vendoring Link is
 * PERMITTED for this GPL-2.0-or-later product (Link's own LICENSE.md is
 * GPL-2.0-or-later), and it also says it is not done here. `link.get_state`
 * therefore reports the model it is speaking (`zene-link-style`) and an
 * `interop` block that states plainly that real Ableton Link interoperability is
 * not compiled in, with the reason - so a reader can never mistake this for the
 * real protocol.
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
#include <QString>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "LinkSync.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! One peer, as link.get_state lists it.
QJsonObject peerJson(const link::LinkPeerView& peer)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), peer.id);
	object.insert(QStringLiteral("name"), peer.name);
	object.insert(QStringLiteral("tempo"), peer.tempo);
	object.insert(QStringLiteral("revision"), static_cast<int>(peer.revision));
	object.insert(QStringLiteral("quantum"), peer.quantum);
	object.insert(QStringLiteral("playing"), peer.playing);
	object.insert(QStringLiteral("age_ms"), static_cast<double>(peer.ageMs));
	return object;
}

/*! link.get_state's payload, also used as the result half of every setter so a
 *  caller sees the new session without a second round trip. */
QJsonObject linkStateJson()
{
	const link::LinkSessionReport report = LinkSyncEngine::instance()->report();
	QJsonObject result;
	result.insert(QStringLiteral("enabled"), report.enabled);
	result.insert(QStringLiteral("model"), report.model);
	result.insert(QStringLiteral("peer_id"), report.peerId);
	result.insert(QStringLiteral("instance"), report.instanceName);
	result.insert(QStringLiteral("peer_count"), report.peerCount);
	QJsonArray peers;
	for (const link::LinkPeerView& peer : report.peers)
	{
		peers.append(peerJson(peer));
	}
	result.insert(QStringLiteral("peers"), peers);
	result.insert(QStringLiteral("quantum"), report.quantum);
	result.insert(QStringLiteral("quantum_choices"),
		QJsonArray{link::MinQuantum, link::DefaultQuantum, link::MaxQuantum});
	result.insert(QStringLiteral("start_stop_sync"), report.startStopSync);
	result.insert(QStringLiteral("session_tempo"), report.sessionTempo);
	result.insert(QStringLiteral("session_revision"), static_cast<int>(report.revision));
	result.insert(QStringLiteral("session_revision_owner"), report.revisionOwner);
	result.insert(QStringLiteral("tempo_is_local"), report.tempoIsLocal);
	result.insert(QStringLiteral("beat"), report.beat);
	result.insert(QStringLiteral("phase_beats"), report.phaseBeats);
	result.insert(QStringLiteral("engine_tempo"), report.engineTempo);
	result.insert(QStringLiteral("engine_phase_beats"), report.enginePhaseBeats);
	result.insert(QStringLiteral("phase_error_beats"), report.phaseErrorBeats);
	result.insert(QStringLiteral("published_count"), static_cast<double>(report.publishedCount));
	result.insert(QStringLiteral("last_packet_age_ms"), static_cast<double>(report.lastPacketAgeMs));
	QJsonObject transport;
	transport.insert(QStringLiteral("available"), report.transportAvailable);
	transport.insert(QStringLiteral("reason"), report.transportReason);
	transport.insert(QStringLiteral("endpoint"), report.transportEndpoint);
	transport.insert(QStringLiteral("publish_interval_ms"), report.publishIntervalMs);
	transport.insert(QStringLiteral("peer_timeout_ms"), report.peerTimeoutMs);
	/* `available` is a MEASUREMENT, so the measurement travels with it: a
	 * datagram sent to the group that a SECOND socket on this host had to
	 * receive. `attempted: false` is "this transport cannot answer", which is
	 * NOT the same as "it passed" - the distinction the whole field exists for
	 * (docs/LINK-SYNC.md section 3). */
	QJsonObject loopback;
	loopback.insert(QStringLiteral("attempted"), report.transportProbeAttempted);
	loopback.insert(QStringLiteral("delivered"), report.transportProbeDelivered);
	loopback.insert(QStringLiteral("elapsed_ms"), report.transportProbeElapsedMs);
	loopback.insert(QStringLiteral("bound_ms"), report.transportProbeBoundMs);
	transport.insert(QStringLiteral("loopback_probe"), loopback);
	result.insert(QStringLiteral("transport"), transport);
	/* The honest half, and the reason it is IN the state and not only in a doc:
	 * this model syncs Zene instances; it does not speak Ableton Link's
	 * protocol, so a Link-enabled third-party app will not join it. */
	QJsonObject interop;
	interop.insert(QStringLiteral("ableton_link"), false);
	interop.insert(QStringLiteral("reason"),
		QStringLiteral("the Ableton Link reference library is not vendored in this build; "
			"vendoring it is licence-permitted (its LICENSE.md is GPL-2.0-or-later) and is a "
			"follow-up lane - docs/LINK-SYNC.md section 1"));
	interop.insert(QStringLiteral("clock"),
		QStringLiteral("peers are assumed to share a monotone clock; true for instances on one "
			"host - docs/LINK-SYNC.md section 4"));
	result.insert(QStringLiteral("interop"), interop);
	return result;
}

//! The transaction payload every setter records (SPEC A16).
QJsonObject transactionOf(const QString& op, const QJsonObject& before, const QJsonObject& inverseArgs)
{
	QJsonObject transaction;
	transaction.insert(QStringLiteral("before"), before);
	transaction.insert(QStringLiteral("inverse"),
		QJsonObject{{QStringLiteral("op"), op}, {QStringLiteral("args"), inverseArgs}});
	transaction.insert(QStringLiteral("reversible"), true);
	transaction.insert(QStringLiteral("mechanism"),
		QStringLiteral("action checkpoint: the recorded undo step calls LinkSyncEngine with the "
			"value in before, exactly as this command sets the new one. Session state is not "
			"project state, so there is no object checkpoint to take"));
	return transaction;
}

void registerLinkGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("link.get_state");
	cmd.group = QStringLiteral("link");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("The session-sync state this instance sees: whether sync is on, "
		"how many peers are in the session and who they are, the session tempo and who declared it, "
		"the shared beat and its phase inside the quantum, this engine's own tempo and phase and the "
		"error between the two, and whether announcements can travel at all (transport.available / "
		"transport.reason). The model is 'zene-link-style' - Ableton Link's semantics (a shared "
		"tempo, a shared beat phase, a quantum, a peer set) without Ableton Link's protocol - and "
		"the `interop` block says so plainly rather than leaving a reader to infer it. Read-only: no "
		"transaction is recorded. There is no interface for any of it; drive it through link.set_* "
		"(docs/KNOWN-LIMITATIONS.md).");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("model"), stringProperty()},
		{QStringLiteral("peer_id"), stringProperty()},
		{QStringLiteral("instance"), stringProperty()},
		{QStringLiteral("peer_count"), integerProperty()},
		{QStringLiteral("peers"), arrayProperty()},
		{QStringLiteral("quantum"), integerProperty()},
		{QStringLiteral("quantum_choices"), arrayProperty()},
		{QStringLiteral("start_stop_sync"), booleanProperty()},
		{QStringLiteral("session_tempo"), numberProperty()},
		{QStringLiteral("session_revision"), integerProperty()},
		{QStringLiteral("session_revision_owner"), stringProperty()},
		{QStringLiteral("tempo_is_local"), booleanProperty()},
		{QStringLiteral("beat"), numberProperty()},
		{QStringLiteral("phase_beats"), numberProperty()},
		{QStringLiteral("engine_tempo"), integerProperty()},
		{QStringLiteral("engine_phase_beats"), numberProperty()},
		{QStringLiteral("phase_error_beats"), numberProperty()},
		{QStringLiteral("published_count"), numberProperty()},
		{QStringLiteral("last_packet_age_ms"), numberProperty()},
		{QStringLiteral("transport"), objectProperty()},
		{QStringLiteral("interop"), objectProperty()},
	});
	// A read of process-wide state: it answers before an engine exists, which is
	// when a caller most needs to know what sync would do.
	cmd.mutating = false;
	cmd.requiresEngine = false;
	cmd.handler = [](const QJsonObject&) {
		return ControlResult::success(linkStateJson());
	};
	registry.registerCommand(cmd);
}

void registerLinkSetEnabled(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("link.set_enabled");
	cmd.group = QStringLiteral("link");
	cmd.verb = QStringLiteral("set_enabled");
	cmd.description = QStringLiteral("Join or leave the session: on starts the transport, joins the "
		"multicast group and begins announcing this instance's tempo and beat every 100 ms; off "
		"stops both and forgets every peer. While sync is on, a tempo change made here - by any "
		"means, including transport.set_tempo - is announced to the session, and a peer's newer "
		"announcement sets this engine's tempo, so two instances stay on one tempo without anyone "
		"relaying between them. Joining also declares this instance's current tempo, so a session "
		"that was already running is adopted from its own newest announcement. The quantum and the "
		"kernel's tempo bounds are reported by link.get_state. Transport is UDP multicast on "
		"224.76.78.75:20808, the group Ableton Link itself uses for discovery.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("enabled"), booleanProperty()},
	}, {QStringLiteral("enabled")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("previous"), booleanProperty()},
		{QStringLiteral("peer_count"), integerProperty()},
		{QStringLiteral("transport"), objectProperty()},
		{QStringLiteral("interop"), objectProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		LinkSyncEngine* sync = LinkSyncEngine::instance();
		const bool previous = sync->enabled();
		const bool requested = args.value(QStringLiteral("enabled")).toBool();

		// SPEC A16: session sync is process state the engine does not journal,
		// so the inverse is a recorded undo STEP on the engine's own stack -
		// the mechanism settings.set and export.set_dither use.
		control::addUndoStep(
			[previous]() { LinkSyncEngine::instance()->setEnabled(previous); },
			[requested]() { LinkSyncEngine::instance()->setEnabled(requested); });
		sync->setEnabled(requested);

		QJsonObject result = linkStateJson();
		result.insert(QStringLiteral("previous"), previous);
		result.insert(QStringLiteral("__transaction"), transactionOf(QStringLiteral("link.set_enabled"),
			QJsonObject{{QStringLiteral("enabled"), previous}},
			QJsonObject{{QStringLiteral("enabled"), previous}}));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerLinkSetQuantum(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("link.set_quantum");
	cmd.group = QStringLiteral("link");
	cmd.verb = QStringLiteral("set_quantum");
	cmd.description = QStringLiteral("Set the quantum, in beats: the length of the cycle the shared "
		"beat phase is measured against and the grid a phase-aligned action would be placed on. 4 "
		"(one 4/4 bar) is the default; 1 to 64 is accepted. The quantum is THIS instance's own "
		"setting and is not part of what peers agree on - a peer that reports a different one is "
		"reported with it (link.get_state peers[].quantum) and is not corrected.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("quantum"), integerProperty(link::MinQuantum, link::MaxQuantum)},
	}, {QStringLiteral("quantum")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("quantum"), integerProperty()},
		{QStringLiteral("previous"), integerProperty()},
		{QStringLiteral("phase_beats"), numberProperty()},
		{QStringLiteral("enabled"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		LinkSyncEngine* sync = LinkSyncEngine::instance();
		const int requested = static_cast<int>(args.value(QStringLiteral("quantum")).toDouble());
		// Validated BEFORE any write and before the recorded step, so a refused
		// quantum changes nothing and records nothing.
		if (requested < link::MinQuantum || requested > link::MaxQuantum)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("quantum %1 is outside %2..%3 beats")
					.arg(requested).arg(link::MinQuantum).arg(link::MaxQuantum));
		}
		const int previous = sync->quantum();
		control::addUndoStep(
			[previous]() { LinkSyncEngine::instance()->setQuantum(previous); },
			[requested]() { LinkSyncEngine::instance()->setQuantum(requested); });
		if (!sync->setQuantum(requested))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the model refused quantum %1").arg(requested));
		}
		QJsonObject result = linkStateJson();
		result.insert(QStringLiteral("previous"), previous);
		result.insert(QStringLiteral("__transaction"), transactionOf(QStringLiteral("link.set_quantum"),
			QJsonObject{{QStringLiteral("quantum"), previous}},
			QJsonObject{{QStringLiteral("quantum"), previous}}));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerLinkSetStartStopSync(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("link.set_start_stop_sync");
	cmd.group = QStringLiteral("link");
	cmd.verb = QStringLiteral("set_start_stop_sync");
	cmd.description = QStringLiteral("Turn transport-start/stop sharing on or off: the flag that "
		"says a session's members share not only the tempo and the beat phase but the decision to be "
		"playing. It is announced to peers (peers report their own through link.get_state) and it is "
		"reported back, but this release does NOT act on it: nothing here starts or stops another "
		"instance's transport, because doing so would write the audio thread's play state from a "
		"network announcement. The flag is the contract's declaration half; the acting half is a "
		"stated limitation (docs/LINK-SYNC.md section 5).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("start_stop_sync"), booleanProperty()},
	}, {QStringLiteral("start_stop_sync")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("start_stop_sync"), booleanProperty()},
		{QStringLiteral("previous"), booleanProperty()},
		{QStringLiteral("enabled"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		LinkSyncEngine* sync = LinkSyncEngine::instance();
		const bool previous = sync->startStopSync();
		const bool requested = args.value(QStringLiteral("start_stop_sync")).toBool();
		control::addUndoStep(
			[previous]() { LinkSyncEngine::instance()->setStartStopSync(previous); },
			[requested]() { LinkSyncEngine::instance()->setStartStopSync(requested); });
		sync->setStartStopSync(requested);
		QJsonObject result = linkStateJson();
		result.insert(QStringLiteral("previous"), previous);
		result.insert(QStringLiteral("__transaction"), transactionOf(QStringLiteral("link.set_start_stop_sync"),
			QJsonObject{{QStringLiteral("start_stop_sync"), previous}},
			QJsonObject{{QStringLiteral("start_stop_sync"), previous}}));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerLinkSetSessionTempo(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("link.set_session_tempo");
	cmd.group = QStringLiteral("link");
	cmd.verb = QStringLiteral("set_session_tempo");
	cmd.description = QStringLiteral("Declare a session tempo, in BPM, to every peer: the timeline is "
		"re-anchored at the declaration so the shared beat does not jump, the revision goes to one "
		"above the highest this instance has seen, and the announcement that carries it is newer than "
		"every declaration already in the session - so peers follow this one rather than the other "
		"way round. This is the explicit form of a tempo change; a plain transport.set_tempo reaches "
		"the session too, on the next announcement, because the model watches the engine's own tempo "
		"for a change it did not make itself. A session tempo is a double on the wire and an integer "
		"in this engine, so what is applied is the rounded value and the engine's own bounds win.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("bpm"), numberProperty()},
	}, {QStringLiteral("bpm")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("session_tempo"), numberProperty()},
		{QStringLiteral("previous"), numberProperty()},
		{QStringLiteral("session_revision"), integerProperty()},
		{QStringLiteral("engine_tempo"), integerProperty()},
		{QStringLiteral("tempo_is_local"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		LinkSyncEngine* sync = LinkSyncEngine::instance();
		const double requested = args.value(QStringLiteral("bpm")).toDouble();
		// Refusals come first and write nothing. Disabled is a refusal rather
		// than a silent no-op: with no session joined, a declaration reaches
		// nobody and would be overwritten by the join that follows it.
		if (!sync->enabled())
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("sync is off, so a declared session tempo would reach nobody and "
					"would be replaced when sync is enabled; call link.set_enabled true first, or "
					"set this instance's own tempo with transport.set_tempo"));
		}
		if (!(requested >= link::MinSessionTempo && requested <= link::MaxSessionTempo))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("bpm %1 is outside %2..%3")
					.arg(requested).arg(link::MinSessionTempo).arg(link::MaxSessionTempo));
		}
		const double previous = sync->sessionTempo();
		control::addUndoStep(
			[previous]() { LinkSyncEngine::instance()->setSessionTempo(previous); },
			[requested]() { LinkSyncEngine::instance()->setSessionTempo(requested); });
		sync->setSessionTempo(requested);
		QJsonObject result = linkStateJson();
		result.insert(QStringLiteral("previous"), previous);
		result.insert(QStringLiteral("__transaction"), transactionOf(QStringLiteral("link.set_session_tempo"),
			QJsonObject{{QStringLiteral("bpm"), previous}},
			QJsonObject{{QStringLiteral("bpm"), previous}}));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerLinkCommands(ControlRegistry& registry)
{
	registerLinkGetState(registry);
	registerLinkSetEnabled(registry);
	registerLinkSetQuantum(registry);
	registerLinkSetStartStopSync(registry);
	registerLinkSetSessionTempo(registry);
}

} // namespace lmms
