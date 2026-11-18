/*
 * ZeneRackTest.cpp - the Zene Rack VST3 bundle, loaded by an external host
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * WHAT THIS PROVES (Q050-267.1). The subject is the built ZeneRack.vst3
 * bundle, opened through the VST3 SDK's own hosting loader (Module::create and
 * IPluginFactory), not through Zene's host wrapper:
 *
 *   1. An external VST3 host discovers the module: the factory names Zene
 *      Studio and lists a kVstAudioEffectClass called "Zene Rack" in the Fx
 *      subcategory, and an instance can be created from that class id.
 *   2. The module reports the one supported chain - Amplifier > BassBooster >
 *      WaveShaper - through the ordered parameter names, the read-only Chain
 *      parameter, the IUnitInfo program list and the saved component state.
 *   3. It processes audio through that chain in that order: the render matches
 *      an independent implementation of the documented stages applied in the
 *      documented order, and differs from the same stages applied in reverse.
 *      A pass-through or a reordered chain cannot satisfy both assertions.
 *
 * The DSP is deliberately simple and fully specified so the expected samples
 * are computed here rather than compared to a recorded file.
 */

#include <QtTest>

#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/module.h"

#ifndef ZENERACK_BUNDLE_PATH
#define ZENERACK_BUNDLE_PATH ""
#endif

namespace
{
using namespace Steinberg;
using namespace Steinberg::Vst;

constexpr char kChainDescriptor[] = "Amplifier > BassBooster > WaveShaper";
constexpr ParamID kChainParam = 1000;
constexpr ParamID kGainParam = 1001;
constexpr ParamID kBassParam = 1002;
constexpr ParamID kDriveParam = 1003;
constexpr SampleRate kSampleRate = 48000.0;
constexpr int32 kBlockSize = 512;

//! Independent model of the documented stages. These mirror BassBooster's
//! FastBassBoost and WaveShaper's Input/Clip sections, not the product source.
struct DocumentedChain
{
	float gain = 1.0f;
	float bassGain = 1.0f;
	float drive = 1.0f;
	double cap[2] = {0.0, 0.0};

	float process (float sample, int channel)
	{
		sample = sample * gain; // 1. Amplifier
		const double frequency = 100.0 * kSampleRate / 44100.0; // 2. BassBooster
		const double gain1 = 1.0 / (frequency + 1.0);
		cap[channel] = (static_cast<double> (sample) + cap[channel] * frequency) * gain1;
		sample = static_cast<float> (
		    (static_cast<double> (sample) + cap[channel] * 2.0) * static_cast<double> (bassGain));
		sample = sample * drive; // 3. WaveShaper input gain, then Clip
		return sample < -1.0f ? -1.0f : (sample > 1.0f ? 1.0f : sample);
	}
};

//! The same three stages in the opposite order: WaveShaper, BassBooster,
//! Amplifier. If the product ran them this way, the render would match this
//! model instead of DocumentedChain.
struct ReverseChain
{
	float gain = 1.0f;
	float bassGain = 1.0f;
	float drive = 1.0f;
	double cap[2] = {0.0, 0.0};

	float process (float sample, int channel)
	{
		sample = sample * drive; // 3. WaveShaper first
		sample = sample < -1.0f ? -1.0f : (sample > 1.0f ? 1.0f : sample);
		const double frequency = 100.0 * kSampleRate / 44100.0; // 2. BassBooster
		const double gain1 = 1.0 / (frequency + 1.0);
		cap[channel] = (static_cast<double> (sample) + cap[channel] * frequency) * gain1;
		sample = static_cast<float> (
		    (static_cast<double> (sample) + cap[channel] * 2.0) * static_cast<double> (bassGain));
		return sample * gain; // 1. Amplifier last
	}
};

struct Instance
{
	IPtr<IComponent> component;
	IPtr<IAudioProcessor> processor;
	IPtr<IEditController> controller;
	IPtr<IUnitInfo> unitInfo;
	bool prepared = false;

	bool open (const VST3::Hosting::PluginFactory& factory, const VST3::UID& id,
	           QString* error)
	{
		component = factory.createInstance<IComponent> (id);
		if (!component)
		{
			*error = QStringLiteral ("factory could not create the component");
			return false;
		}
		processor = U::cast<IAudioProcessor> (component);
		controller = U::cast<IEditController> (component);
		unitInfo = U::cast<IUnitInfo> (component);
		if (!processor || !controller || !unitInfo)
		{
			*error = QStringLiteral ("the component lacks IAudioProcessor/IEditController/IUnitInfo");
			return false;
		}
		if (component->initialize (nullptr) != kResultOk)
		{
			*error = QStringLiteral ("initialize failed");
			return false;
		}
		SpeakerArrangement stereo = SpeakerArr::kStereo;
		if (processor->setBusArrangements (&stereo, 1, &stereo, 1) != kResultOk)
		{
			*error = QStringLiteral ("stereo bus arrangement refused");
			return false;
		}
		ProcessSetup setup {};
		setup.processMode = kRealtime;
		setup.symbolicSampleSize = kSample32;
		setup.maxSamplesPerBlock = kBlockSize;
		setup.sampleRate = kSampleRate;
		if (processor->setupProcessing (setup) != kResultOk)
		{
			*error = QStringLiteral ("setupProcessing failed");
			return false;
		}
		if (component->setActive (true) != kResultOk || processor->setProcessing (true) != kResultOk)
		{
			*error = QStringLiteral ("activation failed");
			return false;
		}
		prepared = true;
		return true;
	}

