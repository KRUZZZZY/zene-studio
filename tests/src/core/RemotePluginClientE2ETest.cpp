/*
 * RemotePluginClientE2ETest.cpp - the remote plugin host<->client contract,
 *                                driven end to end
 *
 * WHAT THIS DRIVES
 *
 * RemotePlugin never runs in the same process as its client: the host side
 * (src/core/RemotePlugin.cpp) starts a client executable, talks to it over a
 * Unix socket and hands it a shared audio block it allocates
 * (RemotePlugin::updateAudioBuffer()). Every claim about that contract used to
 * rest on reading both sides' code plus one side's unit tests - this test
 * executes it:
 *
 *   - realClientProducesAudioInHostPlanes() runs the real client executable
 *     RemoteZynAddSubFx (the only in-tree remote client that starts headless)
 *     as a separate process against a real host, completes the handshake the
 *     way the in-tree host plugins do, and drives two processing periods: the
 *     host must allocate the shared block for the channel counts the *client*
 *     reported and read the synth's audio out of its output planes.
 *
 *   - matchingClientAudioLandsAtExactChannelFrameOffsets() replaces the client
 *     with tests/src/plugins/FakeRemotePluginClient.cpp, which speaks the same
 *     protocol and writes a known planar pattern. A real synth's samples are
 *     not predictable, so the exact channel/frame offsets (and the plane
 *     stride) are pinned here: every sample the host reads is compared with
 *     the value the client wrote at that position.
 *
 *   - stalePreMigrationClientIsRefusedAndRendersSilence() drives a pre-#589
 *     client (retired IdChangeInputCount, interleaved sample writes) and pins
 *     the version-skew verdict: refuse loudly, mark the plugin failed, render
 *     silence. A previous ad-hoc harness found a silent-wrong-audio defect on
 *     exactly this path; this keeps that regression testable.
 *
 * WHAT THIS DOES NOT PROVE (kept explicit so a green run is not misread)
 *
 *   - The real client's samples are checked for liveness and plane coverage,
 *     never sample-exactly: RemoteZynAddSubFx's output depends on the synth's
 *     internal state. Exact offsets are proven by the deterministic fake
 *     client, which shares the transport but not the DSP.
 *   - The other in-tree remote clients (RemoteVstPlugin /
 *     NativeLinuxRemoteVstPlugin64) are not driven: they need a VST2 module to
 *     host, which this tree does not ship, so their extra protocol legs
 *     (IdHostInfoGotten handshake, parameter dumps) remain unexercised here.
 *   - GUI-related messages (IdShowUI/IdHideUI/IdToggleUI, settings/preset file
 *     round trips, the Zyn-specific preset-directory messages) are not sent:
 *     the clients run headless.
 *   - The pre-#589 client is the fixture, not a real historical binary: the
 *     retired wire behaviour is emulated (retired id + interleaved writes).
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

#include "RemotePlugin.h"
#include "RemotePluginAudioPorts.h"

#include <QtTest>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "AudioEngine.h"
#include "Engine.h"
#include "MidiEvent.h"
#include "Song.h"
#include "SharedMemory.h"
#include "VstSyncData.h"

#include "AllocationProbe.h"

using namespace lmms;

namespace
{

//! Ports identical to the in-tree ZynAddSubFx remote plugin's: 0 in / 2 out
//! (ZynAddSubFxInstrument's AudioPortsSettings).
inline constexpr auto ZynLikeSettings = AudioPortsSettings {
	.kind = AudioDataKind::F32,
	.interleaved = false,
	.inputs = 0,
	.outputs = 2
};

class ZynLikePorts final : public RemotePluginAudioPorts<ZynLikeSettings>
{
public:
	using RemotePluginAudioPorts<ZynLikeSettings>::RemotePluginAudioPorts;

	auto channelName(ch_cnt_t, bool) const -> QString override { return {}; }
};

//! Channel counts that are only known once the remote client reports them -
//! the shape the VST/Vestige remote ports use, and the case a version-skewed
//! client's count messages arrive in. (Ports with compile-time counts assert
//! that they never change, so they cannot model this.)
inline constexpr auto DynamicPlanarSettings = AudioPortsSettings {
	.kind = AudioDataKind::F32,
	.interleaved = false
};

class DynamicPlanarPorts final : public RemotePluginAudioPorts<DynamicPlanarSettings>
{
public:
	using RemotePluginAudioPorts<DynamicPlanarSettings>::RemotePluginAudioPorts;

	auto channelName(ch_cnt_t, bool) const -> QString override { return {}; }
};

//! The frame count the deterministic fake client writes its pattern for.
inline constexpr float PatternBase = 1000.0f;

/*!
 * Captures the messages the host logs (a refused client is recognised by the
 * qCritical RemotePlugin::processMessage() emits), chaining to whatever
 * handler was installed before.
 */
class MessageRecorder
{
public:
	MessageRecorder() { s_previous = qInstallMessageHandler(&MessageRecorder::handle); }
	~MessageRecorder() { qInstallMessageHandler(s_previous); }

	auto text() const -> QString { return s_messages.join(QLatin1Char('\n')); }
	void clear() { s_messages.clear(); }

private:
	static void handle(QtMsgType type, const QMessageLogContext& context, const QString& message)
	{
		s_messages.append(message);
		if (s_previous != nullptr) { s_previous(type, context, message); }
	}

	static inline QStringList s_messages;
	static inline QtMessageHandler s_previous = nullptr;
};

#ifdef REMOTE_PLUGIN_E2E_CLIENT_DIR
//! Directory holding the built RemoteZynAddSubFx executable
inline auto realClientDirectory() -> QString { return QStringLiteral(REMOTE_PLUGIN_E2E_CLIENT_DIR); }
#else
inline auto realClientDirectory() -> QString { return {}; }
#endif

#ifdef REMOTE_PLUGIN_E2E_FAKE_CLIENT
//! Path of the hand-written protocol client (POSIX only)
inline auto fakeClientExecutable() -> QString { return QStringLiteral(REMOTE_PLUGIN_E2E_FAKE_CLIENT); }
#else
inline auto fakeClientExecutable() -> QString { return {}; }
#endif

//! Name of the real client executable as RemotePlugin::init() resolves it.
inline constexpr auto RealClientName = "RemoteZynAddSubFx";

