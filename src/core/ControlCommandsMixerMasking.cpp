/*
 * ControlCommandsMixerMasking.cpp - which frequency ranges two mixer channels fight over, and a cut for each
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

/*! Q050-231.1 (R-mixing-7): mixer.masking_report. Read only - it writes no EQ and records no undo step.
 *
 *  What a channel "sounds like" here is the long-term spectrum of the audio clips on the tracks that play into
 *  it (unmuted tracks and clips; each clip's played window), shaped by the channel's own strip EQ response.
 *  The analysis is plain arithmetic over fixed frames (4096-point Hann, at most 64 frames per clip spread
 *  evenly over the window), so the same audio, EQ and priorities give the same report every time.
 *
 *  It runs in the command handler on the main thread, reading the clips' shared, immutable sample buffers;
 *  nothing here touches the audio thread or its buffers. */

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <QJsonArray>

#include "ControlCommandDef.h"
#include "ControlMixerSupport.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "ImportDetectionSpectrum.h"
#include "Mixer.h"
#include "MixerStrip.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int FrameSize = 4096;
constexpr int MaxFramesPerClip = 64;
constexpr int BandCount = 24;
constexpr double LowestHz = 40.0;
constexpr double HighestHz = 16000.0;
constexpr double FloorDb = -70.0;  //!< below this a band holds nothing worth cutting
constexpr int MaxChannels = 16;

//! Band edge @a i (0..BandCount): log-spaced from LowestHz to HighestHz.
double bandEdge(int i) { return LowestHz * std::pow(HighestHz / LowestHz, static_cast<double>(i) / BandCount); }

double bandCentre(int band) { return std::sqrt(bandEdge(band) * bandEdge(band + 1)); }

int bandOf(double hz)
{
	if (hz < LowestHz || hz >= HighestHz) { return -1; }
	return std::clamp(static_cast<int>(std::floor(std::log(hz / LowestHz) / std::log(HighestHz / LowestHz) * BandCount)), 0, BandCount - 1);
}

const QStringList& priorityNames()
{
	static const QStringList names{QStringLiteral("front"), QStringLiteral("middle"), QStringLiteral("back")};
	return names;
}

struct ChannelSpectrum
{
	MixerChannel* channel = nullptr;
	int priority = 1;  //!< index into priorityNames(): 0 front (protected most) .. 2 back (cut first)
	int clips = 0;
	QJsonArray skippedTracks;  //!< tracks that play into the channel but hold no audio clip to read (instruments)
	std::array<double, BandCount> power{};  //!< mean power per band, before the EQ
	std::array<double, BandCount> db{};  //!< the band levels after the strip EQ
	double peakDb = FloorDb;
};

//! Add one clip's frames to @a power (summed band power per frame) and return how many frames it read.
int addClip(const SampleClip& clip, const std::vector<float>& hann, std::array<double, BandCount>& power)
{
	const SampleWindow window = clip.sampleWindow();
	const Sample& sample = clip.sample();
	if (sample.data() == nullptr || window.sourceOut <= window.sourceIn || sample.sampleRate() <= 0) { return 0; }
	const auto length = static_cast<std::size_t>(window.sourceOut - window.sourceIn);
	if (length < static_cast<std::size_t>(FrameSize) || static_cast<std::size_t>(window.sourceOut) > sample.sampleSize()) { return 0; }
	const std::size_t span = length - FrameSize;
	const int frames = static_cast<int>(std::min<std::size_t>(MaxFramesPerClip, span / FrameSize + 1));
	std::vector<float> re(FrameSize);
	std::vector<float> im(FrameSize);
	const SampleFrame* data = sample.data() + window.sourceIn;
	const double binHz = static_cast<double>(sample.sampleRate()) / FrameSize;
	for (int f = 0; f < frames; ++f)
	{
		const std::size_t at = frames > 1 ? span * static_cast<std::size_t>(f) / static_cast<std::size_t>(frames - 1) : 0;
		for (int i = 0; i < FrameSize; ++i)
		{
			const SampleFrame& frame = data[at + static_cast<std::size_t>(i)];
			re[static_cast<std::size_t>(i)] = 0.5f * (frame.left() + frame.right()) * hann[static_cast<std::size_t>(i)];
			im[static_cast<std::size_t>(i)] = 0.f;
		}
		detection::detail::fftRadix2(re, im);
		for (int bin = 1; bin < FrameSize / 2; ++bin)
		{
			const int band = bandOf(bin * binHz);
			if (band < 0) { continue; }
			// 4 / N turns a Hann-windowed sine's peak bin back into its amplitude.
			const double amplitude = std::hypot(re[static_cast<std::size_t>(bin)], im[static_cast<std::size_t>(bin)]) * 4.0 / FrameSize;
			power[static_cast<std::size_t>(band)] += amplitude * amplitude;
		}
	}
	return frames;
}