	void setPlain (ParamID id, double plain)
	{
		const ParamValue normalized = controller->plainParamToNormalized (id, plain);
		QVERIFY (controller->setParamNormalized (id, normalized) == kResultOk);
	}

	void process (const std::vector<float>& left, const std::vector<float>& right,
	              std::vector<float>& outLeft, std::vector<float>& outRight)
	{
		outLeft.assign (left.size (), 0.0f);
		outRight.assign (right.size (), 0.0f);
		float* inputs[2] = {const_cast<float*> (left.data ()), const_cast<float*> (right.data ())};
		float* outputs[2] = {outLeft.data (), outRight.data ()};
		AudioBusBuffers inputBus;
		inputBus.numChannels = 2;
		inputBus.channelBuffers32 = inputs;
		AudioBusBuffers outputBus;
		outputBus.numChannels = 2;
		outputBus.channelBuffers32 = outputs;
		ProcessData data;
		data.symbolicSampleSize = kSample32;
		data.numSamples = static_cast<int32> (left.size ());
		data.numInputs = 1;
		data.inputs = &inputBus;
		data.numOutputs = 1;
		data.outputs = &outputBus;
		QVERIFY (processor->process (data) == kResultOk);
	}

	~Instance ()
	{
		if (component)
		{
			if (prepared)
			{
				processor->setProcessing (false);
				component->setActive (false);
			}
			component->terminate ();
		}
	}
};

//! The bundle's class info: the first audio effect named "Zene Rack".
VST3::Hosting::ClassInfo rackClass (const VST3::Hosting::PluginFactory& factory)
{
	for (const auto& info : factory.classInfos ())
	{
		if (info.name () == "Zene Rack")
		{
			return info;
		}
	}
	return VST3::Hosting::ClassInfo {};
}

QString utf16ToQString (const TChar* text, int capacity)
{
	QString value;
	for (int i = 0; i < capacity && text[i] != 0; ++i)
	{
		value.append (QChar (static_cast<char16_t> (text[i])));
	}
	return value;
}
} // namespace

class ZeneRackTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase ();
	void testExternalHostDiscoversTheRack ();
	void testModuleReportsTheSupportedChainInOrder ();
	void testProcessesTheDocumentedChainInThatOrder ();

private:
	VST3::Hosting::Module::Ptr m_module;
};

void ZeneRackTest::initTestCase ()
{
	const QString path = QStringLiteral (ZENERACK_BUNDLE_PATH);
	if (path.isEmpty () || !QFileInfo::exists (path))
	{
		QFAIL ("no ZeneRack.vst3 bundle available (the zenerack target must be built)");
	}
	std::string error;
	m_module = VST3::Hosting::Module::create (path.toStdString (), error);
	QVERIFY2 (m_module != nullptr,
	          qPrintable (QStringLiteral ("Module::create failed: %1").arg (QString::fromStdString (error))));
}

void ZeneRackTest::testExternalHostDiscoversTheRack ()
{
	const auto& factory = m_module->getFactory ();
	QCOMPARE (QString::fromStdString (factory.info ().vendor ()), QStringLiteral ("Zene Studio"));

	const VST3::Hosting::ClassInfo info = rackClass (factory);
	QCOMPARE (QString::fromStdString (info.name ()), QStringLiteral ("Zene Rack"));
	QCOMPARE (QString::fromStdString (info.category ()), QStringLiteral ("Audio Module Class"));
	QVERIFY (QString::fromStdString (info.subCategoriesString ()).contains (QStringLiteral ("Fx")));

	QString error;
	Instance instance;
	QVERIFY2 (instance.open (factory, info.ID (), &error), qPrintable (error));

	// A host may probe with 0 on a side to mean "leave that side unchanged";
	// VST3 does not mean "refuse" by it. Both no-change forms and the
	// documented stereo pair are accepted.
	SpeakerArrangement stereo = SpeakerArr::kStereo;
	QVERIFY (instance.processor->setBusArrangements (nullptr, 0, nullptr, 0) == kResultOk);
	QVERIFY (instance.processor->setBusArrangements (nullptr, 0, &stereo, 1) == kResultOk);
	QVERIFY (instance.processor->setBusArrangements (&stereo, 1, nullptr, 0) == kResultOk);
	QVERIFY (instance.processor->setBusArrangements (&stereo, 1, &stereo, 1) == kResultOk);
}

