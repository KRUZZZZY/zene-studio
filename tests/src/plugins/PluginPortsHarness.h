/*
 * PluginPortsHarness.h - Part C (task #585) behaviour-preservation harness
 *
 * This header is compiled into two executables:
 *
 *   - PluginPortsMigrationReference: builds the pre-migration plugin sources
 *     extracted verbatim from the base commit into reference modules
 *     (tests/reference/, see ORIGIN.tsv) and renders their output to a file.
 *   - PluginPortsMigrationTest: loads the migrated plugin modules produced by
 *     the normal build, renders the same input through the same control
 *     settings and compares the two renders sample-exactly.
 *
 * Both sides share this header for the test signal, the control-settings
 * round trip and the render loop, so a sample difference can only come from
 * the plugin DSP, not from the harness.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef LMMS_TESTS_PLUGIN_PORTS_HARNESS_H
#define LMMS_TESTS_PLUGIN_PORTS_HARNESS_H

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QString>

#include "AudioBus.h"
#include "Effect.h"
#include "EffectControls.h"
#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "base64.h"

namespace partc
{

using namespace lmms; // SampleFrame, AudioBus, Effect, f_cnt_t, ch_cnt_t

//! Canonical plugin order shared by the reference renderer and the test.
//! peakcontrollereffect is migrated but not rendered here: src/core/PeakController.cpp
//! includes the migrated plugin header, so a pre-migration reference object of the
//! same class has an incompatible layout (ODR) and crashes in the reference process.
inline auto pluginNames() -> const std::array<const char*, 15>&
{
	static const std::array<const char*, 15> names{
		"amplifier", "bassbooster", "bitcrush", "dualfilter",
		"waveshaper", "flanger", "delay",
		"compressor", "crossovereq", "dynamicsprocessor", "lomm", "multitapecho",
		"reverbsc", "stereoenhancer", "stereomatrix"};
	return names;
}

inline constexpr ch_cnt_t ChannelPairs = 1; //!< stereo track channel pair
//! Consecutive buffers per plugin. 64 buffers * 256 frames = 16384 frames,
//! which is longer than the longest wet-path latency under test (ReverbSC's
//! pre-delay is >= 1933 samples, MultitapEcho's first tap is 250 ms at the
//! default step length, DynamicsProcessor's attack is 50 ms), so the stateful
//! wet paths are actually reached and compared.
inline constexpr int Buffers = 64;
inline constexpr float WetLevel = 0.75f;    //!< dry/wet mix used by both sides

struct SettingOverride
{
	const char* name;
	const char* value;
};

/*!
 * Control overrides applied on top of each plugin's own defaults. Values are
 * deliberately non-default so the DSP is actually exercised; ranges match the
 * models created by the plugins' *Controls constructors.
 */
