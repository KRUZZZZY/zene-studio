/*
 * RackTestSupport.h - the fixture the two rack command-group tests share:
 *                     RackMacrosTest.cpp (the macros) and RackZonesTest.cpp
 *                     (the key/velocity zones).
 *
 * ONE definition for the two test files that use it, the same rule
 * ReversibilityTestSupport.h follows for the A16 tests: a second copy of a
 * helper is the drift this split exists to prevent. Both tests drive a real
 * Engine, a real Mixer and real AutomatableModel parameters through a LOCAL test
 * effect - no plugin module, so nothing here starts the plugin loader and the
 * teardown race docs/RACKS.md section 6 records cannot land on these binaries.
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

#ifndef LMMS_RACK_TEST_SUPPORT_H
#define LMMS_RACK_TEST_SUPPORT_H

#include <cstdio>
#include <vector>

#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "Engine.h"
#include "Mixer.h"
#include "Plugin.h"
#include "Rack.h"
#include "RackMacros.h"
#include "RackZones.h"
#include "SampleFrame.h"

namespace racktest
{

using namespace lmms;

//! The descriptor and key of the test effect. The key cannot be null:
//! EffectChain::saveSettings() writes it through effect->key().saveXML().
inline const Plugin::Descriptor s_rackParamDescriptor
{
	"rackparamtest",
	"Rack macro test effect",
	"Two parameters (0..100 and -100..100) for the macro window arithmetic",
	"LMMS",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr
};

inline const Plugin::Descriptor::SubPluginFeatures::Key s_rackParamKey{&s_rackParamDescriptor};

//! The two parameters, with ranges chosen so the window arithmetic is exact:
//! every product used by the tests is representable in binary floating point.
constexpr float kGainMinimum = 0.0f;
constexpr float kGainMaximum = 100.0f;
constexpr float kPanMinimum = -100.0f;
constexpr float kPanMaximum = 100.0f;

constexpr auto kGainName = "Gain";
constexpr auto kPanName = "Panning";

//! Master + the one channel that carries the rack.
constexpr int kChannels = 2;
//! The channel under test.
constexpr int kChannel = 1;
//! The rack chain the macros drive (1 = the first parallel chain).
constexpr int kDrivenChain = 1;

class RackParamControls : public EffectControls
{
public:
	explicit RackParamControls(Effect* effect) :
		EffectControls(effect),
		m_gain(50.0f, kGainMinimum, kGainMaximum, 1.0f, this, QString::fromLatin1(kGainName)),
		m_pan(0.0f, kPanMinimum, kPanMaximum, 1.0f, this, QString::fromLatin1(kPanName))
	{
	}

	auto nodeName() const -> QString override { return QStringLiteral("RackParamControls"); }
	auto controlCount() -> int override { return 2; }
	auto createView() -> gui::EffectControlDialog* override { return nullptr; }

	void saveSettings(QDomDocument& doc, QDomElement& element) override
	{
		Q_UNUSED(doc)
		Q_UNUSED(element)
	}
	void loadSettings(const QDomElement& element) override { Q_UNUSED(element) }

	FloatModel m_gain;
	FloatModel m_pan;
};

class RackParamEffect : public Effect
{
public:
	explicit RackParamEffect(Model* parent) : Effect{&s_rackParamDescriptor, parent, &s_rackParamKey}
	{
	}

	auto controls() -> EffectControls* override { return &m_controls; }

protected:
	auto processImpl(SampleFrame*, const f_cnt_t) -> ProcessStatus override
	{
		return ProcessStatus::Continue;
	}

private:
	RackParamControls m_controls{this};
};

void printEvidence(const char* label, const QString& detail)
{
	std::fprintf(stdout, "RACK_MACRO_EVIDENCE %s %s\n", label, detail.toUtf8().constData());
	std::fflush(stdout);
}

//! A macro target on the driven chain, with @a low/@a high as the window and
//! @a parameter as the name plugin.param_get would report.
inline RackMacroTarget targetOf(const char* parameter, float low, float high)
{
	RackMacroTarget target;
	target.chain = kDrivenChain;
	target.effect = 0;
	target.parameter = QString::fromLatin1(parameter);
	target.low = low;
	target.high = high;
	return target;
}

/*! Starts the engine and builds the fixture both tests need: two mixer
 *  channels, the channel's own chain, one parallel rack chain, and one
 *  RackParamEffect in each of the two chains. Returns false (with @a why set)
 *  when the engine does not come up the way the tests assume - a caller QVERIFYs
 *  on it rather than measuring a fixture that is not there.
 */