//! Points LMMS_PLUGIN_DIR at a directory for one test, and restores what was there before when the test ends.
class ScopedPluginDir
{
public:
	explicit ScopedPluginDir(const QString& dir)
		: m_wasSet{qEnvironmentVariableIsSet("LMMS_PLUGIN_DIR")}
		, m_previous{qEnvironmentVariable("LMMS_PLUGIN_DIR")}
	{
		qputenv("LMMS_PLUGIN_DIR", dir.toUtf8());
	}

	~ScopedPluginDir()
	{
		if (m_wasSet) { qputenv("LMMS_PLUGIN_DIR", m_previous.toUtf8()); }
		else { qunsetenv("LMMS_PLUGIN_DIR"); }
	}

	ScopedPluginDir(const ScopedPluginDir&) = delete;
	ScopedPluginDir& operator=(const ScopedPluginDir&) = delete;

private:
	bool m_wasSet;
	QString m_previous;
};

/*!
 * Widens the processing period's wait for its reply, for one test (RemotePlugin::setPeriodBudgetForTesting()).
 *
 * The real wait is one audio period (about 20 ms). The fake client is another process, so on a loaded machine
 * it misses that window now and then, and a test about what the client says then fails for a reason it does not
 * test (a late reply is skipped, the next request is never sent, the client's report never comes). Tests about
 * the deadline itself - late, stalled or half-written replies - keep the real budget.
 */
class ScopedReplyBudget
{
public:
	explicit ScopedReplyBudget(std::chrono::microseconds budget = std::chrono::seconds{5})
	{
		RemotePlugin::setPeriodBudgetForTesting(budget);
	}
	~ScopedReplyBudget() { RemotePlugin::setPeriodBudgetForTesting(std::chrono::microseconds{0}); }

	ScopedReplyBudget(const ScopedReplyBudget&) = delete;
	ScopedReplyBudget& operator=(const ScopedReplyBudget&) = delete;
};

class ScopedQueryReplyBudget
{
public:
	explicit ScopedQueryReplyBudget(std::chrono::milliseconds timeout = std::chrono::seconds{5})
	{
		RemotePlugin::setQueryReplyTimeoutForTesting(timeout);
	}
	~ScopedQueryReplyBudget() { RemotePlugin::setQueryReplyTimeoutForTesting(std::chrono::milliseconds{0}); }

	ScopedQueryReplyBudget(const ScopedQueryReplyBudget&) = delete;
	ScopedQueryReplyBudget& operator=(const ScopedQueryReplyBudget&) = delete;
};

/*!
 * Runs periods until the client's output-count report moves the count off \a from, or \a limit passes.
 *
 * These clients send a count report in every reply, so no period of theirs is a processed one (asserted for each).
 * The report is applied on this (the main) thread before the next period, as the main thread's timer does - a
 * period with one pending sends nothing. How many periods it takes is not fixed: a period whose MIDI finds no
 * room in the socket (the client process reading slowly on a loaded machine) is skipped by design and its events
 * stay queued, so a fixed count of periods could end before the queue had drained.
 */
template<typename Ports>
void runPeriodsUntilTheCountMoves(RemotePlugin& plugin, Ports& ports, ch_cnt_t from,
	std::chrono::milliseconds limit = std::chrono::seconds{10})
{
	const auto until = std::chrono::steady_clock::now() + limit;
	while (ports.audioPortsModel().out().channelCount() == from && std::chrono::steady_clock::now() < until)
	{
		QVERIFY(!plugin.process());
		plugin.applyDeferredClientRequests();
		std::this_thread::sleep_for(std::chrono::milliseconds{1});   // the client reads between periods
	}
}

//! Shrinks the room a non-blocking send may use, for one test (RemotePluginBase::setSendRoomForTesting()).
class ScopedSendRoom
{
public:
	explicit ScopedSendRoom(std::size_t bytes) { RemotePluginBase::setSendRoomForTesting(bytes); }
	~ScopedSendRoom() { RemotePluginBase::setSendRoomForTesting(0); }

	ScopedSendRoom(const ScopedSendRoom&) = delete;
	ScopedSendRoom& operator=(const ScopedSendRoom&) = delete;
};

//! The allocations \a body makes on this thread, counted by the allocation probe.
template<typename Body>
auto countAllocations(Body&& body) -> std::uint64_t
{
	lmms::test::resetAllocationCount();
	lmms::test::tlCountAllocations = true;
	body();
	lmms::test::tlCountAllocations = false;
	return lmms::test::tlAllocationCount;
}

/*!
 * Starts `executablePath` as this host's remote client and completes the
 * handshake exactly as the in-tree host plugins do (see ZynAddSubFx.cpp):
 * RemotePlugin::init() followed by lock()/waitForInitDone(false)/unlock().
 *
 * RemotePlugin::init() resolves the executable through LMMS_PLUGIN_DIR, so the
 * directory is set here rather than passing an absolute path.
 *
 * @return true if the process started and connected (not necessarily that the
 *         handshake succeeded - check failed() afterwards)
 */
auto startClient(RemotePlugin& plugin, const QString& executablePath,
	const QStringList& extraArgs = {}) -> bool
{
	const QFileInfo info{executablePath};
	if (!info.exists())
	{
		qWarning("client executable '%s' does not exist", qPrintable(executablePath));
		return false;
	}

	qputenv("LMMS_PLUGIN_DIR", info.absolutePath().toUtf8());
	if (plugin.init(info.fileName(), false, extraArgs))
	{
		return false;
	}

	plugin.lock();
	plugin.waitForInitDone(false);
	plugin.unlock();
	return true;
}

} // namespace

