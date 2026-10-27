/*
 * InstrumentLowLatencyMonitorTest.cpp - Q050-174 (MX-159): transport.low_latency monitors an armed LATENT
 * instrument without its PDC alignment delay, measured on rendered audio.
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
 * The subject is the in-tree CLAP fixture (tests/data/clap-test-plugin/clap-test-instrument.c), loaded through
 * plugin.list / plugin.load on real instrument tracks and played by a real MIDI clip through the engine's own
 * period. An instance created while CLAP_TEST_INSTRUMENT_LATENCY is set reports that count and really delays
 * its output by it (InstrumentLatencyPdcTest proves the plain alignment). Here the unarmed REFERENCE track
 * carries a latent effect of EffectDelay frames on its own chain - a later path the armed scope does not
 * cover - so normal PDC delays the latent instrument by EffectDelay - Latency to land on it. Low-latency
 * monitoring must take that alignment off the ARMED latent instrument only: it then sounds after its own
 * latency, still at the fixture's level; the unarmed reference renders bit-identically; off restores the
 * normal render bit-identically; and none of it is saved or journalled.
 */

#include <QtTest>

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <vector>

#include "PhaseDMixerTestSupport.h"

#include "AudioBusHandle.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "EffectChain.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "LatencyCompensation.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "PluginFactory.h"
#include "Song.h"
#include "Track.h"

#ifndef CLAP_TEST_INSTRUMENT_DIR
#define CLAP_TEST_INSTRUMENT_DIR ""
#endif

using namespace lmms;

namespace
{

//! Prime, so an off-by-a-period alignment cannot pass by accident; ~5.8 ms at 44.1 kHz, over the 3 ms default.
constexpr int Latency = 257;
//! The unarmed reference's latent effect: the later path normal PDC aligns the instrument to.
constexpr int EffectDelay = 2000;
//! Periods per render: the onsets sit past EffectDelay frames.
constexpr int Periods = 24;
constexpr float Audible = 1e-6f;
//! What one sounding note of the fixture writes.
constexpr float Level = 0.25f;
constexpr int PollMs = 3000;
const char* const ClapEnv = "CLAP_TEST_INSTRUMENT_LATENCY";

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString fixtureDevice()
{
	const ControlResult listed = run(QStringLiteral("plugin.list"), {{QStringLiteral("format"), QStringLiteral("clap")}});
	for (const QJsonValue& entry : listed.result.value(QStringLiteral("devices")).toArray())
	{
		const QJsonObject device = entry.toObject();
		if (device.value(QStringLiteral("clap_id")).toString() == QLatin1String("org.lmms.test.clap-instrument"))
		{
			return device.value(QStringLiteral("id")).toString();
		}
	}
	return QString();
}

//! A new instrument track named @a name carrying the fixture, with a bar-long note at tick 0.
InstrumentTrack* addPlayingTrack(const QString& name)
{
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	if (track == nullptr) { return nullptr; }
	if (!run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), control::trackIdOf(track)},
		{QStringLiteral("device"), fixtureDevice()}}).ok) { return nullptr; }
	track->setName(name);
	auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
	clip->addNote(Note(TimePos(DefaultTicksPerBar), TimePos(0), 60), false);
	return track;
}

//! Plays the song from tick 0; the master's left channel. Then lets every delay line and ring run out.
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

//! Arms (into the take @a file) or disarms @a track through track.set_arm.
bool setArmed(InstrumentTrack* track, bool armed, const QString& file = QString())
{
	QJsonObject args{{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("armed"), armed}};
	if (!file.isEmpty()) { args.insert(QStringLiteral("file"), file); }
	return run(QStringLiteral("track.set_arm"), args).ok;
}

//! The low-latency call with the documented defaults spelled out (scope armed, 3 ms).
ControlResult lowLatency(bool enabled)
{
	return run(QStringLiteral("transport.low_latency"), {{QStringLiteral("enabled"), enabled},
		{QStringLiteral("scope"), QStringLiteral("armed")}, {QStringLiteral("max_ms"), 3}});
}

//! A saved project's text without its append-only save journal (<z:provenance>).
QString withoutJournal(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	QString text = QString::fromUtf8(file.readAll());
	text.remove(QRegularExpression(QStringLiteral("<z:provenance[\\s\\S]*?</z:provenance>")));
	return text;
}

} // namespace

class InstrumentLowLatencyMonitorTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_reference = nullptr;
	InstrumentTrack* m_latent = nullptr;
	int m_base = -1;

	static bool available()
	{
		if (getPluginFactory()->pluginInfo("clapinstrument").isNull()) { return false; }
		if (!QFileInfo::exists(QStringLiteral(CLAP_TEST_INSTRUMENT_DIR))) { return false; }
		ConfigManager::inst()->setVSTDir(QStringLiteral(CLAP_TEST_INSTRUMENT_DIR));
		return true;
	}

	//! A fresh project: the plain reference (its onset alone is the base), then the latent track.
	void buildPair()
	{
		Engine::getSong()->clearProject();
		qunsetenv(ClapEnv);
		m_reference = addPlayingTrack(QStringLiteral("pdc-reference"));
		QVERIFY(m_reference != nullptr);
		m_base = onsetOf(render());
		QVERIFY2(m_base >= 0, "the reference instrument rendered no sound");
		qputenv(ClapEnv, QByteArray::number(Latency));
		m_latent = addPlayingTrack(QStringLiteral("pdc-latent"));
		QVERIFY(m_latent != nullptr);
		Engine::audioEngine()->renderNextPeriod();
		QVERIFY2(QTest::qWaitFor([this] { return m_latent->audioBusHandle()->latencyFrames() == Latency; }, PollMs),
			"the latent fixture's latency was not reported");
		Engine::audioEngine()->renderNextPeriod();
	}

	//! A render with the other track muted (it still counts in the alignment).
	std::vector<float> renderAlone(InstrumentTrack* track)
	{
		InstrumentTrack* other = track == m_latent ? m_reference : m_latent;
		other->setMuted(true);
		std::vector<float> audio = render();
		other->setMuted(false);
		return audio;
	}

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // the build's host modules
#endif
		qunsetenv(ClapEnv);
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
	}

	//! A case that fails half-way must not leave a track armed or the mode on for the next.
	void cleanup()
	{
		run(QStringLiteral("record.disarm_all"));
		lowLatency(false);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		qunsetenv(ClapEnv);
		Engine::destroy();
	}

	/*! AC1/AC2/AC3: unarmed, the mode leaves the latent instrument's path alone; armed and re-evaluated, it
	 *  still plays but sounds after its own latency only, the result names its path and the frames skipped,
	 *  and the unarmed reference renders bit-identically. Off restores the normal render bit-identically. */
	void lowLatencyMonitorsTheArmedLatentInstrumentWithoutAlignment()
	{
		if (!available()) { QSKIP("this build has no clapinstrument module or CLAP fixture"); }
		buildPair();
		QTemporaryDir takes;
		QVERIFY(takes.isValid());
		EffectChain* referenceChain = m_reference->audioBusHandle()->effects();
		auto* later = new partd::LatentDelayEffect(referenceChain, EffectDelay);
		referenceChain->appendEffect(later);
		Engine::audioEngine()->renderNextPeriod();
		const std::vector<float> normalLatent = renderAlone(m_latent);
		const std::vector<float> normalReference = renderAlone(m_reference);
		const std::vector<float> normalBoth = render();
		QCOMPARE(onsetOf(normalLatent) - m_base, EffectDelay);  // aligned to the later path
		QCOMPARE(onsetOf(normalReference) - m_base, EffectDelay);

		const ControlResult unarmed = lowLatency(true);
		QVERIFY2(unarmed.ok, qPrintable(unarmed.errorMessage));
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
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(0));
		QCOMPARE(path.value(QStringLiteral("track")).toString(), control::trackIdOf(m_latent));
		QCOMPARE(path.value(QStringLiteral("channel")).toString(), channel);
		QCOMPARE(path.value(QStringLiteral("path")).toString(), control::trackIdOf(m_latent) + QStringLiteral(" -> ") + channel);
		QCOMPARE(path.value(QStringLiteral("latency_frames")).toInt(), Latency);
		QCOMPARE(path.value(QStringLiteral("alignment_skipped_frames")).toInt(), EffectDelay - Latency);
		QVERIFY(on.result.value(QStringLiteral("bypassed")).toArray().isEmpty());  // the unarmed path's effect runs
		QVERIFY(!later->dontRun());
		QVERIFY(!m_reference->audioBusHandle()->monitorUnaligned());
		const std::vector<float> lowLatent = renderAlone(m_latent);
		const int lowOnset = onsetOf(lowLatent);
		std::printf("LOW_LATENCY_EVIDENCE base=%d normal=%d monitored=%d total=%d\n", m_base, onsetOf(normalLatent),
			lowOnset, Engine::mixer()->totalLatencyFrames());
		QCOMPARE(lowOnset - m_base, Latency);  // its own latency only, not the alignment
		QCOMPARE(lowLatent[static_cast<std::size_t>(lowOnset)], Level);  // still playing, not muted
		QCOMPARE(sha256(renderAlone(m_reference)), sha256(normalReference));  // the unarmed path is unchanged
		QCOMPARE(Engine::mixer()->totalLatencyFrames(), EffectDelay);

		const ControlResult off = lowLatency(false);
		QVERIFY2(off.ok, qPrintable(off.errorMessage));
		QVERIFY(off.result.value(QStringLiteral("instruments")).toArray().isEmpty());
		QVERIFY(!m_latent->audioBusHandle()->monitorUnaligned());
		QCOMPARE(sha256(renderAlone(m_latent)), sha256(normalLatent));
		QCOMPARE(sha256(render()), sha256(normalBoth));
	}

	/*! AC3: session state. Turning it on records no undo step, the project saved while it is on is the
	 *  project saved while it is off (the save journal aside), and opening it brings back no path unaligned. */
	void theLowLatencyModeIsNeitherSavedNorUndone()
	{
		if (!available()) { QSKIP("this build has no clapinstrument module or CLAP fixture"); }
		buildPair();
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QVERIFY(setArmed(m_latent, true, dir.filePath(QStringLiteral("take.wav"))));
		const int depth = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		const QString offFile = dir.filePath(QStringLiteral("off.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(offFile));
		const ControlResult on = lowLatency(true);
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
	}
};

QTEST_GUILESS_MAIN(InstrumentLowLatencyMonitorTest)
#include "InstrumentLowLatencyMonitorTest.moc"
