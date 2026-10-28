/*
 * RemotePluginAudioPortsTest.cpp - buffer attach/detach lifecycle of the
 *                                  remote plugin audio-ports controller, and
 *                                  the layout contract of the shared audio
 *                                  block it allocates
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

#include "RemotePluginAudioPorts.h"
#include "RemotePluginBase.h"

#include <QtTest>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <type_traits>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#ifndef SYNC_WITH_SHM_FIFO
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "AllocationProbe.h"
#include "AudioEngine.h"
#include "AudioPortsModel.h"
#include "Engine.h"
#include "OutOfProcessHosting.h"
#include "RemotePlugin.h"

using namespace lmms;

namespace
{

//! Warnings seen while a refusal is processed. Qt's log path allocates through malloc, which the allocation
//! probe does not see, so a warning is counted directly.
int g_warningsSeen = 0;
void countWarnings(QtMsgType type, const QMessageLogContext&, const QString&)
{
	if (type == QtWarningMsg) { ++g_warningsSeen; }
}

#ifndef SYNC_WITH_SHM_FIFO // only the socket-transport cases count criticals
//! Criticals seen while a malformed message is refused, for the same reason as above.
int g_criticalsSeen = 0;
void countCriticals(QtMsgType type, const QMessageLogContext&, const QString&)
{
	if (type == QtCriticalMsg) { ++g_criticalsSeen; }
}
#endif

//! AudioPortsModel is abstract (bufferPropertiesChanging)
class TestAudioPortsModel : public AudioPortsModel
{
public:
	using AudioPortsModel::AudioPortsModel;

protected:
	void bufferPropertiesChanging(ch_cnt_t, ch_cnt_t, f_cnt_t) override {}
};

//! Concrete controller; exposes the protected buffer pointer for assertions
class TestAudioPortsController : public RemotePluginAudioPortsController
{
public:
	explicit TestAudioPortsController(AudioPortsModel& model)
		: RemotePluginAudioPortsController{model}
	{
	}

	void activate(f_cnt_t /*frames*/) override { ++m_activateCalls; }

	auto buffers() const -> RemotePlugin* { return m_buffers; }
	auto activateCalls() const -> int { return m_activateCalls; }

private:
	int m_activateCalls = 0;
};

#ifndef SYNC_WITH_SHM_FIFO
class TestRemotePluginBase final : public RemotePluginBase
{
public:
	explicit TestRemotePluginBase(int socket) : RemotePluginBase()
	{
		m_socket = socket;
	}

	~TestRemotePluginBase() override { ::close(m_socket); }

	bool processMessage(const message&) override { return false; }

	//! Lets a test observe the timeout a receive asks for (and supply the bytes the receive then reads)
	//! without waiting on the wall clock.
	bool waitForMessageReadable(int timeoutMs) override
	{
		if (waitHook) { return waitHook(timeoutMs); }
		return RemotePluginBase::waitForMessageReadable(timeoutMs);
	}

	std::function<bool(int)> waitHook;
	int lastWaitMs = -1;
};
#endif

//! Channel counts that are only known once the remote plugin reports them, i.e.
//! the shape `VestigeAudioPorts`/`VstEffectAudioPorts` use (#589).
constexpr auto DynamicPlanarSettings = AudioPortsSettings {
	.kind = AudioDataKind::F32,
	.interleaved = false
};

//! Real remote-ports implementation, so the shared audio block of a
//! `RemotePlugin` is allocated by exactly the code the plugins use.
class TestRemotePluginAudioPorts final : public RemotePluginAudioPorts<DynamicPlanarSettings>
{
public:
	using RemotePluginAudioPorts<DynamicPlanarSettings>::RemotePluginAudioPorts;

	auto channelName(ch_cnt_t, bool) const -> QString override { return {}; }
};

//! Offset of plane `channel` from the block's base pointer, in samples
constexpr auto planeOffset(ch_cnt_t channel, f_cnt_t frames) -> std::ptrdiff_t
{
	return static_cast<std::ptrdiff_t>(channel) * frames;
}

#ifndef SYNC_WITH_SHM_FIFO
/*
 * A stand-in for a remote plugin *client*, for the two failures that can only be
 * reached with a live client on the other end of the socket: the host's
 * `process()` refuses to run without one, and every wait in it is a wait on the
 * client. The peer is a python3 script because the client half of this protocol
 * has no host-buildable implementation: `RemoteVstPlugin`'s process() needs a
 * real VST library, and the Windows client binaries are CI-only.
 *
 * `RemotePlugin::init()` starts it with the socket path as argv[1]; the test
 * passes the log path and the behaviour mode as extra arguments.
 * Framing is `RemotePluginBase::sendMessage`: int32 message id, int32 argument
 * count, then that many int32-length-prefixed strings.
 *
 * Modes:
 *   "reply"        - answer every period request with IdProcessingDone
 *   "silent"       - read and log everything, never answer
 *   "die-on-start" - exit without answering on the first period request, i.e.
 *                    the client dies mid-period
 *   "no-connect"   - exit at once without ever connecting to the socket
 */
auto writeTestPeer(const QString& dir, const QString& python) -> QString
{
	const auto scriptPath = QDir{dir}.filePath(QStringLiteral("test-peer.py"));
	QFile script{scriptPath};
	if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		return {};
	}

	QByteArray contents = "#!" + python.toUtf8() + "\n";
	contents += R"PY(
import socket, struct, sys, time

# RemoteMessageIDs (RemotePluginBase.h)
ID_QUIT = 3
ID_START_PROCESSING = 9
ID_PROCESSING_DONE = 10
ID_DEBUG_MESSAGE = 25

sock_path, log_path, mode = sys.argv[1], sys.argv[2], sys.argv[3]

if mode == "no-connect":
    # Launched and gone without ever reaching the host socket: the host's
    # poll() for a connection can only time out.
    sys.exit(0)

client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
client.connect(sock_path)
log = open(log_path, "a", buffering=1)

def read_exact(size):
    data = b""
    while len(data) < size:
        chunk = client.recv(size - len(data))
        if not chunk:
            return None
        data += chunk
    return data