inline auto overridesFor(const std::string& plugin) -> std::vector<SettingOverride>
{
	if (plugin == "amplifier")
	{
		return {{"volume", "150"}, {"pan", "-30"}, {"left", "80"}, {"right", "60"}};
	}
	if (plugin == "bassbooster")
	{
		return {{"freq", "80"}, {"gain", "1.4"}, {"ratio", "0.7"}};
	}
	if (plugin == "bitcrush")
	{
		return {{"ingain", "1.2"}, {"innoise", "0.05"}, {"outgain", "0.9"},
			{"outclip", "0.8"}, {"rate", "8000"}, {"stereodiff", "0.2"},
			{"levels", "16"}, {"rateon", "1"}, {"depthon", "1"}};
	}
	if (plugin == "dualfilter")
	{
		return {{"enabled1", "1"}, {"filter1", "1"}, {"cut1", "800"},
			{"res1", "1.5"}, {"gain1", "150"}, {"mix", "0.7"},
			{"enabled2", "1"}, {"filter2", "0"}, {"cut2", "3000"},
			{"res2", "1.2"}, {"gain2", "90"}};
	}
	if (plugin == "waveshaper")
	{
		return {{"inputGain", "1.2"}, {"outputGain", "0.9"}, {"clipInput", "1"}};
	}
	if (plugin == "flanger")
	{
		return {{"DelayTimeSamples", "0.005"}, {"LfoFrequency", "0.4"},
			{"LfoAmount", "0.001"}, {"LfoPhase", "90"}, {"Feedback", "0.5"},
			{"WhiteNoise", "0.01"}, {"Invert", "0"}};
	}
	if (plugin == "delay")
	{
		return {{"DelayTimeSamples", "0.005"}, {"FeebackAmount", "0.6"},
			{"LfoFrequency", "0.3"}, {"LfoAmount", "0.0005"}, {"OutGain", "-6"}};
	}
	if (plugin == "compressor")
	{
		return {{"threshold", "-20"}, {"ratio", "4"}, {"attack", "5"}, {"release", "200"},
			{"knee", "6"}, {"inGain", "3"}, {"outGain", "6"}, {"mix", "70"},
			{"stereoLink", "2"}, {"limiter", "0.5"}, {"rms", "50"}, {"tilt", "2"},
			{"tiltFreq", "300"}, {"blend", "0.5"}};
	}
	if (plugin == "crossovereq")
	{
		return {{"xover12", "200"}, {"xover23", "1500"}, {"xover34", "6000"},
			{"gain1", "3"}, {"gain2", "-4"}, {"gain3", "1"}, {"gain4", "-1.5"},
			{"mute1", "1"}, {"mute2", "0"}, {"mute3", "1"}, {"mute4", "0"}};
	}
	if (plugin == "dynamicsprocessor")
	{
		return {{"inputGain", "1.5"}, {"outputGain", "0.7"}, {"attack", "50"},
			{"release", "300"}, {"stereoMode", "1"}};
	}
	if (plugin == "lomm")
	{
		return {{"depth", "0.6"}, {"time", "3"}, {"inVol", "-6"}, {"outVol", "6"},
			{"upward", "2"}, {"downward", "0"}, {"split1", "4000"}, {"split2", "200"},
			{"knee", "12"}, {"rmsTime", "20"}};
	}
	if (plugin == "multitapecho")
	{
		return {{"steps", "8"}, {"steplength", "250"}, {"drygain", "6"},
			{"swapinputs", "1"}, {"stages", "2"}};
	}
	// Not rendered by this harness (see pluginNames() note) — kept so the settings
	// are ready if the reference module becomes loadable in a later slice.
	if (plugin == "peakcontrollereffect")
	{
		return {{"base", "0.25"}, {"amount", "0.5"}, {"attack", "0.1"}, {"decay", "0.2"},
			{"treshold", "0.3"}, {"abs", "1"}, {"amountmult", "2"}, {"mute", "0"}};
	}
	if (plugin == "reverbsc")
	{
		return {{"input_gain", "6"}, {"size", "0.95"}, {"color", "6000"},
			{"output_gain", "-6"}};
	}
	if (plugin == "stereoenhancer")
	{
		return {{"width", "120"}};
	}
	if (plugin == "stereomatrix")
	{
		return {{"l-l", "0.8"}, {"l-r", "0.2"}, {"r-l", "0.1"}, {"r-r", "0.9"}};
	}
	return {};
}

/*!
 * Deterministic, integer-only test signal: identical bit patterns in every
 * process and on every platform (no libm, no RNG).
 */
inline auto signalSample(std::uint32_t x) -> float
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return static_cast<float>(x >> 9) * (1.0f / 4194304.0f) - 1.0f; // [-1, 1)
}

inline void fillInput(std::vector<SampleFrame>& data, f_cnt_t frames, std::uint32_t seed)
{
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const auto i = static_cast<std::uint32_t>(f) + 1u;
		data[f] = SampleFrame{signalSample(seed + 0x9e3779b9u * i),
			signalSample(seed + 0x85ebca6bu * i + 0x1234567u)};
	}
}

/*!
 * Applies the test settings through the real project load path: the plugin's
 * own defaults are serialised to XML first (so every model the controls class
 * knows about is present, including opaque blobs such as WaveShaper's wave
 * table), then the overrides are written and the document is loaded back.
 */
inline void applyTestSettings(Effect& fx, const std::string& plugin)
{
	QDomDocument doc;
	QDomElement root = doc.createElement("effect");
	root.setAttribute("on", 1);
	root.setAttribute("wet", QString::number(WetLevel, 'g', 9));
	root.setAttribute("autoquit", 0); // keep the effect awake for every buffer

	// saveState() is the public entry point; it dispatches to the controls'
	// saveSettings(), which writes every model as an XML attribute.
	QDomElement controls = fx.controls()->saveState(doc, root);
	for (const auto& o : overridesFor(plugin))
	{
		controls.setAttribute(QString::fromStdString(o.name), QString::fromStdString(o.value));
	}

	// DynamicsProcessor's default wavegraph is the identity curve, which turns
	// the envelope-follower + curve-lookup path into a linear gain. Replace it
	// with a hard-knee curve (~9 dB of compression above -10 dBFS) so the
	// dynamics are actually exercised. Serialised exactly like the plugin does
	// (200 little-endian floats, base64 in the "waveShape" attribute), so both
	// the migrated and the reference build load the same curve.
	if (plugin == "dynamicsprocessor")
	{
		std::array<float, 200> shape{};
		for (int i = 0; i < 200; ++i)
		{
			const float x = (i + 1.0f) / 200.0f;
			shape[i] = x < 0.3f ? x : 0.3f + (x - 0.3f) * 0.35f;
		}
		QString encoded;
		base64::encode(reinterpret_cast<const char*>(shape.data()),
			static_cast<int>(shape.size() * sizeof(float)), encoded);
		controls.setAttribute(QStringLiteral("waveShape"), encoded);
	}

	fx.loadSettings(root);
}

