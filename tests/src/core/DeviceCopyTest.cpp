/*
 * DeviceCopyTest.cpp - plugin.copy, chain.copy, plugin.ab and the plug-in organiser and folders
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
/*! 0.5.0 L10: a device copied to another channel keeps its settings and one undo takes it back
 *  (MX-076); a chain copied with 'replace' puts the destination's devices back on undo; plugin.ab
 *  stores two settings, toggles between them and a recall is one undo step (MX-092); a macro target's
 *  curve is accepted by the command (MX-105); plugin.organise marks a device and plugin.organisation
 *  lists it, undoably (MX-166); plugin.paths_set replaces a format's folders (MX-163); chain.bypass_all
 *  bypasses every device and chain.clean removes the bypassed ones, each one undo step (MX-078); the
 *  preferred plug-in format is kept and undone (MX-167). */

#include <QtTest>

#include <QDir>
#include <QJsonArray>

#include <vector>

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "PluginOrganiser.h"
#include "PluginPaths.h"
#include "Rack.h"
#include "RemoteControls.h"
#include "ReversibilityTestSupport.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString ch(int index) { return control::channelIdOf(Engine::mixer()->mixerChannel(index)); }

EffectChain& chainOf(int index) { return Engine::mixer()->mixerChannel(index)->m_fxChain; }

//! The first effect this build can load onto @a target: its fx-<n>, or empty.
QString loadAnEffect(const QString& target)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")},
		{QStringLiteral("loadable_only"), true}}).result.value(QStringLiteral("devices")).toArray())
	{
		const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target},
			{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}});
		if (loaded.ok) { return loaded.result.value(QStringLiteral("id")).toString(); }
	}
	return QString();
}

//! The first float parameter of @a plugin on @a target and a value inside its range away from the current one.
bool aParameter(const QString& target, const QString& plugin, QString* name, double* other)
{
	// plugin.param_get reads ONE parameter (by 'name' or 'index'): walk the indices until there is none.
	for (int index = 0; index < 256; ++index)
	{
		const ControlResult read = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), plugin}, {QStringLiteral("index"), index}});
		if (!read.ok) { break; }
		const QJsonObject p = read.result.value(QStringLiteral("parameter")).toObject();
		if (p.value(QStringLiteral("type")).toString() == QLatin1String("boolean")) { continue; }  // a float one, not "enabled"
		const double lo = p.value(QStringLiteral("min")).toDouble();
		const double hi = p.value(QStringLiteral("max")).toDouble();
		if (hi - lo <= 0.0) { continue; }
		*name = p.value(QStringLiteral("name")).toString();
		const double now = p.value(QStringLiteral("value")).toDouble();
		*other = std::abs(now - lo) > (hi - lo) * 0.25 ? lo : hi;
		return true;
	}
	return false;
}

//! Load the built-in device named @a name (plugin.list's 'name') onto @a target: its fx-<n>, or empty.
QString loadByName(const QString& target, const QString& name)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
		.result.value(QStringLiteral("devices")).toArray())
	{
		if (device.toObject().value(QStringLiteral("name")).toString() != name) { continue; }
		return run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target},
			{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}}).result.value(QStringLiteral("id")).toString();
	}
	return QString();
}

//! Run @a blocks periods of a steady (@a level, @a level) signal through @a chain in @a block (one
//! period, allocated by the caller); the last block's peak.
float runBlocks(EffectChain& chain, std::vector<SampleFrame>& block, float level, int blocks)
{
	const auto fpp = static_cast<f_cnt_t>(block.size());
	SampleFrame* data[1] = {block.data()};
	float peak = 0.f;
	for (int b = 0; b < blocks; ++b)
	{
		for (SampleFrame& frame : block) { frame = SampleFrame(level, level); }
		AudioBus bus{data, 1, fpp};
		chain.processAudioBuffer(bus);
		peak = 0.f;
		for (const SampleFrame& frame : block) { peak = std::max(peak, std::abs(frame[0])); }
	}
	return peak;
}

