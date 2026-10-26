/*
 * DeviceToolsChainTest.cpp - post-fader inserts, the device delta, clip printing, plugin.replace and folding and options
 *                              (split out of DeviceToolsTest to keep both under the file-length ratchet)
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
/*! 0.5.0 L10 (W3), moved unchanged from DeviceToolsTest.cpp: a post-fader insert parameter is found and
 *  reported (MX-082); a device delta is what it changes (MX-080); a clip is printed through a chain's devices
 *  (MX-083); a device is hot-swapped in its slot (WF-016); a device folds and carries its options (MX-113 / MX-099). */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>

#include <cmath>
#include <vector>

#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DeviceOptions.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "ParameterAliases.h"
#include "Plugin.h"
#include "Rack.h"
#include "RackSplit.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

MixerChannel* channel(int index) { return Engine::mixer()->mixerChannel(index); }

QString ch(int index) { return control::channelIdOf(channel(index)); }

//! The built-in effect named @a name: its dev-<n> in plugin.list, or empty.
QString deviceNamed(const QString& name)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
		.result.value(QStringLiteral("devices")).toArray())
	{
		if (device.toObject().value(QStringLiteral("name")).toString() == name) { return device.toObject().value(QStringLiteral("id")).toString(); }
	}
	return QString();
}

//! Load the built-in device named @a name onto @a target: its fx-<n>, or empty.
QString loadByName(const QString& target, const QString& name)
{
	const QString device = deviceNamed(name);
	if (device.isEmpty()) { return QString(); }
	return run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target}, {QStringLiteral("device"), device}})
		.result.value(QStringLiteral("id")).toString();
}

double parameterValue(const QString& target, const QString& plugin, const QString& name)
{
	// plugin.param_get reads ONE parameter, by 'name' (or 'index').
	const ControlResult read = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), target},
		{QStringLiteral("plugin"), plugin}, {QStringLiteral("name"), name}});
	return read.ok ? read.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble() : -1e9;
}

//! The device ids the chain entry of dsp.get_state carries.
QList<QString> idsOf(const QJsonObject& entry)
{
	QList<QString> ids;
	for (const QJsonValue& device : entry.value(QStringLiteral("devices")).toArray())
	{
		ids << device.toObject().value(QStringLiteral("id")).toString();
	}
	return ids;
}

bool setParameter(const QString& target, const QString& plugin, const QString& name, double value)
{
	return run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin},
		{QStringLiteral("name"), name}, {QStringLiteral("value"), value}}).ok;
}

//! @a blocks periods of a steady (@a level, @a level) signal through channel @a index's chain; the
//! last block's left peak (signed: the value of its first frame).
float runSteady(int index, float level, int blocks)
{
	static std::vector<SampleFrame> block;
	block.assign(Engine::audioEngine()->framesPerPeriod(), SampleFrame());
	SampleFrame* data[1] = {block.data()};
	for (int b = 0; b < blocks; ++b)
	{
		// Each block is one period: the serial model prepass consumes pending setter ramps first.
		{
			auto changeGuard = Engine::audioEngine()->requestChangesGuard();
			Engine::audioEngine()->prepareAutomatableModelBuffers();
		}
		for (SampleFrame& frame : block) { frame = SampleFrame(level, level); }
		AudioBus bus{data, 1, static_cast<f_cnt_t>(block.size())};
		channel(index)->m_fxChain.processAudioBuffer(bus);
		AutomatableModel::incrementPeriodCounter();
	}
	return block.front()[0];
}

Effect* effectOn(int index)
{
	const std::vector<Effect*>& effects = channel(index)->m_fxChain.effects();
	return effects.empty() ? nullptr : effects.back();
}

} // namespace

class DeviceToolsChainTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test reads and writes channel buffers itself
		while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
		ControlRegistry::setReady(true);
		// foldingAndOptionsAreKeptAndUndone works on the amplifier on channel 1, which the earlier cases of
		// DeviceToolsTest loaded and left there; this binary does not run those cases, so the fixture does it.
		loadByName(ch(1), QStringLiteral("amplifier"));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}
	//! MX-082: a parameter of a channel's post-fader insert is found by a search of the channel,
	//! and the hit names the target that addresses it.
	void aPostFaderInsertParameterIsFound()
	{
		const QString post = ch(2) + QStringLiteral("/post");
		const QString amp = loadByName(post, QStringLiteral("amplifier"));
		if (amp.isEmpty()) { QSKIP("this build has no amplifier module"); }
		const ControlResult found = run(QStringLiteral("plugin.param_search"), {{QStringLiteral("query"), QStringLiteral("volum")},
			{QStringLiteral("target"), ch(2)}});
		QVERIFY2(found.ok, qPrintable(found.errorMessage));
		QVERIFY2(found.result.value(QStringLiteral("count")).toInt() >= 1, "the post-fader insert's Volume was not found on the channel");
		for (const QJsonValue& hit : found.result.value(QStringLiteral("parameters")).toArray())
		{
			QCOMPARE(hit.toObject().value(QStringLiteral("target")).toString(), post);
		}
	}

	//! Luna 5670: a channel's post-fader inserts are chains that channel carries, so the
	//! project-wide dsp.get_state must report the device sitting in them and count it.
	void aPostFaderInsertIsReportedByTheProjectWideState()
	{
		const QString post = ch(2) + QStringLiteral("/post");
		const QString amp = loadByName(post, QStringLiteral("amplifier"));
		if (amp.isEmpty()) { QSKIP("this build has no amplifier module"); }

		const ControlResult all = run(QStringLiteral("dsp.get_state"));
		QVERIFY2(all.ok, qPrintable(all.errorMessage));
		int found = 0;
		int devices = 0;
		for (const QJsonValue& chain : all.result.value(QStringLiteral("chains")).toArray())
		{
			const QJsonObject entry = chain.toObject();
			devices += entry.value(QStringLiteral("count")).toInt();
			if (entry.value(QStringLiteral("id")).toString() != post) { continue; }
			++found;
			// earlier cases leave inserts of their own on this channel, so the chain is only
			// pinned to carrying the device just loaded - that it is carried at all is the claim.
			QVERIFY2(entry.value(QStringLiteral("count")).toInt() >= 1,
				"the post-fader insert chain was reported as empty");
			QVERIFY2(idsOf(entry).contains(amp),
				qPrintable(QStringLiteral("the post-fader chain was reported without the %1 just loaded").arg(amp)));
			QCOMPARE(entry.value(QStringLiteral("count")).toInt(),
				entry.value(QStringLiteral("devices")).toArray().size());
		}
		QVERIFY2(found == 1,
			"the post-fader insert's device was omitted from the project-wide dsp.get_state");
		QCOMPARE(all.result.value(QStringLiteral("device_count")).toInt(), devices);
	}

	//! MX-080: the delta of a device that changes nothing is silence; of one that doubles the level,
	//! the input itself; the switch toggles and is not journalled.
	void theDeltaIsWhatTheDeviceChanges()
	{
		const QString amp = loadByName(ch(3), QStringLiteral("amplifier"));
		if (amp.isEmpty()) { QSKIP("this build has no amplifier module"); }
		const QJsonObject device{{QStringLiteral("target"), ch(3)}, {QStringLiteral("plugin"), amp}};
		QVERIFY(setParameter(ch(3), amp, QStringLiteral("Volume"), 100.0));  // unity
		QVERIFY(std::abs(runSteady(3, 0.4f, 4) - 0.4f) < 1e-4f);
		const ControlResult on = run(QStringLiteral("plugin.delta"), device);  // toggles on
		QVERIFY2(on.ok, qPrintable(on.errorMessage));
		QVERIFY(on.result.value(QStringLiteral("on")).toBool());
		QVERIFY(std::abs(runSteady(3, 0.4f, 4)) < 1e-4f);         // it changes nothing: silence
		QVERIFY(setParameter(ch(3), amp, QStringLiteral("Volume"), 200.0));
		QVERIFY(std::abs(runSteady(3, 0.4f, 4) - 0.4f) < 1e-3f);  // twice the input, minus the input
		QJsonObject off = device;
		off.insert(QStringLiteral("on"), false);
		QVERIFY(run(QStringLiteral("plugin.delta"), off).ok);
		QVERIFY(std::abs(runSteady(3, 0.4f, 4) - 0.8f) < 1e-3f);
		QJsonObject missing = device;
		missing.insert(QStringLiteral("plugin"), QStringLiteral("fx-999999"));
		QCOMPARE(run(QStringLiteral("plugin.delta"), missing).errorKind, ControlErrorKind::NotFound);
		channel(3)->m_fxChain.clear();
	}

	//! MX-083: a clip printed through a chain's devices takes their effect, the chain stays as it was,
	//! an empty chain is refused, and undo puts the clip's source back.
	void aClipIsPrintedThroughAChain()
	{
		const QString amp = loadByName(ch(3), QStringLiteral("amplifier"));
		if (amp.isEmpty()) { QSKIP("this build has no amplifier module"); }
		QVERIFY(setParameter(ch(3), amp, QStringLiteral("Volume"), 200.0));
		Track* track = Track::create(Track::Type::Sample, Engine::getSong());
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		clip->setSampleBuffer(std::make_shared<SampleBuffer>(std::vector<SampleFrame>(4000, SampleFrame(0.25f, 0.25f)), rate));
		const QString id = control::clipIdOf(clip);
		QCOMPARE(run(QStringLiteral("clip.process"), {{QStringLiteral("clip"), id}, {QStringLiteral("from"), ch(2)}}).errorKind,
			ControlErrorKind::Refused);  // ch-2's own chain holds no device
		const ControlResult printed = run(QStringLiteral("clip.process"), {{QStringLiteral("clip"), id}, {QStringLiteral("from"), ch(3)}});
		QVERIFY2(printed.ok, qPrintable(printed.errorMessage));
		QVERIFY(std::abs(clip->sample().data()[2000][0] - 0.5f) < 1e-3f);
		QCOMPARE(channel(3)->m_fxChain.effects().size(), std::size_t{1});  // the chain itself is untouched
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(std::abs(clip->sample().data()[2000][0] - 0.25f) < 1e-6f);
		channel(3)->m_fxChain.clear();
	}

	//! WF-016: plugin.replace puts a fresh device at the replaced one's slot of the same chain, and one
	//! control.undo brings the old one back with its settings.
	void aDeviceIsReplacedInItsSlot()
	{
		const QString amp = loadByName(ch(3), QStringLiteral("amplifier"));
		if (amp.isEmpty()) { QSKIP("this build has no amplifier module"); }
		QVERIFY(setParameter(ch(3), amp, QStringLiteral("Volume"), 150.0));
		const ControlResult swapped = run(QStringLiteral("plugin.replace"), {{QStringLiteral("target"), ch(3)},
			{QStringLiteral("plugin"), amp}, {QStringLiteral("device"), deviceNamed(QStringLiteral("amplifier"))}});
		QVERIFY2(swapped.ok, qPrintable(swapped.errorMessage));
		QCOMPARE(swapped.result.value(QStringLiteral("index")).toInt(), 0);
		const QString fresh = swapped.result.value(QStringLiteral("plugin")).toString();
		QVERIFY(fresh != amp);
		QCOMPARE(channel(3)->m_fxChain.effects().size(), std::size_t{1});  // in the old one's place, not beside it
		QCOMPARE(control::effectIdOf(effectOn(3)), fresh);
		QCOMPARE(parameterValue(ch(3), fresh, QStringLiteral("Volume")), 100.0);  // a fresh device: its own default
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(channel(3)->m_fxChain.effects().size(), std::size_t{1});
		QCOMPARE(parameterValue(ch(3), control::effectIdOf(effectOn(3)), QStringLiteral("Volume")), 150.0);  // the old settings
		QCOMPARE(run(QStringLiteral("plugin.replace"), {{QStringLiteral("target"), ch(3)}, {QStringLiteral("plugin"), QStringLiteral("fx-999999")},
			{QStringLiteral("device"), deviceNamed(QStringLiteral("amplifier"))}}).errorKind, ControlErrorKind::NotFound);
		channel(3)->m_fxChain.clear();
	}

	void replacingADeviceMarksTheProjectModified()
	{
		channel(3)->m_fxChain.clear();
		const QString device = deviceNamed(QStringLiteral("amplifier"));
		if (device.isEmpty()) { QSKIP("this build has no amplifier module"); }
		const QString old = loadByName(ch(3), QStringLiteral("amplifier"));
		if (old.isEmpty()) { QSKIP("this build has no loadable amplifier module"); }

		Song* song = Engine::getSong();
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString baseline = dir.filePath(QStringLiteral("before-replace.mmp"));
		QVERIFY(song->saveProjectFile(baseline));
		song->noteProjectSaved(baseline);
		QVERIFY(!song->isModified());
		const ControlResult missing = run(QStringLiteral("plugin.replace"), {{QStringLiteral("target"), ch(3)},
			{QStringLiteral("plugin"), QStringLiteral("fx-999999")}, {QStringLiteral("device"), device}});
		QCOMPARE(missing.errorKind, ControlErrorKind::NotFound);
		QVERIFY(!song->isModified());  // a refused replacement changes nothing

		const ControlResult replaced = run(QStringLiteral("plugin.replace"), {{QStringLiteral("target"), ch(3)},
			{QStringLiteral("plugin"), old}, {QStringLiteral("device"), device}});
		QVERIFY2(replaced.ok, qPrintable(replaced.errorMessage));
		QVERIFY(song->isModified());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		channel(3)->m_fxChain.clear();
	}

	//! MX-113 / MX-099: folding and the options are carried by the device, a latency override reaches
	//! the chain's delay compensation, the generic editor refuses the plug-in's own window, and both undo.
	void foldingAndOptionsAreKeptAndUndone()
	{
		Effect* effect = effectOn(1);
		if (effect == nullptr) { QSKIP("this build has no amplifier module"); }
		const QJsonObject device{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), control::effectIdOf(effect)}};
		QVERIFY(run(QStringLiteral("plugin.fold"), device).ok);  // toggles
		QVERIFY(effect->deviceOptions().folded);
		QJsonObject options = device;
		options.insert(QStringLiteral("latency_override"), 512);
		options.insert(QStringLiteral("keep_running"), true);
		options.insert(QStringLiteral("generic_editor"), true);
		QVERIFY(run(QStringLiteral("plugin.options"), options).ok);
		QCOMPARE(channel(1)->m_fxChain.latencyFrames(), 512);
		QVERIFY(effect->deviceOptions().keepRunning);
		const ControlResult editor = run(QStringLiteral("plugin.editor_open"), device);
		QVERIFY(!editor.ok);  // a headless run refuses it before the device is asked: requires a display
		if (editor.errorKind != ControlErrorKind::Requires) { QVERIFY(editor.errorMessage.contains(QStringLiteral("generic editor"))); }
		QJsonObject badScale = device;
		badScale.insert(QStringLiteral("editor_scale"), 9.0);
		QCOMPARE(run(QStringLiteral("plugin.options"), badScale).errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(channel(1)->m_fxChain.latencyFrames(), 0);
		QVERIFY(!effect->deviceOptions().keepRunning);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!effect->deviceOptions().folded);
		QVERIFY(effect->deviceOptions().isDefault());
	}
};

QTEST_MAIN(DeviceToolsChainTest)
#include "DeviceToolsChainTest.moc"
