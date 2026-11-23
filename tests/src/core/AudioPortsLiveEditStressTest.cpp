/*
 * AudioPortsLiveEditStressTest.cpp - live pin edits against a render period
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

//! Q050-305: change audio pins and channel counts while audio renders.
//!
//! The routing state of `AudioPortsModel` - the packed pin matrices, their
//! used-channel caches, the cached direct-routing decision and the cached
//! track-channel upper bound - is written by the pin connector, by the
//! `port.set_pin(s)` control command and by the project loader, and it is read
//! by the render thread inside a render period. `AudioEngine::renderNextPeriod()`
//! owns `m_changeMutex` for the whole period, and every mutator of that state
//! takes the same guard (`takeRoutingEditGuard()`, src/core/AudioPortsModel.cpp).
//! This test is the evidence for that contract, and it is the regression that
//! fails when the contract is dropped again:
//!
//!  - `liveEditsWaitForTheRenderPeriod()` is the deterministic half. A period is
//!    opened on this thread (the guard, exactly as `renderNextPeriod()` holds it)
//!    and one mutator at a time is driven on a second thread. The edit must park
//!    on the guard - `AudioEngine::modelChangeWaiters()` counts it, and the edit
//!    must NOT have completed when the period ends. Without the guard the edit
//!    runs inside the period, which is the race the finding describes.
//!  - `pinEditsDuringRenderKeepCachesAndOutputConsistent()` is the stress half.
//!    A real `PluginAudioPorts` device renders real periods through
//!    `AudioPorts::Router` - a deterministic input signal, a deterministic
//!    processor - while a second thread cycles through pin, batch-pin and
//!    track-channel-count edits. Inside every period, and under the same guard
//!    the router runs under, the caches are compared against a recomputation from
//!    the pins the router is about to read, and the routed audio is compared
//!    against the value those pins imply. A routing state that is not the one the
//!    caches and the audio came from is a failure: a stale cache, a torn pin word
//!    or an edit that landed half way through the matrix.
//!
//! Nothing here is timing-based: the ordering observation is "it is parked" vs
//! "it finished", and the stress half asserts on state and audio values, never
//! on a clock. TSAN_OPTIONS evidence is collected by
//! tests/run-audio-ports-live-edit-tsan.sh.

#include <QDomDocument>
#include <QScopeGuard>
#include <QtTest>

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

#include "AudioBus.h"
#include "AudioEngine.h"
#include "AudioPorts.h"
#include "AudioPortsModel.h"
#include "Engine.h"
#include "PluginAudioPorts.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

//! Stereo 2x2 processor with interleaved, in-place buffers: the combination for
//! which `sampleFrameCompatible()` is true, so the router's direct-routing
//! optimization is reachable as well as its normal path.
constexpr auto InterleavedStereo = AudioPortsSettings{
	.kind = AudioDataKind::F32,
	.interleaved = true,
	.inputs = 2,
	.outputs = 2,
	.inplace = true,
	.buffered = true,
};

static_assert(Validate<InterleavedStereo>{}(), "test settings must be valid");
static_assert(InterleavedStereo.sampleFrameCompatible());

using StereoPorts = PluginAudioPorts<InterleavedStereo>;

//! The stereo track-channel pair the mixer hands a router, backed by owned storage.
class TestBus
{
public:
	explicit TestBus(f_cnt_t frames)
		: m_storage(static_cast<std::size_t>(frames))
		, m_pointers{m_storage.data()}
		, m_bus(m_pointers.data(), 1, frames)
	{
	}

	auto audioBus() -> AudioBus& { return m_bus; }

	void fill(float left, float right)
	{
		for (auto& frame : m_storage) { frame = SampleFrame{left, right}; }
	}

	auto left(f_cnt_t frame) const -> float { return m_storage[frame].left(); }
	auto right(f_cnt_t frame) const -> float { return m_storage[frame].right(); }

private:
	std::vector<SampleFrame> m_storage;
	std::array<SampleFrame*, 1> m_pointers{};
	AudioBus m_bus;
};

//! `AudioPortsModel` is abstract; the ordering half needs the model and no buffers.
class ModelOnlyPorts : public AudioPortsModel
{
public:
	using AudioPortsModel::AudioPortsModel;

protected:
	void bufferPropertiesChanging(ch_cnt_t, ch_cnt_t, f_cnt_t) override {}
};

using PinMap = AudioPortsModel::PinMap;

auto anyPin(const std::vector<bool>& row) -> bool
{
	return std::any_of(row.begin(), row.end(), [](bool pin) { return pin; });
}

//! What the render thread reads for one period: the pins plus the caches the
//! router and the mixer consult, captured under the period's guard.
struct RoutingState
{
	PinMap inPins;
	PinMap outPins;
	ch_cnt_t trackChannels = 0;
	ch_cnt_t channelsIn = 0;
	ch_cnt_t channelsOut = 0;
	std::bitset<MaxTrackChannels> usedTrackIn;
	std::bitset<MaxTrackChannels> usedTrackOut;
	std::vector<bool> usedIn;
	std::vector<bool> usedOut;
	ch_cnt_t upperBound = 0;
	std::optional<ch_cnt_t> directRouting;

	RoutingState() = default;
	explicit RoutingState(const AudioPortsModel& model)
		: inPins(model.in().pins())
		, outPins(model.out().pins())
		, trackChannels(model.trackChannelCount())
		, channelsIn(model.in().channelCount())
		, channelsOut(model.out().channelCount())
		, usedTrackIn(model.in().usedTrackChannels())
		, usedTrackOut(model.out().usedTrackChannels())
		, usedIn(model.in().usedChannels())
		, usedOut(model.out().usedChannels())
		, upperBound(model.trackChannelsUpperBound())
		, directRouting(model.directRouting())
	{
	}
};

//! The cached upper bound, recomputed from the pins: the highest used track
//! channel rounded up to a whole pair, never below DEFAULT_CHANNELS.
auto expectedUpperBound(const RoutingState& state) -> ch_cnt_t
{
	ch_cnt_t bound = DEFAULT_CHANNELS;
	for (ch_cnt_t tc = 0; tc < state.trackChannels; ++tc)
	{
		const auto row = static_cast<std::size_t>(tc);
		if (anyPin(state.inPins[row]) || anyPin(state.outPins[row]))
		{
			bound = std::max<ch_cnt_t>(bound, static_cast<ch_cnt_t>(tc + 1));
		}
	}
	return static_cast<ch_cnt_t>(bound + bound % 2);
}

//! The cached direct-routing decision, recomputed from the pins: one connected
//! pair, routed straight L->L and R->R, in both matrices and on the same pair.
auto expectedDirectRouting(const RoutingState& state) -> std::optional<ch_cnt_t>
{
	const auto check = [&state](const PinMap& pins) -> std::optional<ch_cnt_t>
	{
		std::optional<ch_cnt_t> ret;
		int nonzero = 0;
		for (ch_cnt_t tc = 0; tc < state.trackChannels; tc += 2)
		{
			const auto row = static_cast<std::size_t>(tc);
			const auto next = static_cast<std::size_t>(tc + 1);
			if (pins[row][0] || pins[row][1] || pins[next][0] || pins[next][1])
			{
				if (++nonzero > 1) { return std::nullopt; }
			}
			if (!ret && pins[row][0] && !pins[row][1] && !pins[next][0] && pins[next][1])
			{
				ret = static_cast<ch_cnt_t>(tc / 2);
			}
		}
		return ret;
	};

	if ((state.channelsIn != 0 && state.channelsIn != 2) || (state.channelsOut != 0 && state.channelsOut != 2))
	{
		return std::nullopt;
	}
	if (state.channelsIn == 0) { return check(state.outPins); }
	if (state.channelsOut == 0) { return check(state.inPins); }
	const auto in = check(state.inPins);
	const auto out = check(state.outPins);
	if (!in || !out || in != out) { return std::nullopt; }
	return in;
}

//! Every disagreement between the captured caches and the pins they cache, in
//! words (empty when they agree).
auto cacheDisagreements(const RoutingState& state) -> QStringList
{
	QStringList bad;

	if (state.inPins.size() != state.trackChannels || state.outPins.size() != state.trackChannels)
	{
		bad << QStringLiteral("pin matrices have %1/%2 rows for %3 track channels")
			.arg(state.inPins.size()).arg(state.outPins.size()).arg(state.trackChannels);
		return bad;
	}
	for (const auto* pins : {&state.inPins, &state.outPins})
	{
		for (const auto& row : *pins)
		{
			if (row.size() != state.channelsIn || row.size() != state.channelsOut)
			{
				bad << QStringLiteral("a pin row has %1 columns for %2/%3 channels")
					.arg(row.size()).arg(state.channelsIn).arg(state.channelsOut);
				return bad;
			}
		}
	}

	const auto checkUsed = [&bad](const PinMap& pins, const std::bitset<MaxTrackChannels>& usedTrack,
		const std::vector<bool>& usedChannels, const ch_cnt_t matrixTrackChannels, const char* which)
	{
		for (ch_cnt_t tc = 0; tc < MaxTrackChannels; ++tc)
		{
			const auto row = static_cast<std::size_t>(tc);
			const bool expected = tc < matrixTrackChannels && anyPin(pins[row]);
			if (usedTrack.test(row) != expected)
			{
				bad << QStringLiteral("%1 matrix: used track channel %2 is cached %3, pins say %4")
					.arg(QString::fromLatin1(which)).arg(tc)
					.arg(usedTrack.test(row) ? "on" : "off").arg(expected ? "on" : "off");
			}
		}
		for (std::size_t pc = 0; pc < usedChannels.size(); ++pc)
		{
			bool expected = false;
			for (ch_cnt_t tc = 0; tc < matrixTrackChannels && !expected; ++tc)
			{
				expected = pins[static_cast<std::size_t>(tc)][pc];
			}
			if (usedChannels[pc] != expected)
			{
				bad << QStringLiteral("%1 matrix: used processor channel %2 is cached %3, pins say %4")
					.arg(QString::fromLatin1(which)).arg(pc)
					.arg(usedChannels[pc] ? "on" : "off").arg(expected ? "on" : "off");
			}
		}
	};

	checkUsed(state.inPins, state.usedTrackIn, state.usedIn, state.trackChannels, "in");
	checkUsed(state.outPins, state.usedTrackOut, state.usedOut, state.trackChannels, "out");

	if (state.upperBound != expectedUpperBound(state))
	{
		bad << QStringLiteral("cached track-channel upper bound is %1, the pins say %2")
			.arg(state.upperBound).arg(expectedUpperBound(state));
	}
	const auto direct = expectedDirectRouting(state);
	if (state.directRouting != direct)
	{
		bad << QStringLiteral("cached direct routing is %1, the pins say %2")
			.arg(state.directRouting ? QString::number(*state.directRouting) : QStringLiteral("off"),
				direct ? QString::number(*direct) : QStringLiteral("off"));
	}
	return bad;
}

//! The deterministic processor: two constant output channels, and the first
//! frame of the input it was handed.
struct ProcessorProbe
{
	static constexpr std::array<float, 2> output{10.0f, 20.0f};
	float input[2] = {0.0f, 0.0f};
};

//! What one period must produce, from the pins the router reads in that period.
//! Only the bus's single pair is routed, so only track channels 0 and 1 reach it;
//! a track channel with no output pin keeps the value it had (pass-through).
auto expectedProcessorInput(const RoutingState& state, std::size_t processorChannel, float busL, float busR) -> float
{
	float sum = 0.0f;
	if (state.inPins[0][processorChannel]) { sum += busL; }
	if (state.inPins[1][processorChannel]) { sum += busR; }
	return sum;
}

auto expectedBusChannel(const RoutingState& state, std::size_t trackChannel, float unchanged) -> float
{
	bool any = false;
	float sum = 0.0f;
	for (std::size_t pc = 0; pc < ProcessorProbe::output.size(); ++pc)
	{
		if (state.outPins[trackChannel][pc])
		{
			any = true;
			sum += ProcessorProbe::output[pc];
		}
	}
	return any ? sum : unchanged;
}

constexpr float BusLeft = 1.0f;
constexpr float BusRight = 2.0f;

//! One step of the deterministic edit script the control side runs. Every index
//! is inside the matrix the step runs on, and the whole cycle returns to the state
//! it started from.
void applyEditStep(StereoPorts& ports, int step)
{
	switch (step)
	{
		case 0: ports.in().setPin(0, 0, false); break;
		case 1: ports.in().setPin(1, 1, false); break;
		case 2: ports.in().setPin(0, 1, true); break;
		case 3:
			ports.setPins({{false, 0, 0, true}, {false, 1, 1, true}, {true, 0, 0, true}, {true, 1, 1, true}});
			break;
		case 4: ports.growTrackChannels(8); break;
		case 5: ports.in().setPin(6, 1, true); break;
		case 6: ports.out().setPin(7, 0, true); break;
		case 7: ports.setPins({{false, 6, 1, false}, {true, 7, 0, false}}); break;
		case 8: ports.setTrackChannelCount(DEFAULT_CHANNELS); break;
		case 9: ports.setChannelCounts(2, 2); break;
		case 10: ports.setTrackChannelCount(DEFAULT_CHANNELS + 2); break;
		case 11: ports.in().setPin(2, 0, true); break;
		case 12: ports.out().setPin(3, 1, true); break;
		case 13: ports.setPins({{false, 2, 0, false}, {false, 3, 1, false}, {false, 0, 1, false},
			{false, 1, 0, false}}); break;
		case 14: ports.setTrackChannelCount(DEFAULT_CHANNELS); break;
		default: ports.setChannelCounts(2, 2); break;
	}
}

constexpr int EditScriptLength = 15;

//! Records an edit that emitted its model signal while a render period was open. The
//! flag it reads is set by the render half inside the period's guard, so a routing edit
//! that took the guard can never see it set.
void noteEditInsideAPeriod(const std::atomic<int>& periodOpen, std::atomic<int>& insideAPeriod)
{
	if (periodOpen.load(std::memory_order_acquire) != 0)
	{
		insideAPeriod.fetch_add(1, std::memory_order_relaxed);
	}
}

} // namespace


class AudioPortsLiveEditStressTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY2(Engine::audioEngine() != nullptr, "the guard under test is the audio engine's");
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	/**
	 * Q050-305: a routing edit made while a render period is open must park on the
	 * engine's model-change guard, not land inside the period.
	 *
	 * The period is opened here with `requestChangeInModel()`, which is the lock
	 * `AudioEngine::renderNextPeriod()` holds for a whole period. The edit runs on a
	 * second thread. Two observations decide the slot and neither is a deadline:
	 * `modelChangeWaiters()` becomes non-zero only once the edit has reached the
	 * guard and is parked on it, and the edit must NOT have finished while the period
	 * is still open. Without the guard the edit returns inside the period, which is
	 * exactly the write the render thread could read half way through.
	 */
	void liveEditsWaitForTheRenderPeriod()
	{
		auto* const engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);

		ModelOnlyPorts model{2, 2, false};

		struct Edit
		{
			const char* name;
			std::function<void()> apply;
			std::function<bool()> applied;
		};

		const std::vector<Edit> edits{
			{"Matrix::setPin()", [&model] { model.in().setPin(0, 1, true); },
				[&model] { return model.in().enabled(0, 1); }},
			{"AudioPortsModel::setPins()",
				[&model] { model.setPins({{false, 0, 0, false}, {true, 0, 1, true}}); },
				[&model] { return !model.in().enabled(0, 0) && model.out().enabled(0, 1); }},
			{"AudioPortsModel::setTrackChannelCount()",
				[&model] { model.setTrackChannelCount(DEFAULT_CHANNELS + 2); },
				[&model] { return model.trackChannelCount() == DEFAULT_CHANNELS + 2; }},
			{"AudioPortsModel::setChannelCounts()",
				[&model] { model.setChannelCounts(2, 4); },
				[&model] { return model.out().channelCount() == 4; }},
			{"AudioPortsModel::setAllChannelCounts()",
				[&model] { model.setAllChannelCounts(DEFAULT_CHANNELS, 2, 2); },
				[&model]
				{
					return model.trackChannelCount() == DEFAULT_CHANNELS
						&& model.in().channelCount() == 2 && model.out().channelCount() == 2;
				}},
			{"AudioPortsModel::growTrackChannels()",
				[&model] { model.growTrackChannels(6); },
				[&model] { return model.trackChannelCount() == 6; }},
		};

		for (const Edit& edit : edits)
		{
			engine->requestChangeInModel();
			QVERIFY2(engine->isChangeInModelHeld(), "this thread did not take the render period's guard");

			std::atomic<bool> finished{false};
			std::thread editor([&edit, &finished]
			{
				edit.apply();
				finished.store(true, std::memory_order_release);
			});
			// A failed assertion returns from the slot at once, and a joinable std::thread
			// destroyed on that path calls std::terminate, which would hide the assertion.
			const auto joinEditor = qScopeGuard([&editor] { if (editor.joinable()) { editor.join(); } });

			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!finished.load(std::memory_order_acquire) && engine->modelChangeWaiters() == 0
				&& std::chrono::steady_clock::now() < deadline)
			{
				std::this_thread::yield();
			}

			const bool finishedInsideThePeriod = finished.load(std::memory_order_acquire);
			const int parked = engine->modelChangeWaiters();

			engine->doneChangeInModel();
			editor.join();


			QVERIFY2(!finishedInsideThePeriod,
				qPrintable(QStringLiteral("%1 changed the routing state while the render period was open: "
					"the render thread reads the pin matrices and their caches inside that period, so the "
					"edit must wait for it to end (Q050-305)").arg(QString::fromLatin1(edit.name))));
			QVERIFY2(parked > 0,
				qPrintable(QStringLiteral("%1 never reached the engine's model-change guard, so the period "
					"it runs in is not the render period the router reads under")
					.arg(QString::fromLatin1(edit.name))));
			QVERIFY2(edit.applied(),
				qPrintable(QStringLiteral("%1 did not apply once the period ended").arg(QString::fromLatin1(edit.name))));
		}
	}

	/**
	 * Q050-305: pins and channel counts edited while audio renders must leave the
	 * caches the router uses and the audio the router produces consistent with the
	 * pins.
	 *
	 * The render half runs real periods: a `PluginAudioPorts` stereo device, a real
	 * `AudioBus` pair and `AudioPorts::Router::process()`, under the same guard
	 * `AudioEngine::renderNextPeriod()` holds. Inside the period the routing state is
	 * captured, the caches are compared against that capture, and the audio is
	 * compared against the value the captured pins imply - so a cache that lags a pin
	 * edit, a pin word read half written, or an edit that landed between the capture
	 * and the router's read fails the period.
	 */
	void pinEditsDuringRenderKeepCachesAndOutputConsistent()
	{
		auto* const engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);

		StereoPorts ports{false};
		ports.init();
		QVERIFY2(ports.active(), "the device must be routed for the router to run at all");

		const auto frames = engine->framesPerPeriod();
		QVERIFY(frames > 0);
		TestBus bus{frames};

		ports.out().setPin(0, 0, true);
		ports.out().setPin(1, 1, true);

		// An edit that lands inside a period is a contract violation; the period's own
		// flag is set under the guard, so an edit that took the guard can never see it.
		std::atomic<int> periodOpen{0};
		std::atomic<int> editsInsideAPeriod{0};
		connect(&ports, &Model::dataChanged, &ports,
			[&periodOpen, &editsInsideAPeriod] { noteEditInsideAPeriod(periodOpen, editsInsideAPeriod); },
			Qt::DirectConnection);
		connect(&ports, &Model::propertiesChanged, &ports,
			[&periodOpen, &editsInsideAPeriod] { noteEditInsideAPeriod(periodOpen, editsInsideAPeriod); },
			Qt::DirectConnection);

		constexpr int Periods = 600;
		std::atomic<bool> stop{false};
		std::atomic<int> edits{0};
		std::thread editor([&ports, &stop, &edits]
		{
			int step = 0;
			while (!stop.load(std::memory_order_acquire))
			{
				applyEditStep(ports, step % EditScriptLength);
				++step;
				edits.fetch_add(1, std::memory_order_relaxed);
				std::this_thread::yield();
			}
		});
		const auto stopAndJoin = qScopeGuard([&stop, &editor]
		{
			stop.store(true, std::memory_order_release);
			if (editor.joinable()) { editor.join(); }
		});

		int inconsistentPeriods = 0;
		QString firstFailure;
		const auto fail = [&inconsistentPeriods, &firstFailure](QString message)
		{
			if (inconsistentPeriods == 0) { firstFailure = std::move(message); }
			++inconsistentPeriods;
		};

		int editsSeen = 0;
		bool controlThreadStalled = false;
		for (int period = 0; period < Periods; ++period)
		{
			// Let the control thread complete at least one edit BEFORE this period, so
			// every period is preceded by an edit and the two really interleave. The
			// wait is bounded only so a stalled control thread reports itself instead of
			// hanging; the assertion below is on the number of edits, not on the clock.
			const auto progressDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (edits.load(std::memory_order_acquire) <= editsSeen
				&& std::chrono::steady_clock::now() < progressDeadline)
			{
				std::this_thread::yield();
			}
			editsSeen = edits.load(std::memory_order_acquire);
			if (editsSeen <= period)
			{
				controlThreadStalled = true;
				break;
			}

			const auto guard = engine->requestChangesGuard();
			periodOpen.fetch_add(1, std::memory_order_acq_rel);

			const RoutingState state{ports};

			for (const QString& bad : cacheDisagreements(state)) { fail(bad); }

			bus.fill(BusLeft, BusRight);
			ProcessorProbe probe;
			auto router = ports.getRouter();
			const auto status = router.process(bus.audioBus(), [&probe](auto inOut)
			{
				probe.input[0] = inOut.data()[0];
				probe.input[1] = inOut.data()[1];
				for (f_cnt_t frame = 0; frame < inOut.frames(); ++frame)
				{
					inOut.data()[frame * 2] = ProcessorProbe::output[0];
					inOut.data()[frame * 2 + 1] = ProcessorProbe::output[1];
				}
				return ProcessStatus::Continue;
			});
			if (status != ProcessStatus::Continue) { fail(QStringLiteral("the router stopped the period")); }

			periodOpen.fetch_sub(1, std::memory_order_acq_rel);

			for (std::size_t pc = 0; pc < ProcessorProbe::output.size(); ++pc)
			{
				const float expected = expectedProcessorInput(state, pc, BusLeft, BusRight);
				if (probe.input[pc] != expected)
				{
					fail(QStringLiteral("period %1: processor input %2 is %3, the pins say %4")
						.arg(period).arg(pc).arg(probe.input[pc]).arg(expected));
				}
			}
			const float expectedLeft = expectedBusChannel(state, 0, BusLeft);
			const float expectedRight = expectedBusChannel(state, 1, BusRight);
			if (bus.left(0) != expectedLeft || bus.right(0) != expectedRight)
			{
				fail(QStringLiteral("period %1: track channels are %2/%3, the pins say %4/%5")
					.arg(period).arg(bus.left(0)).arg(bus.right(0)).arg(expectedLeft).arg(expectedRight));
			}

			std::this_thread::yield();
		}

		stop.store(true, std::memory_order_release);
		editor.join();

		const int editCount = edits.load(std::memory_order_relaxed);
		QVERIFY2(!controlThreadStalled,
			"the control thread stopped making edits while the render ran, so the last periods did not "
			"overlap an edit and prove nothing");
		QVERIFY2(editCount >= Periods,
			qPrintable(QStringLiteral("the control thread completed only %1 edits for %2 render periods, so the "
				"two did not interleave and this run proves nothing").arg(editCount).arg(Periods)));
		QVERIFY2(inconsistentPeriods == 0,
			qPrintable(QStringLiteral("%1 of %2 render periods read a routing state the caches or the audio "
				"disagree with; first: %3").arg(inconsistentPeriods).arg(Periods).arg(firstFailure)));
		QVERIFY2(editsInsideAPeriod.load(std::memory_order_relaxed) == 0,
			qPrintable(QStringLiteral("%1 routing edits ran inside a render period")
				.arg(editsInsideAPeriod.load(std::memory_order_relaxed))));

		// The state the edits left behind is still the state the caches describe, and it
		// survives a save and a load into a fresh device.
		const RoutingState finalState{ports};
		QCOMPARE(cacheDisagreements(finalState), QStringList{});

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("audioengine"));
		doc.appendChild(root);
		ports.saveSettings(doc, root);
		StereoPorts reloaded{false};
		reloaded.init();
		reloaded.loadSettings(root);
		const RoutingState reloadedState{reloaded};
		// A save records the track channels the pins need, not the count the session happened
		// to have, so the reload grows to the same bound and holds the same pins up to it.
		QCOMPARE(reloadedState.upperBound, finalState.upperBound);
		QVERIFY(reloadedState.inPins.size() >= static_cast<std::size_t>(finalState.upperBound)
			&& reloadedState.outPins.size() >= static_cast<std::size_t>(finalState.upperBound));
		for (std::size_t row = 0; row < static_cast<std::size_t>(finalState.upperBound); ++row)
		{
			QVERIFY2(reloadedState.inPins[row] == finalState.inPins[row],
				qPrintable(QStringLiteral("input pins of track channel %1 did not survive a save and load")
					.arg(row)));
			QVERIFY2(reloadedState.outPins[row] == finalState.outPins[row],
				qPrintable(QStringLiteral("output pins of track channel %1 did not survive a save and load")
					.arg(row)));
		}
		QCOMPARE(cacheDisagreements(reloadedState), QStringList{});

		qInfo("LIVEEDITS periods=%d edits=%d edits_inside_a_period=%d", Periods, editCount,
			editsInsideAPeriod.load(std::memory_order_relaxed));
	}
};

QTEST_GUILESS_MAIN(AudioPortsLiveEditStressTest)
#include "AudioPortsLiveEditStressTest.moc"