//! Read every unmuted audio clip on the unmuted tracks that play into @a spectrum's channel.
void analyse(ChannelSpectrum& spectrum, const std::vector<float>& hann)
{
	int frames = 0;
	for (Track* track : Engine::getSong()->tracks())
	{
		const IntModel* model = track->mixerChannelModel();
		if (model == nullptr || model->value() != spectrum.channel->index() || track->isMuted()) { continue; }
		int before = spectrum.clips;
		for (Clip* clip : track->getClips())
		{
			const auto* audio = dynamic_cast<const SampleClip*>(clip);
			if (audio == nullptr || audio->isMuted()) { continue; }
			const int read = addClip(*audio, hann, spectrum.power);
			frames += read;
			if (read > 0) { ++spectrum.clips; }
		}
		if (spectrum.clips == before && dynamic_cast<SampleTrack*>(track) == nullptr) { spectrum.skippedTracks.append(trackIdOf(track)); }
	}
	const double rate = Engine::audioEngine()->outputSampleRate();
	for (int band = 0; band < BandCount; ++band)
	{
		const double mean = frames > 0 ? spectrum.power[static_cast<std::size_t>(band)] / frames : 0.0;
		const double level = mean > 0.0 ? 10.0 * std::log10(mean) + mixerstrip::eqResponseDb(*spectrum.channel, bandCentre(band), rate) : -120.0;
		spectrum.db[static_cast<std::size_t>(band)] = level;
		spectrum.peakDb = std::max(spectrum.peakDb, level);
	}
}

//! A band is part of a channel's sound when it is above the floor and within @a threshold of the channel's loudest band.
bool holds(const ChannelSpectrum& s, int band, double threshold)
{
	const double level = s.db[static_cast<std::size_t>(band)];
	return level > FloorDb && level >= s.peakDb - threshold;
}

double rangeLevel(const ChannelSpectrum& s, int first, int last)
{
	double level = -120.0;
	for (int band = first; band <= last; ++band) { level = std::max(level, s.db[static_cast<std::size_t>(band)]); }
	return level;
}

double rounded(double value, double step) { return std::round(value / step) * step; }

//! The exact mixer.channel_eq arguments that would make the cut on @a target's strip EQ.
QJsonObject stripEqProposal(const MixerChannel& target, double low, double high, double centre, double cut)
{
	QJsonObject eq{{QStringLiteral("channel"), channelIdOf(&target)}};
	const auto add = [&](const char* key, const FloatModel& model) {
		eq.insert(QLatin1String(key), rounded(std::clamp(static_cast<double>(model.value()) + cut, -18.0, 18.0), 0.1));
	};
	if (high <= 200.0) { add("low", target.m_eqLowModel); }  // a range under the 100 Hz shelf's reach
	else if (low >= 6000.0) { add("high", target.m_eqHighModel); }  // over the 8 kHz shelf's
	else
	{
		add("mid", target.m_eqMidModel);
		eq.insert(QStringLiteral("mid_freq"), rounded(std::clamp(centre, 100.0, 10000.0), 1.0));
	}
	return eq;
}

//! One clash: @a a and @a b both hold bands first..last. Cut the lower priority one; at equal priority the
//! quieter one in the range (the louder one owns it), and at equal level too the later channel.
QJsonObject clash(const ChannelSpectrum& a, const ChannelSpectrum& b, int first, int last)
{
	const double aDb = rangeLevel(a, first, last);
	const double bDb = rangeLevel(b, first, last);
	bool cutA = false;
	QString reason;
	if (a.priority != b.priority)
	{
		cutA = a.priority > b.priority;
		reason = QStringLiteral("lower priority");
	}
	else if (std::abs(aDb - bDb) > 0.5)
	{
		cutA = aDb < bDb;
		reason = QStringLiteral("equal priority: the quieter channel in this range");
	}
	else
	{
		cutA = a.channel->index() > b.channel->index();
		reason = QStringLiteral("equal priority and level: the later channel");
	}
	const ChannelSpectrum& target = cutA ? a : b;
	const ChannelSpectrum& keep = cutA ? b : a;
	const double low = bandEdge(first);
	const double high = bandEdge(last + 1);
	const double centre = std::sqrt(low * high);
	const double cut = -rounded(std::clamp(3.0 + 0.25 * (rangeLevel(keep, first, last) - rangeLevel(target, first, last)), 1.0, 6.0), 0.1);
	return {{QStringLiteral("a"), channelIdOf(a.channel)}, {QStringLiteral("b"), channelIdOf(b.channel)},
		{QStringLiteral("low_hz"), rounded(low, 0.1)}, {QStringLiteral("high_hz"), rounded(high, 0.1)},
		{QStringLiteral("centre_hz"), rounded(centre, 0.1)}, {QStringLiteral("a_db"), rounded(aDb, 0.01)},
		{QStringLiteral("b_db"), rounded(bDb, 0.01)}, {QStringLiteral("target"), channelIdOf(target.channel)},
		{QStringLiteral("protect"), channelIdOf(keep.channel)}, {QStringLiteral("reason"), reason},
		{QStringLiteral("cut_db"), cut}, {QStringLiteral("strip_eq"), stripEqProposal(*target.channel, low, high, centre, cut)}};
}

