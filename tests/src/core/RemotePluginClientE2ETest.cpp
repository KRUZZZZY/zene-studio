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
 * Copyright (c) 2026 LMMS developers
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
#include <cmath>
#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "Engine.h"
#include "MidiEvent.h"
#include "Song.h"
#include "SharedMemory.h"
#include "VstSyncData.h"

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

private:
	std::unique_ptr<MessageRecorder> m_recorder;
};

QTEST_GUILESS_MAIN(RemotePluginClientE2ETest)

#include "RemotePluginClientE2ETest.moc"
