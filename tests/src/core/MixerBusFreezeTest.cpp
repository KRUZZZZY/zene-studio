/*
 * MixerBusFreezeTest.cpp - freezing a mixer bus (Q050-124)
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

/*! Render comparisons for a frozen bus. Two sources feed a bus that runs them through a device and a pan, and the
 *  bus feeds the master directly and through a second channel. The bus's capture (what the freeze render writes)
 *  must be its processed output; with the take installed and the sources SILENT the master must come out exactly
 *  as the live mix did (the take replaced the inputs and the downstream routes carried it); unfreezing must give
 *  the live mix back. Then: the same through freeze.bus / freeze.unfreeze_bus with control.undo, the mixer's own
 *  save/load (and an older element, a missing and an unreadable take), refused and failed renders, a take shorter
 *  than the song, an end-to-end song render with a project save/open, and the allocation probe on the playback
 *  path. The child render is replaced by a test hook that writes the in-process capture (a QtTest binary is not the
 *  application); tests/control-bus-freeze-transcript.py drives the real child over the socket. */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include <sndfile.h>

#include "AllocationProbe.h"
#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "BounceInPlace.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerBusFreeze.h"
#include "PhaseDMixerTestSupport.h"
#include "SampleBuffer.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLaneTestSupport.h"

using namespace lmms;
using namespace partd;

namespace
{

constexpr float SendB = 0.5f;
constexpr float DeviceL = 0.5f;
constexpr float DeviceR = 0.25f;
constexpr float BusPan = 0.5f;  // balance: left *= 0.5, right untouched
constexpr int Periods = 6;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! What the render hook writes: the take a test prepared, or a failure it was asked to simulate.
enum class HookMode { Take, Fail, Empty, Garbage };
std::vector<SampleFrame> s_hookTake;
HookMode s_hookMode = HookMode::Take;
int s_hookCalls = 0;

bool writeFloatWav(const QString& path, const std::vector<SampleFrame>& frames, int rate)
{
	SF_INFO info{};
	info.samplerate = rate;
	info.channels = 2;
	info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	SNDFILE* file = sf_open(QFile::encodeName(path).constData(), SFM_WRITE, &info);
	if (file == nullptr) { return false; }
	const auto count = static_cast<sf_count_t>(frames.size());
	const bool written = count == 0 || sf_writef_float(file, frames.front().data(), count) == count;
	return sf_close(file) == 0 && written;
}

bool renderHook(MixerChannel*, const QString& stagedPath, int rate, QString* error)
{
	++s_hookCalls;
	switch (s_hookMode)
	{
	case HookMode::Fail: *error = QStringLiteral("simulated render failure"); return false;
	case HookMode::Empty: return writeFloatWav(stagedPath, {}, rate);
	case HookMode::Garbage:
	{
		QFile file(stagedPath);
		return file.open(QIODevice::WriteOnly) && file.write("not a wav file at all") > 0;
	}
	case HookMode::Take: break;
	}
	return writeFloatWav(stagedPath, s_hookTake, rate);
}

float maxDifference(const std::vector<SampleFrame>& a, const std::vector<SampleFrame>& b, std::size_t from = 0,
	std::size_t to = static_cast<std::size_t>(-1))
{
	if (a.size() != b.size()) { return 1.0e9f; }
	float worst = 0.0f;
	for (std::size_t f = from; f < std::min(to, a.size()); ++f)
	{
		worst = std::max({worst, std::fabs(a[f][0] - b[f][0]), std::fabs(a[f][1] - b[f][1])});
	}
	return worst;
}

float peak(const std::vector<SampleFrame>& a, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1))
{
	float level = 0.0f;
	for (std::size_t f = from; f < std::min(to, a.size()); ++f)
	{
		level = std::max({level, std::fabs(a[f][0]), std::fabs(a[f][1])});
	}
	return level;
}

MixerChannel* channelWithId(int id)
{
	Mixer* mixer = Engine::mixer();
	for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		if (mixer->mixerChannel(i)->id() == id) { return mixer->mixerChannel(i); }
	}
	return nullptr;
}

QJsonObject busListEntry(int id)
{
	for (const QJsonValue& value : run(QStringLiteral("bus.list")).result.value(QStringLiteral("buses")).toArray())
	{
		if (value.toObject().value(QStringLiteral("id")).toString() == QStringLiteral("ch-%1").arg(id))
		{
			return value.toObject();
		}
	}
	return {};
}