def read_int():
    data = read_exact(4)
    return None if data is None else struct.unpack("<i", data)[0]

while True:
    message_id = read_int()
    if message_id is None:
        break
    arguments = read_int()
    if arguments is None:
        break
    for _ in range(arguments):
        length = read_int()
        if length is None:
            break
        if length > 0 and read_exact(length) is None:
            break
    log.write(str(message_id) + "\n")
    if message_id == ID_QUIT:
        break
    if message_id == ID_START_PROCESSING and mode == "die-on-start":
        time.sleep(0.25)  # the host's wait must observe the death, not a reply
        break
    if message_id == ID_START_PROCESSING and mode == "reply":
        client.sendall(struct.pack("<ii", ID_PROCESSING_DONE, 0))
    if message_id == ID_START_PROCESSING and mode == "debugreply":
        # A debug text (longer than the pending slot, so decoding it used to grow a string) and the
        # reply in one write: the audio thread must read the reply that follows it.
        text = b"debug-" + b"x" * 1000
        client.sendall(struct.pack("<iii", ID_DEBUG_MESSAGE, 1, len(text)) + text
                       + struct.pack("<ii", ID_PROCESSING_DONE, 0))
)PY";

	if (script.write(contents) != contents.size())
	{
		return {};
	}
	script.close();

	if (!QFile::setPermissions(scriptPath,
			QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
				| QFileDevice::ReadGroup | QFileDevice::ExeGroup
				| QFileDevice::ReadOther | QFileDevice::ExeOther))
	{
		return {};
	}

	return scriptPath;
}

//! Message ids the test peer logged, in arrival order
auto peerLogIds(const QString& logPath) -> QList<int>
{
	QList<int> ids;
	QFile log{logPath};
	if (!log.open(QIODevice::ReadOnly))
	{
		return ids;
	}
	const auto lines = QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'),
		Qt::SkipEmptyParts);
	for (const auto& line : lines)
	{
		ids.push_back(line.trimmed().toInt());
	}
	return ids;
}
#endif // SYNC_WITH_SHM_FIFO

} // namespace

class RemotePluginAudioPortsTest : public QObject
{
	Q_OBJECT

	//! True when `ProcessWatcher::m_quit` is a `std::atomic<bool>`. Detection
	//! fails closed (false) whenever the member is not visible from here - the
	//! LMMS_TESTING friend seam missing, or the flag not atomic - so the
	//! shutdown-flag race regression fails at run time instead of failing to
	//! compile in both states.
	template<typename T, typename = void>
	struct WatcherQuitFlagIsAtomic : std::false_type {};
	template<typename T>
	struct WatcherQuitFlagIsAtomic<T, std::void_t<decltype(T::m_quit)>>
		: std::is_same<decltype(T::m_quit), std::atomic<bool>> {};

	//! Test seam for the cached running state of a remote plugin (cluster
	//! new-p15-172). The QProcess lives in the watcher thread (init() moves it
	//! there) and the audio thread reads isRunning(), so the running state is
	//! cached in an atomic the process lifecycle updates. Detection fails closed:
	//! without the LMMS_TESTING friend seam - i.e. in the unfixed tree, where
	//! isRunning() reads the QProcess - the cache cannot be steered, so the
	//! regression fails at run time instead of failing to compile.
	template<typename T, typename = void>
	struct CachedRunningState
	{
		//! @return false: there is no cached running state to steer.
		static bool steer(T&, bool) { return false; }
		//! @return false: the QProcess is not reachable from here.
		static bool processRunning(T&) { return false; }
		//! @return false: the process lifecycle cannot be driven from here.
		static bool finishProcess(T&) { return false; }
	};
	template<typename T>
	struct CachedRunningState<T, std::void_t<decltype(T::m_clientRunning)>>
	{
		//! Sets the cached running state, i.e. what the audio thread reads.
		static bool steer(T& _plugin, bool _running)
		{
			_plugin.m_clientRunning.store(_running, std::memory_order_release);
			return true;
		}
		//! The QProcess' own state: what isRunning() used to read from the audio thread.
		static bool processRunning(T& _plugin)
		{
			return _plugin.m_process.state() != QProcess::NotRunning;
		}
		//! Emits the QProcess' finished() signal, i.e. what a client that goes away
		//! does. The host's processFinished() slot is connected to it directly.
		static bool finishProcess(T& _plugin)
		{
			emit _plugin.m_process.finished(0, QProcess::NormalExit);
			return true;
		}
	};

