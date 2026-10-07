/*
 * InstrumentLatencyPdcTest.cpp - MX-157 (BUGS 11.34): a hosted INSTRUMENT's latency reaches delay
 * compensation, measured on rendered audio.
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

/*
 * WHAT THIS PROVES
 * ----------------
 * The subjects are the in-tree fixtures, loaded through plugin.list / plugin.load onto real instrument tracks
 * and played by a real MIDI clip through the engine's own period (AudioEngine::renderNextPeriod):
 *   - tests/data/clap-test-plugin/clap-test-instrument.c (CLAP_TEST_INSTRUMENT_LATENCY),
 *   - tests/data/vst3-test-instrument/vst3-test-instrument.cpp (VST3_TEST_INSTRUMENT_LATENCY), and
 *   - tests/data/lv2-test-instrument/lv2-test-instrument.c (LV2_TEST_INSTRUMENT_LATENCY).
 * An instance created while its variable is set is LATENT: it reports the count as its latency AND really
 * delays its output by it. An instance created without the variable is the plain fixture - the reference
 * track. Both play the same note at the same tick, so without delay compensation the reference sounds
 * `latency` frames before the latent instrument; with it, the mixer delays the reference by exactly that
 * much and the two onsets land on the same frame. Each case renders:
 *   1. the reference alone, before the latent instrument exists (its onset is the base),
 *   2. the reference with the latent instrument present but muted (it must start `latency` frames later),
 *   3. the latent instrument alone (the fixture's own delay: `latency` frames after the base), and
 *   4. both: the first audible frame must already carry BOTH levels - one step, not two.
 * A latency change is the plug-in's own signal - a CLAP restart request, a VST3 kLatencyChanged, a new
 * value on the LV2 latency port - and each host picks it up on its GUI-thread poll timer, which the test
 * lets run (QTest::qWaitFor) rather than calling.
 * Nothing in the assertions uses the MX-157 API: the same file compiles against the tree before it, which
 * is the red run.
 */

#include <QtTest>

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <vector>

#include "AllocationProbe.h"

#include "AudioBusHandle.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentOutputBuses.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "PluginFactory.h"
#include "SampleFrame.h"
#include "Song.h"

#ifndef CLAP_TEST_INSTRUMENT_DIR
#define CLAP_TEST_INSTRUMENT_DIR ""
#endif
#ifndef VST3_TEST_INSTRUMENT_DIR
#define VST3_TEST_INSTRUMENT_DIR ""
#endif
#ifndef LV2_TEST_INSTRUMENT_DIR
#define LV2_TEST_INSTRUMENT_DIR ""
#endif

using namespace lmms;

namespace
{

//! Prime, so it is a multiple of no block size: an off-by-a-period alignment cannot pass by accident.
constexpr int Latency = 257;
//! The count a latent instance switches to in the change cases.
constexpr int ChangedLatency = 113;
//! Periods rendered per measurement: the note sounds throughout (it is a bar long).
constexpr int Periods = 8;
//! Anything above this is sound; the fixtures write exact constants or exact zero.
constexpr float Audible = 1e-6f;

//! How long a host's poll timer gets to notice a latency (the slowest, the LV2 host's, runs every 500 ms).
constexpr int PollMs = 3000;

const char* const ClapEnv = "CLAP_TEST_INSTRUMENT_LATENCY";
const char* const Vst3Env = "VST3_TEST_INSTRUMENT_LATENCY";
const char* const Lv2Env = "LV2_TEST_INSTRUMENT_LATENCY";

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

void evidence(const char* format, int a, int b, int c, int d)
{
	std::printf("INST_PDC_EVIDENCE ");
	std::printf(format, a, b, c, d);
	std::printf("\n");
	std::fflush(stdout);
}

//! The dev-<n> id plugin.list gives the device of @a format whose @a key is @a value, or empty.
QString deviceOf(const QString& format, const QString& key, const QString& value)
{
	const ControlResult listed = run(QStringLiteral("plugin.list"), {{QStringLiteral("format"), format}});
	for (const QJsonValue& entry : listed.result.value(QStringLiteral("devices")).toArray())
	{
		const QJsonObject device = entry.toObject();
		if (device.value(key).toString() == value) { return device.value(QStringLiteral("id")).toString(); }
	}
	return QString();
}

//! A new instrument track named @a name, carrying @a device (through plugin.load) and a bar-long note at
//! tick 0. The name is what pdc.report lists the track's input under.
InstrumentTrack* addPlayingTrack(const QString& device, const QString& name)
{
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	if (track == nullptr) { return nullptr; }
	const ControlResult loaded = run(QStringLiteral("plugin.load"),
		{{QStringLiteral("target"), control::trackIdOf(track)}, {QStringLiteral("device"), device}});
	if (!loaded.ok) { return nullptr; }
	track->setName(name);
	auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
	clip->addNote(Note(TimePos(DefaultTicksPerBar), TimePos(0), 60), false);
	return track;
}

//! Plays the song from tick 0 for Periods periods; the master's left channel. Then lets every delay line,
//! release and fixture ring run out, so the next render starts from silence.
std::vector<float> render()
{
	AudioEngine* engine = Engine::audioEngine();
	Song* song = Engine::getSong();
	song->setPlayPos(0, Song::PlayMode::Song);
	song->playSong();
	std::vector<float> left;
	for (int period = 0; period < Periods; ++period)
	{
		for (const SampleFrame& frame : engine->renderNextPeriod()) { left.push_back(frame[0]); }
	}
	song->stop();
	const int flush = (2 * LatencyCompensation::MaxFrames) / static_cast<int>(engine->framesPerPeriod()) + 8;
	for (int period = 0; period < flush; ++period) { engine->renderNextPeriod(); }
	return left;
}

//! The first audible frame of @a audio, or -1.
int onsetOf(const std::vector<float>& audio)
{
	for (std::size_t i = 0; i < audio.size(); ++i)
	{
		if (std::abs(audio[i]) > Audible) { return static_cast<int>(i); }
	}
	return -1;
}

QByteArray sha256(const std::vector<float>& audio)
{
	return QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(audio.data()),
		static_cast<qsizetype>(audio.size() * sizeof(float))), QCryptographicHash::Sha256).toHex();
}

