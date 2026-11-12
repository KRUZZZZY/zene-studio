/*
 * ClipEffectsTest.cpp - an audio clip's own realtime effects (Q050-111)
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

/*! The claims, each measured on the build's real amplifier module through the registered commands an agent sends
 *  (plugin.* with target clip-<n>, dsp.get_state):
 *   1. Two clips OVERLAP on one track; an amplifier at 50 % on the first one halves the first clip's audio and
 *      leaves the second's exactly as it was - in the span where only the second plays, the render is bit for
 *      bit the one without the effect, and in the overlap only the first clip's share is halved. A clip effect
 *      run after the track sums its clips (or on the track's chain) would halve the second clip too.
 *   2. Bypassing the effect renders the dry song bit for bit, and so does removing it.
 *   3. The chain - each device's plug-in, settings, order and bypass - survives a project save and reopen, and
 *      the reopened clip still processes; a clip element with no chain loads with none (an older project).
 *   4. add / reorder / bypass / edit / remove through the agent surface, each one control.undo step; a MIDI clip
 *      is refused as a target.
 *   5. The per-period processing allocates nothing. */

#include <QtTest>

#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "AllocationProbe.h"

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr int kSampleRate = 44100;
constexpr float kFirstLevel = 0.5f;
constexpr float kSecondLevel = 0.25f;
constexpr f_cnt_t kPeriod = 1024;

int ticksForOneSecond() { return static_cast<int>(kSampleRate / Engine::framesPerTick(kSampleRate)); }
int framesForTicks(int ticks) { return static_cast<int>(ticks * Engine::framesPerTick(kSampleRate)); }

std::shared_ptr<const SampleBuffer> constantBuffer(float level)
{
	const auto frames = static_cast<std::size_t>(2 * kSampleRate);
	return std::make_shared<const SampleBuffer>(
		std::vector<SampleFrame>(frames, SampleFrame(level, level)).data(), frames, kSampleRate);
}

int dataOffset(const QByteArray& wav)
{
	int offset = 12;
	while (offset + 8 <= wav.size())
	{
		qint32 size = 0;
		std::memcpy(&size, wav.constData() + offset + 4, sizeof(size));
		if (wav.mid(offset, 4) == QByteArrayLiteral("data")) { return offset + 8; }
		offset += 8 + size + (size & 1);
	}
	return -1;
}

float wavLeft(const QByteArray& wav, int frame)
{
	const int offset = dataOffset(wav);
	if (offset < 0 || offset + frame * 4 + 2 > wav.size()) { return -2.0f; }
	qint16 value = 0;
	std::memcpy(&value, wav.constData() + offset + frame * 4, sizeof(value));
	return static_cast<float>(value) / 32768.0f;
}

//! The PCM bytes of frames [from, to) - what "bit for bit" compares.
QByteArray wavSpan(const QByteArray& wav, int from, int to)
{
	const int offset = dataOffset(wav);
	return offset < 0 ? QByteArray() : wav.mid(offset + from * 4, (to - from) * 4);
}

ControlResult run(const QString& command, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(command, args);
}

//! The dev-<n> id of the build's amplifier, or empty when the module is not built.
QString amplifierDevice()
{
	const QJsonArray devices = run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
		.result.value(QStringLiteral("devices")).toArray();
	for (const QJsonValue& device : devices)
	{
		if (device.toObject().value(QStringLiteral("name")).toString() == QLatin1String("amplifier"))
		{
			return device.toObject().value(QStringLiteral("id")).toString();
		}
	}
	return {};
}

//! The devices dsp.get_state reports for @a target.
QJsonArray devicesOf(const QString& target)
{
	const QJsonArray chains = run(QStringLiteral("dsp.get_state"), {{QStringLiteral("target"), target}})
		.result.value(QStringLiteral("chains")).toArray();
	return chains.isEmpty() ? QJsonArray() : chains.first().toObject().value(QStringLiteral("devices")).toArray();
}

double parameterValue(const QJsonObject& device, const QString& name)
{
	for (const QJsonValue& p : device.value(QStringLiteral("parameters")).toArray())
	{
		if (p.toObject().value(QStringLiteral("name")).toString() == name) { return p.toObject().value(QStringLiteral("value")).toDouble(); }
	}
	return -1.0;
}

} // namespace

class ClipEffectsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the export below renders synchronously
		ControlRegistry::setReady(true);
		m_amplifier = amplifierDevice();
		QVERIFY2(!m_amplifier.isEmpty(), "this build lists no amplifier module (build the 'amplifier' target)");
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Claim 1 (acceptance: only the processed clip's output changes).
	void anEffectOnOneClipChangesOnlyThatClip()
	{
		buildSong();
		const QByteArray dry = render();
		const QString first = control::clipIdOf(firstClip());
		const QString fx = addAmplifier(first, 50.0);
		QVERIFY(!fx.isEmpty());
		const QByteArray wet = render();
		QVERIFY2(!dry.isEmpty() && !wet.isEmpty(), "the export produced no file");

		const int t = ticksForOneSecond();
		const int firstOnly = framesForTicks(t / 4);
		const int overlap = framesForTicks(3 * t / 4);
		const int secondOnly = framesForTicks(5 * t / 4);
		QVERIFY(std::fabs(wavLeft(dry, firstOnly) - kFirstLevel) < 0.002f);
		QVERIFY(std::fabs(wavLeft(dry, overlap) - (kFirstLevel + kSecondLevel)) < 0.002f);
		QVERIFY(std::fabs(wavLeft(dry, secondOnly) - kSecondLevel) < 0.002f);

		QVERIFY2(std::fabs(wavLeft(wet, firstOnly) - kFirstLevel * 0.5f) < 0.002f,
			qPrintable(QStringLiteral("the processed clip alone measured %1, expected %2")
				.arg(wavLeft(wet, firstOnly)).arg(kFirstLevel * 0.5f)));
		QVERIFY2(std::fabs(wavLeft(wet, overlap) - (kFirstLevel * 0.5f + kSecondLevel)) < 0.002f,
			qPrintable(QStringLiteral("the overlap measured %1: only the first clip's share may be halved (%2)")
				.arg(wavLeft(wet, overlap)).arg(kFirstLevel * 0.5f + kSecondLevel)));
		// Once the first clip has stopped sounding (the dry render drops to the second clip's level alone), its
		// sibling plays by itself to its end: exactly the dry bytes.
		int alone = framesForTicks(t);
		while (alone < framesForTicks(3 * t / 2) && std::fabs(wavLeft(dry, alone) - kSecondLevel) > 0.002f) { ++alone; }
		const int secondEnd = framesForTicks(3 * t / 2) - 64;
		QVERIFY2(secondEnd - alone > kSampleRate / 4, "the second clip never plays alone in the dry render");
		QVERIFY2(wavSpan(wet, alone, secondEnd) == wavSpan(dry, alone, secondEnd),
			"the sibling clip's audio changed: the clip effect reached a clip it is not on");
		// The clip's chain is not the track's: the track carries no device.
		QCOMPARE(devicesOf(control::trackIdOf(firstClip()->getTrack())).size(), 0);
	}

	//! Claim 2: a bypassed effect, and a removed one, leave the dry render.
	void aBypassedOrRemovedEffectRendersTheDryClip()
	{
		buildSong();
		const QByteArray dry = render();
		const QString first = control::clipIdOf(firstClip());
		const QString fx = addAmplifier(first, 50.0);
		QVERIFY(run(QStringLiteral("plugin.bypass"), {{QStringLiteral("target"), first}, {QStringLiteral("plugin"), fx},
			{QStringLiteral("bypass"), true}}).ok);
		QVERIFY2(render() == dry, "a bypassed clip effect changed the render");
		QVERIFY(run(QStringLiteral("plugin.bypass"), {{QStringLiteral("target"), first}, {QStringLiteral("plugin"), fx},
			{QStringLiteral("bypass"), false}}).ok);
		QVERIFY2(render() != dry, "the effect switched back on did not process");
		QVERIFY(run(QStringLiteral("plugin.unload"), {{QStringLiteral("target"), first}, {QStringLiteral("plugin"), fx}}).ok);
		QVERIFY2(render() == dry, "a removed clip effect still changed the render");
	}

	//! Claim 3: the chain survives save and reopen - identity, settings, order, bypass - and still processes.
	void theChainSurvivesSaveAndReopen()
	{
		buildSong();
		const QString first = control::clipIdOf(firstClip());
		QVERIFY(!addAmplifier(first, 50.0).isEmpty());
		const QString b = addAmplifier(first, 150.0);
		QVERIFY(run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), first}, {QStringLiteral("plugin"), b},
			{QStringLiteral("name"), QStringLiteral("Panning")}, {QStringLiteral("value"), -30.0}}).ok);
		QVERIFY(run(QStringLiteral("plugin.move"), {{QStringLiteral("target"), first}, {QStringLiteral("plugin"), b},
			{QStringLiteral("position"), 0}}).ok);
		QVERIFY(run(QStringLiteral("plugin.bypass"), {{QStringLiteral("target"), first}, {QStringLiteral("plugin"), b},
			{QStringLiteral("bypass"), true}}).ok);
		// The first render after a parameter edit carries the parameter's smoothing ramp in its first period; a
		// reopened device starts AT its value, so the comparison is made on a settled render on both sides.
		render();
		const QByteArray before = render();

		const QString path = m_dir.filePath(QStringLiteral("clip-effects.mmp"));
		QVERIFY(song()->saveProjectFile(path));
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QVERIFY2(QString::fromUtf8(file.readAll()).contains(QStringLiteral("<fxchain")), "no chain in the saved file");
		file.close();

		song()->loadProject(path);
		QVERIFY(firstClip() != nullptr);
		const QString reopened = control::clipIdOf(firstClip());
		const QJsonArray devices = devicesOf(reopened);
		QCOMPARE(devices.size(), 2);
		const QJsonObject slot0 = devices.at(0).toObject();
		const QJsonObject slot1 = devices.at(1).toObject();
		QCOMPARE(slot0.value(QStringLiteral("plugin")).toString(), QStringLiteral("amplifier"));
		QCOMPARE(slot1.value(QStringLiteral("plugin")).toString(), QStringLiteral("amplifier"));
		// Order and settings: the moved 150 % / -30 pan device first and bypassed, the 50 % one second and on.
		QCOMPARE(parameterValue(slot0, QStringLiteral("Volume")), 150.0);
		QCOMPARE(parameterValue(slot0, QStringLiteral("Panning")), -30.0);
		QCOMPARE(slot0.value(QStringLiteral("enabled")).toBool(), false);
		QCOMPARE(parameterValue(slot1, QStringLiteral("Volume")), 50.0);
		QCOMPARE(slot1.value(QStringLiteral("enabled")).toBool(), true);
		// The sibling clip reopened with no chain of its own.
		QCOMPARE(secondClip()->clipEffects()->effects().size(), std::size_t{0});
		// And the reopened chain processes exactly as the saved one did.
		render();
		QVERIFY2(render() == before, "the reopened clip renders differently from the saved one");
	}

	//! Claim 3, the older-project half: a clip element without a chain loads none, and restoring one over a clip
	//! that has devices takes them off (the checkpoint taken before the first device is such an element).
	void aClipWithoutAChainLoadsNone()
	{
		buildSong();
		SampleClip* clip = firstClip();
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("track"));
		doc.appendChild(parent);
		const QDomElement plain = clip->saveState(doc, parent);
		QVERIFY2(plain.firstChildElement(QStringLiteral("fxchain")).isNull(), "a clip without effects wrote a chain");
		addAmplifier(control::clipIdOf(clip), 50.0);
		QCOMPARE(clip->clipEffects()->effects().size(), std::size_t{1});
		clip->restoreState(plain);
		QCOMPARE(clip->clipEffects()->effects().size(), std::size_t{0});
	}

	//! Claim 4: every edit through the agent surface, each one control.undo step.
	void theAgentEditsTheChainOneUndoStepEach()
	{
		buildSong();
		const QString first = control::clipIdOf(firstClip());
		QVERIFY(run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), first},
			{QStringLiteral("device"), m_amplifier}}).ok);
		QCOMPARE(devicesOf(first).size(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(devicesOf(first).size(), 0);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(devicesOf(first).size(), 1);

		const QString live = devicesOf(first).at(0).toObject().value(QStringLiteral("id")).toString();
		const QString b = addAmplifier(first, 70.0);
		QVERIFY(run(QStringLiteral("plugin.move"), {{QStringLiteral("target"), first}, {QStringLiteral("plugin"), b},
			{QStringLiteral("position"), 0}}).ok);
		QCOMPARE(devicesOf(first).at(0).toObject().value(QStringLiteral("id")).toString(), b);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(devicesOf(first).at(0).toObject().value(QStringLiteral("id")).toString(), live);

		QVERIFY(run(QStringLiteral("plugin.bypass"), {{QStringLiteral("target"), first}, {QStringLiteral("plugin"), b},
			{QStringLiteral("bypass"), true}}).ok);
		QCOMPARE(devicesOf(first).at(1).toObject().value(QStringLiteral("enabled")).toBool(), false);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(devicesOf(first).at(1).toObject().value(QStringLiteral("enabled")).toBool(), true);

		QVERIFY(run(QStringLiteral("plugin.unload"), {{QStringLiteral("target"), first}, {QStringLiteral("plugin"), b}}).ok);
		QCOMPARE(devicesOf(first).size(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(devicesOf(first).size(), 2);
		QCOMPARE(parameterValue(devicesOf(first).at(1).toObject(), QStringLiteral("Volume")), 70.0);

		// Only an audio clip carries a chain: a MIDI clip is a typed refusal, not a chain that never sounds.
		auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song()));
		QVERIFY(instrumentTrack != nullptr);
		Clip* midi = instrumentTrack->createClip(TimePos(0));
		const ControlResult refused = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), control::clipIdOf(midi)},
			{QStringLiteral("device"), m_amplifier}});
		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
	}

	//! Claim 5: the clip's effects run with no allocation per period (the handle is built outside the probe).
	void theClipEffectsAllocateNothingPerPeriod()
	{
		buildSong();
		SampleClip* clip = firstClip();
		addAmplifier(control::clipIdOf(clip), 50.0);
		SamplePlayHandle handle(clip);
		handle.setClipEffects(clip);
		std::vector<SampleFrame> buffer(Engine::audioEngine()->framesPerPeriod(), SampleFrame(0.0f, 0.0f));
		const auto span = std::span<SampleFrame>(buffer);
		// Outside the engine's render loop the volume's smoothing buffer is never advanced, so the level here is
		// not the set one (the render test above measures that); what this proves is that the chain ran on the
		// handle's own buffer - it is no longer the dry level - and that running it allocates nothing.
		for (int period = 0; period < 4; ++period) { handle.play(span); }
		QVERIFY2(buffer[64].left() < kFirstLevel - 0.01f,
			qPrintable(QStringLiteral("the handle's buffer is at the dry %1: the clip effect did not run")
				.arg(buffer[64].left())));

		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int period = 0; period < 32; ++period) { handle.play(span); }
		test::tlCountAllocations = false;
		QCOMPARE(static_cast<qulonglong>(test::tlAllocationCount), static_cast<qulonglong>(0));
	}