class RemotePluginClientE2ETest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		m_recorder = std::make_unique<MessageRecorder>();
	}

	void cleanupTestCase()
	{
		m_recorder.reset();
		Engine::destroy();
	}

	/*!
	 * A real remote plugin client, as a separate process.
	 *
	 * The host must: spawn RemoteZynAddSubFx, complete the handshake, allocate
	 * the shared audio block for the channel counts the client reports
	 * (0 in / 2 out), and read the synth's own audio out of its output planes
	 * for at least two periods. The synth's sample values are not predictable,
	 * so what is pinned is that audio - finite, non-silent audio - arrives in
	 * both output planes, i.e. the block the client writes into really is the
	 * block the host reads.
	 */
	void realClientProducesAudioInHostPlanes()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const QString client = QDir{realClientDirectory()}.filePath(QString::fromLatin1(RealClientName));
		if (realClientDirectory().isEmpty() || !QFile::exists(client))
		{
			QSKIP("the real remote client (RemoteZynAddSubFx) is not part of this build: "
				"the real-client half of the host<->client contract is UNEXERCISED by this run");
		}

		ZynLikePorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		QVERIFY2(startClient(plugin, client),
			qPrintable(QString{"could not start/connect the remote client %1"}.arg(client)));
		QVERIFY2(!plugin.failed(), "the remote client did not complete the handshake");

		auto* buffers = ports.buffers();
		QVERIFY(buffers != nullptr);

		// The shared block exists, sized for the counts the *client* reported
		QVERIFY2(buffers->initialized(), "the host never allocated the shared audio block");
		QCOMPARE(buffers->frames(), frames);
		QCOMPARE(ports.audioPortsModel().in().channelCount(), ch_cnt_t{0});
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{2});

		auto out = buffers->output();
		QCOMPARE(out.empty(), false);
		QCOMPARE(out.channels(), ch_cnt_t{2});
		QCOMPARE(out.frames(), frames);
		// Plane 1 begins exactly `frames` floats after plane 0: the offsets the
		// client's planar writes are read at.
		QCOMPARE(out.bufferPtr(1) - out.bufferPtr(0), static_cast<std::ptrdiff_t>(frames));

		// The client takes its plane stride from the shared VST-sync block at
		// IdSyncKey (RemotePluginClient::processMessage()); the host lays its
		// own planes out with framesPerPeriod(). They have to be the same
		// number, or the two sides disagree about the layout.
		{
			SharedMemory<VstSyncData> syncData;
			syncData.attach(Engine::getSong()->syncKey());
			QVERIFY2(syncData.get() != nullptr, "the VST-sync block could not be attached");
			QCOMPARE(syncData->bufferSize, static_cast<int>(frames));
		}

		// What the production host does after the handshake (ZynAddSubFx.cpp).
		plugin.updateSampleRate(Engine::audioEngine()->outputSampleRate());
		plugin.lock();
		plugin.sendMessage(RemotePlugin::message(IdBufferSizeInformation).addInt(frames));
		plugin.unlock();

		// A held note, so the synth has something to render.
		plugin.processMidiEvent(MidiEvent{MidiNoteOn, 0, 60, 100}, 0);
		const ScopedReplyBudget replyInTime;

		float peak[2] = {0.0f, 0.0f};
		for (int period = 1; period <= 2; ++period)
		{
			QVERIFY2(plugin.process(), qPrintable(QString{"period %1 was not processed"}.arg(period)));

			for (ch_cnt_t channel = 0; channel < ch_cnt_t{2}; ++channel)
			{
				float channelPeak = 0.0f;
				for (f_cnt_t frame = 0; frame < frames; ++frame)
				{
					const float sample = out.sample(channel, frame);
					QVERIFY2(std::isfinite(sample),
						qPrintable(QString{"period %1, channel %2, frame %3: non-finite sample %4"}
							.arg(period).arg(channel).arg(frame).arg(double(sample))));
					channelPeak = std::max(channelPeak, std::abs(sample));
				}
				peak[channel] = std::max(peak[channel], channelPeak);
			}
		}

		// RemotePlugin::process() zero-fills the output planes before asking the
		// client for a period, so non-zero samples here are the client's own
		// audio: the synth was audible on both channels.
		QVERIFY2(peak[0] > 1e-4f, "no audio arrived in the host's left output plane");
		QVERIFY2(peak[1] > 1e-4f, "no audio arrived in the host's right output plane");
		qInfo("real client: 2 periods of %llu frames each, block sized for the client's "
			"(0 in / 2 out) counts; peak |left| = %g, peak |right| = %g",
			static_cast<unsigned long long>(frames), double(peak[0]), double(peak[1]));
	}

	/*!
	 * Exact channel/frame offsets, with a client whose output is known.
	 *
	 * The fake client writes `plane0[f] = 1000 * period + f` and
	 * `plane1[f] = -(1000 * period + f)`; every sample the host reads through
	 * its output-port views is compared against those values for two periods.
	 * A changed plane offset, a changed plane stride or a stale plane in the
	 * host's ports makes this fail.
	 */
	void matchingClientAudioLandsAtExactChannelFrameOffsets()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the exact "
				"channel/frame offsets of the contract are UNEXERCISED by this run");
		}

		ZynLikePorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		QVERIFY2(startClient(plugin, client, {QStringLiteral("modern")}),
			"could not start/connect the deterministic fake client");
		QVERIFY2(!plugin.failed(), "the fake client did not complete the handshake");
		QVERIFY2(ports.buffers()->initialized(), "the host never allocated the shared audio block");

		auto out = ports.buffers()->output();
		QCOMPARE(out.channels(), ch_cnt_t{2});
		QCOMPARE(out.frames(), frames);
		QCOMPARE(out.bufferPtr(1) - out.bufferPtr(0), static_cast<std::ptrdiff_t>(frames));

		const ScopedReplyBudget replyInTime;
		for (int period = 1; period <= 2; ++period)
		{
			QVERIFY2(!plugin.failed(), "the fake client died between periods");
			QVERIFY2(plugin.process(), qPrintable(QString{"period %1 was not processed"}.arg(period)));

			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				const float expected = PatternBase * period + static_cast<float>(frame);
				const float left = out.sample(0, frame);
				const float right = out.sample(1, frame);
				if (left != expected || right != -expected)
				{
					QFAIL(qPrintable(QString{"period %1, frame %2: host read (%3, %4), client wrote (%5, %6)"}
						.arg(period).arg(frame)
						.arg(double(left)).arg(double(right))
						.arg(double(expected)).arg(double(-expected))));
				}
			}
		}
		qInfo("deterministic client: 2 periods of %llu frames matched the client's planar "
			"writes sample for sample (plane0[f] = 1000*period + f, plane1[f] = -that)",
			static_cast<unsigned long long>(frames));
	}

	/*!
	 * Version skew: a client built against the retired pre-#589 interleaved
	 * protocol announces its channel counts with a retired id. The host must
	 * refuse it loudly, mark the plugin failed, and render silence - not read
	 * the client's interleaved writes as if they were planar planes (the
	 * silent-wrong-audio defect the preserved stale-client harness found).
	 */
	void stalePreMigrationClientIsRefusedAndRendersSilence()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the version-skew "
				"refusal is UNEXERCISED by this run");
		}

		// Dynamic counts: a pre-#589 client is exactly a client whose counts
		// arrive over the wire, which is the case the retired ids drive.
		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		m_recorder->clear();
		QVERIFY2(startClient(plugin, client, {QStringLiteral("stale")}),
			"could not start/connect the pre-#589 fake client");

		// The verdict: a retired id is refused, loudly, and fails the plugin.
		QVERIFY2(plugin.failed(), "the host accepted a client speaking the retired protocol");
		const QString log = m_recorder->text();
		QVERIFY2(log.contains(QStringLiteral("removed message id")), qPrintable(log));
		QVERIFY2(log.contains(QStringLiteral("pre-#589")), qPrintable(log));

		// Nothing was allocated for the refused client's counts, so there is no
		// shared block its interleaved writes could have landed in.
		QVERIFY2(!ports.buffers()->initialized(),
			"a refused client still caused a shared audio block to be allocated");

		// The dangerous variant: the host already has a block (a plugin whose
		// ports are learned, and then activated, before/while the client
		// connects). Even then process() must fail and leave silence behind,
		// never consume the block.
		ports.audioPortsModel().setChannelCounts(2, 2);
		QVERIFY(ports.buffers()->initialized());
		QCOMPARE(ports.buffers()->frames(), frames);

		auto out = ports.buffers()->output();
		std::fill(out.bufferPtr(0), out.bufferPtr(0) + frames, 1.0f);
		std::fill(out.bufferPtr(1), out.bufferPtr(1) + frames, 1.0f);

		QVERIFY2(!plugin.process(), "a failed plugin reported a processed period");

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(out.sample(0, frame), 0.0f);
			QCOMPARE(out.sample(1, frame), 0.0f);
		}
		// NOTE: tearing the failed plugin down kills the client process, so the
		// run logs "QProcess: Destroyed while process is still running" and the
		// crash diagnostics - a failed plugin never sends IdQuit. That noise is
		// the documented shutdown path, not a test failure.
		qInfo("pre-#589 client: retired count message refused, plugin failed, "
			"%llu already-allocated output frames left silent",
			static_cast<unsigned long long>(frames));
	}

	/*!
	 * A client that never answers IdStartProcessing must not hold the audio thread.
	 *
	 * RemotePlugin::process() gives the client one period to reply. Past that it
	 * renders the period as silence and returns, and the next period does not send
	 * a new request while the reply is still owed. The fake client here takes the
	 * request and stays silent, so both calls must come back inside a bounded time
	 * with silence in the output planes.
	 */
	void stalledClientCannotHoldTheAudioPeriod()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the stalled-client "
				"bound is UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("stall")}),
			"could not start/connect the stalled fake client");
		QVERIFY2(!plugin.failed(), "the stalled fake client did not complete the handshake");
		QVERIFY(ports.buffers()->initialized());

		auto out = ports.buffers()->output();
		std::fill(out.bufferPtr(0), out.bufferPtr(0) + frames, 1.0f);
		std::fill(out.bufferPtr(1), out.bufferPtr(1) + frames, 1.0f);

		// One period is the budget; the bound below is generous to CI load but far below the
		// fake client's 30 s read timeout, which is how long the unbounded wait used to take.
		const auto bound = std::chrono::milliseconds{500};

		auto started = std::chrono::steady_clock::now();
		QVERIFY2(!plugin.process(), "a period the client never answered reported as processed");
		QVERIFY2(std::chrono::steady_clock::now() - started < bound,
			"process() waited for a client that never answered");

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(out.sample(0, frame), 0.0f);
			QCOMPARE(out.sample(1, frame), 0.0f);
		}

		// The reply is still owed, so the next period is skipped without a new request.
		started = std::chrono::steady_clock::now();
		QVERIFY(!plugin.process());
		QVERIFY2(std::chrono::steady_clock::now() - started < bound,
			"a second period waited again for the client's owed reply");
	}

	/*!
	 * processMidiEvent() never waits for the communication lock.
	 *
	 * The instrument hosts call it from the audio thread, while the main thread may hold
	 * the lock across a whole request/reply exchange. Here another thread holds the lock
	 * for 300 ms; the call must return at once, and the event is not lost.
	 */
	void midiEventDoesNotWaitForTheCommunicationLock()
	{
		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		std::atomic<bool> held{false};
		std::thread holder{[&] {
			plugin.lock();
			held = true;
			std::this_thread::sleep_for(std::chrono::milliseconds{300});
			plugin.unlock();
		}};
		while (!held) { std::this_thread::yield(); }

		const auto started = std::chrono::steady_clock::now();
		plugin.processMidiEvent(MidiEvent(MidiNoteOn, 0, 60, 100), 0);
		const auto elapsed = std::chrono::steady_clock::now() - started;
		holder.join();

		QVERIFY2(elapsed < std::chrono::milliseconds{100},
			"processMidiEvent() waited for the communication lock");
		QCOMPARE(plugin.midiEventsDropped(), std::uint64_t{0});
	}

	/*!
	 * MIDI queued with processMidiEvent() reaches the client before its next period.
	 *
	 * The "midi" fake client reports how many IdMidiEvent messages it received by asking
	 * for one more output than that, in a channel-count request sent during the period.
	 * Three events queued before the period must therefore arrive as 4 outputs, and that
	 * request is applied on the main thread, not inside the period.
	 */
	void queuedMidiEventsReachTheClientBeforeItsPeriod()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the MIDI hand-off "
				"is UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("midi")}),
			"could not start/connect the midi fake client");
		QVERIFY2(!plugin.failed(), "the midi fake client did not complete the handshake");
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{2});

		for (int offset = 0; offset < 3; ++offset)
		{
			plugin.processMidiEvent(MidiEvent(MidiNoteOn, 0, 60 + offset, 100), offset);
		}

		auto out = ports.buffers()->output();
		std::fill(out.bufferPtr(0), out.bufferPtr(0) + frames, 1.0f);
		std::fill(out.bufferPtr(1), out.bufferPtr(1) + frames, 1.0f);

		const ScopedReplyBudget replyInTime;
		QVERIFY(!plugin.process());
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{2});
		QTRY_COMPARE_WITH_TIMEOUT(ports.audioPortsModel().out().channelCount(), ch_cnt_t{4}, 2000);
	}

	/*!
	 * A client that answers after the period was given up cannot reach the instrument.
	 *
	 * The fake client answers its first request 150 ms late and writes its planes only then.
	 * The period is silent (process() returns false), and once the planes have been written
	 * the instrument's output must still be silent: it reads the host's own block, which the
	 * late client never touches.
	 */
	void lateClientCannotReachTheInstrumentOutput()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the late-reply "
				"isolation is UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("late")}),
			"could not start/connect the late fake client");
		QVERIFY2(!plugin.failed(), "the late fake client did not complete the handshake");

		auto out = ports.buffers()->output();
		std::fill(out.bufferPtr(0), out.bufferPtr(0) + frames, 1.0f);
		std::fill(out.bufferPtr(1), out.bufferPtr(1) + frames, 1.0f);

		QVERIFY(!plugin.process());

		// Let the late client write its planes and send its reply, then look again.
		std::this_thread::sleep_for(std::chrono::milliseconds{400});
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(out.sample(0, frame), 0.0f);
			QCOMPARE(out.sample(1, frame), 0.0f);
		}
	}

	/*!
	 * A client that stops in the middle of a message cannot hold the audio period.
	 *
	 * The fake client sends the start of a message, pauses for a second, then finishes it. The
	 * period must give up within its budget, not wait for the rest of the message. The next period
	 * reads the finished message and the reply, and completes.
	 */
	void clientStoppingMidMessageCannotHoldThePeriod()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the mid-message bound "
				"is UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("midframe")}),
			"could not start/connect the midframe fake client");
		QVERIFY2(!plugin.failed(), "the midframe fake client did not complete the handshake");

		const auto bound = std::chrono::milliseconds{500};
		const auto started = std::chrono::steady_clock::now();
		QVERIFY(!plugin.process());
		QVERIFY2(std::chrono::steady_clock::now() - started < bound,
			"process() waited for the rest of a message the client had stopped in");

		// The client finishes its message and reply after its pause; the next period completes.
		std::this_thread::sleep_for(std::chrono::milliseconds{1200});
		const ScopedReplyBudget replyInTime;
		QVERIFY2(plugin.process(), "the period after the client finished its message did not complete");
	}

	/*!
	 * A note-off the full queue refuses is not lost: its channel is sent all-notes-off, after the
	 * events queued before it, and the refusal is counted and reported on the main thread.
	 *
	 * 256 note-ons fill the queue; one note-off on channel 3 is refused. The "silencer" fake client
	 * must receive all 256 note-ons followed by one all-notes-off, and reports exactly that order.
	 */
	void refusedNoteOffBecomesAllNotesOffInOrder()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the refused-note-off "
				"conversion is UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("silencer")}),
			"could not start/connect the silencer fake client");
		QVERIFY2(!plugin.failed(), "the silencer fake client did not complete the handshake");
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{2});

		constexpr int queueCapacity = 256;
		for (int i = 0; i < queueCapacity; ++i)
		{
			plugin.processMidiEvent(MidiEvent(MidiNoteOn, 0, 60, 100), 0);
		}
		QCOMPARE(plugin.midiEventsDropped(), std::uint64_t{0});

		plugin.processMidiEvent(MidiEvent(MidiNoteOff, 3, 60, 0), 0);
		QCOMPARE(plugin.midiEventsDropped(), std::uint64_t{1});

		m_recorder->clear();
		// At least four periods: the drain is capped per period, so the cut comes after the note-ons in the later ones.
		const ScopedReplyBudget replyInTime;
		runPeriodsUntilTheCountMoves(plugin, ports, ch_cnt_t{2});
		QTRY_COMPARE_WITH_TIMEOUT(ports.audioPortsModel().out().channelCount(), ch_cnt_t{3}, 2000);

		// The main-thread pump reports the refusal when it applies the deferred work.
		QVERIFY2(m_recorder->text().contains(QStringLiteral("refused by a full queue")),
			"the refused MIDI event was not reported on the main thread");
	}

	/*!
	 * A reply that comes after its period is drained, and never read as the next period's.
	 *
	 * The "late" client answers its first request 150 ms late, with planes for period 1. Period 2
	 * must send a new request only after the late reply is consumed, and must render period 2's
	 * planes (value 2000 + frame, since the client counts its periods) and not period 1's.
	 */
	void lateReplyIsDrainedAndNeverUsedForTheNextPeriod()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the late-reply drain "
				"is UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("late")}),
			"could not start/connect the late fake client");
		QVERIFY(!plugin.failed());

		QVERIFY(!plugin.process());
		QCOMPARE(plugin.processingOverruns(), std::uint64_t{1});
		std::this_thread::sleep_for(std::chrono::milliseconds{400});

		const ScopedReplyBudget replyInTime;      // period 1 had the real budget, so its reply was late
		QVERIFY2(plugin.process(), "the period after the late reply did not complete");
		QCOMPARE(plugin.processingOverruns(), std::uint64_t{1});

		auto out = ports.buffers()->output();
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(out.sample(0, frame), 2000.0f + static_cast<float>(frame));
			QCOMPARE(out.sample(1, frame), -(2000.0f + static_cast<float>(frame)));
		}
	}

	/*!
	 * The realtime paths allocate nothing in steady state.
	 *
	 * Counted with the allocation probe, on this thread: process() once a client is running
	 * and its replies are in step, processMidiEvent() (a queued event), and
	 * applyDeferredClientRequests() when nothing is pending (the main thread's timer tick).
	 * Replies to a channel-count change, or a resize, are main-thread work and are not counted.
	 */
	void realtimePathsAllocateNothingInSteadyState()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the realtime "
				"allocation check is UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client), "could not start/connect the fake client");
		QVERIFY(!plugin.failed());
		const ScopedReplyBudget replyInTime;

		// Warm-up: let every first-use allocation happen before counting.
		for (int i = 0; i < 3; ++i) { QVERIFY(plugin.process()); }
		plugin.processMidiEvent(MidiEvent(MidiNoteOn, 0, 60, 100), 0);
		QVERIFY(plugin.process());
		plugin.applyDeferredClientRequests();

		const auto processAllocations = countAllocations([&] {
			for (int i = 0; i < 5; ++i) { plugin.process(); }
		});
		QCOMPARE(processAllocations, std::uint64_t{0});

		const auto midiAllocations = countAllocations([&] {
			for (int i = 0; i < 100; ++i)
			{
				plugin.processMidiEvent(MidiEvent(MidiNoteOn, 0, 60 + (i % 12), 100), i);
			}
		});
		QCOMPARE(midiAllocations, std::uint64_t{0});
		QVERIFY(plugin.process()); // drains those 100 events on the audio side (the queue holds them)

		const auto applyAllocations = countAllocations([&] {
			for (int i = 0; i < 100; ++i) { plugin.applyDeferredClientRequests(); }
		});
		QCOMPARE(applyAllocations, std::uint64_t{0});
	}

	/*!
	 * A main-thread request during rendering gets its reply, and the audio period is not held behind it.
	 *
	 * The main thread asks IdIsUIVisible while another thread renders periods. The answer must come back, and no
	 * period may take longer than the wait the main thread holds the communication lock for (periods are
	 * skipped during that wait, never blocked).
	 */
	void uiQueryDuringRenderingDoesNotBlockThePeriod()
	{
		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the main-thread query during "
				"rendering is UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client), "could not start/connect the fake client");
		QVERIFY(!plugin.failed());

		std::atomic<bool> rendering{true};
		std::atomic<long long> slowestPeriodUs{0};
		std::thread renderer{[&] {
			while (rendering)
			{
				const auto started = std::chrono::steady_clock::now();
				plugin.process();
				const auto took = std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - started).count();
				if (took > slowestPeriodUs) { slowestPeriodUs = took; }
				std::this_thread::yield();
			}
		}};

		// Many queries, each while the renderer is running periods: a reply that the renderer takes instead of the
		// main thread shows up as a query that never returns.
		std::this_thread::sleep_for(std::chrono::milliseconds{20});
		int answers = 0;
		std::chrono::steady_clock::duration slowestQuery{};
		for (int query = 0; query < 20; ++query)
		{
			const auto started = std::chrono::steady_clock::now();
			answers += plugin.isUIVisible() == 1 ? 1 : 0;
			slowestQuery = std::max(slowestQuery, std::chrono::steady_clock::now() - started);
		}
		rendering = false;
		renderer.join();

		QCOMPARE(answers, 20);
		QVERIFY2(slowestQuery < std::chrono::seconds{2}, "a main-thread query did not get its reply");
		QVERIFY2(slowestPeriodUs.load() < 200000, "a period was held for the main thread's request");
		QVERIFY(!plugin.failed());
	}

	/*!
	 * A complete message near the one-megabyte window does not fail the plugin.
	 *
	 * The fake client writes a 900 KB debug text whole, before its reply: far above the 16 KB window of the first
	 * version and above any socket buffer, but inside the 1 MB window the audio thread peeks through. The period
	 * cannot see it complete, so it gives up (silence, not a failed plugin). The main thread's wait for its own
	 * reply then reads the message and the reply, and the next period completes.
	 */
	void messageNearTheOneMegabyteWindowDoesNotFailThePlugin()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);
		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the large-message path is "
				"UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("bigdebug")}), "could not start/connect the fake client");
		QVERIFY(!plugin.failed());

		QVERIFY(!plugin.process());
		QVERIFY2(!plugin.failed(), "a complete message near the window size failed the plugin");

		// The main thread reads the big message as part of its own wait.
		const ScopedQueryReplyBudget queryInTime;
		QCOMPARE(plugin.isUIVisible(), 1);
		QVERIFY(!plugin.failed());
		const ScopedReplyBudget replyInTime;
		QVERIFY2(plugin.process(), "the period after the large message did not complete");
	}

	/*!
	 * A note-on queued after a refused note-off sounds: its channel's all-notes-off is sent at the refusal,
	 * before that note-on, not after it.
	 *
	 * The queue holds 256 note-ons; a note-off on channel 3 is refused; then, once the first periods have drained
	 * part of the queue, a note-on on channel 3 is queued. The "orderer" client reports 3 outputs only when a
	 * note-on arrived after the all-notes-off.
	 */
	void noteOnAfterARefusedNoteOffSounds()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);
		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the refusal ordering is "
				"UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("orderer")}), "could not start/connect the orderer client");
		QVERIFY(!plugin.failed());

		for (int i = 0; i < 256; ++i) { plugin.processMidiEvent(MidiEvent(MidiNoteOn, 0, 60, 100), 0); }
		plugin.processMidiEvent(MidiEvent(MidiNoteOff, 3, 60, 0), 0);
		QCOMPARE(plugin.midiEventsDropped(), std::uint64_t{1});

		// Each period's count report is applied before the next, as in refusedNoteOffBecomesAllNotesOffInOrder().
		const ScopedReplyBudget replyInTime;
		QVERIFY(!plugin.process());
		plugin.applyDeferredClientRequests();
		plugin.processMidiEvent(MidiEvent(MidiNoteOn, 3, 64, 100), 0);   // queued after the refusal

		runPeriodsUntilTheCountMoves(plugin, ports, ch_cnt_t{2});
		QTRY_COMPARE_WITH_TIMEOUT(ports.audioPortsModel().out().channelCount(), ch_cnt_t{3}, 2000);
	}

	/*!
	 * The MIDI sent in one period is capped; the rest waits in the queue.
	 *
	 * 256 note-ons are queued. The "capper" client reports, in its next period's request, how many note-ons the
	 * previous period carried, as 2 + (count / 128). A cap of 128 per period gives 3; no cap gives 4.
	 */
	void midiSentPerPeriodIsCapped()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);
		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the per-period cap is "
				"UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("capper")}), "could not start/connect the capper client");
		QVERIFY(!plugin.failed());

		for (int i = 0; i < 256; ++i) { plugin.processMidiEvent(MidiEvent(MidiNoteOn, 0, 60, 100), 0); }
		// The client's second request carries the first's note-on count: 3 with the cap, 4 without it.
		const ScopedReplyBudget replyInTime;
		runPeriodsUntilTheCountMoves(plugin, ports, ch_cnt_t{2});
		QTRY_COMPARE_WITH_TIMEOUT(ports.audioPortsModel().out().channelCount(), ch_cnt_t{3}, 2000);
	}

	/*!
	 * The per-period MIDI cap holds across periods skipped for want of room in the socket.
	 *
	 * The host's send room is shrunk to a few frames, so a period sends a little MIDI and then finds no room: it
	 * is skipped before its request. The client is still in the same period until that request arrives, so the
	 * periods after it may only send what is left of the cap. The "capper" client reports 3 when its period carried
	 * at most 128 note-ons and 4 when it carried more. (Found under machine load on 2026-10-20: a request that found
	 * no room after its period's MIDI let the next period send another 128, and the client got all 256 at once.)
	 */
	void midiCapHoldsAcrossPeriodsSkippedForRoom()
	{
#ifndef __linux__
		QSKIP("the send-room seam needs Linux's queued-bytes count (SIOCOUTQ)");
#endif
		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the cap across skipped periods is "
				"UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("capper")}), "could not start/connect the capper client");
		QVERIFY(!plugin.failed());

		for (int i = 0; i < 256; ++i) { plugin.processMidiEvent(MidiEvent(MidiNoteOn, 0, 60, 100), 0); }
		const ScopedReplyBudget replyInTime;
		{
			const ScopedSendRoom tight{2048};      // a few frames: every period runs out of room part-way
			runPeriodsUntilTheCountMoves(plugin, ports, ch_cnt_t{2});
		}
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{3});
	}

	/*!
	 * A hung client cannot freeze a main-thread query for longer than its bound.
	 *
	 * The "deafquery" client never answers the visibility query. The query returns -1 within
	 * RemotePlugin::QueryReplyTimeout, and the cached visibility stays unknown.
	 */
	void hungQueryCannotFreezeTheMainThread()
	{
		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the bounded query is "
				"UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("deafquery")}), "could not start/connect the fake client");
		QVERIFY(!plugin.failed());

		const auto started = std::chrono::steady_clock::now();
		const int visible = plugin.isUIVisible();
		const auto elapsed = std::chrono::steady_clock::now() - started;

		QCOMPARE(visible, -1);
		QCOMPARE(plugin.cachedUIVisible(), -1);
		QVERIFY2(elapsed < std::chrono::seconds{1}, "a query to a hung client froze the main thread");
	}

	/*!
	 * A visibility answer with no argument in it is refused, not read.
	 *
	 * The "emptyquery" client answers the visibility query with a complete message that carries no argument. The
	 * reply is the awaited one, so it never reaches processMessage(): the host must check it before reading it. The
	 * query returns -1, the cached visibility stays unknown, and the plugin is failed.
	 */
	void emptyQueryAnswerIsRefusedNotRead()
	{
		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the empty answer is "
				"UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("emptyquery")}), "could not start/connect the fake client");
		QVERIFY(!plugin.failed());

		QCOMPARE(plugin.isUIVisible(), -1);
		QCOMPARE(plugin.cachedUIVisible(), -1);
		QVERIFY(plugin.failed());
	}

	/*!
	 * A period before any shared block exists cannot be held by a client stuck mid-message.
	 *
	 * The "halfhandshake" client stops half-way through its first message for two seconds. The plugin is started
	 * without waiting for its handshake, so periods run with no shared block yet. Each must return at once, with
	 * silence, instead of reading the connection.
	 */
	void periodWithoutBufferCannotHoldTheAudioThread()
	{
		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the no-buffer period is "
				"UNEXERCISED by this run");
		}

		const QFileInfo info{client};
		ScopedPluginDir pluginDir{info.absolutePath()};
		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(!plugin.init(info.fileName(), false, {QStringLiteral("halfhandshake")}),
			"could not start the half-handshake client");
		QVERIFY(!plugin.failed());
		QVERIFY(!ports.buffers()->initialized());

		for (int period = 0; period < 3; ++period)
		{
			const auto started = std::chrono::steady_clock::now();
			QVERIFY(!plugin.process());
			QVERIFY2(std::chrono::steady_clock::now() - started < std::chrono::milliseconds{500},
				"a period without a shared block waited for the client");
		}

		// Let the handshake finish, so the plugin can be torn down cleanly.
		plugin.lock();
		plugin.waitForInitDone(false);
		plugin.unlock();
	}

	/*!
	 * A query whose reply came after its timeout does not answer the next query.
	 *
	 * The "latequery" client answers the first visibility query 300 ms late, with "1", and the second at once with
	 * "0". The first query times out (-1); after the late reply has arrived, the second must return "0", not the
	 * late "1".
	 */
	void lateQueryReplyDoesNotAnswerTheNextQuery()
	{
		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the late-reply accounting is "
				"UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("latequery")}), "could not start/connect the fake client");
		QVERIFY(!plugin.failed());

		QCOMPARE(plugin.isUIVisible(), -1);
		std::this_thread::sleep_for(std::chrono::milliseconds{400});
		QCOMPARE(plugin.isUIVisible(), 0);
		QCOMPARE(plugin.cachedUIVisible(), 0);
	}

	/*!
	 * A reply that never arrives does not poison the queries that follow it.
	 *
	 * The "dropquery" client never answers the first visibility query. The query times out (-1). Its count of abandoned
	 * replies expires after RemotePlugin::StaleQueryExpiry, so the next query, whose reply does arrive, returns that
	 * reply. Without the expiry the count stayed and every later query returned -1.
	 */
	void neverArrivingReplyExpiresAndLaterQueriesWork()
	{
		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the expiry of abandoned queries is "
				"UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("dropquery")}), "could not start/connect the fake client");
		QVERIFY(!plugin.failed());

		QCOMPARE(plugin.isUIVisible(), -1);
		std::this_thread::sleep_for(RemotePlugin::StaleQueryExpiry + std::chrono::milliseconds{300});
		QCOMPARE(plugin.isUIVisible(), 1);
		QCOMPARE(plugin.cachedUIVisible(), 1);
	}

	/*!
	 * A handshake completes without any main-thread wait: the deferred timer reads the client's messages while no
	 * shared block exists. The "halfhandshake" client finishes its start-up after two seconds.
	 */
	void preBufferHandshakeCompletesWithoutAMainThreadWait()
	{
		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the pre-buffer pump is "
				"UNEXERCISED by this run");
		}

		const QFileInfo info{client};
		ScopedPluginDir pluginDir{info.absolutePath()};
		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(!plugin.init(info.fileName(), false, {QStringLiteral("halfhandshake")}),
			"could not start the half-handshake client");

		// Only the event loop runs here: the timer is what reads the handshake.
		for (int waited = 0; waited < 6000 && !ports.buffers()->initialized(); waited += 20)
		{
			QTest::qWait(20);
		}
		QVERIFY2(ports.buffers()->initialized(), "the handshake did not complete without a main-thread wait");
		QVERIFY(!plugin.failed());

		plugin.lock();
		plugin.waitForInitDone(false);
		plugin.unlock();
	}

	/*!
	 * A lost connection is reported on the main thread, and fails the plugin.
	 *
	 * The "garbage" client answers the first period with a frame that is not a message. The connection is lost on the
	 * audio thread; the main thread must log it and mark the plugin failed, not leave the instrument silent without a
	 * word.
	 */
	void lostConnectionIsReportedOnTheMainThread()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);
		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the lost-connection report is "
				"UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("garbage")}), "could not start/connect the fake client");
		QVERIFY(!plugin.failed());

		m_recorder->clear();
		const ScopedReplyBudget replyInTime;      // the garbage frame must arrive inside the period to be read
		QVERIFY(!plugin.process());
		plugin.applyDeferredClientRequests();

		QVERIFY2(plugin.failed(), "a lost connection did not fail the plugin");
		QVERIFY2(m_recorder->text().contains(QStringLiteral("connection to the remote plugin client was lost")),
			qPrintable(m_recorder->text()));
	}

	/*!
	 * Re-sizing a remote plugin's buffers holds off rendering.
	 *
	 * Another thread stands in for the render thread by holding the audio engine's change
	 * mutex for 300 ms. A channel-count change on the main thread must wait for it, because
	 * it moves the view pointers the render thread reads.
	 */
	void bufferResizeWaitsForRenderingToFinish()
	{
		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the resize guard is "
				"UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client), "could not start/connect the fake client");
		QVERIFY(!plugin.failed());

		std::atomic<bool> held{false};
		std::atomic<bool> release{false};
		std::thread renderStandIn{[&] {
			Engine::audioEngine()->requestChangeInModel();
			held = true;
			while (!release) { std::this_thread::yield(); }
			std::this_thread::sleep_for(std::chrono::milliseconds{300});
			Engine::audioEngine()->doneChangeInModel();
		}};
		while (!held) { std::this_thread::yield(); }

		const auto started = std::chrono::steady_clock::now();
		release = true;
		ports.audioPortsModel().setChannelCounts(0, 3);
		const auto elapsed = std::chrono::steady_clock::now() - started;
		renderStandIn.join();

		QVERIFY2(elapsed >= std::chrono::milliseconds{250},
			"the buffer resize did not wait for the render thread's change guard");
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{3});
	}

	/*!
	 * A channel-count request sent by the client during a period is not applied on the
	 * audio thread.
	 *
	 * Applying it re-sizes the shared buffer, which allocates and can create shared
	 * memory. The host must keep its layout for the period, render silence, and apply
	 * the new counts on the main thread afterwards.
	 */
	void channelCountRequestDuringPeriodIsAppliedOnTheMainThread()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const QString client = fakeClientExecutable();
		if (client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("the deterministic fake client was not built (POSIX only); the deferred "
				"channel-count path is UNEXERCISED by this run");
		}

		DynamicPlanarPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		QVERIFY2(startClient(plugin, client, {QStringLiteral("recount")}),
			"could not start/connect the recount fake client");
		QVERIFY2(!plugin.failed(), "the recount fake client did not complete the handshake");
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{2});

		auto out = ports.buffers()->output();
		std::fill(out.bufferPtr(0), out.bufferPtr(0) + frames, 1.0f);
		std::fill(out.bufferPtr(1), out.bufferPtr(1) + frames, 1.0f);

		// The client asked for 4 outputs during this period and sent no planes for them.
		const ScopedReplyBudget replyInTime;
		QVERIFY(!plugin.process());
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{2});
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(out.sample(0, frame), 0.0f);
			QCOMPARE(out.sample(1, frame), 0.0f);
		}

		// The main thread applies it: its timer runs every 10 ms, so a short event-loop wait is enough.
		QTRY_COMPARE_WITH_TIMEOUT(ports.audioPortsModel().out().channelCount(), ch_cnt_t{4}, 2000);
	}

	/*!
	 * A shared-memory array is aligned for the element type it holds.
	 *
	 * The remote plugin contract passes shared arrays between the host and the
	 * client (the audio block, the VST-sync block). The array data starts
	 * after the in-band size header, so the data offset has to be a multiple
	 * of the element alignment: an element type that asks for more alignment
	 * than the header provides would otherwise be indexed through a
	 * misaligned pointer, which is undefined behaviour.
	 *
	 * The creator and the attacher have to agree on the offset, so both views
	 * are checked here, together with the data that travels between them.
	 */
	void sharedMemoryArrayIsAlignedForItsElementType()
	{
		struct alignas(16) AlignedElement
		{
			float values[4];
		};

		using Array = SharedMemory<AlignedElement[]>;

		Array owner;
		owner.create(4);
		QVERIFY(owner.get() != nullptr);
		QVERIFY2(reinterpret_cast<std::uintptr_t>(owner.get()) % alignof(AlignedElement) == 0,
			"the shared array data is not aligned for its element type");

		Array attached;
		attached.attach(owner.key());
		QVERIFY(attached.get() != nullptr);
		QVERIFY2(reinterpret_cast<std::uintptr_t>(attached.get()) % alignof(AlignedElement) == 0,
			"the attached shared array data is not aligned for its element type");

		QCOMPARE(owner.size(), std::size_t{4});
		QCOMPARE(attached.size(), std::size_t{4});

		owner[0].values[0] = 1.25f;
		QCOMPARE(attached[0].values[0], 1.25f);
	}

private:
	std::unique_ptr<MessageRecorder> m_recorder;
};

QTEST_GUILESS_MAIN(RemotePluginClientE2ETest)

#include "RemotePluginClientE2ETest.moc"