inline bool initRackFixture(QString* why)
{
	Engine::init(true);
	if (Engine::audioEngine() == nullptr || Engine::mixer() == nullptr)
	{
		*why = QStringLiteral("the engine did not come up headless");
		return false;
	}
	// The dummy device thread renders in the background; the tests drive the
	// model synchronously, so stop it (RackTest's setup).
	Engine::audioEngine()->audioDev()->stopProcessing();
	ControlRegistry::setReady(true);

	auto mixer = Engine::mixer();
	while (mixer->numChannels() < kChannels) { mixer->createChannel(); }

	MixerChannel* channel = mixer->mixerChannel(kChannel);
	channel->m_fxChain.appendEffect(new RackParamEffect(&channel->m_fxChain));
	Rack& rack = channel->m_rack;
	if (rack.addChain() != kDrivenChain)
	{
		*why = QStringLiteral("the rack's first parallel chain is not chain %1").arg(kDrivenChain);
		return false;
	}
	rack.chain(kDrivenChain)->appendEffect(new RackParamEffect(rack.chain(kDrivenChain)));
	return true;
}

inline void teardownRackFixture()
{
	ControlRegistry::setReady(false);
	Engine::destroy();
}

inline auto rackUnderTest() -> Rack& { return Engine::mixer()->mixerChannel(kChannel)->m_rack; }

//! The effect the macros drive: the first device of the rack's first parallel
//! chain. nullptr rather than an out-of-bounds read when the fixture is not
//! what it should be.
inline auto effectUnderTest() -> Effect*
{
	EffectChain* const chain = rackUnderTest().chain(kDrivenChain);
	if (chain == nullptr || chain->effects().empty()) { return nullptr; }
	return chain->effects()[0];
}

//! The driven effect's parameter named @a name, resolved by DISPLAY NAME - the
//! way the production code resolves a macro target. Indexing the list by
//! position would be wrong: controlEffectParameters reports the effect's own
//! child order plus any linked group, so position is not the parameter.
inline auto parameterModel(const QString& name) -> FloatModel*
{
	Effect* const effect = effectUnderTest();
	if (effect == nullptr) { return nullptr; }
	for (AutomatableModel* model : controlEffectParameters(effect))
	{
		if (model->displayName() == name) { return dynamic_cast<FloatModel*>(model); }
	}
	return nullptr;
}

inline auto gainModel() -> FloatModel* { return parameterModel(QString::fromLatin1(kGainName)); }
inline auto panModel() -> FloatModel* { return parameterModel(QString::fromLatin1(kPanName)); }

//! The bus the channel under test is processed on, for the predicates that ask
//! whether a block of this shape can be rendered through the rack.
inline auto currentBus() -> AudioBus
{
	static std::vector<SampleFrame> block;
	static SampleFrame* data[1] = {nullptr};
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	block.assign(static_cast<std::size_t>(fpp), SampleFrame{});
	data[0] = block.data();
	return AudioBus{data, 1, fpp};
}

//! Channel @a kChannel's own <rack> element inside a saved <mixer> document.
inline auto savedRackElement(const QDomElement& root) -> QDomElement
{
	for (QDomElement channel = root.firstChildElement(QStringLiteral("mixerchannel"));
		!channel.isNull();
		channel = channel.nextSiblingElement(QStringLiteral("mixerchannel")))
	{
		if (channel.attribute(QStringLiteral("num")).toInt() == kChannel)
		{
			return channel.firstChildElement(QStringLiteral("rack"));
		}
	}
	return QDomElement{};
}

//! The mixer XML the reload halves of the tests load: two channels, channel
//! kChannel's rack carrying one macro with one target and two zones, with no
//! effects anywhere - an effect instantiated from XML starts the plugin loader,
//! the teardown race docs/RACKS.md section 6 records, so the load halves assert
//! the macro target as DATA and a live write is asserted through the fixture
//! the tests build themselves.
inline auto reloadFixtureXml() -> const char*
{
	return "<mixer>\n"
		"  <mixerchannel num=\"0\" muted=\"0\" volume=\"1\" name=\"Master\">\n"
		"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
		"  </mixerchannel>\n"
		"  <mixerchannel num=\"1\" muted=\"0\" volume=\"1\" name=\"Rack\">\n"
		"    <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
		"    <rack version=\"2\" selected=\"1\">\n"
		"      <chain index=\"1\">\n"
		"        <fxchain numofeffects=\"0\" enabled=\"0\"/>\n"
		"      </chain>\n"
		"      <macro name=\"Loaded\" value=\"0.25\">\n"
		"        <target chain=\"1\" effect=\"0\" parameter=\"Gain\" low=\"0.5\" high=\"1\"/>\n"
		"      </macro>\n"
		"      <zone low_key=\"36\" high_key=\"48\" low_velocity=\"0\" high_velocity=\"200\" "
		"chain=\"1\" sample=\"kick.wav\"/>\n"
		"      <zone low_key=\"49\" high_key=\"60\" low_velocity=\"100\" high_velocity=\"200\" "
		"chain=\"0\"/>\n"
		"    </rack>\n"
		"    <send channel=\"0\" amount=\"1\"/>\n"
		"  </mixerchannel>\n"
		"</mixer>\n";
}

} // namespace racktest

#endif // LMMS_RACK_TEST_SUPPORT_H