	template<typename T, typename = void>
	struct RemoteInvalidFlagIsAtomic : std::false_type {};
	template<typename T>
	struct RemoteInvalidFlagIsAtomic<T, std::void_t<decltype(T::m_invalid)>>
		: std::is_same<decltype(T::m_invalid), std::atomic<bool>> {};

private slots:
	void initTestCase()
	{
		// The ports model takes its frame count from the audio engine, and the
		// device buffers it allocates are sized from it.
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! `ProcessWatcher::m_quit` is stored by stop()/reset() on the calling
	//! thread while the watcher's run() loop reads it on its own thread.
	//! `volatile` does not synchronize those accesses (a C++ data race, so
	//! shutdown need not be observed reliably); the flag must be an atomic.
	void processWatcherQuitFlagIsAtomic()
	{
		QVERIFY(( WatcherQuitFlagIsAtomic<ProcessWatcher>::value ));
	}

	//! The audio thread must not read the QProcess' state.
	//!
	//! `init()` moves the QProcess into the watcher thread, and `process()` reads
	//! `isRunning()` on the audio thread: `QProcess::state()` is a plain member of
	//! a QObject that lives in another thread, and reading it from the audio thread
	//! is neither synchronized nor allowed. The running state is therefore cached in
	//! an atomic the process lifecycle updates, and `isRunning()` answers from that
	//! cache.
	//!
	//! What is pinned here: `isRunning()` follows the cache, not the QProcess. The
	//! QProcess was never started in this test, so its own state is "not running" -
	//! a QProcess read would answer "not running" even with the cache saying
	//! "running". The seam is the LMMS_TESTING friend declaration in
	//! include/RemotePlugin.h; without it the probe cannot steer the cache and this
	//! test fails at run time (it does not fail to compile).
	void runningStateIsCachedNotReadFromTheProcess()
	{
		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		// The cache is what isRunning() answers: it can be steered at all.
		QVERIFY2(CachedRunningState<RemotePlugin>::steer(plugin, true),
			"the remote plugin does not cache its running state - isRunning() reads the QProcess");

		// The QProcess' own state disagrees with the cache: it was never started.
		QVERIFY(!CachedRunningState<RemotePlugin>::processRunning(plugin));

		// The cache wins: the client counts as running although the QProcess does not.
		QVERIFY(plugin.isRunning());

		// ... and the other way round: the client counts as gone although the
		// QProcess' state was never touched.
		QVERIFY(CachedRunningState<RemotePlugin>::steer(plugin, false));
		QVERIFY(!plugin.isRunning());
	}

	//! The cached running state follows the client's lifecycle.
	//!
	//! The cache is what the audio thread reads, so it has to follow the client: a
	//! client that is there must be asked for periods, and a client that is gone
	//! must not be. The QProcess emits finished() when the client goes away, and
	//! the host's processFinished() slot - connected to it directly - is what
	//! clears the cached state. The signal is emitted here (no client is started
	//! in this test) to drive the same code path a real exit takes, without a
	//! process and without waiting for one.
	void cachedRunningStateFollowsTheProcessLifecycle()
	{
		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		// The client is there: the cached state says so.
		QVERIFY(CachedRunningState<RemotePlugin>::steer(plugin, true));
		QVERIFY(plugin.isRunning());

		// The client goes away: the lifecycle signal clears the cached state.
		QVERIFY(CachedRunningState<RemotePlugin>::finishProcess(plugin));
		QVERIFY(!plugin.isRunning());
	}

	//! The socket reader, audio sender and watcher can invalidate or inspect the
	//! same transport at once, so its connection flag must be atomic.
	void remoteConnectionInvalidFlagIsAtomic()
	{
		QVERIFY(( RemoteInvalidFlagIsAtomic<RemotePluginBase>::value ));
	}

	//! RemotePlugin's ctor attaches its buffers to the ports and its dtor
	//! detaches them again; both are the controller's documented entry points.
	void ConnectAndDisconnectBuffers()
	{
		TestAudioPortsModel model{false};
		TestAudioPortsController controller{model};

		QCOMPARE(controller.buffers(), nullptr);
		QCOMPARE(&controller.audioPortsModel(), static_cast<AudioPortsModel*>(&model));

		{
			RemotePlugin plugin{controller};
			QCOMPARE(controller.buffers(), &plugin);
		}
		QCOMPARE(controller.buffers(), nullptr);

		controller.activate(512);
		QCOMPARE(controller.activateCalls(), 1);
	}

	/**
	 * The shared audio block layout `RemotePlugin::updateAudioBuffer()` and
	 * `RemotePluginClient::doProcessing()` both speak (#589):
	 *
	 *   - it is one flat array of exactly (channelsIn + channelsOut) * frames floats;
	 *   - it is channel-major planar: `channelsIn` blocks of `frames` floats,
	 *     then `channelsOut` blocks of `frames` floats;
	 *   - the ports model's channel counts are the counts it was allocated for;
	 *   - the input and output views are disjoint.
	 *
	 * Reading that block as interleaved sample frames - what the retired
	 * pre-migration code did - is the regression this test exists to catch.
	 */
	void updateAudioBufferLayout()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		constexpr auto channelsIn = ch_cnt_t{3};
		constexpr auto channelsOut = ch_cnt_t{2};

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		// Nothing is allocated until the ports learn the channel counts and are
		// activated (see RemotePlugin::waitForInitDone()).
		QCOMPARE(ports.buffers()->initialized(), false);
		QCOMPARE(ports.active(), false);

		ports.audioPortsModel().setChannelCounts(channelsIn, channelsOut);

		// The model reports the counts the block was allocated for
		QCOMPARE(ports.audioPortsModel().in().channelCount(), channelsIn);
		QCOMPARE(ports.audioPortsModel().out().channelCount(), channelsOut);
		QCOMPARE(ports.active(), true);
		QCOMPARE(ports.buffers()->initialized(), true);
		QCOMPARE(ports.buffers()->frames(), frames);

		auto in = ports.buffers()->input();
		auto out = ports.buffers()->output();
		QCOMPARE(in.channels(), channelsIn);
		QCOMPARE(out.channels(), channelsOut);
		QCOMPARE(in.frames(), frames);
		QCOMPARE(out.frames(), frames);

		// Every plane of a side is exactly `frames` floats after the previous one
		for (ch_cnt_t channel = 1; channel < channelsIn; ++channel)
		{
			QCOMPARE(in.bufferPtr(channel) - in.bufferPtr(channel - 1), frames);
		}
		for (ch_cnt_t channel = 1; channel < channelsOut; ++channel)
		{
			QCOMPARE(out.bufferPtr(channel) - out.bufferPtr(channel - 1), frames);
		}

		// The output planes start directly after all input planes: no gap, no overlap
		QCOMPARE(out.bufferPtr(0) - in.bufferPtr(0), planeOffset(channelsIn, frames));
		QVERIFY(in.bufferPtr(channelsIn - 1) + frames <= out.bufferPtr(0));

		// A repeat call with the same arguments reuses the block and hands back
		// its base pointer, which is where the input planes start
		auto* base = plugin.updateAudioBuffer(channelsIn, channelsOut, frames);
		QCOMPARE(base, in.bufferPtr(0));
		QCOMPARE(base + planeOffset(channelsIn, frames), out.bufferPtr(0));

		// The last output plane ends exactly where the block ends
		QCOMPARE(out.bufferPtr(channelsOut - 1) + frames,
			base + planeOffset(channelsIn + channelsOut, frames));

		// A write at a known offset lands in the expected channel and frame
		in.bufferPtr(2)[5] = 42.0f;
		out.bufferPtr(1)[7] = -7.0f;
		QCOMPARE(in.sample(2, 5), 42.0f);
		QCOMPARE(out.sample(1, 7), -7.0f);

		// ... and the views alias the shared block, not a copy of it
		QCOMPARE(base[2 * frames + 5], 42.0f);
		QCOMPARE(base[planeOffset(channelsIn + 1, frames) + 7], -7.0f);

		// The client side computes its output pointer the same way that the host
		// laid the block out (RemotePluginClient::doProcessing()):
		//     out = shm + inputCount * bufferSize
		const auto clientOutputOffset = planeOffset(channelsIn, frames);
		QCOMPARE(out.bufferPtr(0) - base, clientOutputOffset);
	}