void ZeneRackTest::testModuleReportsTheSupportedChainInOrder ()
{
	const auto& factory = m_module->getFactory ();
	QString error;
	Instance instance;
	QVERIFY2 (instance.open (factory, rackClass (factory).ID (), &error), qPrintable (error));

	// The parameter surface, in the order the host displays it.
	QVERIFY (instance.controller->getParameterCount () >= 4);
	const auto nameAt = [&] (int index) {
		ParameterInfo info {};
		if (instance.controller->getParameterInfo (index, info) != kResultOk)
		{
			return QStringLiteral ("<no parameter>");
		}
		return utf16ToQString (info.title, 128);
	};
	QCOMPARE (nameAt (1), QStringLiteral ("1 Amplifier Gain"));
	QCOMPARE (nameAt (2), QStringLiteral ("2 BassBooster Gain"));
	QCOMPARE (nameAt (3), QStringLiteral ("3 WaveShaper Drive"));

	// The read-only Chain parameter reports the same ordered string.
	ParameterInfo chainInfo {};
	bool foundChain = false;
	for (int i = 0; i < instance.controller->getParameterCount (); ++i)
	{
		ParameterInfo info {};
		QVERIFY (instance.controller->getParameterInfo (i, info) == kResultOk);
		if (info.id == kChainParam)
		{
			chainInfo = info;
			foundChain = true;
		}
	}
	QVERIFY (foundChain);
	QVERIFY ((chainInfo.flags & ParameterInfo::kIsReadOnly) != 0);
	String128 chainString {};
	QVERIFY (instance.controller->getParamStringByValue (kChainParam, 0.0, chainString) == kResultOk);
	QCOMPARE (utf16ToQString (chainString, 128), QString::fromLatin1 (kChainDescriptor));

	// The program list is the host-visible chain menu.
	QCOMPARE (instance.unitInfo->getProgramListCount (), 1);
	ProgramListInfo listInfo {};
	QVERIFY (instance.unitInfo->getProgramListInfo (0, listInfo) == kResultOk);
	QCOMPARE (listInfo.programCount, 1);
	String128 programName {};
	QVERIFY (instance.unitInfo->getProgramName (listInfo.id, 0, programName) == kResultOk);
	QCOMPARE (utf16ToQString (programName, 128), QString::fromLatin1 (kChainDescriptor));

	// The saved component state also names the chain, so a host that only
	// stores state still has the report.
	MemoryStream state;
	QVERIFY (instance.component->getState (&state) == kResultOk);
	const QByteArray saved (state.getData (), static_cast<int> (state.getSize ()));
	QVERIFY2 (saved.contains (kChainDescriptor), saved.constData ());
}

void ZeneRackTest::testProcessesTheDocumentedChainInThatOrder ()
{
	const auto& factory = m_module->getFactory ();
	QString error;
	Instance instance;
	QVERIFY2 (instance.open (factory, rackClass (factory).ID (), &error), qPrintable (error));

	instance.setPlain (kGainParam, 2.0);
	instance.setPlain (kBassParam, 1.0);
	instance.setPlain (kDriveParam, 1.0);

	constexpr int frames = 256;
	std::vector<float> left (frames);
	std::vector<float> right (frames);
	for (int i = 0; i < frames; ++i)
	{
		left[static_cast<std::size_t> (i)] = 0.9f * static_cast<float> (i) / static_cast<float> (frames);
		right[static_cast<std::size_t> (i)] = 0.6f * std::sin (0.05f * static_cast<float> (i));
	}
	std::vector<float> outLeft;
	std::vector<float> outRight;
	instance.process (left, right, outLeft, outRight);

	DocumentedChain forward;
	forward.gain = 2.0f;
	forward.bassGain = 1.0f;
	forward.drive = 1.0f;
	ReverseChain reversed;
	reversed.gain = 2.0f;
	reversed.bassGain = 1.0f;
	reversed.drive = 1.0f;

	double largestForwardError = 0.0;
	double largestReverseError = 0.0;
	for (int i = 0; i < frames; ++i)
	{
		const auto index = static_cast<std::size_t> (i);
		const float expectedLeft = forward.process (left[index], 0);
		const float expectedRight = forward.process (right[index], 1);
		largestForwardError = std::max (largestForwardError,
		                                std::abs (static_cast<double> (outLeft[index]) - expectedLeft));
		largestForwardError = std::max (largestForwardError,
		                                std::abs (static_cast<double> (outRight[index]) - expectedRight));

		// The same stages in REVERSE order, for the same inputs.
		const float reverseLeft = reversed.process (left[index], 0);
		const float reverseRight = reversed.process (right[index], 1);
		largestReverseError = std::max (largestReverseError,
		                                std::abs (static_cast<double> (outLeft[index]) - reverseLeft));
		largestReverseError = std::max (largestReverseError,
		                                std::abs (static_cast<double> (outRight[index]) - reverseRight));
	}

	QVERIFY2 (largestForwardError <= 1.0e-6,
	          qPrintable (QStringLiteral ("documented order mismatch: %1").arg (largestForwardError)));
	QVERIFY2 (largestReverseError > 0.1,
	          qPrintable (QStringLiteral ("the chain order is not observable: %1").arg (largestReverseError)));
}

QTEST_MAIN (ZeneRackTest)
#include "ZeneRackTest.moc"
