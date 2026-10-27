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
 * Three further cases measure the LIMITS the limitations page documents against this behaviour (Q050-169):
 * the over-capacity CLAMP (an instrument above LatencyCompensation::MaxFrames is delayed by MaxFrames and
 * the total says so, with no chain warning), the AUTOMATION exclusion (AutomationLatency stamps a clip on
 * the instrument's own parameter, or on an effect of its track's chain, at 0 - never the instrument's
 * latency - while the mixer channel it feeds does see the alignment), and LIVE MONITORING
 * (transport.low_latency stops a latent EFFECT but nothing bypasses the instrument: the reference stays
 * delayed by its latency). The page that writes all of this down is checked here too:
 * theLimitationsPageStatesWhichHostsReportLatency reads docs/KNOWN-LIMITATIONS.md and fails if it stops
 * naming the hosts that report, the paths that do not, or the limits stated against them.
 * Nothing in the assertions uses the MX-157 API: the same file compiles against the tree before it, which
 * is the red run.
 */

#include <QtTest>

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <vector>

#include "AllocationProbe.h"
#include "PhaseDMixerTestSupport.h"

#include "AudioBusHandle.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationLatency.h"
#include "AutomationTrack.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentOutputBuses.h"
#include "InstrumentTrack.h"
#include "LatencyCompensation.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "PluginFactory.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

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
//! An instrument latency above the delay lines' capacity (LatencyCompensation::MaxFrames, 16384): the
//! over-capacity case the limitations page documents. The CLAP fixture's own bound is 32768.
constexpr int OverCapacityLatency = LatencyCompensation::MaxFrames + 3616;  // 20000
//! The latent effect the low-latency case puts on the instrument's own chain.
constexpr int EffectDelay = 2000;
//! Periods rendered per measurement: the note sounds throughout (it is a bar long).
constexpr int Periods = 8;
//! Q050-174: periods a render aligned to the EffectDelay path needs (its onset is past 8 periods).
constexpr int AlignedPeriods = 24;
//! Anything above this is sound; the fixtures write exact constants or exact zero.
constexpr float Audible = 1e-6f;

//! How long a host's poll timer gets to notice a latency (the slowest, the LV2 host's, runs every 500 ms).
constexpr int PollMs = 3000;

const char* const ClapEnv = "CLAP_TEST_INSTRUMENT_LATENCY";
const char* const Vst3Env = "VST3_TEST_INSTRUMENT_LATENCY";
const char* const Lv2Env = "LV2_TEST_INSTRUMENT_LATENCY";

//! Counts the once-only PDC capacity diagnostic (EffectChain: "PDC delay-line capacity exceeded") while
//! forwarding every other message, so a test can assert that an over-capacity INSTRUMENT logs nothing.
int g_capacityWarnings = 0;
QtMessageHandler g_previousMessageHandler = nullptr;

void countCapacityWarnings(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
	if (type == QtWarningMsg && message.contains(QStringLiteral("PDC delay-line capacity")))
	{
		++g_capacityWarnings;
	}
	if (g_previousMessageHandler != nullptr) { g_previousMessageHandler(type, context, message); }
}

//! Installs the counter for its lifetime and restores whatever handler was there (QVERIFY returns early).
struct CapacityWarningGuard
{
	CapacityWarningGuard()
	{
		g_capacityWarnings = 0;
		g_previousMessageHandler = qInstallMessageHandler(countCapacityWarnings);
	}
	~CapacityWarningGuard() { qInstallMessageHandler(g_previousMessageHandler); }
};

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