	//! A channel-count change reallocates the block at the new size and the
	//! ports' views follow it; a stale view would read freed shared memory.
	void updateAudioBufferFollowsChannelCountChange()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		ports.audioPortsModel().setChannelCounts(4, 2);

		auto in = ports.buffers()->input();
		auto out = ports.buffers()->output();
		QCOMPARE(in.channels(), ch_cnt_t{4});
		QCOMPARE(out.channels(), ch_cnt_t{2});
		QCOMPARE(out.bufferPtr(0) - in.bufferPtr(0), planeOffset(4, frames));

		ports.audioPortsModel().setChannelCounts(1, 1);

		auto newIn = ports.buffers()->input();
		auto newOut = ports.buffers()->output();
		QCOMPARE(ports.audioPortsModel().in().channelCount(), ch_cnt_t{1});
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{1});
		QCOMPARE(newIn.channels(), ch_cnt_t{1});
		QCOMPARE(newOut.channels(), ch_cnt_t{1});

		// 1 input plane + 1 output plane, still channel-major planar
		QCOMPARE(newOut.bufferPtr(0) - newIn.bufferPtr(0), planeOffset(1, frames));

		// ... and the new block is really writable
		newOut.bufferPtr(0)[frames - 1] = 1.5f;
		QCOMPARE(newOut.sample(0, frames - 1), 1.5f);
	}

	//! A plugin that never initialized has no remote client, so process() must
	//! report failure and render silence: it zeroes the shared output planes,
	//! which is what the pre-migration path did to the caller's output buffer.
	void processZeroesOutputsWhenNotRunning()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		constexpr auto channelsOut = ch_cnt_t{2};

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		ports.audioPortsModel().setChannelCounts(channelsOut, channelsOut);

		auto out = ports.buffers()->output();
		QCOMPARE(out.channels(), channelsOut);
		std::fill(out.bufferPtr(0), out.bufferPtr(0) + frames, 1.0f);
		std::fill(out.bufferPtr(1), out.bufferPtr(1) + frames, 1.0f);

		QCOMPARE(plugin.process(), false);

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(out.sample(0, frame), 0.0f);
			QCOMPARE(out.sample(1, frame), 0.0f);
		}
	}

	//! The message a real client sends resolves to a resized shared block: this
	//! is the path RemotePluginClient::setInputOutputCount() drives over the
	//! socket, and the one that used to resize the legacy interleaved buffer.
	void clientChannelCountMessageResizesSharedBuffer()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		QCOMPARE(ports.buffers()->initialized(), false);

		plugin.processMessage(RemotePlugin::message(IdChangeInputOutputCount).addInt(3).addInt(2));

		// The ports model got the counts and the block was allocated for them
		QCOMPARE(ports.audioPortsModel().in().channelCount(), ch_cnt_t{3});
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{2});
		QCOMPARE(ports.buffers()->initialized(), true);
		QCOMPARE(ports.buffers()->frames(), frames);

		auto in = ports.buffers()->input();
		auto out = ports.buffers()->output();
		QCOMPARE(in.channels(), ch_cnt_t{3});
		QCOMPARE(out.channels(), ch_cnt_t{2});
		QCOMPARE(out.bufferPtr(0) - in.bufferPtr(0), planeOffset(3, frames));
	}

	//! Version skew: the two retired single-count ids (#589) must be refused, not
	//! honoured. A client old enough to send them speaks the interleaved layout,
	//! so acting on the request would leave the two sides disagreeing about the
	//! shared block - the buffer must stay exactly as it was.
	//! NOTE: the plugin cannot be put into the "not failed" state without a real
	//! client process, so what this pins is the refusal (no resize, no state
	//! change) and the silence that process() then renders; the qCritical() the
	//! host logs on this path is the diagnostic.
	void removedCountMessagesAreRefused()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		constexpr auto channelsIn = ch_cnt_t{2};
		constexpr auto channelsOut = ch_cnt_t{2};

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		ports.audioPortsModel().setChannelCounts(channelsIn, channelsOut);

		auto inBefore = ports.buffers()->input();
		auto outBefore = ports.buffers()->output();
		QCOMPARE(ports.buffers()->frames(), frames);
		std::fill(outBefore.bufferPtr(0), outBefore.bufferPtr(0) + frames, 1.0f);

		plugin.processMessage(RemotePlugin::message(IdChangeInputCount).addInt(7));

		// Refused: neither the counts nor the allocation moved
		QCOMPARE(ports.audioPortsModel().in().channelCount(), channelsIn);
		QCOMPARE(ports.audioPortsModel().out().channelCount(), channelsOut);
		QCOMPARE(ports.buffers()->frames(), frames);
		QCOMPARE(ports.buffers()->input().bufferPtr(0), inBefore.bufferPtr(0));
		QCOMPARE(ports.buffers()->output().bufferPtr(0), outBefore.bufferPtr(0));

		// And the plugin renders silence instead of reading the block with the
		// wrong layout
		QCOMPARE(plugin.process(), false);
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			QCOMPARE(outBefore.sample(0, frame), 0.0f);
		}
	}

	//! A client reports its channel counts as signed ints on the wire, and the
	//! host cast them straight to the unsigned `ch_cnt_t`: a negative count
	//! wrapped (65535 for -1) and an oversized one was accepted, both sizing the
	//! shared block from a value no client can have. Out-of-range counts must be
	//! refused at this boundary - the counts and the allocation stay as they
	//! were.
	void outOfRangeChannelCountMessagesAreRefused()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		constexpr auto channelsIn = ch_cnt_t{2};
		constexpr auto channelsOut = ch_cnt_t{2};

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		ports.audioPortsModel().setChannelCounts(channelsIn, channelsOut);
		QCOMPARE(ports.buffers()->initialized(), true);

		auto inBefore = ports.buffers()->input();
		auto outBefore = ports.buffers()->output();

		// Negative: -1 and -8 would wrap to 65535 and 65528
		plugin.processMessage(
			RemotePlugin::message(IdChangeInputOutputCount).addInt(-1).addInt(2));
		QCOMPARE(ports.audioPortsModel().in().channelCount(), channelsIn);
		QCOMPARE(ports.audioPortsModel().out().channelCount(), channelsOut);

		plugin.processMessage(
			RemotePlugin::message(IdChangeInputOutputCount).addInt(-8).addInt(2));
		QCOMPARE(ports.audioPortsModel().in().channelCount(), channelsIn);

		// Oversized: far above MaxChannelsPerAudioBuffer, and the value that
		// wraps an int into positive range when cast
		plugin.processMessage(
			RemotePlugin::message(IdChangeInputOutputCount).addInt(100000).addInt(2));
		QCOMPARE(ports.audioPortsModel().in().channelCount(), channelsIn);

		plugin.processMessage(
			RemotePlugin::message(IdChangeInputOutputCount).addInt(2).addInt(100000));
		QCOMPARE(ports.audioPortsModel().out().channelCount(), channelsOut);

		// Refused all the way: neither the counts nor the block moved
		QCOMPARE(ports.buffers()->initialized(), true);
		QCOMPARE(ports.buffers()->input().bufferPtr(0), inBefore.bufferPtr(0));
		QCOMPARE(ports.buffers()->output().bufferPtr(0), outBefore.bufferPtr(0));
		QCOMPARE(ports.buffers()->input().channels(), channelsIn);
		QCOMPARE(ports.buffers()->output().channels(), channelsOut);
	}

	//! processMessage can run on the audio thread, so a refused count must not log there: a log allocates and
	//! locks. Counted on this thread, refusing a negative and an oversized count allocates nothing.
	void refusedChannelCountsAllocateNothing()
	{
		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		ports.audioPortsModel().setChannelCounts(ch_cnt_t{2}, ch_cnt_t{2});
		QCOMPARE(ports.buffers()->initialized(), true);
		const auto negative = RemotePlugin::message(IdChangeInputOutputCount).addInt(-1).addInt(2);
		const auto oversized = RemotePlugin::message(IdChangeInputOutputCount).addInt(2).addInt(100000);

		const QtMessageHandler previous = qInstallMessageHandler(countWarnings);
		g_warningsSeen = 0;
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		plugin.processMessage(negative);
		plugin.processMessage(oversized);
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		qInstallMessageHandler(previous);

		QCOMPARE(allocations, std::uint64_t{0});
		QCOMPARE(g_warningsSeen, 0);
		QCOMPARE(ports.audioPortsModel().in().channelCount(), ch_cnt_t{2});
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{2});
	}

	//! The refusal is not lost: the main-thread side writes the warning once, and nothing is left to report after.
	void refusedChannelCountIsReportedOnceFromTheMainThread()
	{
		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};
		plugin.processMessage(RemotePlugin::message(IdChangeInputOutputCount).addInt(-1).addInt(2));
		QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("out-of-range channel count")));
		QVERIFY(plugin.reportRefusedChannelCounts());
		QVERIFY(!plugin.reportRefusedChannelCounts());
	}

	//! A reallocation that fails must leave the buffers reporting NOT
	//! initialized. `initialized()` is `m_frames != 0`, so a stale frame count
	//! keeps the ports "active" while the pointer table is null - the router's
	//! send would then dereference a null pointer table. `updateAudioBuffer()`
	//! returns nullptr both for counts it refuses (the ports model allows
	//! channelsOut == 0, but not 0/0) and for a failed shared-memory allocation;
	//! both share the branch this pins.
	void failedReallocationLeavesBuffersInactive()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		auto* buffers = ports.buffers();

		// A successful allocation first, so there is state to go stale
		buffers->updateBuffers(2, 2, frames);
		QCOMPARE(buffers->initialized(), true);
		QCOMPARE(buffers->frames(), frames);

		// The refused reallocation (0/0 counts; any nullptr from
		// updateAudioBuffer() takes this path)
		buffers->updateBuffers(0, 0, frames);

		QCOMPARE(buffers->initialized(), false);
		QCOMPARE(buffers->frames(), f_cnt_t{0});
	}

