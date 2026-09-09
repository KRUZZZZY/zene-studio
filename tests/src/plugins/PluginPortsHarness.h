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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
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
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "LmmsTypes.h"
#include "Midi.h"
#include "MidiEvent.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"
#include "TimePos.h"
#include "base64.h"

namespace partc
{

using namespace lmms; // SampleFrame, AudioBus, Effect, f_cnt_t, ch_cnt_t

//! Canonical plugin order shared by the reference renderer and the test.
//! peakcontrollereffect is migrated but not rendered here: src/core/PeakController.cpp
//! includes the migrated plugin header, so a pre-migration reference object of the
//! same class has an incompatible layout (ODR) and crashes in the reference process.
inline auto pluginNames() -> const std::array<const char*, 20>&
{
	static const std::array<const char*, 20> names{
		"amplifier", "bassbooster", "bitcrush", "dualfilter",
		"waveshaper", "flanger", "delay",
		"compressor", "crossovereq", "dynamicsprocessor", "lomm", "multitapecho",
		"reverbsc", "stereoenhancer", "stereomatrix",
		// Slice 3 (task #589): analysers, Dispersion, granular shifter and Eq.
		"dispersion", "vectorscope", "analyzer", "granularpitchshifter", "eq"};
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
	// Slice 3 (task #589) plugins.
	if (plugin == "dispersion")
	{
		return {{"amount", "12"}, {"freq", "440"}, {"reso", "2.0"},
			{"feedback", "0.7"}, {"dc", "1"}};
	}
	if (plugin == "vectorscope")
	{
		return {{"Logarithmic", "1"}, {"LinesMode", "0"}};
	}
	if (plugin == "analyzer")
	{
		return {{"Waterfall", "1"}, {"Smooth", "1"}, {"Stereo", "1"}, {"PeakHold", "1"},
			{"LogX", "0"}, {"LogY", "0"}, {"RangeX", "1"}, {"RangeY", "2"},
			{"BlockSize", "3"}, {"WindowType", "1"}, {"EnvelopeRes", "0.5"},
			{"SpectrumRes", "2.0"}, {"PeakDecayFactor", "0.995"}, {"AverageWeight", "0.3"},
			{"WaterfallHeight", "400"}, {"WaterfallGamma", "0.5"}, {"WindowOverlap", "4"},
			{"ZeroPadding", "2"}};
	}
	if (plugin == "granularpitchshifter")
	{
		return {{"pitch", "7"}, {"size", "50"}, {"spray", "0.01"}, {"jitter", "0.3"},
			{"twitch", "0.2"}, {"pitchSpread", "5"}, {"spraySpread", "0.5"},
			{"shape", "1.5"}, {"fadeLength", "0.5"}, {"feedback", "0.4"},
			{"minLatency", "0.05"}, {"prefilter", "0"}, {"density", "4"}, {"glide", "0.2"}};
	}
	if (plugin == "eq")
	{
		return {{"Inputgain", "3"}, {"Outputgain", "-3"}, {"Lowshelfgain", "6"},
			{"Peak1gain", "4"}, {"Peak2gain", "-5"}, {"Peak3gain", "2"}, {"Peak4gain", "-2"},
			{"HighShelfgain", "4"}, {"HPres", "0.8"}, {"LowShelfres", "0.8"},
			{"Peak1bw", "1.5"}, {"Peak2bw", "1.0"}, {"Peak3bw", "2.0"}, {"Peak4bw", "1.2"},
			{"HighShelfres", "0.9"}, {"LPres", "0.8"}, {"HPfreq", "80"},
			{"LowShelffreq", "120"}, {"Peak1freq", "250"}, {"Peak2freq", "800"},
			{"Peak3freq", "3000"}, {"Peak4freq", "8000"}, {"Highshelffreq", "12000"},
			{"LPfreq", "16000"}, {"HPactive", "1"}, {"Lowshelfactive", "1"},
			{"Peak1active", "1"}, {"Peak2active", "1"}, {"Peak3active", "1"},
			{"Peak4active", "1"}, {"Highshelfactive", "1"}, {"LPactive", "0"},
			{"LP12", "1"}, {"LP24", "0"}, {"LP48", "0"}, {"HP12", "1"}, {"HP24", "0"},
			{"HP48", "0"}, {"LP", "0"}, {"HP", "0"}, {"AnalyseIn", "1"}, {"AnalyseOut", "1"}};
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

// ---------------------------------------------------------------------------
// Slice 4 (task #589): instrument plugins.
//
// Instruments are driven by NotePlayHandle state instead of a fixed buffer, so
// the harness reproduces the engine dispatch exactly:
//   * InstrumentTrack::playNote() is called once per period for every
//     instrument (that is where playNoteImpl() runs),
//   * InstrumentPlayHandle::play() is called once per period for
//     single-streamed instruments (OpulenZ), after the notes are processed,
//   * settings are applied through the instrument's own saveState() /
//     restoreState() round trip, which is how InstrumentTrack persists an
//     instrument.
// ---------------------------------------------------------------------------

//! Canonical instrument order shared by the reference renderer and the test.
inline auto instrumentNames() -> const std::array<const char*, 14>&
{
	static const std::array<const char*, 14> names{
		"freeboy", "nes", "sid", "opulenz", "sfxr",
		"bitinvader", "watsyn", "xpressive", "vibedstrings", "kicker",
		// Slice 5 (task #589): multi-oscillator and sample-playback instruments.
		"tripleoscillator", "monstro", "organic", "audiofileprocessor"};
	return names;
}

/*!
 * Instrument control overrides applied on top of each plugin's own defaults.
 * Attribute names come from the instruments' own saveSettings() output; the
 * values are deliberately non-default so the DSP is actually exercised.
 * Expression-driven controls (Xpressive's W* and O* strings) are left at
 * their defaults so a malformed expression cannot skew the comparison.
 */
inline auto instrumentOverridesFor(const std::string& plugin) -> std::vector<SettingOverride>
{
	if (plugin == "freeboy")
	{
		return {{"ch1vol", "12"}, {"ch2vol", "10"}, {"ch3vol", "14"}, {"ch4vol", "8"},
			{"ch1wpd", "2"}, {"ch2wpd", "1"}, {"ch1vsd", "1"}, {"ch2vsd", "0"},
			{"ch1so1", "3"}, {"ch1so2", "5"}, {"ch2so1", "2"}, {"ch3on", "1"},
			{"ch4so1", "6"}, {"ch4so2", "7"}, {"Treble", "4"}, {"Bass", "-3"},
			{"st", "2"}, {"sd", "1"}, {"srs", "3"}, {"srw", "2"},
			{"so1vol", "10"}, {"so2vol", "6"}};
	}
	if (plugin == "nes")
	{
		return {{"on1", "1"}, {"on2", "1"}, {"on3", "1"}, {"on4", "1"},
			{"vol", "14"}, {"vol1", "12"}, {"vol2", "12"}, {"vol3", "12"}, {"vol4", "12"},
			{"dc1", "2"}, {"dc2", "1"}, {"crs1", "1"}, {"crs2", "2"},
			{"envon1", "1"}, {"envlen1", "4"}, {"envloop1", "1"},
			{"sweep1", "1"}, {"swamt1", "3"}, {"swrate1", "2"},
			{"nmode4", "1"}, {"nfreq4", "4"}, {"nq4", "6"}};
	}
	if (plugin == "sid")
	{
		return {{"pulsewidth0", "0.3"}, {"attack0", "2"}, {"decay0", "4"}, {"sustain0", "8"},
			{"release0", "6"}, {"waveform0", "1"}, {"coarse0", "1"}, {"sync0", "1"},
			{"pulsewidth1", "0.6"}, {"attack1", "3"}, {"decay1", "5"}, {"sustain1", "10"},
			{"release1", "4"}, {"waveform1", "2"}, {"ringmod1", "1"},
			{"filterFC", "1024"}, {"filterResonance", "6"}, {"filterMode", "1"},
			{"volume", "12"}};
	}
	if (plugin == "opulenz")
	{
		return {{"op1_a", "8"}, {"op1_d", "6"}, {"op1_s", "3"}, {"op1_r", "7"},
			{"op1_lvl", "50"}, {"op1_mul", "2"}, {"op1_waveform", "1"},
			{"op2_a", "6"}, {"op2_d", "5"}, {"op2_s", "4"}, {"op2_r", "8"},
			{"op2_lvl", "40"}, {"op2_mul", "4"}, {"op2_waveform", "2"},
			{"feedback", "3"}, {"fm", "1"}, {"vib_depth", "2"}, {"trem_depth", "2"}};
	}
	if (plugin == "sfxr")
	{
		return {{"waveForm", "1"}, {"startFreq", "0.4"}, {"minFreq", "0.05"},
			{"slide", "0.2"}, {"dSlide", "0.1"}, {"vibDepth", "0.2"}, {"vibSpeed", "0.3"},
			{"changeAmt", "0.3"}, {"changeSpeed", "0.4"}, {"sqrDuty", "0.6"},
			{"sqrSweep", "0.1"}, {"repeatSpeed", "0.3"}, {"phaserOffset", "0.2"},
			{"phaserSweep", "0.3"}, {"lpFilCut", "0.7"}, {"lpFilCutSweep", "0.1"},
			{"lpFilReso", "0.6"}, {"hpFilCut", "0.2"}, {"hpFilCutSweep", "0.1"},
			{"att", "0.05"}, {"hold", "0.1"}, {"sus", "0.3"}, {"dec", "0.4"}};
	}
	if (plugin == "bitinvader")
	{
		return {{"sampleLength", "64"}, {"interpolation", "1"}, {"normalize", "1"}};
	}
	if (plugin == "watsyn")
	{
		return {{"a1_vol", "80"}, {"a2_vol", "60"}, {"b1_vol", "50"}, {"b2_vol", "40"},
			{"a1_mult", "2"}, {"a2_mult", "3"}, {"b1_mult", "1.5"}, {"b2_mult", "4"},
			{"a1_ltune", "0.1"}, {"a2_rtune", "-0.2"}, {"b1_ltune", "0.3"},
			{"b2_rtune", "-0.4"}, {"a1_pan", "0.2"}, {"b2_pan", "-0.3"},
			{"abmix", "0.7"}, {"envAmt", "0.5"}, {"envAtt", "0.05"},
			{"envHold", "0.2"}, {"envDec", "0.3"}, {"xtalk", "0.2"}, {"amod", "1.2"}};
	}
	if (plugin == "xpressive")
	{
		return {{"A1", "0.7"}, {"A2", "0.4"}, {"A3", "0.9"}, {"PAN1", "0.3"},
			{"PAN2", "-0.4"}, {"RELTRANS", "0.5"}, {"smoothW1", "0.3"},
			{"smoothW2", "0.2"}, {"smoothW3", "0.4"}, {"interpolateW1", "1"},
			{"interpolateW2", "1"}, {"interpolateW3", "0"}};
	}
	if (plugin == "vibedstrings")
	{
		return {{"active0", "1"}, {"volume0", "0.8"}, {"stiffness0", "0.5"}, {"pick0", "0.3"},
			{"pickup0", "0.4"}, {"octave0", "1"}, {"length0", "0.6"}, {"pan0", "0.2"},
			{"detune0", "0.1"}, {"slap0", "0.2"}, {"impulse0", "0.3"},
			{"active1", "1"}, {"volume1", "0.5"}, {"stiffness1", "0.7"}, {"pick1", "0.4"},
			{"length1", "0.5"}, {"detune1", "-0.1"}};
	}
	if (plugin == "kicker")
	{
		return {{"startfreq", "120"}, {"endfreq", "40"}, {"decay", "0.4"}, {"dist", "2"},
			{"distend", "0.5"}, {"gain", "1.2"}, {"env", "0.6"}, {"noise", "0.2"},
			{"click", "0.3"}, {"slope", "0.7"}, {"startnote", "1"}, {"endnote", "0"}};
	}
	// Slice 5 (task #589) instruments.
	if (plugin == "tripleoscillator")
	{
		return {
			{"vol0", "80"}, {"pan0", "-25"}, {"coarse0", "12"}, {"finel0", "5"}, {"finer0", "-5"},
			{"phoffset0", "90"}, {"stphdetun0", "30"}, {"wavetype0", "2"}, {"modalgo1", "1"},
			{"useWaveTable1", "0"},
			{"vol1", "60"}, {"pan1", "0"}, {"coarse1", "-12"}, {"finel1", "3"}, {"finer1", "3"},
			{"phoffset1", "180"}, {"stphdetun1", "60"}, {"wavetype1", "3"}, {"modalgo2", "2"},
			{"useWaveTable2", "0"},
			{"vol2", "40"}, {"pan2", "25"}, {"coarse2", "7"}, {"finel2", "-7"}, {"finer2", "7"},
			{"phoffset2", "270"}, {"stphdetun2", "15"}, {"wavetype2", "1"}, {"modalgo3", "3"},
			{"useWaveTable3", "0"}};
	}
	if (plugin == "monstro")
	{
		return {
			{"o1vol", "80"}, {"o1pan", "-20"}, {"o1crs", "12"}, {"o1ftl", "10"}, {"o1ftr", "-10"},
			{"o1spo", "90"}, {"o1pw", "0.3"}, {"o1ssr", "0"}, {"o1ssf", "0"},
			{"o2vol", "60"}, {"o2pan", "20"}, {"o2crs", "-12"}, {"o2ftl", "5"}, {"o2ftr", "5"},
			{"o2spo", "180"}, {"o2wav", "2"}, {"o2syn", "0"}, {"o2synr", "0"},
			{"o3vol", "40"}, {"o3pan", "0"}, {"o3crs", "7"}, {"o3spo", "270"}, {"o3sub", "0.5"},
			{"o3wav1", "0"}, {"o3wav2", "3"}, {"o3syn", "0"}, {"o3synr", "0"},
			{"l1wav", "1"}, {"l1att", "0.1"}, {"l1rat", "5"}, {"l1phs", "0.25"},
			{"l2wav", "2"}, {"l2att", "0.2"}, {"l2rat", "2"}, {"l2phs", "0.5"},
			{"e1pre", "0.05"}, {"e1att", "0.05"}, {"e1hol", "0.1"}, {"e1dec", "0.2"},
			{"e1sus", "0.7"}, {"e1rel", "0.3"}, {"e1slo", "0.5"},
			{"e2pre", "0.1"}, {"e2att", "0.1"}, {"e2hol", "0.2"}, {"e2dec", "0.3"},
			{"e2sus", "0.5"}, {"e2rel", "0.4"}, {"e2slo", "0.5"},
			{"o23mo", "1"},
			{"v1e1", "1"}, {"v1e2", "0.5"}, {"v1l1", "0.3"}, {"v1l2", "0.2"},
			{"v2e1", "0.5"}, {"v2e2", "0.3"}, {"v2l1", "0.2"}, {"v2l2", "0.1"},
			{"v3e1", "0.4"}, {"v3e2", "0.2"}, {"v3l1", "0.1"}, {"v3l2", "0.1"},
			{"f1e1", "0.2"}, {"f1e2", "0.1"}, {"f2e1", "0.1"}, {"f3e1", "0.1"},
			{"w1e1", "0.2"}, {"s3e1", "0.3"}};
	}
	if (plugin == "organic")
	{
		return {
			{"num_osc", "8"}, {"foldback", "0.2"}, {"vol", "80"},
			{"vol0", "90"}, {"pan0", "-30"}, {"newharmonic0", "2"}, {"newdetune0", "10"}, {"wavetype0", "1"},
			{"vol1", "70"}, {"pan1", "30"}, {"newharmonic1", "3"}, {"newdetune1", "-10"}, {"wavetype1", "2"},
			{"vol2", "60"}, {"pan2", "0"}, {"newharmonic2", "4"}, {"newdetune2", "5"}, {"wavetype2", "3"},
			{"vol3", "50"}, {"pan3", "10"}, {"newharmonic3", "5"}, {"newdetune3", "-5"}, {"wavetype3", "4"},
			{"vol4", "40"}, {"pan4", "-10"}, {"newharmonic4", "6"}, {"newdetune4", "3"}, {"wavetype4", "5"},
			{"vol5", "30"}, {"pan5", "15"}, {"newharmonic5", "7"}, {"newdetune5", "-3"}, {"wavetype5", "1"},
			{"vol6", "20"}, {"pan6", "-15"}, {"newharmonic6", "8"}, {"newdetune6", "2"}, {"wavetype6", "2"},
			{"vol7", "10"}, {"pan7", "5"}, {"newharmonic7", "9"}, {"newdetune7", "-2"}, {"wavetype7", "0"}};
	}
	if (plugin == "audiofileprocessor")
	{
		return {{"amp", "140"}, {"sframe", "0.1"}, {"eframe", "0.9"}, {"lframe", "0.5"},
			{"looped", "1"}, {"reversed", "0"}, {"stutter", "0"}, {"interp", "2"}};
	}
	return {};
}

//! Save/restore round trip, exactly the way InstrumentTrack persists an instrument.
inline void applyInstrumentTestSettings(Instrument& inst, const std::string& plugin)
{
	QDomDocument doc;
	QDomElement root = doc.createElement("instrument");
	// saveState() serialises every model the instrument knows about; the
	// returned element is the instrument's own element, which restoreState()
	// reads back.
	QDomElement saved = inst.saveState(doc, root);
	for (const auto& o : instrumentOverridesFor(plugin))
	{
		saved.setAttribute(QString::fromStdString(o.name), QString::fromStdString(o.value));
	}
	// AudioFileProcessor renders a sample: give it a deterministic one through
	// the same base64 blob the plugin writes for a sample without a file path -
	// raw SampleFrame bytes, exactly the format SampleBuffer::toBase64() /
	// fromBase64() round-trip.
	if (plugin == "audiofileprocessor")
	{
		constexpr int SampleFrames = 2048;
		std::vector<SampleFrame> sample(SampleFrames);
		for (int f = 0; f < SampleFrames; ++f)
		{
			sample[f] = SampleFrame{
				signalSample(0x5f356495u + 0x9e3779b9u * static_cast<std::uint32_t>(f + 1)),
				signalSample(0x2c1b3c6du + 0x85ebca6bu * static_cast<std::uint32_t>(f + 1))};
		}
		const QByteArray bytes{reinterpret_cast<const char*>(sample.data()),
			static_cast<int>(sample.size() * sizeof(SampleFrame))};
		saved.setAttribute(QStringLiteral("sampledata"), QString::fromLatin1(bytes.toBase64()));
	}
	inst.restoreState(saved);
}

/*!
 * Renders one continuously held note through the instrument and returns the
 * interleaved float output. The note is never released, so every instrument
 * sees the same held note; the per-period buffer is cleared first, matching
 * the mixer's zeroed working buffer.
 */
inline auto renderInstrumentBuffers(Instrument& inst, f_cnt_t frames, int key) -> std::vector<float>
{
	InstrumentTrack* track = inst.instrumentTrack();
	std::vector<SampleFrame> data(static_cast<std::size_t>(frames));

	const Note note{TimePos{static_cast<tick_t>(frames * Buffers)}, TimePos{0}, key};
	NotePlayHandle nph{track, 0, frames * Buffers, note, nullptr, -1, NotePlayHandle::Origin::MidiClip};

	if (inst.isSingleStreamed())
	{
		// InstrumentPlayHandle only produces sound once a voice is open.
		inst.handleMidiEvent(MidiEvent{MidiNoteOn, 0, static_cast<std::int16_t>(key), 100}, TimePos{0}, 0);
	}

	std::vector<float> out;
	out.reserve(static_cast<std::size_t>(Buffers) * frames * 2);

	for (int b = 0; b < Buffers; ++b)
	{
		std::fill(data.begin(), data.end(), SampleFrame{0.0f, 0.0f});
		const std::span<SampleFrame> span{data};

		// InstrumentTrack::playNote() first, then InstrumentPlayHandle::play().
		inst.playNote(&nph, span);
		if (inst.isSingleStreamed())
		{
			inst.play(span);
		}

		for (f_cnt_t f = 0; f < frames; ++f)
		{
			out.push_back(data[f].left());
			out.push_back(data[f].right());
		}
	}

	// Hand plugin-owned note data back while the note handle is still alive.
	if (nph.m_pluginData != nullptr)
	{
		inst.deleteNotePluginData(&nph);
		nph.m_pluginData = nullptr;
	}
	return out;
}

//! Fresh-thread wrapper, for the same RNG-state reason as renderInFreshThread().
inline auto renderInstrumentInFreshThread(Instrument& inst, f_cnt_t frames, int key) -> std::vector<float>
{
	std::vector<float> out;
	std::thread worker{[&] { out = renderInstrumentBuffers(inst, frames, key); }};
	worker.join();
	return out;
}

} // namespace partc

#endif // LMMS_TESTS_PLUGIN_PORTS_HARNESS_H