//! pdc.report's latency_frames for the track input named @a name, or -1.
int reportedTrackLatency(const QString& name)
{
	const ControlResult report = run(QStringLiteral("pdc.report"));
	for (const QJsonValue& entry : report.result.value(QStringLiteral("track_inputs")).toArray())
	{
		const QJsonObject input = entry.toObject();
		if (input.value(QStringLiteral("name")).toString() == name) { return input.value(QStringLiteral("latency_frames")).toInt(); }
	}
	return -1;
}

} // namespace

class InstrumentLatencyPdcTest : public QObject
{
	Q_OBJECT

private:
	//! What one format's subject looks like.
	struct Subject
	{
		const char* env;
		QString format;
		QString key;      //!< the plugin.list field that names the class
		QString value;    //!< its value for the fixture's class
		QString fixtureDir;
		float level;      //!< what one sounding note writes
	};

	static Subject clap()
	{
		return {ClapEnv, QStringLiteral("clap"), QStringLiteral("clap_id"), QStringLiteral("org.lmms.test.clap-instrument"),
			QStringLiteral(CLAP_TEST_INSTRUMENT_DIR), 0.25f};
	}

	static Subject vst3()
	{
		return {Vst3Env, QStringLiteral("vst3"), QStringLiteral("name"), QStringLiteral("Zene VST3 Test Instrument"),
			QStringLiteral(VST3_TEST_INSTRUMENT_DIR), 0.5f};
	}

	static Subject lv2()
	{
		return {Lv2Env, QStringLiteral("lv2"), QStringLiteral("uri"), QStringLiteral("urn:zene:test:lv2-instrument"),
			QStringLiteral(LV2_TEST_INSTRUMENT_DIR), 0.25f};
	}

	//! Skips unless this build has the host module and the fixture; points the plug-in scan at the fixture.
	static bool available(const Subject& subject, const char* module)
	{
		if (getPluginFactory()->pluginInfo(module).isNull()) { return false; }
		if (subject.fixtureDir.isEmpty() || !QFileInfo::exists(subject.fixtureDir)) { return false; }
		// CLAP and VST3 are scanned in the plug-in directory; LV2 is on LV2_PATH (initTestCase).
		if (subject.format != QLatin1String("lv2")) { ConfigManager::inst()->setVSTDir(subject.fixtureDir); }
		return true;
	}

	InstrumentTrack* m_reference = nullptr;
	InstrumentTrack* m_latent = nullptr;
	int m_base = -1;