#ifndef SYNC_WITH_SHM_FIFO
	//! A peer cannot make the host treat a count larger than a full FIFO frame
	//! as a message, even when it closes before sending any argument payload.
	void oversizedArgumentCountInvalidatesRemoteMessage()
	{
		int sockets[2];
		QVERIFY(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

		{
			TestRemotePluginBase receiver{sockets[0]};
			constexpr auto maxMessageArguments = (512 * 1024 - 8) / 4;
			const std::int32_t header[] = {IdDebugMessage, maxMessageArguments + 1};
			QVERIFY(::write(sockets[1], header, sizeof(header)) ==
				static_cast<ssize_t>(sizeof(header)));
			QVERIFY(::shutdown(sockets[1], SHUT_WR) == 0);

			const auto malformed = receiver.receiveMessage();
			QVERIFY(receiver.isInvalid());
			QCOMPARE(malformed.id, static_cast<int>(IdUndefined));
		}

		::close(sockets[1]);
	}

	//! The bounded socket decoder must enforce the same argument-count cap as the owning socket and FIFO readers.
	void boundedReceiveRejectsOversizedArgumentCount()
	{
		int sockets[2];
		QVERIFY(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

		{
			TestRemotePluginBase receiver{sockets[0]};
			constexpr int maxMessageArguments = (512 * 1024 - 8) / 4;
			constexpr std::int32_t count = maxMessageArguments + 1;
			constexpr std::size_t frameSize = 2 * sizeof(std::int32_t)
				+ static_cast<std::size_t>(count) * sizeof(std::int32_t);
			int socketBufferSize = 2 * 1024 * 1024;
			QVERIFY(::setsockopt(sockets[0], SOL_SOCKET, SO_RCVBUF,
				&socketBufferSize, sizeof(socketBufferSize)) == 0);
			QVERIFY(::setsockopt(sockets[1], SOL_SOCKET, SO_SNDBUF,
				&socketBufferSize, sizeof(socketBufferSize)) == 0);

			std::vector<char> frame(frameSize, '\0'); // every zero-length argument has a zero length prefix
			const std::int32_t header[] = {IdDebugMessage, count};
			std::memcpy(frame.data(), header, sizeof(header));
			QVERIFY(::write(sockets[1], frame.data(), frame.size()) == static_cast<ssize_t>(frame.size()));

			RemotePluginBase::message message;
			const auto result = receiver.receiveFrameBefore(std::chrono::steady_clock::now(), message, true);
			QVERIFY(result == RemotePluginBase::ReceiveResult::Lost);
			QVERIFY(receiver.isInvalid());
		}

		::close(sockets[1]);
	}

	//! A frame that has started can be finished slowly, but never past the query's own deadline: a 100 ms
	//! query that sees a partial message must not wait up to StartedFrameTimeout (2 s) for the rest.
	//! The receive asks waitForMessageReadable() for each piece; the hook records that bound and reports
	//! that the rest never arrives, so no wall clock is involved.
	void startedFrameWaitIsCappedByTheQueryDeadline()
	{
		int sockets[2];
		QVERIFY(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

		{
			TestRemotePluginBase receiver{sockets[0]};
			// Only the header is on the socket: the peek can tell that a frame started, but not its size.
			const std::int32_t header[] = {IdDebugMessage, 1};
			QVERIFY(::write(sockets[1], header, sizeof(header)) == static_cast<ssize_t>(sizeof(header)));

			receiver.waitHook = [&receiver](int timeoutMs)
			{
				if (timeoutMs == 0) { return true; }
				receiver.lastWaitMs = timeoutMs;
				return false; // the rest of the frame never arrives
			};

			const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{200};
			RemotePluginBase::message m;
			const auto result = receiver.receiveWithin(deadline, RemotePluginBase::StartedFrameTimeout, m);

			QVERIFY(result == RemotePluginBase::ReceiveResult::Lost);
			QVERIFY(receiver.isInvalid());
			// The old reader asked for the whole 2 s started-frame bound; the query budget is 200 ms.
			QVERIFY2(receiver.lastWaitMs < 1000,
				"a started frame ignored the query deadline and asked to wait up to 2 s");
		}

		::close(sockets[1]);
	}

	//! A partial frame must not make the host allocate a string the wire length claims before the payload
	//! has arrived: one argument of 8 MiB is refused as soon as its length exceeds what a frame can hold.
	//! The hook supplies the length only after the peek has seen the 8-byte header, so the buggy path really
	//! reaches the allocation, and it stops before any payload read.
	void oversizedStartedFrameLengthIsRefusedBeforeAllocating()
	{
		int sockets[2];
		QVERIFY(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

		{
			TestRemotePluginBase receiver{sockets[0]};
			const std::int32_t header[] = {IdDebugMessage, 1};
			QVERIFY(::write(sockets[1], header, sizeof(header)) == static_cast<ssize_t>(sizeof(header)));

			constexpr std::int32_t claimedLength = 8 * 1024 * 1024;
			int positiveWaits = 0;
			receiver.waitHook = [&sockets, &positiveWaits](int timeoutMs)
			{
				if (timeoutMs == 0) { return true; }
				++positiveWaits;
				if (positiveWaits == 3)
				{
					// The receive is about to read the argument length: give it the oversized one.
					const std::int32_t length = claimedLength;
					return ::write(sockets[1], &length, sizeof(length))
						== static_cast<ssize_t>(sizeof(length));
				}
				return positiveWaits < 4; // the payload never arrives
			};

			lmms::test::tlCountAllocations = true;
			lmms::test::resetAllocationCount();
			RemotePluginBase::message m;
			const auto result = receiver.receiveWithin(
				std::chrono::steady_clock::now() + std::chrono::seconds{1},
				std::chrono::milliseconds{50}, m);
			const auto allocations = lmms::test::tlAllocationCount;
			lmms::test::tlCountAllocations = false;

			QVERIFY(result == RemotePluginBase::ReceiveResult::Lost);
			QVERIFY(receiver.isInvalid());
			QCOMPARE(allocations, std::uint64_t{0});
		}

		::close(sockets[1]);
	}

#endif

#ifndef SYNC_WITH_SHM_FIFO
	//! A client executable that launches but never connects to the host socket
	//! is not a start: `oop.*` reports starts, and the crash-loop refusal is
	//! measured against them, so recording one for a client that never reached
	//! the host invents history the session did not have. The record must stay
	//! untouched when the poll for a connection times out.
	//! Costs the poll() timeout in init(), hence the single case here.
	void clientThatNeverConnectsIsNotRecordedAsAStart()
	{
		const auto python = QStandardPaths::findExecutable(QStringLiteral("python3"));
		if (python.isEmpty())
		{
			QSKIP("the client stand-in needs python3 for the unix-socket protocol");
		}

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto peer = writeTestPeer(dir.path(), python);
		QVERIFY(!peer.isEmpty());
		const auto logPath = dir.filePath(QStringLiteral("peer.log"));
		const auto client = QFileInfo{peer}.fileName();

		auto& tracker = oop::HostTracker::instance();
		tracker.resetAll();

		{
			TestRemotePluginAudioPorts ports{false, nullptr};
			RemotePlugin plugin{ports.controller()};

			// The peer exits at once without connecting: poll() times out and
			// no client is on the other end of the socket.
			plugin.init(peer, false, {logPath, QStringLiteral("no-connect")});
			QVERIFY(!plugin.isRunning());
		}

		QCOMPARE(tracker.record(client).starts, 0);
		tracker.resetAll();
	}

	//! The client dies mid-period: the wait for IdProcessingDone comes back
	//! without a reply, and discarding that result made process() report success
	//! on planes the client may have written only partially - which the router
	//! then mixed. It must report failure and leave silence instead.
	void abandonedWaitLeavesSilenceAndReportsFailure()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const auto python = QStandardPaths::findExecutable(QStringLiteral("python3"));
		if (python.isEmpty())
		{
			QSKIP("the client stand-in needs python3 for the unix-socket protocol");
		}

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto peer = writeTestPeer(dir.path(), python);
		QVERIFY(!peer.isEmpty());
		const auto logPath = dir.filePath(QStringLiteral("peer.log"));

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		// The host starts the peer and it connects straight away, which is what
		// init()'s accept() waits for; a false return means no failure was
		// reported.
		QVERIFY2(plugin.init(peer, false, {logPath, QStringLiteral("die-on-start")}) == false,
			"the host started a client that connects");
		if (!plugin.isRunning())
		{
			QSKIP("the client stand-in did not start");
		}

		// Allocate the block the client would write its period into
		plugin.processMessage(RemotePlugin::message(IdChangeInputOutputCount).addInt(2).addInt(2));

		auto out = ports.buffers()->output();
		QCOMPARE(out.channels(), ch_cnt_t{2});
		QCOMPARE(out.frames(), frames);

		// Plant data in the output planes: a period the client abandoned must
		// not leak the planes' previous content into the mix.
		for (ch_cnt_t channel = 0; channel < out.channels(); ++channel)
		{
			std::fill(out.bufferPtr(channel), out.bufferPtr(channel) + frames, 1.0f);
		}

		QCOMPARE(plugin.process(), false);

		for (ch_cnt_t channel = 0; channel < out.channels(); ++channel)
		{
			for (f_cnt_t frame = 0; frame < frames; ++frame)
			{
				QCOMPARE(out.sample(channel, frame), 0.0f);
			}
		}

		// The peer logged the period request before dying, so what was exercised
		// is the abandoned wait - not process()'s "not running" guard.
		QVERIFY(peerLogIds(logPath).contains(IdStartProcessing));
	}

	//! Positive control for the check above: a client that answers
	//! IdProcessingDone still makes process() report success.
	void repliedPeriodReportsSuccess()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const auto python = QStandardPaths::findExecutable(QStringLiteral("python3"));
		if (python.isEmpty())
		{
			QSKIP("the client stand-in needs python3 for the unix-socket protocol");
		}

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto peer = writeTestPeer(dir.path(), python);
		QVERIFY(!peer.isEmpty());
		const auto logPath = dir.filePath(QStringLiteral("peer.log"));

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		QVERIFY2(plugin.init(peer, false, {logPath, QStringLiteral("reply")}) == false,
			"the host started a client that connects");
		if (!plugin.isRunning())
		{
			QSKIP("the client stand-in did not start");
		}

		plugin.processMessage(RemotePlugin::message(IdChangeInputOutputCount).addInt(2).addInt(2));

		QCOMPARE(plugin.process(), true);
	}

	//! The audio-thread receive of a complete debug frame builds argument views, not strings: decoding
	//! it allocates nothing and the text is still exactly the wire bytes. No peer or deadline is involved.
	void completeDebugFrameDecodesWithoutAllocating()
	{
		int sockets[2];
		QVERIFY(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

		{
			TestRemotePluginBase receiver{sockets[0]};
			const std::string text(1000, 'x');
			const std::int32_t header[] = {IdDebugMessage, 1, static_cast<std::int32_t>(text.size())};
			QVERIFY(::write(sockets[1], header, sizeof(header)) == static_cast<ssize_t>(sizeof(header)));
			QVERIFY(::write(sockets[1], text.data(), text.size()) == static_cast<ssize_t>(text.size()));

			lmms::test::tlCountAllocations = true;
			lmms::test::resetAllocationCount();
			RemotePluginBase::message m;
			const auto result = receiver.receiveFrameBefore(
				std::chrono::steady_clock::now() + std::chrono::seconds{1}, m, true);
			const auto allocations = lmms::test::tlAllocationCount;
			lmms::test::tlCountAllocations = false;

			QVERIFY(result == RemotePluginBase::ReceiveResult::Got);
			QCOMPARE(m.id, static_cast<int>(IdDebugMessage));
			QCOMPARE(m.size(), std::size_t{1});
			QVERIFY(m.argumentView(0) == std::string_view{text});
			QCOMPARE(allocations, std::uint64_t{0});
		}

		::close(sockets[1]);
	}

	//! A client can send a debug text and then its IdProcessingDone during a period. The audio thread
	//! reads both, but decoding the text must not allocate: the arguments are views into a preallocated
	//! frame buffer. The peer writes both frames in one go, so the period really consumes the debug
	//! message before its reply.
	void debugMessageDuringPeriodDoesNotAllocate()
	{
		const auto python = QStandardPaths::findExecutable(QStringLiteral("python3"));
		if (python.isEmpty())
		{
			QSKIP("the client stand-in needs python3 for the unix-socket protocol");
		}

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto peer = writeTestPeer(dir.path(), python);
		QVERIFY(!peer.isEmpty());
		const auto logPath = dir.filePath(QStringLiteral("peer.log"));

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		QVERIFY2(plugin.init(peer, false, {logPath, QStringLiteral("debugreply")}) == false,
			"the host started a client that connects");
		if (!plugin.isRunning())
		{
			QSKIP("the client stand-in did not start");
		}

		plugin.processMessage(RemotePlugin::message(IdChangeInputOutputCount).addInt(2).addInt(2));

		// The first period may allocate for one-time setup; the measured one must not.
		QCOMPARE(plugin.process(), true);

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		const bool processed = plugin.process();
		const auto allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;

		QVERIFY2(processed, "the period did not consume the debug message and its reply");
		QCOMPARE(allocations, std::uint64_t{0});
	}

	//! Zero output channels: the ports model accepts channelsOut == 0, so the
	//! output span is empty and every period returns false. The period request
	//! must not be sent at all - its reply would never be read, so the client's
	//! replies would pile up in the socket until a send blocked (on the audio
	//! thread), and a refused send after a false return would also skew the
	//! protocol by one period.
	void zeroOutputPluginNeverRequestsAPeriod()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const auto python = QStandardPaths::findExecutable(QStringLiteral("python3"));
		if (python.isEmpty())
		{
			QSKIP("the client stand-in needs python3 for the unix-socket protocol");
		}

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto peer = writeTestPeer(dir.path(), python);
		QVERIFY(!peer.isEmpty());
		const auto logPath = dir.filePath(QStringLiteral("peer.log"));

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		QVERIFY2(plugin.init(peer, false, {logPath, QStringLiteral("silent")}) == false,
			"the host started a client that connects");
		if (!plugin.isRunning())
		{
			QSKIP("the client stand-in did not start");
		}

		// The client reports two inputs and no outputs: a 2-plane block whose
		// output span is empty.
		plugin.processMessage(RemotePlugin::message(IdChangeInputOutputCount).addInt(2).addInt(0));
		QCOMPARE(ports.audioPortsModel().in().channelCount(), ch_cnt_t{2});
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{0});
		QCOMPARE(ports.buffers()->initialized(), true);

		for (int period = 0; period < 3; ++period)
		{
			QCOMPARE(plugin.process(), false);
		}

		// Give the peer time to receive anything that was sent, then check it
		// saw no period request - while proving it did see the messages sent
		// before it, so an empty log cannot pass for "nothing was sent".
		QTest::qWait(250);
		const auto ids = peerLogIds(logPath);
		QVERIFY(ids.contains(IdSyncKey));
		QVERIFY(ids.contains(IdChangeSharedMemoryKey));
		QVERIFY(!ids.contains(IdStartProcessing));
	}

	//! The wire format carries the argument count with the message, so a structurally
	//! valid (complete) message can still arrive carrying fewer arguments than the
	//! handler reads: message::getInt() indexes its vector without bounds-checking, so
	//! reading argument 1 out of a one-argument IdChangeInputOutputCount is undefined
	//! behaviour - it returns whatever sits after the message's arguments (garbage
	//! counts, or a crash). The host must refuse the incomplete report BEFORE indexing:
	//! nothing is applied, the refusal is reported loudly, and the plugin is failed like
	//! any other protocol mismatch - the client may already lay its own planes out for
	//! counts the host never applied.
	//! The "not failed" precondition needs a real client, hence the stand-in peer.
	void channelCountMessageMissingAnArgumentIsRefused()
	{
		const auto python = QStandardPaths::findExecutable(QStringLiteral("python3"));
		if (python.isEmpty())
		{
			QSKIP("the client stand-in needs python3 for the unix-socket protocol");
		}

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto peer = writeTestPeer(dir.path(), python);
		QVERIFY(!peer.isEmpty());
		const auto logPath = dir.filePath(QStringLiteral("peer.log"));

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		QVERIFY2(plugin.init(peer, false, {logPath, QStringLiteral("silent")}) == false,
			"the host started a client that connects");
		if (!plugin.isRunning())
		{
			QSKIP("the client stand-in did not start");
		}
		QVERIFY(!plugin.failed());

		ports.audioPortsModel().setChannelCounts(ch_cnt_t{2}, ch_cnt_t{2});
		QVERIFY(ports.buffers()->initialized());
		auto inBefore = ports.buffers()->input();
		auto outBefore = ports.buffers()->output();

		// The incomplete message: one argument where the protocol has two.
		g_criticalsSeen = 0;
		const QtMessageHandler previous = qInstallMessageHandler(countCriticals);
		plugin.processMessage(RemotePlugin::message(IdChangeInputOutputCount).addInt(2));
		qInstallMessageHandler(previous);

		// Refused loudly, once, on the thread that took the message...
		QCOMPARE(g_criticalsSeen, 1);
		// ... with the plugin failed, exactly like any other protocol mismatch ...
		QVERIFY(plugin.failed());
		// ... and the message never indexed: the counts and the block did not move.
		QCOMPARE(ports.audioPortsModel().in().channelCount(), ch_cnt_t{2});
		QCOMPARE(ports.audioPortsModel().out().channelCount(), ch_cnt_t{2});
		QCOMPARE(ports.buffers()->input().bufferPtr(0), inBefore.bufferPtr(0));
		QCOMPARE(ports.buffers()->output().bufferPtr(0), outBefore.bufferPtr(0));
	}

	//! A complete IdDebugMessage with no text in it. The host's handler indexed argument 0 of an empty message.
	void zeroArgumentDebugMessageIsRefused()
	{
		refusesMessageWithoutArguments(RemotePlugin::message(IdDebugMessage));
	}

	//! A complete IdIsUIVisible with no answer in it. The host's handler indexed argument 0 of an empty message.
	void zeroArgumentIsUIVisibleMessageIsRefused()
	{
		refusesMessageWithoutArguments(RemotePlugin::message(IdIsUIVisible));
	}

private:
	//! A message that is complete on the wire but carries fewer arguments than its handler reads is refused before
	//! anything is read: the refusal is reported once (here, on the main thread) and the plugin is failed.
	void refusesMessageWithoutArguments(const RemotePlugin::message& _m)
	{
		const auto python = QStandardPaths::findExecutable(QStringLiteral("python3"));
		if (python.isEmpty())
		{
			QSKIP("the client stand-in needs python3 for the unix-socket protocol");
		}

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto peer = writeTestPeer(dir.path(), python);
		QVERIFY(!peer.isEmpty());
		const auto logPath = dir.filePath(QStringLiteral("peer.log"));

		TestRemotePluginAudioPorts ports{false, nullptr};
		RemotePlugin plugin{ports.controller()};

		QVERIFY2(plugin.init(peer, false, {logPath, QStringLiteral("silent")}) == false,
			"the host started a client that connects");
		if (!plugin.isRunning())
		{
			QSKIP("the client stand-in did not start");
		}
		QVERIFY(!plugin.failed());

		g_criticalsSeen = 0;
		const QtMessageHandler previous = qInstallMessageHandler(countCriticals);
		plugin.processMessage(_m);
		qInstallMessageHandler(previous);

		QCOMPARE(g_criticalsSeen, 1);
		QVERIFY(plugin.failed());
	}
#endif // SYNC_WITH_SHM_FIFO
};

QTEST_GUILESS_MAIN(RemotePluginAudioPortsTest)

#include "RemotePluginAudioPortsTest.moc"