//! A, B -> bus (device + pan) -> master, and bus -> C -> master. A and B reach nothing else.
struct Graph
{
	int a = 0;
	int b = 0;
	int bus = 0;
	int c = 0;
	int busId = 0;
};

Graph buildGraph(bool withDevice)
{
	Mixer* mixer = Engine::mixer();
	mixer->clear();
	Graph g;
	g.a = mixer->createChannel();
	g.b = mixer->createChannel();
	g.bus = mixer->createBusChannel();
	g.c = mixer->createChannel();
	mixer->deleteChannelSend(g.a, 0);
	mixer->deleteChannelSend(g.b, 0);
	mixer->createChannelSend(g.a, g.bus, 1.0f);
	mixer->createChannelSend(g.b, g.bus, SendB);
	mixer->createChannelSend(g.bus, g.c, 0.5f);
	MixerChannel* bus = mixer->mixerChannel(g.bus);
	if (withDevice) { bus->m_fxChain.appendEffect(new FixedGainEffect(&bus->m_fxChain, DeviceL, DeviceR)); }
	bus->m_panModel.setValue(BusPan);
	bus->m_volumeModel.setValue(0.8f);  // the fader stays live on the take
	g.busId = bus->id();
	return g;
}

float inputA(int frame) { return 0.5f * hashSignal(static_cast<std::uint32_t>(frame)); }
float inputB(int frame) { return 0.5f * hashSignal(static_cast<std::uint32_t>(frame) + 1000003u); }

/*! Renders @a periods periods with the timeline at @a firstPeriod; the sources carry their signal when @a feed. */
std::vector<SampleFrame> renderPass(const Graph& g, int periods, bool feed, bool playing = true, int firstPeriod = 0)
{
	PeriodHarness harness(Engine::mixer());
	const f_cnt_t fpp = harness.fpp();
	std::vector<SampleFrame> out;
	for (int p = firstPeriod; p < firstPeriod + periods; ++p)
	{
		busfreeze::setPeriod(playing, static_cast<f_cnt_t>(p) * fpp);
		if (feed)
		{
			for (f_cnt_t f = 0; f < fpp; ++f) { harness.in()[f] = SampleFrame(inputA(p * fpp + f), inputA(p * fpp + f)); }
			harness.feed(g.a);
			for (f_cnt_t f = 0; f < fpp; ++f) { harness.in()[f] = SampleFrame(inputB(p * fpp + f), inputB(p * fpp + f)); }
			harness.feed(g.b);
		}
		const std::vector<SampleFrame>& block = harness.render();
		out.insert(out.end(), block.begin(), block.end());
	}
	busfreeze::setPeriod(false, 0);
	return out;
}

//! What the bus's freeze render writes: its pre-fader output (the in-process half of the child render).
std::vector<SampleFrame> capturePass(const Graph& g, int periods)
{
	QString error;
	if (!busfreeze::armCapture(*Engine::mixer(), g.busId, &error)) { return {}; }
	std::vector<SampleFrame> take = renderPass(g, periods, true);
	busfreeze::disarmCapture(*Engine::mixer());
	return take;
}

std::shared_ptr<const SampleBuffer> bufferOf(const std::vector<SampleFrame>& frames)
{
	return std::make_shared<const SampleBuffer>(std::vector<SampleFrame>(frames),
		static_cast<int>(Engine::audioEngine()->outputSampleRate()));
}

} // namespace

class MixerBusFreezeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		initEngine();
		AudioEngineWorkerThread::setDeterministicProcessing(true);
		ControlRegistry::setReady(true);
		busfreeze::setRenderHookForTest(&renderHook);
	}

	void cleanupTestCase()
	{
		busfreeze::setRenderHookForTest(nullptr);
		ControlRegistry::setReady(false);
		AudioEngineWorkerThread::setDeterministicProcessing(false);
		destroyEngine();
	}

	void init()
	{
		s_hookMode = HookMode::Take;
		s_hookCalls = 0;
	}

	//! The capture is the bus's processed output; the take replaces the live inputs and feeds every downstream
	//! route; stopping the transport or unfreezing gives the live mix back.
	void frozenBusPlaysItsCaptureInPlaceOfTheLiveInputs()
	{
		const Graph g = buildGraph(true);
		const f_cnt_t fpp = periodFrames();
		const std::vector<SampleFrame> take = capturePass(g, Periods);
		QCOMPARE(take.size(), static_cast<std::size_t>(Periods * fpp));
		// Routed inputs, through the device and the pan, before the fader.
		float worst = 0.0f;
		for (int f = 0; f < Periods * static_cast<int>(fpp); ++f)
		{
			const float sum = inputA(f) + SendB * inputB(f);
			worst = std::max({worst, std::fabs(take[f][0] - sum * DeviceL * BusPan), std::fabs(take[f][1] - sum * DeviceR)});
		}
		QVERIFY2(worst < 1.0e-6f, qPrintable(QStringLiteral("capture differs from the processed bus by %1").arg(worst)));

		const std::vector<SampleFrame> live = renderPass(g, Periods, true);
		QVERIFY(peak(live) > 0.05f);

		MixerChannel* bus = Engine::mixer()->mixerChannel(g.bus);
		busfreeze::install(*bus, QStringLiteral("in-memory"), bufferOf(take));
		const std::vector<SampleFrame> frozenSilent = renderPass(g, Periods, false);
		QVERIFY2(maxDifference(frozenSilent, live) < 1.0e-6f, "the frozen bus must play the live mix with its inputs silent");
		const std::vector<SampleFrame> frozenFed = renderPass(g, Periods, true);
		QVERIFY2(maxDifference(frozenFed, live) < 1.0e-6f, "the live inputs must not be added to the take");
		// The take plays with the transport only: stopped, the bus is live (and its inputs are silent here).
		QCOMPARE(peak(renderPass(g, 2, false, false)), 0.0f);

		busfreeze::clear(*bus);
		QVERIFY(!bus->m_freeze.isFrozen());
		QVERIFY2(maxDifference(renderPass(g, Periods, true), live) < 1.0e-6f, "unfreezing must give the live mix back");
		QCOMPARE(peak(renderPass(g, 2, false)), 0.0f);
	}

	//! A take shorter than the song: the period it ends in is completed with silence, and past it the bus is live.
	void aShortTakeEndsCleanlyAndTheBusGoesLiveAfterIt()
	{
		const Graph g = buildGraph(true);
		const f_cnt_t fpp = periodFrames();
		const std::vector<SampleFrame> live = renderPass(g, 4, true);
		std::vector<SampleFrame> take = capturePass(g, 4);
		take.resize(static_cast<std::size_t>(2 * fpp + fpp / 2));
		busfreeze::install(*Engine::mixer()->mixerChannel(g.bus), QStringLiteral("short"), bufferOf(take));

		const std::vector<SampleFrame> silent = renderPass(g, 4, false);
		QVERIFY(maxDifference(silent, live, 0, 2 * fpp + fpp / 2) < 1.0e-6f);
		QCOMPARE(peak(silent, 2 * fpp + fpp / 2), 0.0f);
		// Past the end the live inputs are heard again, at the same place in the song.
		const std::vector<SampleFrame> fed = renderPass(g, 1, true, true, 3);
		std::vector<SampleFrame> liveLast(live.begin() + 3 * fpp, live.end());
		QVERIFY(maxDifference(fed, liveLast) < 1.0e-6f);
		busfreeze::clear(*Engine::mixer()->mixerChannel(g.bus));
	}

	//! freeze.bus / freeze.unfreeze_bus, bus.list's readback, one control.undo each way, and the mixer's save/load.
	void commandsUndoAndSaveLoad()
	{
		const Graph g = buildGraph(false);
		const std::vector<SampleFrame> live = renderPass(g, Periods, true);
		s_hookTake = capturePass(g, Periods);
		const QString channel = QStringLiteral("ch-%1").arg(g.busId);
		const QString out = m_dir.filePath(QStringLiteral("bus-take.wav"));

		const ControlResult frozen = run(QStringLiteral("freeze.bus"), {{QStringLiteral("channel"), channel},
			{QStringLiteral("out"), out}});
		QVERIFY2(frozen.ok, qPrintable(frozen.errorMessage));
		QCOMPARE(frozen.result.value(QStringLiteral("frozen")).toBool(), true);
		QCOMPARE(frozen.result.value(QStringLiteral("audio_ready")).toBool(), true);
		QCOMPARE(frozen.result.value(QStringLiteral("path")).toString(), out);
		QCOMPARE(frozen.result.value(QStringLiteral("frames")).toDouble(), static_cast<double>(s_hookTake.size()));
		QCOMPARE(busListEntry(g.busId).value(QStringLiteral("frozen")).toBool(), true);
		QCOMPARE(busListEntry(g.busId).value(QStringLiteral("frozen_audio")).toString(), out);
		QVERIFY(maxDifference(renderPass(g, Periods, false), live) < 1.0e-6f);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(!channelWithId(g.busId)->m_freeze.isFrozen(), "one control.undo must unfreeze the bus");
		QCOMPARE(peak(renderPass(g, 2, false)), 0.0f);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY(channelWithId(g.busId)->m_freeze.audioReady());

		// Save and load the mixer: the take comes back with the bus, and plays.
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(element);
		Engine::mixer()->saveSettings(doc, element);
		QVERIFY(doc.toString().contains(QStringLiteral("frozenAudio=")));
		Engine::mixer()->loadSettings(element);
		MixerChannel* reloaded = channelWithId(g.busId);
		QVERIFY(reloaded != nullptr && reloaded->isBus());
		QCOMPARE(reloaded->m_freeze.path, out);
		QVERIFY(reloaded->m_freeze.audioReady());
		QVERIFY2(maxDifference(renderPass(g, Periods, false), live) < 1.0e-6f, "the reloaded take must play");

		const ControlResult thawed = run(QStringLiteral("freeze.unfreeze_bus"), {{QStringLiteral("channel"), channel}});
		QVERIFY2(thawed.ok, qPrintable(thawed.errorMessage));
		QCOMPARE(thawed.result.value(QStringLiteral("frozen")).toBool(), false);
		QVERIFY(maxDifference(renderPass(g, Periods, true), live) < 1.0e-6f);
		QVERIFY(QFileInfo::exists(out));  // unfreezing is not a delete
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(channelWithId(g.busId)->m_freeze.path, out);
		QVERIFY2(channelWithId(g.busId)->m_freeze.audioReady(), "one control.undo must put the same take back");
		QVERIFY(maxDifference(renderPass(g, Periods, false), live) < 1.0e-6f);

		// An element without the attribute - every older project - loads unfrozen.
		QDomDocument old;
		QDomElement oldElement = old.createElement(QStringLiteral("mixer"));
		old.appendChild(oldElement);
		Engine::mixer()->saveSettings(old, oldElement);
		for (QDomElement ch = oldElement.firstChildElement(QStringLiteral("mixerchannel")); !ch.isNull();
			ch = ch.nextSiblingElement(QStringLiteral("mixerchannel")))
		{
			ch.removeAttribute(QStringLiteral("frozenAudio"));
		}
		Engine::mixer()->loadSettings(oldElement);
		QVERIFY(!channelWithId(g.busId)->m_freeze.isFrozen());
		QVERIFY(maxDifference(renderPass(g, Periods, true), live) < 1.0e-6f);
		QCOMPARE(busListEntry(g.busId).value(QStringLiteral("frozen")).toBool(), false);
	}

	//! A take whose file moved or is not audio keeps the bus frozen (so it can be relinked or unfrozen) and lets the
	//! live inputs play: nothing goes silent and nothing crashes.
	void aMissingOrUnreadableTakeFallsBackToTheLiveInputs()
	{
		const Graph g = buildGraph(false);
		const std::vector<SampleFrame> live = renderPass(g, Periods, true);
		const QString moved = m_dir.filePath(QStringLiteral("gone.wav"));
		const QString garbage = m_dir.filePath(QStringLiteral("garbage.wav"));
		{
			QFile file(garbage);
			QVERIFY(file.open(QIODevice::WriteOnly));
			file.write("RIFF but not really");
		}
		for (const QString& path : {moved, garbage})
		{
			QDomDocument doc;
			QDomElement element = doc.createElement(QStringLiteral("mixer"));
			doc.appendChild(element);
			Engine::mixer()->saveSettings(doc, element);
			for (QDomElement ch = element.firstChildElement(QStringLiteral("mixerchannel")); !ch.isNull();
				ch = ch.nextSiblingElement(QStringLiteral("mixerchannel")))
			{
				if (ch.attribute(QStringLiteral("id")).toInt() == g.busId) { ch.setAttribute(QStringLiteral("frozenAudio"), path); }
			}
			Engine::mixer()->loadSettings(element);
			MixerChannel* bus = channelWithId(g.busId);
			QVERIFY(bus->m_freeze.isFrozen());
			QVERIFY(!bus->m_freeze.audioReady());
			QCOMPARE(busListEntry(g.busId).value(QStringLiteral("frozen_audio_ready")).toBool(), false);
			QVERIFY2(maxDifference(renderPass(g, Periods, true), live) < 1.0e-6f, qPrintable(path));
			QVERIFY(run(QStringLiteral("freeze.unfreeze_bus"), {{QStringLiteral("channel"), QStringLiteral("ch-%1").arg(g.busId)}}).ok);
		}
	}

	//! Every refusal and every failed render leaves the bus unfrozen, the mix live and no take file behind.
	void refusalsAndFailedRendersChangeNothing()
	{
		const Graph g = buildGraph(false);
		const std::vector<SampleFrame> live = renderPass(g, Periods, true);
		s_hookTake = capturePass(g, Periods);
		const QString channel = QStringLiteral("ch-%1").arg(g.busId);
		const auto freeze = [&](const QString& target, const QString& out) {
			return run(QStringLiteral("freeze.bus"), {{QStringLiteral("channel"), target}, {QStringLiteral("out"), out}});
		};
		const int sourceId = Engine::mixer()->mixerChannel(g.a)->id();
		QVERIFY(!freeze(QStringLiteral("ch-%1").arg(sourceId), m_dir.filePath(QStringLiteral("a.wav"))).ok);
		QVERIFY(!freeze(QStringLiteral("ch-%1").arg(Engine::mixer()->mixerChannel(0)->id()), m_dir.filePath(QStringLiteral("m.wav"))).ok);
		QVERIFY(!freeze(channel, QStringLiteral("relative.wav")).ok);
		const int emptyBus = Engine::mixer()->createBusChannel();
		QVERIFY(!freeze(QStringLiteral("ch-%1").arg(Engine::mixer()->mixerChannel(emptyBus)->id()), m_dir.filePath(QStringLiteral("e.wav"))).ok);
		QCOMPARE(s_hookCalls, 0);  // refused before anything was rendered

		for (HookMode mode : {HookMode::Fail, HookMode::Empty, HookMode::Garbage})
		{
			s_hookMode = mode;
			const QString out = m_dir.filePath(QStringLiteral("failed-%1.wav").arg(static_cast<int>(mode)));
			const ControlResult failed = freeze(channel, out);
			QVERIFY(!failed.ok);
			QCOMPARE(failed.errorKind, ControlErrorKind::Refused);
			QVERIFY(!channelWithId(g.busId)->m_freeze.isFrozen());
			QVERIFY2(!QFileInfo::exists(out), "a failed render must not install a take file");
		}
		QVERIFY(maxDifference(renderPass(g, Periods, true), live) < 1.0e-6f);

		s_hookMode = HookMode::Take;
		QVERIFY(freeze(channel, m_dir.filePath(QStringLiteral("ok.wav"))).ok);
		QVERIFY2(!freeze(channel, m_dir.filePath(QStringLiteral("again.wav"))).ok, "a frozen bus must not be frozen again");
		QVERIFY(run(QStringLiteral("freeze.unfreeze_bus"), {{QStringLiteral("channel"), channel}}).ok);
		QVERIFY(!run(QStringLiteral("freeze.unfreeze_bus"), {{QStringLiteral("channel"), channel}}).ok);
	}

	//! The playback path - substitution, the downstream fader/sends/meters and the master mix - allocates nothing.
	void framePlaybackAllocatesNothing()
	{
		const Graph g = buildGraph(true);
		MixerChannel* bus = Engine::mixer()->mixerChannel(g.bus);
		busfreeze::install(*bus, QStringLiteral("probe"), bufferOf(capturePass(g, Periods)));
		PeriodHarness harness(Engine::mixer());
		harness.render();  // first touch outside the probe

		std::uint64_t allocations = 0;
		float heard = 0.0f;
		for (int p = 0; p < Periods; ++p)
		{
			busfreeze::setPeriod(true, static_cast<f_cnt_t>(p) * harness.fpp());
			test::resetAllocationCount();
			test::tlCountAllocations = true;
			const std::vector<SampleFrame>& out = harness.render();  // deterministic: every mixer job runs here
			test::tlCountAllocations = false;
			allocations += test::tlAllocationCount + test::tlFreeCount;
			heard = std::max(heard, peak(out));
		}
		busfreeze::setPeriod(false, 0);
		QCOMPARE(static_cast<unsigned long long>(allocations), 0ULL);
		// Nothing was fed: only the take can have sounded, so the probed periods did run the playback path.
		QVERIFY2(heard > 0.05f, "the probed periods must have played the take");

		// The substitution itself, called directly (after the renders, so it cannot feed them).
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		busfreeze::setPeriod(true, 0);
		const bool substituted = busfreeze::substitute(*bus, harness.fpp());
		test::tlCountAllocations = false;
		busfreeze::setPeriod(false, 0);
		QVERIFY(substituted);
		QCOMPARE(static_cast<unsigned long long>(test::tlAllocationCount + test::tlFreeCount), 0ULL);
		busfreeze::clear(*bus);
	}

	//! End to end on the real transport: a sample track feeds a channel that feeds the bus. The take captured from
	//! the song plays with the track muted, survives a project save and open, and unfreezing gives the track back.
	void songRenderSurvivesProjectSaveAndOpen()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		song->getTimeline(Song::PlayMode::Song).setLoopEnabled(false);
		// The channels first: a track's channel model is ranged over the channels that exist when it is made.
		Mixer* mixer = Engine::mixer();
		const int source = mixer->createChannel();
		mixer->deleteChannelSend(source, 0);
		const int busIndex = mixer->createBusChannel();
		mixer->createChannelSend(source, busIndex, 1.0f);
		mixer->mixerChannel(busIndex)->m_panModel.setValue(BusPan);
		QVector<SampleClip*> clips;
		SampleTrack* track = taketest::makeTakeTrack(m_dir.path(), 1, &clips);
		QVERIFY(track != nullptr);
		track->mixerChannelModel()->setValue(source);
		QCOMPARE(track->mixerChannelModel()->value(), source);
		const int busId = mixer->mixerChannel(busIndex)->id();

		const auto play = [song](int periods) {
			std::vector<SampleFrame> out;
			song->setPlayPos(0, Song::PlayMode::Song);
			song->playSong();
			for (int p = 0; p < periods; ++p)
			{
				const auto block = Engine::audioEngine()->renderNextPeriod();
				out.insert(out.end(), block.begin(), block.end());
			}
			song->stop();
			// The engine hands back the previous period's mix: the first block of a pass is not this pass.
			out.erase(out.begin(), out.begin() + periodFrames());
			return out;
		};
		const int periods = 40;
		play(4);  // the pan's value ramp from its default settles in the first periods after it is set
		const std::vector<SampleFrame> live = play(periods);
		QVERIFY(peak(live) > 0.1f);
		QString error;
		QVERIFY(busfreeze::armCapture(*mixer, busId, &error));
		const std::vector<SampleFrame> captured = play(periods + 1);
		busfreeze::disarmCapture(*mixer);
		// The capture is the bus's pre-fader output; master = bus here (unity faders), so they match.
		const std::vector<SampleFrame> head(captured.begin(), captured.begin() + static_cast<std::ptrdiff_t>(live.size()));
		std::size_t first = 0;
		while (first < live.size() && std::fabs(head[first][0] - live[first][0]) < 1.0e-6f) { ++first; }
		QVERIFY2(maxDifference(head, live) < 1.0e-6f, qPrintable(QStringLiteral("capture differs by %1 from frame %2 (live %3, capture %4)")
			.arg(maxDifference(head, live)).arg(first).arg(first < live.size() ? live[first][0] : 0.f)
			.arg(first < live.size() ? head[first][0] : 0.f)));
		s_hookTake = captured;

		const QString channel = QStringLiteral("ch-%1").arg(busId);
		QVERIFY(run(QStringLiteral("freeze.bus"), {{QStringLiteral("channel"), channel},
			{QStringLiteral("out"), m_dir.filePath(QStringLiteral("song-take.wav"))}}).ok);
		track->setMuted(true);
		QVERIFY2(maxDifference(play(periods), live) < 1.0e-6f, "the frozen bus must play the song's take, track muted");

		const QString project = m_dir.filePath(QStringLiteral("bus-freeze.mmp"));
		QVERIFY(song->saveProjectFile(project));
		song->loadProject(project);
		MixerChannel* bus = channelWithId(busId);
		QVERIFY(bus != nullptr);
		QVERIFY(bus->m_freeze.audioReady());
		QVERIFY2(maxDifference(play(periods), live) < 1.0e-6f, "the take must play after the project is opened again");

		QVERIFY(run(QStringLiteral("freeze.unfreeze_bus"), {{QStringLiteral("channel"), channel}}).ok);
		QCOMPARE(peak(play(periods)), 0.0f);  // the track is still muted: the live bus is silent
		for (Track* t : song->tracks()) { t->setMuted(false); }
		QVERIFY2(maxDifference(play(periods), live) < 1.0e-6f, "unfrozen, the bus plays its live input again");
	}

private:
	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(MixerBusFreezeTest)
#include "MixerBusFreezeTest.moc"