double parameterValue(const QString& target, const QString& plugin, const QString& name)
{
	const ControlResult read = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), target},
		{QStringLiteral("plugin"), plugin}, {QStringLiteral("name"), name}});
	return read.ok ? read.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble() : -1e9;
}

} // namespace

class DeviceCopyTest : public QObject
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
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aDeviceIsCopiedWithItsSettings()
	{
		const QString plugin = loadAnEffect(ch(1));
		if (plugin.isEmpty()) { QSKIP("this build loads no effect"); }
		QString name;
		double other = 0.0;
		QVERIFY(aParameter(ch(1), plugin, &name, &other));
		QVERIFY(run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), plugin},
			{QStringLiteral("name"), name}, {QStringLiteral("value"), other}}).ok);
		const ControlResult copied = run(QStringLiteral("plugin.copy"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), plugin},
			{QStringLiteral("to"), ch(2)}});
		QVERIFY2(copied.ok, qPrintable(copied.errorMessage));
		QCOMPARE(chainOf(2).effects().size(), std::size_t{1});
		const QString copy = copied.result.value(QStringLiteral("plugins")).toArray().first().toString();
		QVERIFY(std::abs(parameterValue(ch(2), copy, name) - other) < 1e-3);  // the settings came along
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(chainOf(2).effects().empty());
		QCOMPARE(run(QStringLiteral("plugin.copy"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), QStringLiteral("fx-9999")},
			{QStringLiteral("to"), ch(2)}}).errorKind, ControlErrorKind::NotFound);
	}

	void aChainIsCopiedAndReplacesAnother()
	{
		if (chainOf(1).effects().empty()) { QSKIP("this build loads no effect"); }
		const QString there = loadAnEffect(ch(3));
		QVERIFY(!there.isEmpty());
		loadAnEffect(ch(3));
		QCOMPARE(chainOf(3).effects().size(), std::size_t{2});
		const ControlResult copied = run(QStringLiteral("chain.copy"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("to"), ch(3)},
			{QStringLiteral("replace"), true}});
		QVERIFY2(copied.ok, qPrintable(copied.errorMessage));
		QCOMPARE(chainOf(3).effects().size(), chainOf(1).effects().size());
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the copies out, the two devices back
		QCOMPARE(chainOf(3).effects().size(), std::size_t{2});
		QCOMPARE(run(QStringLiteral("chain.copy"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("to"), ch(1)}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void abComparesTwoSettings()
	{
		if (chainOf(1).effects().empty()) { QSKIP("this build loads no effect"); }
		const QString plugin = control::effectIdOf(chainOf(1).effects().front());
		QString name;
		double other = 0.0;
		QVERIFY(aParameter(ch(1), plugin, &name, &other));
		const double first = parameterValue(ch(1), plugin, name);
		const QJsonObject self{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), plugin}};
		auto with = [self](const QString& action, const QString& slot = QString()) {
			QJsonObject args = self;
			args.insert(QStringLiteral("action"), action);
			if (!slot.isEmpty()) { args.insert(QStringLiteral("slot"), slot); }
			return run(QStringLiteral("plugin.ab"), args);
		};
		QVERIFY(with(QStringLiteral("store"), QStringLiteral("a")).ok);
		QCOMPARE(with(QStringLiteral("recall"), QStringLiteral("b")).errorKind, ControlErrorKind::NotFound);  // nothing in B
		QVERIFY(run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), plugin},
			{QStringLiteral("name"), name}, {QStringLiteral("value"), other}}).ok);
		QVERIFY(with(QStringLiteral("store"), QStringLiteral("b")).ok);
		const ControlResult toggled = with(QStringLiteral("toggle"));
		QVERIFY2(toggled.ok, qPrintable(toggled.errorMessage));
		QCOMPARE(toggled.result.value(QStringLiteral("current")).toString(), QStringLiteral("a"));
		QVERIFY(std::abs(parameterValue(ch(1), plugin, name) - first) < 1e-3);  // A heard again
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(std::abs(parameterValue(ch(1), plugin, name) - other) < 1e-3);  // the recall undone
		const QJsonObject state = with(QStringLiteral("get")).result;
		QVERIFY(state.value(QStringLiteral("has_a")).toBool() && state.value(QStringLiteral("has_b")).toBool());
	}

	//! MX-145 / MX-153: the native Gate closes on a signal under its threshold and opens on one over
	//! it; Gate and Modulation process a block without allocating (the realtime rule).
	void theGateAndModulationDevicesWork()
	{
		Engine::mixer()->mixerChannel(3)->m_fxChain.clear();
		const QString gate = loadByName(ch(3), QStringLiteral("gate"));
		if (gate.isEmpty()) { QSKIP("this build has no gate module"); }
		const QJsonObject self{{QStringLiteral("target"), ch(3)}, {QStringLiteral("plugin"), gate}};
		for (const auto& [name, value] : {std::pair{"Release", 5.0}, std::pair{"Hold", 0.0}, std::pair{"Attack", 0.1}})
		{
			QJsonObject args = self;
			args.insert(QStringLiteral("name"), QLatin1String(name));
			args.insert(QStringLiteral("value"), value);
			QVERIFY(run(QStringLiteral("plugin.param_set"), args).ok);
		}
		EffectChain& chain = chainOf(3);
		std::vector<SampleFrame> block(Engine::audioEngine()->framesPerPeriod());
		QVERIFY(runBlocks(chain, block, 0.001f, 40) < 0.001f * 0.1f);  // -60 dBFS under a -40 dB threshold: closed to the floor
		QVERIFY(runBlocks(chain, block, 0.5f, 40) > 0.45f);             // well over it: open
		QVERIFY(chain.effects().front()->gainReductionDb() < 0.5f);
		const QString modulation = loadByName(ch(3), QStringLiteral("modulation"));
		QVERIFY(!modulation.isEmpty());
		runBlocks(chain, block, 0.2f, 2);  // warm-up
		test::tlCountAllocations = true;
		test::resetAllocationCount();
		runBlocks(chain, block, 0.2f, 1);
		const std::uint64_t allocations = test::tlAllocationCount;
		test::tlCountAllocations = false;
		QCOMPARE(allocations, std::uint64_t{0});
	}

	//! MX-103: wrapping every device moves them into a new rack chain and selects it; undo puts them
	//! back in the channel's chain and removes the chain; redo makes it again with the devices.
	void wrappingMovesTheDevicesIntoARackChain()
	{
		if (chainOf(1).effects().empty()) { QSKIP("this build loads no effect"); }
		const std::size_t devices = chainOf(1).effects().size();
		Rack& rack = Engine::mixer()->mixerChannel(1)->m_rack;
		const int chainsBefore = rack.chainCount();
		const ControlResult wrapped = run(QStringLiteral("rack.wrap"), {{QStringLiteral("channel"), ch(1)}});
		QVERIFY2(wrapped.ok, qPrintable(wrapped.errorMessage));
		QVERIFY(chainOf(1).effects().empty());
		QCOMPARE(rack.chainCount(), chainsBefore + 1);
		QCOMPARE(rack.chain(rack.chainCount() - 1)->effects().size(), devices);
		QCOMPARE(rack.selectedChain(), rack.chainCount() - 1);  // every device wrapped: the channel sounds the same
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(chainOf(1).effects().size(), devices);
		QCOMPARE(rack.chainCount(), chainsBefore);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(rack.chainCount(), chainsBefore + 1);
		QCOMPARE(rack.chain(rack.chainCount() - 1)->effects().size(), devices);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(run(QStringLiteral("rack.wrap"), {{QStringLiteral("channel"), ch(2)}}).errorKind, ControlErrorKind::NotFound);
	}

	//! MX-102: a device on a parallel chain is addressed by its fx-<n> on the channel, like the chain's
	//! own - bypass and unload reach it, and the unload's undo puts it back on its chain.
	void aParallelChainsDeviceIsAddressedOnTheChannel()
	{
		Rack& rack = Engine::mixer()->mixerChannel(2)->m_rack;
		const int chain = run(QStringLiteral("rack.add_chain"), {{QStringLiteral("channel"), ch(2)}}).result.value(QStringLiteral("chain")).toInt();
		QVERIFY(chain >= 1);
		QString loaded;
		for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")},
			{QStringLiteral("loadable_only"), true}}).result.value(QStringLiteral("devices")).toArray())
		{
			const ControlResult r = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), ch(2)}, {QStringLiteral("chain"), chain},
				{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}});
			if (r.ok) { loaded = r.result.value(QStringLiteral("id")).toString(); break; }
		}
		if (loaded.isEmpty()) { QSKIP("this build loads no effect"); }
		QVERIFY(run(QStringLiteral("plugin.bypass"), {{QStringLiteral("target"), ch(2)}, {QStringLiteral("plugin"), loaded},
			{QStringLiteral("bypass"), true}}).ok);
		QVERIFY(!rack.chain(chain)->effects().front()->isEnabled());
		QVERIFY(run(QStringLiteral("plugin.unload"), {{QStringLiteral("target"), ch(2)}, {QStringLiteral("plugin"), loaded}}).ok);
		QVERIFY(rack.chain(chain)->effects().empty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(rack.chain(chain)->effects().size(), std::size_t{1});  // back on its own chain
	}

	//! MX-106: the remote knobs drive the target device's parameters eight at a time.
	void remoteKnobsDriveAPageOfParameters()
	{
		if (chainOf(1).effects().empty()) { QSKIP("this build loads no effect"); }
		QCOMPARE(run(QStringLiteral("remote.page")).errorKind, ControlErrorKind::Refused);  // nothing connected
		QVERIFY(run(QStringLiteral("remote.connect"), {{QStringLiteral("first_cc"), 20}}).ok);
		const QString plugin = control::effectIdOf(chainOf(1).effects().front());
		const ControlResult targeted = run(QStringLiteral("remote.target"), {{QStringLiteral("target"), ch(1)},
			{QStringLiteral("plugin"), plugin}, {QStringLiteral("follow"), false}});
		QVERIFY2(targeted.ok, qPrintable(targeted.errorMessage));
		const QJsonArray page = targeted.result.value(QStringLiteral("parameters")).toArray();
		QVERIFY(!page.isEmpty());
		const QString first = page.first().toObject().value(QStringLiteral("name")).toString();
		QVERIFY(RemoteControls::instance()->applyKnob(0, 127));  // all the way up
		const QJsonObject params = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), ch(1)},
			{QStringLiteral("plugin"), plugin}}).result;
		for (const QJsonValue& value : params.value(QStringLiteral("parameters")).toArray())
		{
			const QJsonObject p = value.toObject();
			if (p.value(QStringLiteral("name")).toString() != first) { continue; }
			QVERIFY(std::abs(p.value(QStringLiteral("value")).toDouble() - p.value(QStringLiteral("max")).toDouble()) < 1e-3);
			break;
		}
		QVERIFY(!RemoteControls::instance()->applyKnob(8, 64));  // there are eight knobs
		QCOMPARE(run(QStringLiteral("remote.page"), {{QStringLiteral("page"), 999}}).result.value(QStringLiteral("page")).toInt(),
			targeted.result.value(QStringLiteral("pages")).toInt() - 1);  // clamped
		QVERIFY(run(QStringLiteral("remote.disconnect")).ok);
		QVERIFY(RemoteControls::instance() == nullptr);
	}

	//! MX-111: a drum rack is a folder of sampler pads, each with its own channel sending to one bus;
	//! one undo takes it all away.
	void aDrumRackHasAChainPerPad()
	{
		const QString dir = QStringLiteral(ZENE_DRUM_SAMPLES_DIR);
		const int tracksBefore = static_cast<int>(Engine::getSong()->tracks().size());
		const int channelsBefore = Engine::mixer()->numChannels();
		const ControlResult made = run(QStringLiteral("track.drum_rack"), {{QStringLiteral("name"), QStringLiteral("Kit")},
			{QStringLiteral("pads"), QJsonArray{QJsonObject{{QStringLiteral("sample"), dir + QStringLiteral("/bassdrum01.ogg")}},
				QJsonObject{{QStringLiteral("sample"), dir + QStringLiteral("/bassdrum02.ogg")}, {QStringLiteral("key"), 38}}}}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore + 3);  // two pads and the folder
		QCOMPARE(Engine::mixer()->numChannels(), channelsBefore + 3);                       // two pad channels and the bus
		const QString bus = made.result.value(QStringLiteral("bus")).toString();
		const QJsonArray channels = made.result.value(QStringLiteral("channels")).toArray();
		ControlResult ignored;
		MixerChannel* pad = control::resolveMixerChannel(channels.at(1).toString(), &ignored);
		QVERIFY(pad != nullptr && pad->m_sends.size() == 1);
		QCOMPARE(control::channelIdOf(pad->m_sends.front()->receiver()), bus);  // the pad's chain feeds the bus
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore);
		QCOMPARE(Engine::mixer()->numChannels(), channelsBefore);
		QCOMPARE(run(QStringLiteral("track.drum_rack"), {{QStringLiteral("pads"), QJsonArray{QJsonObject{
			{QStringLiteral("sample"), QStringLiteral("/no/such/file.wav")}}}}}).errorKind, ControlErrorKind::NotFound);
	}

	//! The rack's undo finds its pads by id: track.remove and its undo rebuild a pad as a new object,
	//! which a kept Track* does not name.
	void aDrumRackPadIsFoundAgainAfterItsRemovalIsUndone()
	{
		const QString dir = QStringLiteral(ZENE_DRUM_SAMPLES_DIR);
		const int tracksBefore = static_cast<int>(Engine::getSong()->tracks().size());
		const ControlResult made = run(QStringLiteral("track.drum_rack"), {{QStringLiteral("pads"),
			QJsonArray{QJsonObject{{QStringLiteral("sample"), dir + QStringLiteral("/bassdrum01.ogg")}}}}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		const QString pad = made.result.value(QStringLiteral("tracks")).toArray().first().toString();
		QVERIFY(revtest::makerUndoFindsTrackAfterRemovalUndone(pad));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore);  // the folder went too
	}

	void theOrganiserMarksADevice()
	{
		QCOMPARE(pluginorganiser::keyOf(QStringLiteral("ladspaeffect"), {{QStringLiteral("file"), QStringLiteral("caps")},
			{QStringLiteral("plugin"), QStringLiteral("Chorus")}}), QStringLiteral("ladspaeffect|file=caps|plugin=Chorus"));
		const QString key = QStringLiteral("amplifier");
		const ControlResult marked = run(QStringLiteral("plugin.organise"), {{QStringLiteral("key"), key},
			{QStringLiteral("favourite"), true}, {QStringLiteral("folder"), QStringLiteral("Gain")}});
		QVERIFY2(marked.ok, qPrintable(marked.errorMessage));
		QVERIFY(pluginorganiser::entry(key).favourite);
		const QJsonObject listed = run(QStringLiteral("plugin.organisation"), {{QStringLiteral("folder"), QStringLiteral("Gain")}}).result;
		QCOMPARE(listed.value(QStringLiteral("entries")).toArray().size(), 1);
		QVERIFY(listed.value(QStringLiteral("folders")).toArray().contains(QStringLiteral("Gain")));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(pluginorganiser::entry(key).isDefault());
		QCOMPARE(run(QStringLiteral("plugin.organise"), {{QStringLiteral("favourite"), true}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void theFoldersAreSetAndUndone()
	{
		const QString dir = QDir::tempPath();
		const ControlResult set = run(QStringLiteral("plugin.paths_set"), {{QStringLiteral("format"), QStringLiteral("vst3")},
			{QStringLiteral("dirs"), QJsonArray{dir}}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(pluginpaths::configured(pluginpaths::Format::Vst3), QStringList{dir});
		QCOMPARE(pluginpaths::searchDirs(pluginpaths::Format::Vst3).front(), dir);  // searched first
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(pluginpaths::configured(pluginpaths::Format::Vst3).isEmpty());
		QCOMPARE(run(QStringLiteral("plugin.paths_set"), {{QStringLiteral("format"), QStringLiteral("au")},
			{QStringLiteral("dirs"), QJsonArray{}}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void aChainIsBypassedAndCleaned()
	{
		const QString target = ch(3);
		QVERIFY(!loadAnEffect(target).isEmpty());
		QVERIFY(!loadAnEffect(target).isEmpty());
		const std::size_t count = chainOf(3).effects().size();
		QVERIFY(count >= 2);
		std::vector<bool> before;
		for (const Effect* effect : chainOf(3).effects()) { before.push_back(effect->isEnabled()); }
		QVERIFY(run(QStringLiteral("chain.bypass_all"), {{QStringLiteral("target"), target}}).ok);
		for (const Effect* effect : chainOf(3).effects()) { QVERIFY(!effect->isEnabled()); }
		const ControlResult cleaned = run(QStringLiteral("chain.clean"), {{QStringLiteral("target"), target}});
		QVERIFY2(cleaned.ok, qPrintable(cleaned.errorMessage));
		QCOMPARE(cleaned.result.value(QStringLiteral("removed")).toInt(), static_cast<int>(count));
		QVERIFY(chainOf(3).effects().empty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the devices back, bypassed
		QCOMPARE(chainOf(3).effects().size(), count);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // and each as it was
		std::vector<bool> after;
		for (const Effect* effect : chainOf(3).effects()) { after.push_back(effect->isEnabled()); }
		QCOMPARE(after, before);
	}

	void aPreferredFormatIsKeptAndUndone()
	{
		QVERIFY(run(QStringLiteral("plugin.prefer_format"), {{QStringLiteral("format"), QStringLiteral("clap")}}).ok);
		QCOMPARE(pluginorganiser::preferredFormat(), QStringLiteral("clap"));
		QCOMPARE(run(QStringLiteral("plugin.prefer_format"), {{QStringLiteral("format"), QStringLiteral("au")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(pluginorganiser::preferredFormat().isEmpty());
	}

	//! W6: the organiser tells an open effect picker that it changed - plugin.organise and plugin.prefer_format
	//! over the socket, and their undo - once per real change, and not for a write that changes nothing.
	void theOrganiserSignalsItsChanges()
	{
		QSignalSpy changed(pluginorganiser::notifier(), &pluginorganiser::Notifier::changed);
		const QString key = QStringLiteral("amplifier");
		QVERIFY(run(QStringLiteral("plugin.organise"), {{QStringLiteral("key"), key}, {QStringLiteral("hidden"), true}}).ok);
		QCOMPARE(changed.count(), 1);
		pluginorganiser::setEntry(key, pluginorganiser::entry(key));  // the same entry again: nothing to show
		QCOMPARE(changed.count(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(changed.count(), 2);
		QVERIFY(pluginorganiser::entry(key).isDefault());
		QVERIFY(run(QStringLiteral("plugin.prefer_format"), {{QStringLiteral("format"), QStringLiteral("lv2")}}).ok);
		QCOMPARE(changed.count(), 3);
		pluginorganiser::setPreferredFormat(QStringLiteral("lv2"));  // unchanged
		QCOMPARE(changed.count(), 3);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(changed.count(), 4);
		QVERIFY(pluginorganiser::preferredFormat().isEmpty());
	}
};

QTEST_MAIN(DeviceCopyTest)
#include "DeviceCopyTest.moc"