//! Plays the song from tick 0 for @a periods periods; the master's left channel. Then lets every delay line,
//! release and fixture ring run out, so the next render starts from silence. A case whose onsets sit deep
//! (the over-capacity clamp) asks for more periods than the default the alignment cases use.
std::vector<float> render(int periods = Periods)
{
	AudioEngine* engine = Engine::audioEngine();
	Song* song = Engine::getSong();
	song->setPlayPos(0, Song::PlayMode::Song);
	song->playSong();
	std::vector<float> left;
	for (int period = 0; period < periods; ++period)
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
	//! latent track (created with the variable set to @a latency, which stays set while it plays).
	void buildPair(const Subject& subject, const QString& latentValue = QString(), int latency = Latency)
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
		qputenv(subject.env, QByteArray::number(latency));
		m_latent = addPlayingTrack(deviceOf(subject.format, subject.key, latentValue.isEmpty() ? subject.value : latentValue),
			QStringLiteral("pdc-latent"));
		QVERIFY(m_latent != nullptr);
		waitForLatency(latency);
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

	//! Q050-174: a render with every track but @a track muted (each still counts in the alignment).
	std::vector<float> renderAlone(InstrumentTrack* track)
	{
		InstrumentTrack* other = track == m_latent ? m_reference : m_latent;
		other->setMuted(true);
		std::vector<float> audio = render(AlignedPeriods);
		other->setMuted(false);
		return audio;
	}

	//! Q050-174: arms (into the take @a file) or disarms @a track through track.set_arm.
	static bool setArmed(InstrumentTrack* track, bool armed, const QString& file = QString())
	{
		QJsonObject args{{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("armed"), armed}};
		if (!file.isEmpty()) { args.insert(QStringLiteral("file"), file); }
		const ControlResult result = run(QStringLiteral("track.set_arm"), args);
		if (!result.ok) { std::printf("INST_PDC_EVIDENCE track.set_arm: %s\n", qPrintable(result.errorMessage)); }
		return result.ok;
	}

	//! Q050-174: a saved project's text without its append-only save journal (<z:provenance>).
	static QString withoutJournal(const QString& path)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) { return QString(); }
		QString text = QString::fromUtf8(file.readAll());
		text.remove(QRegularExpression(QStringLiteral("<z:provenance[\\s\\S]*?</z:provenance>")));
		return text;
	}

	//! A one-object automation clip on @a model at the song's start: what AutomationLatency then stamps
	//! with the model's upstream frames.
	AutomationClip* automationClipOn(AutomatableModel* model)
	{
		auto* track = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
		if (track == nullptr || model == nullptr) { return nullptr; }
		auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
		clip->addObject(model);
		return clip;
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

	//! Q050-174: a case that fails half-way must not leave a track armed or the session mode on for the next.
	void cleanup()
	{
		run(QStringLiteral("record.disarm_all"));
		run(QStringLiteral("transport.low_latency"), {{QStringLiteral("enabled"), false}});
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

	/*! Q050-169: the pages that write this behaviour down must say what this file measures. The
	 *  limitations page's supported-host bullet no longer denies instrument latency compensation; the
	 *  "Hosted instrument latency" section names the hosts that report (CLAP, VST3, LV2) and the paths
	 *  that do not (out-of-process, VST2 (Vestige), Carla; built-in instruments are 0), keeps the limits
	 *  stated against that behaviour - the 100 ms / 500 ms polls, the 16384-frame clamp, the automation
	 *  exclusion and live monitoring - and pins each limit to the case that measures it. README carries
	 *  the same reconciliation, so the two documents cannot drift apart again. Both pages are read at run
	 *  time, so a build whose docs are unreachable SKIPs rather than reports a pass it did not measure. */
	void theLimitationsPageStatesWhichHostsReportLatency()
	{
		QFile file(QCoreApplication::applicationDirPath() + QStringLiteral("/../../docs/KNOWN-LIMITATIONS.md"));
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			QSKIP("docs/KNOWN-LIMITATIONS.md is not reachable from this build directory");
		}
		const QString flat = QString::fromUtf8(file.readAll()).simplified();
		QVERIFY2(!flat.contains(QStringLiteral("no instrument latency compensation")),
			"docs/KNOWN-LIMITATIONS.md denies instrument latency compensation beside the supported hosts");
		const QStringList claims = {
			QStringLiteral("see \"Hosted instrument latency\""),
			QStringLiteral("Only the native CLAP, VST3 and LV2 instrument hosts report"),
			QStringLiteral("those three are the supported instrument hosts for delay compensation"),
			QStringLiteral("Built-in instruments report 0"),
			QStringLiteral("out-of-process, VST2 (Vestige) and Carla paths are unsupported for delay compensation, not for hosting"),
			QStringLiteral("the host's 100 ms poll answers"),
			QStringLiteral("next 100 ms poll"),
			QStringLiteral("latency port on a 500 ms poll"),
			QStringLiteral("LatencyCompensation::MaxFrames`, 16384 frames"),
			QStringLiteral("clamped like a chain's"),
			QStringLiteral("Automation compensation (`AutomationLatency`) does not count an instrument's latency"),
			QStringLiteral("nothing bypasses an instrument the way the low-latency mode bypasses latent effects"),
			// The three limits are each pinned to the case that measures them, so a limit that stops
			// being tested (or a test renamed out from under the page) fails here.
			QStringLiteral("instrumentLatencyAboveTheCapacityIsClampedAndDiagnosed"),
			QStringLiteral("automationCompensationDoesNotCountTheInstrumentsLatency"),
			QStringLiteral("lowLatencyModeDoesNotBypassALatentInstrument"),
		};
		for (const QString& claim : claims)
		{
			QVERIFY2(flat.contains(claim),
				qPrintable(QStringLiteral("the limitations page no longer states: %1").arg(claim)));
		}
		// README carries the same reconciliation (the denial is gone, the positive claim is in).
		QFile readme(QCoreApplication::applicationDirPath() + QStringLiteral("/../../README.md"));
		if (readme.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			const QString page = QString::fromUtf8(readme.readAll()).simplified();
			QVERIFY2(!page.contains(QStringLiteral("no instrument latency compensation")),
				"README.md reintroduces the instrument-latency-compensation denial");
			QVERIFY2(page.contains(QStringLiteral("reports its latency")),
				"README.md no longer states the instrument reports its latency to delay compensation");
		}
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

	/*! Q050-169: the over-capacity clamp the limitations page documents, measured on rendered audio.
	 *  An instrument reports 17000 frames (above LatencyCompensation::MaxFrames, 16384); the delay
	 *  line can only hold MaxFrames, so the reference is delayed by MaxFrames - NOT by the full
	 *  17000 - and the two onsets land MaxFrames apart with the clamped-away remainder between them.
	 *  The published total says so (totalLatencyFrames clamped to MaxFrames, pdc.report's
	 *  delay_line_clamped true), and - unlike a chain - no capacity warning is logged, because the
	 *  once-only diagnostic lives in EffectChain::refreshLatency, which an instrument is not part of. */
	void instrumentLatencyAboveTheCapacityIsClampedAndDiagnosed()
	{
		if (!available(clap(), "clapinstrument")) { QSKIP("this build has no clapinstrument module or CLAP fixture"); }
		CapacityWarningGuard guard;
		buildPair(clap(), QString(), OverCapacityLatency);
		m_latent->setMuted(true);
		const int referenceOnset = onsetOf(render(80));
		m_latent->setMuted(false);
		m_reference->setMuted(true);
		const int latentOnset = onsetOf(render(80));
		m_reference->setMuted(false);
		evidence("over-cap base=%d reference=%d latent=%d total=%d", m_base, referenceOnset, latentOnset,
			Engine::mixer()->totalLatencyFrames());
		QCOMPARE(latentOnset - m_base, OverCapacityLatency);  // the fixture really is that late
		QCOMPARE(referenceOnset - m_base, LatencyCompensation::MaxFrames);  // the clamp: only MaxFrames of delay
		QCOMPARE(latentOnset - referenceOnset, OverCapacityLatency - LatencyCompensation::MaxFrames);
		// The published total is clamped to what the lines can apply, and pdc.report says so.
		QCOMPARE(Engine::mixer()->totalLatencyFrames(), LatencyCompensation::MaxFrames);
		QCOMPARE(reportedTrackLatency(QStringLiteral("pdc-latent")), OverCapacityLatency);  // the report is the raw figure
		const ControlResult report = run(QStringLiteral("pdc.report"));
		QVERIFY2(report.result.value(QStringLiteral("delay_line_clamped")).toBool(),
			"pdc.report does not say the delay line clamped");
		QCOMPARE(report.result.value(QStringLiteral("delay_line_capacity_frames")).toInt(),
			static_cast<int>(LatencyCompensation::MaxFrames));
		// An instrument's over-capacity latency logs no warning (the diagnostic is a chain's).
		QCOMPARE(g_capacityWarnings, 0);
	}

	/*! Q050-169: the automation limit the limitations page documents, measured with a REAL latent
	 *  instrument. AutomationLatency counts a chain's devices and a channel's alignment, never the
	 *  instrument that FEEDS the track - so a clip on a parameter of the latent instrument, and a
	 *  clip on an effect appended to that instrument track's OWN chain, are both stamped 0 and read
	 *  `Latency` frames early against the instrument's audio. A model on the mixer channel the
	 *  instrument feeds is the contrast: it DOES see the alignment, so the 0 is specific, not a dead
	 *  path. (The channel case is W3EngineTest's; this is the instrument-track case.) */
	void automationCompensationDoesNotCountTheInstrumentsLatency()
	{
		if (!available(clap(), "clapinstrument")) { QSKIP("this build has no clapinstrument module or CLAP fixture"); }
		buildPair(clap());
		QCOMPARE(m_latent->audioBusHandle()->latencyFrames(), Latency);
		Engine::audioEngine()->renderNextPeriod();  // the mixer's alignment pass publishes it

		// A parameter OF the latent instrument (the CLAP fixture's Gain): upstreamFrames is 0.
		AutomatableModel* instrumentParam = m_latent->instrument()->parameterModel(0);
		QVERIFY2(instrumentParam != nullptr, "the latent CLAP fixture exposes no automatable parameter");
		QCOMPARE(AutomationLatency::upstreamFrames(instrumentParam), 0);

		// An EFFECT on that instrument track's OWN chain: the chain starts at alignment 0, so the
		// instrument's latency (the source's, not the chain's) is not counted here either.
		EffectChain* trackChain = m_latent->audioBusHandle()->effects();
		auto* effect = new partd::LatentDelayEffect(trackChain, 0);  // reports 0, so the alignment is undisturbed
		trackChain->appendEffect(effect);
		auto* effectParam = new FloatModel(0.f, 0.f, 1.f, 0.001f, effect, QStringLiteral("Probe"));
		QCOMPARE(AutomationLatency::upstreamFrames(effectParam), 0);

		// The stamped clips agree: both are 0, not Latency.
		AutomationClip* instrumentClip = automationClipOn(instrumentParam);
		AutomationClip* effectClip = automationClipOn(effectParam);
		QVERIFY(instrumentClip != nullptr && effectClip != nullptr);
		AutomationLatency::instance().setEnabled(true);
		QCOMPARE(instrumentClip->latencyFrames(), 0);
		QCOMPARE(effectClip->latencyFrames(), 0);
		AutomationLatency::instance().setEnabled(false);

		// Contrast: the channel the instrument feeds DOES see the alignment (Latency), so the 0 above
		// is the instrument-specific exclusion the page documents, not a live-path that never runs.
		MixerChannel* channel = Engine::mixer()->mixerChannel(m_latent->mixerChannelModel()->value());
		QCOMPARE(AutomationLatency::upstreamFrames(&channel->m_volumeModel), Latency);
	}

	/*! Q050-169: the live-monitoring limit the limitations page documents, measured on rendered
	 *  audio. transport.low_latency (MX-159) stops latent EFFECTS (Effect::setDontRun) whose latency
	 *  is over the threshold; an instrument is not an Effect in any chain, so nothing bypasses it. A
	 *  latent EFFECT on a spare channel in scope IS stopped (dontRun true) while the mode is on - the
	 *  contrast the page draws - and meanwhile the latent INSTRUMENT is not: the reference it feeds
	 *  stays delayed by exactly its latency, and the total is unchanged. */
	void lowLatencyModeDoesNotBypassALatentInstrument()
	{
		if (!available(clap(), "clapinstrument")) { QSKIP("this build has no clapinstrument module or CLAP fixture"); }
		buildPair(clap());
		// A latent effect on a spare channel, in scope for 'all', to show what the mode DOES stop.
		Mixer* mixer = Engine::mixer();
		EffectChain* spareChain = &mixer->mixerChannel(mixer->createChannel())->m_fxChain;
		auto* probe = new partd::LatentDelayEffect(spareChain, Latency);
		spareChain->appendEffect(probe);
		Engine::audioEngine()->renderNextPeriod();
		// Baseline: normal PDC delays the reference by the instrument's latency.
		const int normal = onsetOf(render());
		QCOMPARE(normal - m_base, Latency);
		QVERIFY(!probe->dontRun());
		// Arm low-latency monitoring with a threshold (1 ms ~ 44 frames) BOTH the instrument's latency
		// and the spare effect's (257 frames each) clear. If the mode applied to instruments the
		// reference would stop being delayed; it does not - but the spare EFFECT above is stopped,
		// which is the contrast the page draws.
		const ControlResult on = run(QStringLiteral("transport.low_latency"),
			{{QStringLiteral("enabled"), true}, {QStringLiteral("max_ms"), 1}, {QStringLiteral("scope"), QStringLiteral("all")}});
		QVERIFY2(on.ok, qPrintable(on.errorMessage));
		QVERIFY2(probe->dontRun(), "low-latency mode did not stop the latent EFFECT it is meant to stop");
		const int lowLatency = onsetOf(render());
		QCOMPARE(lowLatency - m_base, Latency);  // the instrument is still delayed: not bypassed
		QCOMPARE(Engine::mixer()->totalLatencyFrames(), Latency);
		// Off restores the session state (the effect runs again).
		const ControlResult off = run(QStringLiteral("transport.low_latency"), {{QStringLiteral("enabled"), false}});
		QVERIFY2(off.ok, qPrintable(off.errorMessage));
		QVERIFY(!probe->dontRun());
	}

	/*! Q050-174 AC1/AC2/AC3: low-latency monitoring of an ARMED latent instrument, on rendered audio. The
	 *  unarmed reference track carries a latent effect (EffectDelay frames) on its own chain - a later
	 *  path the armed scope does not cover - so normal PDC delays the latent instrument by
	 *  EffectDelay - Latency to land on it. With the mode on (default scope armed, 3 ms) and the latent
	 *  track NOT armed, nothing changes; armed and re-evaluated, the instrument still plays but sounds
	 *  after its own latency only, the result names its path and the frames skipped, and the unarmed
	 *  reference renders bit-identically. Off restores the normal render bit-identically. */
	void lowLatencyMonitorsTheArmedLatentInstrumentWithoutAlignment()
	{
		if (!available(clap(), "clapinstrument")) { QSKIP("this build has no clapinstrument module or CLAP fixture"); }
		buildPair(clap());
		QTemporaryDir takes;
		QVERIFY(takes.isValid());
		EffectChain* referenceChain = m_reference->audioBusHandle()->effects();
		auto* later = new partd::LatentDelayEffect(referenceChain, EffectDelay);
		referenceChain->appendEffect(later);
		Engine::audioEngine()->renderNextPeriod();
		const std::vector<float> normalLatent = renderAlone(m_latent);
		const std::vector<float> normalReference = renderAlone(m_reference);
		const std::vector<float> normalBoth = render(AlignedPeriods);
		QCOMPARE(onsetOf(normalLatent) - m_base, EffectDelay);  // aligned to the later path
		QCOMPARE(onsetOf(normalReference) - m_base, EffectDelay);

		// Unarmed: the default scope leaves the latent instrument's path alone.
		// The documented defaults (an earlier case left the session at scope all, 1 ms).
		const ControlResult unarmed = run(QStringLiteral("transport.low_latency"), {{QStringLiteral("enabled"), true},
			{QStringLiteral("scope"), QStringLiteral("armed")}, {QStringLiteral("max_ms"), 3}});
		QVERIFY2(unarmed.ok, qPrintable(unarmed.errorMessage));
		QCOMPARE(unarmed.result.value(QStringLiteral("scope")).toString(), QStringLiteral("armed"));
		QVERIFY(unarmed.result.value(QStringLiteral("instruments")).toArray().isEmpty());
		QVERIFY(!m_latent->audioBusHandle()->monitorUnaligned());
		QCOMPARE(sha256(renderAlone(m_latent)), sha256(normalLatent));

		// Armed, and called again: the policy is re-evaluated and takes the instrument's path.
		QVERIFY(setArmed(m_latent, true, takes.filePath(QStringLiteral("take.wav"))));
		const ControlResult on = run(QStringLiteral("transport.low_latency"));
		QVERIFY2(on.ok, qPrintable(on.errorMessage));
		const QJsonArray paths = on.result.value(QStringLiteral("instruments")).toArray();
		QCOMPARE(paths.size(), 1);
		const QJsonObject path = paths.at(0).toObject();
		QCOMPARE(path.value(QStringLiteral("track")).toString(), control::trackIdOf(m_latent));
		QCOMPARE(path.value(QStringLiteral("name")).toString(), QStringLiteral("pdc-latent"));
		QCOMPARE(path.value(QStringLiteral("channel")).toString(), control::channelIdOf(Engine::mixer()->mixerChannel(0)));
		QCOMPARE(path.value(QStringLiteral("latency_frames")).toInt(), Latency);
		QCOMPARE(path.value(QStringLiteral("alignment_skipped_frames")).toInt(), EffectDelay - Latency);
		QVERIFY(on.result.value(QStringLiteral("bypassed")).toArray().isEmpty());  // the unarmed path's effect runs
		QVERIFY(!later->dontRun());
		QVERIFY(m_latent->audioBusHandle()->monitorUnaligned());
		QVERIFY(!m_reference->audioBusHandle()->monitorUnaligned());
		const std::vector<float> lowLatent = renderAlone(m_latent);
		const int lowOnset = onsetOf(lowLatent);
		evidence("low-latency base=%d normal=%d monitored=%d total=%d", m_base, onsetOf(normalLatent), lowOnset,
			Engine::mixer()->totalLatencyFrames());
		QCOMPARE(lowOnset - m_base, Latency);  // its own latency only, not the alignment
		QCOMPARE(lowLatent[static_cast<std::size_t>(lowOnset)], clap().level);  // still playing, not muted
		QCOMPARE(sha256(renderAlone(m_reference)), sha256(normalReference));  // the unarmed path is unchanged
		QCOMPARE(Engine::mixer()->totalLatencyFrames(), EffectDelay);

		// Off: the alignment is back, and so is the normal output.
		const ControlResult off = run(QStringLiteral("transport.low_latency"), {{QStringLiteral("enabled"), false}});
		QVERIFY2(off.ok, qPrintable(off.errorMessage));
		QVERIFY(off.result.value(QStringLiteral("instruments")).toArray().isEmpty());
		QVERIFY(!m_latent->audioBusHandle()->monitorUnaligned());
		QCOMPARE(sha256(renderAlone(m_latent)), sha256(normalLatent));
		QCOMPARE(sha256(render(AlignedPeriods)), sha256(normalBoth));
		QVERIFY(setArmed(m_latent, false));
	}

	/*! Q050-174 AC3: the mode is session state. Turning it on records no undo step, the project saved
	 *  while it is on is the project saved while it is off (the save journal aside), and opening that
	 *  file brings back no instrument path monitored unaligned. */
	void theLowLatencyModeIsNeitherSavedNorUndone()
	{
		if (!available(clap(), "clapinstrument")) { QSKIP("this build has no clapinstrument module or CLAP fixture"); }
		buildPair(clap());
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QVERIFY(setArmed(m_latent, true, dir.filePath(QStringLiteral("take.wav"))));
		const int depth = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		const QString offFile = dir.filePath(QStringLiteral("off.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(offFile));
		const ControlResult on = run(QStringLiteral("transport.low_latency"), {{QStringLiteral("enabled"), true},
			{QStringLiteral("scope"), QStringLiteral("armed")}, {QStringLiteral("max_ms"), 3}});
		QVERIFY2(on.ok, qPrintable(on.errorMessage));
		QCOMPARE(on.result.value(QStringLiteral("instruments")).toArray().size(), 1);
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), depth);
		const QString onFile = dir.filePath(QStringLiteral("on.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(onFile));
		QCOMPARE(withoutJournal(onFile), withoutJournal(offFile));
		QVERIFY(setArmed(m_latent, false));
		Engine::getSong()->loadProject(onFile);
		int instruments = 0;
		for (Track* track : Engine::getSong()->tracks())
		{
			if (auto* instrument = dynamic_cast<InstrumentTrack*>(track))
			{
				++instruments;
				QVERIFY(!instrument->audioBusHandle()->monitorUnaligned());
			}
		}
		QCOMPARE(instruments, 2);
		QVERIFY(run(QStringLiteral("transport.low_latency"), {{QStringLiteral("enabled"), false}}).ok);
	}
};

QTEST_GUILESS_MAIN(InstrumentLatencyPdcTest)
#include "InstrumentLatencyPdcTest.moc"