//! Every run of adjacent bands both channels hold, as one clash each.
void clashesOf(const ChannelSpectrum& a, const ChannelSpectrum& b, double threshold, QJsonArray& out)
{
	int first = -1;
	for (int band = 0; band <= BandCount; ++band)
	{
		const bool shared = band < BandCount && holds(a, band, threshold) && holds(b, band, threshold);
		if (shared && first < 0) { first = band; }
		if (!shared && first >= 0)
		{
			out.append(clash(a, b, first, band - 1));
			first = -1;
		}
	}
}

//! The selected channels, once each, in mixer order, with their priorities; false with @a error set when an
//! argument is wrong.
bool selection(const QJsonObject& args, std::vector<ChannelSpectrum>& out, ControlResult* error)
{
	const QJsonObject priorities = args.value(QStringLiteral("priorities")).toObject();
	for (const QJsonValue& value : args.value(QStringLiteral("channels")).toArray())
	{
		MixerChannel* channel = resolveMixerChannel(value.toString(), error);
		if (channel == nullptr) { return false; }
		const auto same = [channel](const ChannelSpectrum& s) { return s.channel == channel; };
		if (std::any_of(out.begin(), out.end(), same)) { continue; }
		ChannelSpectrum s;
		s.channel = channel;
		out.push_back(s);
	}
	for (auto it = priorities.begin(); it != priorities.end(); ++it)
	{
		const int rank = static_cast<int>(priorityNames().indexOf(it.value().toString()));
		const auto named = [&](const ChannelSpectrum& s) { return channelIdOf(s.channel) == it.key(); };
		const auto found = std::find_if(out.begin(), out.end(), named);
		if (rank < 0 || found == out.end())
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'priorities' maps each of 'channels' to front, "
				"middle or back; '%1' is not one, or is not among the channels").arg(it.key()));
			return false;
		}
		found->priority = rank;
	}
	if (out.size() < 2 || out.size() > MaxChannels)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'channels' names 2..16 different channels"));
		return false;
	}
	std::sort(out.begin(), out.end(), [](const ChannelSpectrum& l, const ChannelSpectrum& r) { return l.channel->index() < r.channel->index(); });
	return true;
}

QJsonObject channelJson(const ChannelSpectrum& s)
{
	return {{QStringLiteral("channel"), channelIdOf(s.channel)}, {QStringLiteral("name"), s.channel->m_name},
		{QStringLiteral("priority"), priorityNames().at(s.priority)}, {QStringLiteral("clips"), s.clips},
		{QStringLiteral("peak_db"), rounded(s.peakDb, 0.01)}, {QStringLiteral("unanalysed_tracks"), s.skippedTracks}};
}

ControlResult maskingReport(const QJsonObject& args)
{
	std::vector<ChannelSpectrum> spectra;
	ControlResult error;
	if (!selection(args, spectra, &error)) { return error; }
	const double threshold = std::clamp(args.value(QStringLiteral("threshold_db")).toDouble(12.0), 3.0, 40.0);
	const std::vector<float> hann = detection::detail::hannWindow(FrameSize);
	for (ChannelSpectrum& s : spectra) { analyse(s, hann); }
	QJsonArray clashes;
	for (std::size_t i = 0; i < spectra.size(); ++i)
	{
		for (std::size_t j = i + 1; j < spectra.size(); ++j) { clashesOf(spectra[i], spectra[j], threshold, clashes); }
	}
	QJsonArray channels;
	for (const ChannelSpectrum& s : spectra) { channels.append(channelJson(s)); }
	return ControlResult::success({{QStringLiteral("channels"), channels}, {QStringLiteral("clashes"), clashes},
		{QStringLiteral("threshold_db"), threshold}, {QStringLiteral("low_hz"), LowestHz}, {QStringLiteral("high_hz"), HighestHz},
		{QStringLiteral("bands"), BandCount}});
}

} // namespace

void registerMixerMaskingCommands(ControlRegistry& registry)
{
	defineCommand(registry, QStringLiteral("mixer.masking_report"), QStringLiteral("Which frequency ranges the selected mixer "
		"'channels' (2..16) fight over, from their audio: each channel's long-term spectrum (the unmuted audio clips on the "
		"tracks playing into it, through its strip EQ; instrument tracks are listed as unanalysed) in 24 log bands from 40 Hz "
		"to 16 kHz. A band counts for a channel within 'threshold_db' (3..40, 12) of its loudest band and above -70 dB. Each "
		"run of bands two channels share is a clash: the pair 'a'/'b', the range, and a proposed cut ('cut_db', -1..-6) on "
		"'target' - the lower of the 'priorities' (channel id to front, middle or back; middle unless set), at equal "
		"priority the quieter channel there - with the mixer.channel_eq arguments that would make it ('strip_eq'). The same "
		"audio, EQ and priorities give the same report. Read only: it changes no EQ and records no undo step."),
		{{QStringLiteral("channels"), arrayProperty()}, {QStringLiteral("priorities"), objectProperty()},
			{QStringLiteral("threshold_db"), numberProperty()}},
		{QStringLiteral("channels")}, false, maskingReport);
}

} // namespace lmms