	//! A fresh project: the reference track (created with the variable unset), its base onset, then the
	//! latent track (created with the variable set to Latency, which stays set while it plays).
	void buildPair(const Subject& subject, const QString& latentValue = QString())
	{
		Engine::getSong()->clearProject();
		qunsetenv(subject.env);
		const QString reference = deviceOf(subject.format, subject.key, subject.value);
		QVERIFY2(!reference.isEmpty(), qPrintable(QStringLiteral("plugin.list format=%1 lacks the fixture").arg(subject.format)));
		m_reference = addPlayingTrack(reference, QStringLiteral("pdc-reference"));
		QVERIFY(m_reference != nullptr);
		const std::vector<float> alone = render();
		m_base = onsetOf(alone);
		QVERIFY2(m_base >= 0, "the reference instrument rendered no sound");
		QCOMPARE(alone[static_cast<std::size_t>(m_base)], subject.level);
		// The reference is a zero-latency hosted instrument: this hash is the same before and after MX-157.
		std::printf("INST_PDC_EVIDENCE %s zero_latency_reference_sha256=%s onset=%d\n", qPrintable(subject.format),
			sha256(alone).constData(), m_base);
		qputenv(subject.env, QByteArray::number(Latency));
		m_latent = addPlayingTrack(deviceOf(subject.format, subject.key, latentValue.isEmpty() ? subject.value : latentValue),
			QStringLiteral("pdc-latent"));
		QVERIFY(m_latent != nullptr);
		waitForLatency(Latency);
	}

	//! Renders a period (an LV2 plug-in writes its latency port as it runs) and lets the hosts' poll timers
	//! run until the latent track reports @a latency. Asserts nothing: the measurements below do.
	void waitForLatency(int latency)
	{
		Engine::audioEngine()->renderNextPeriod();
		const bool reported = QTest::qWaitFor(
			[this, latency] { return m_latent->audioBusHandle()->latencyFrames() == latency; }, PollMs);
		std::printf("INST_PDC_EVIDENCE latency %d reported by the poll: %s\n", latency, reported ? "yes" : "no");
		Engine::audioEngine()->renderNextPeriod();  // the mixer's alignment pass reads it
	}

	//! Renders 2-4 of the file comment against @a latency.
	void provesAlignment(const Subject& subject, int latency)
	{
		m_latent->setMuted(true);
		const int referenceOnset = onsetOf(render());
		m_latent->setMuted(false);
		m_reference->setMuted(true);
		const int latentOnset = onsetOf(render());
		m_reference->setMuted(false);
		const std::vector<float> both = render();
		const int bothOnset = onsetOf(both);
		evidence("base=%d reference_with_latent=%d latent=%d both=%d", m_base, referenceOnset, latentOnset, bothOnset);
		QCOMPARE(latentOnset - m_base, latency);  // the fixture really is that late
		QCOMPARE(referenceOnset - m_base, latency);  // so PDC delays the other track by exactly that much
		QCOMPARE(bothOnset, latentOnset);
		QCOMPARE(both[static_cast<std::size_t>(bothOnset)], 2.0f * subject.level);  // one step: both at once
		QCOMPARE(Engine::mixer()->totalLatencyFrames(), latency);
		QCOMPARE(m_latent->audioBusHandle()->latencyFrames(), latency);
		QCOMPARE(reportedTrackLatency(QStringLiteral("pdc-latent")), latency);
		QCOMPARE(reportedTrackLatency(QStringLiteral("pdc-reference")), 0);
	}

