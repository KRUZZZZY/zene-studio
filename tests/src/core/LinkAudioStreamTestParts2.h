/*
 * LinkAudioStreamTest.cpp - the PCM stream between instances (Q050-024,
 *                           include/LinkAudioStream.h, docs/LINK-AUDIO.md).
 *
 * HERMETIC: every socket here is bound to 127.0.0.1 and every peer is 127.0.0.1. The one test that
 * runs Ableton Link confines its discovery to the loopback interface first (see pcmStreamsWhileLinkSyncIsActive).
 * Nothing is multicast or broadcast beyond the loopback interface (the Link test confines Link's discovery
 * to it), there is no name lookup, UPnP or internet.
 *
 * Proof of the acceptance criteria: the codec, the ring and the refusals pin the
 * building blocks; loopbackExchangeIsSampleExact moves a counting ramp from one
 * stream to another over a real UDP socket and compares every sample;
 * audioThreadAllocatesNothing counts operator new on the calling thread through
 * the shared AllocationProbe; realtimeStressReportsBoundedUnderrunsAndLatency runs
 * two threads on a 1 ms cadence (a 48 kHz, 48-frame period each) for 3 s and
 * bounds underruns, buffered latency and ordering; the command tests show the
 * registered surface, its undo, and that link.* (tempo, quantum) is untouched.
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

// Complete fixture method definitions; included once after the fixture declaration.

void LinkAudioStreamTest::omittedEnabledIsRefusedAndTheRunningStreamKeepsRunning() {
	// 'enabled' is required by the schema (ControlCommandsLinkAudio.cpp:180; docs/LINK-AUDIO.md:80), so a call
	// that only sets peer/latency is refused by ControlRegistry::invoke before mergeArgs runs: the stream stays up.
	const ControlResult on = run(QStringLiteral("link.audio_set_enabled"),
		{{QStringLiteral("enabled"), true}, {QStringLiteral("listen_port"), 0}});
	QVERIFY2(on.ok, qPrintable(on.errorMessage));
	QVERIFY(run(QStringLiteral("link.audio_get_state")).result.value(QStringLiteral("running")).toBool());

	const ControlResult omitted = run(QStringLiteral("link.audio_set_enabled"),
		{{QStringLiteral("peer"), QStringLiteral("127.0.0.1")}, {QStringLiteral("latency_ms"), 40}});
	QVERIFY(!omitted.ok);
	QVERIFY2(omitted.errorMessage.contains(QStringLiteral("missing required property 'enabled'")),
		qPrintable(omitted.errorMessage));
	QVERIFY(run(QStringLiteral("link.audio_get_state")).result.value(QStringLiteral("running")).toBool());

	QVERIFY(run(QStringLiteral("link.audio_set_enabled"), {{QStringLiteral("enabled"), false}}).ok);
	QVERIFY(!run(QStringLiteral("link.audio_get_state")).result.value(QStringLiteral("running")).toBool());
}
void LinkAudioStreamTest::theStreamLeavesLinkSyncAlone() {
	// Link stays OFF here (it would multicast); the claim is that the two surfaces share no
	// state: link.* answers identically with the stream running, and link's own setters work.
	const QJsonObject before = run(QStringLiteral("link.get_state")).result;
	const ControlResult started = run(QStringLiteral("link.audio_set_enabled"),
		{{QStringLiteral("enabled"), true}, {QStringLiteral("listen_port"), 0}});
	QVERIFY2(started.ok, qPrintable(started.errorMessage));
	const QJsonObject during = run(QStringLiteral("link.get_state")).result;
	for (const char* key : {"enabled", "model", "quantum", "start_stop_sync", "session_tempo", "peer_count"})
	{
		QCOMPARE(during.value(QLatin1String(key)), before.value(QLatin1String(key)));
	}
	QVERIFY(run(QStringLiteral("link.set_quantum"), {{QStringLiteral("quantum"), 8}}).ok);
	QCOMPARE(run(QStringLiteral("link.get_state")).result.value(QStringLiteral("quantum")).toInt(), 8);
	QVERIFY(run(QStringLiteral("link.audio_get_state")).result.value(QStringLiteral("running")).toBool());
	run(QStringLiteral("link.set_quantum"), {{QStringLiteral("quantum"), before.value(QStringLiteral("quantum")).toInt()}});
	run(QStringLiteral("link.audio_set_enabled"), {{QStringLiteral("enabled"), false}});
}