/*!
 * Renders `Buffers` consecutive buffers through the effect's AudioBus entry
 * point - the production path, which dispatches either to the migrated
 * AudioPorts router (AudioPlugin) or to the legacy processImpl(SampleFrame*)
 * implementation - and returns the interleaved float output.
 */
inline auto renderBuffers(Effect& fx, f_cnt_t frames) -> std::vector<float>
{
	std::vector<SampleFrame> data(static_cast<std::size_t>(ChannelPairs) * frames);
	SampleFrame* busPointers[ChannelPairs] = {data.data()};

	std::vector<float> out;
	out.reserve(static_cast<std::size_t>(Buffers) * frames * 2);

	for (int b = 0; b < Buffers; ++b)
	{
		fillInput(data, frames, 0x51ed270bu * static_cast<std::uint32_t>(b + 1));

		AudioBus bus{busPointers, ChannelPairs, frames};
		fx.processAudioBuffer(bus);

		for (f_cnt_t f = 0; f < frames; ++f)
		{
			out.push_back(data[f].left());
			out.push_back(data[f].right());
		}
	}
	return out;
}

/*!
 * Renders on a fresh thread so that thread-local RNG state (lmms_math.h
 * fastRand) starts from its initial seed for every plugin and every process;
 * this keeps the comparison independent of render order.
 */
inline auto renderInFreshThread(Effect& fx, f_cnt_t frames) -> std::vector<float>
{
	std::vector<float> out;
	std::thread worker{[&] { out = renderBuffers(fx, frames); }};
	worker.join();
	return out;
}

inline auto checksum(const std::vector<float>& samples) -> QString
{
	const QByteArray bytes{reinterpret_cast<const char*>(samples.data()),
		static_cast<int>(samples.size() * sizeof(float))};
	return QString::fromLatin1(
		QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

struct PluginRender
{
	QString name;
	QString checksum;
	std::vector<float> samples;
};

inline auto writeRenders(const QString& path, const std::vector<PluginRender>& renders) -> bool
{
	QFile file{path};
	if (!file.open(QIODevice::WriteOnly))
	{
		return false;
	}
	QDataStream stream{&file};
	stream.setVersion(QDataStream::Qt_5_0);
	stream << quint32(0x50415254u); // "PART"
	stream << quint32(renders.size());
	for (const auto& r : renders)
	{
		stream << r.name;
		stream << quint64(r.samples.size());
		for (const float s : r.samples)
		{
			stream << s;
		}
	}
	return stream.status() == QDataStream::Ok;
}

inline auto readRenders(const QString& path) -> std::vector<PluginRender>
{
	std::vector<PluginRender> renders;
	QFile file{path};
	if (!file.open(QIODevice::ReadOnly))
	{
		return renders;
	}
	QDataStream stream{&file};
	stream.setVersion(QDataStream::Qt_5_0);
	quint32 magic = 0;
	quint32 count = 0;
	stream >> magic >> count;
	if (magic != 0x50415254u)
	{
		return {};
	}
	for (quint32 i = 0; i < count; ++i)
	{
		PluginRender r;
		quint64 n = 0;
		stream >> r.name >> n;
		r.samples.resize(static_cast<std::size_t>(n));
		for (std::size_t j = 0; j < r.samples.size(); ++j)
		{
			stream >> r.samples[j];
		}
		r.checksum = checksum(r.samples);
		renders.push_back(std::move(r));
	}
	return renders;
}

//! Largest absolute sample difference; 0.0f means bit-identical renders.
inline auto maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) -> float
{
	if (a.size() != b.size())
	{
		return std::numeric_limits<float>::infinity();
	}
	float worst = 0.0f;
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		const float d = std::fabs(a[i] - b[i]);
		if (d > worst)
		{
			worst = d;
		}
	}
	return worst;
}

} // namespace partc

#endif // LMMS_TESTS_PLUGIN_PORTS_HARNESS_H