	//! The plug-in signals a new count; its host's GUI poll picks it up, and PDC follows on rendered audio.
	void provesAChangeIsPickedUp(const Subject& subject)
	{
		qputenv(subject.env, QByteArray::number(ChangedLatency));
		waitForLatency(ChangedLatency);  // the plug-in sees the new count in its next block and says so
		provesAlignment(subject, ChangedLatency);
	}

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // the build's host modules
#endif
		qunsetenv(ClapEnv);
		qunsetenv(Vst3Env);
		qunsetenv(Lv2Env);
		// The LV2 fixture's bundle is found the way lilv finds any: on LV2_PATH, read when the engine starts.
		if (!QStringLiteral(LV2_TEST_INSTRUMENT_DIR).isEmpty()) { qputenv("LV2_PATH", LV2_TEST_INSTRUMENT_DIR); }
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		std::printf("INST_PDC_EVIDENCE frames_per_period=%d latency=%d changed=%d\n",
			static_cast<int>(Engine::audioEngine()->framesPerPeriod()), Latency, ChangedLatency);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		qunsetenv(ClapEnv);
		qunsetenv(Vst3Env);
		qunsetenv(Lv2Env);
		Engine::destroy();
	}

	//! CLAP: the instrument reports Latency frames and the reference track is delayed by exactly that.
	void clapInstrumentLatencyAlignsTheOtherTracks()
	{
		if (!available(clap(), "clapinstrument")) { QSKIP("this build has no clapinstrument module or CLAP fixture"); }
		buildPair(clap());
		provesAlignment(clap(), Latency);
	}

	//! CLAP: request_restart + a new count at activate() reaches PDC through the host's poll.
	void clapLatencyChangeIsPickedUp()
	{
		if (!available(clap(), "clapinstrument")) { QSKIP("this build has no clapinstrument module or CLAP fixture"); }
		buildPair(clap());
		provesAChangeIsPickedUp(clap());
	}

	/*! CLAP multi-output (MX-114): the latent instrument's Aux bus, routed to its own channel, is as late as
	 *  its main output. The Aux channel's alignment must count that, or the master delays the Aux channel
	 *  a second time (main and Aux would then sound Latency apart). Every source starts on one frame. */
	void routedOutputBusIsAlignedWithTheMainOutput()
	{
		if (!available(clap(), "clapinstrument")) { QSKIP("this build has no clapinstrument module or CLAP fixture"); }
		buildPair(clap(), QStringLiteral("org.lmms.test.clap-instrument-multiout"));
		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		const ControlResult routed = run(QStringLiteral("plugin.output_bus_route"), {{QStringLiteral("target"),
			control::trackIdOf(m_latent)}, {QStringLiteral("bus"), 1},
			{QStringLiteral("channel"), control::channelIdOf(mixer->mixerChannel(index))}});
		QVERIFY2(routed.ok, qPrintable(routed.errorMessage));
		const std::vector<float> all = render();
		const int onset = onsetOf(all);
		evidence("base=%d all=%d aux_channel_alignment=%d total=%d", m_base, onset, mixer->channelInputLatency(
			static_cast<mix_ch_t>(index)), mixer->totalLatencyFrames());
		QCOMPARE(onset - m_base, Latency);
		// reference 0.25 + main 0.25 + Aux 0.125 (TEST_INSTRUMENT_AUX_SCALE), all on the same frame
		QCOMPARE(all[static_cast<std::size_t>(onset)], 0.625f);
		QCOMPARE(mixer->totalLatencyFrames(), Latency);
	}

	/*! The realtime rule on the paths MX-157 touches: the mixer's alignment pass (which walks the latent
	 *  instruments' routed buses), the latent track's handle and a routed bus's delivery allocate nothing. */
	void theAlignmentPassAndTheRoutedBusAllocateNothing()
	{
		if (!available(clap(), "clapinstrument")) { QSKIP("this build has no clapinstrument module or CLAP fixture"); }
		buildPair(clap(), QStringLiteral("org.lmms.test.clap-instrument-multiout"));
		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		QVERIFY(run(QStringLiteral("plugin.output_bus_route"), {{QStringLiteral("target"), control::trackIdOf(m_latent)},
			{QStringLiteral("bus"), 1}, {QStringLiteral("channel"), control::channelIdOf(mixer->mixerChannel(index))}}).ok);
		Engine::audioEngine()->renderNextPeriod();
		InstrumentOutputBuses* buses = m_latent->instrument()->outputBuses();
		QVERIFY(buses != nullptr);
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		mixer->prepareMasterMix();
		m_latent->audioBusHandle()->doProcessing();
		buses->send(1, fpp, false);
		test::tlCountAllocations = false;
		QCOMPARE(static_cast<qulonglong>(test::tlAllocationCount), static_cast<qulonglong>(0));
		QCOMPARE(mixer->totalLatencyFrames(), Latency);
	}

	//! VST3: the same proof against the VST3 fixture (getLatencySamples).
	void vst3InstrumentLatencyAlignsTheOtherTracks()
	{
		if (!available(vst3(), "vst3instrument")) { QSKIP("this build has no vst3instrument module or VST3 fixture"); }
		buildPair(vst3());
		provesAlignment(vst3(), Latency);
	}

	//! VST3: restartComponent(kLatencyChanged) reaches PDC through the host's poll.
	void vst3LatencyChangeIsPickedUp()
	{
		if (!available(vst3(), "vst3instrument")) { QSKIP("this build has no vst3instrument module or VST3 fixture"); }
		buildPair(vst3());
		provesAChangeIsPickedUp(vst3());
	}

	//! LV2: the same proof against the LV2 fixture (its lv2:reportsLatency port).
	void lv2InstrumentLatencyAlignsTheOtherTracks()
	{
		if (!available(lv2(), "lv2instrument")) { QSKIP("this build has no lv2instrument module or LV2 fixture"); }
		buildPair(lv2());
		provesAlignment(lv2(), Latency);
	}

	//! LV2: a new value on the latency port reaches PDC through the host's poll.
	void lv2LatencyChangeIsPickedUp()
	{
		if (!available(lv2(), "lv2instrument")) { QSKIP("this build has no lv2instrument module or LV2 fixture"); }
		buildPair(lv2());
		provesAChangeIsPickedUp(lv2());
	}
};

QTEST_GUILESS_MAIN(InstrumentLatencyPdcTest)
#include "InstrumentLatencyPdcTest.moc"