private:
	Song* song() const { return Engine::getSong(); }

	SampleTrack* selfTrack() const
	{
		for (Track* track : song()->tracks())
		{
			if (auto* sample = dynamic_cast<SampleTrack*>(track)) { return sample; }
		}
		return nullptr;
	}

	SampleClip* firstClip() const { return dynamic_cast<SampleClip*>(selfTrack()->getClip(0)); }
	SampleClip* secondClip() const { return dynamic_cast<SampleClip*>(selfTrack()->getClip(1)); }

	//! One sample track with two constant clips that overlap: the first over [0, 1 s) at 0.5, the second over
	//! [0.5 s, 1.5 s) at 0.25 - so each span of the render says which clips sound in it and at what level.
	void buildSong()
	{
		song()->clearProject();
		auto* track = new SampleTrack(song());
		const int t = ticksForOneSecond();
		const std::pair<int, float> clips[] = {{0, kFirstLevel}, {t / 2, kSecondLevel}};
		for (const auto& [start, level] : clips)
		{
			auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(start)));
			Q_ASSERT(clip != nullptr);
			clip->setSampleBuffer(constantBuffer(level));
			clip->changeLength(TimePos(t));
			clip->setAutoResize(false);
		}
		song()->updateLength();
	}

	//! plugin.load of the amplifier on @a target, its volume set to @a volume; its fx-<n> id.
	QString addAmplifier(const QString& target, double volume)
	{
		const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target},
			{QStringLiteral("device"), m_amplifier}});
		if (!loaded.ok) { qWarning("plugin.load: %s", qPrintable(loaded.errorMessage)); return {}; }
		const QString id = loaded.result.value(QStringLiteral("id")).toString();
		const ControlResult set = run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), id}, {QStringLiteral("name"), QStringLiteral("Volume")},
			{QStringLiteral("value"), volume}});
		if (!set.ok) { qWarning("plugin.param_set: %s", qPrintable(set.errorMessage)); return {}; }
		return id;
	}

	QByteArray render()
	{
		const QString path = m_dir.filePath(QStringLiteral("clip-effects.wav"));
		QFile::remove(path);
		const OutputSettings settings(kSampleRate, 192, OutputSettings::BitDepth::Depth16Bit,
			OutputSettings::StereoMode::Stereo);
		ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, path);
		if (!renderer.isReady()) { return QByteArray(); }
		renderer.startProcessing();
		renderer.wait();
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) { return QByteArray(); }
		return file.readAll();
	}

	QTemporaryDir m_dir;
	QString m_amplifier;
};

QTEST_GUILESS_MAIN(ClipEffectsTest)
#include "ClipEffectsTest.moc"
