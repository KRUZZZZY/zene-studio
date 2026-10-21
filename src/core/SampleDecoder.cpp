/*
 * SampleDecoder.cpp - Decodes audio files in various formats
 *
 * Copyright (c) 2023 saker <sakertooth@gmail.com>
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

#include "SampleDecoder.h"

#include <QFile>
#include <QString>
#include <algorithm>
#include <cmath>
#include <memory>
#include <sndfile.h>
#ifdef LMMS_TESTING
#include <atomic>
#endif

#ifdef LMMS_HAVE_OGGVORBIS
#include <vorbis/vorbisfile.h>
#endif

#include "AudioEngine.h"
#include "DrumSynth.h"
#include "Engine.h"
#include "LmmsTypes.h"
#include "SampleDecoderFrameLoop.h"

namespace lmms {

namespace {

#ifdef LMMS_TESTING
std::atomic<std::uint64_t> s_fullDecodeCallsForTesting{0};
std::atomic<std::uint64_t> s_limitedDecodeCallsForTesting{0};
std::atomic<bool> s_failNextOggReadForTesting{false};
std::atomic<int> s_oggCloseCallsForTesting{0};
#endif

using Decoder = std::optional<SampleDecoder::Result> (*)(const QString&);

auto decodeSampleSF(const QString& audioFile) -> std::optional<SampleDecoder::Result>;
auto decodeSampleDS(const QString& audioFile) -> std::optional<SampleDecoder::Result>;
#ifdef LMMS_HAVE_OGGVORBIS
auto decodeSampleOggVorbis(const QString& audioFile) -> std::optional<SampleDecoder::Result>;
#endif

static constexpr std::array<Decoder, 3> decoders = {&decodeSampleSF,
#ifdef LMMS_HAVE_OGGVORBIS
	&decodeSampleOggVorbis,
#endif
	&decodeSampleDS};

auto decodeSampleSF(const QString& audioFile) -> std::optional<SampleDecoder::Result>
{
	SNDFILE* sndFile = nullptr;
	auto sfInfo = SF_INFO{};

	// TODO: Remove use of QFile
	auto file = QFile{audioFile};
	if (!file.open(QIODevice::ReadOnly)) { return std::nullopt; }

	sndFile = sf_open_fd(file.handle(), SFM_READ, &sfInfo, false);
	if (sf_error(sndFile) != 0) { return std::nullopt; }

	auto buf = std::vector<sample_t>(sfInfo.channels * sfInfo.frames);
	sf_read_float(sndFile, buf.data(), buf.size());

	sf_close(sndFile);
	file.close();

	auto result = std::vector<SampleFrame>(sfInfo.frames);
	sample_decoder_detail::forEachSampleFrame(result.size(), [&](std::size_t i) {
		if (sfInfo.channels == 1)
		{
			// Upmix from mono to stereo
			result[i] = {buf[i], buf[i]};
		}
		else if (sfInfo.channels > 1)
		{
			// TODO: Add support for higher number of channels (i.e., 5.1 channel systems)
			// The current behavior assumes stereo in all cases excluding mono.
			// This may not be the expected behavior, given some audio files with a higher number of channels.
			const auto channelOffset = i * static_cast<std::size_t>(sfInfo.channels);
			result[i] = {buf[channelOffset], buf[channelOffset + std::size_t{1}]};
		}
		return true;
	});

	return SampleDecoder::Result{std::move(result), static_cast<int>(sfInfo.samplerate)};
}

auto decodeSampleDS(const QString& audioFile) -> std::optional<SampleDecoder::Result>
{
	// Populated by DrumSynth::GetDSFileSamples
	int_sample_t* dataPtr = nullptr;

	// A DS file is SYNTHESISED, and it is synthesised at the audio engine's own
	// rate, so with no engine there is nothing to synthesise at: refuse rather
	// than dereference a null engine. This is reachable in a process that never
	// brought the audio subsystem up (a headless test, a tool) for ANY file the
	// decoders before this one cannot read, and it was a SIGSEGV until the import
	// detector's refusal path found it (ImportDetectionTest, 2026-09-15).
	AudioEngine* engine = Engine::audioEngine();
	if (engine == nullptr) { return std::nullopt; }

	auto ds = DrumSynth{};
	const auto engineRate = engine->outputSampleRate();
	const auto frames = ds.GetDSFileSamples(audioFile, dataPtr, DEFAULT_CHANNELS, engineRate);
	const auto data = std::unique_ptr<int_sample_t[]>{dataPtr}; // NOLINT, we have to use a C-style array here

	if (frames <= 0 || !data) { return std::nullopt; }

	auto result = std::vector<SampleFrame>(frames);
	src_short_to_float_array(data.get(), &result[0][0], frames * DEFAULT_CHANNELS);

	return SampleDecoder::Result{std::move(result), static_cast<int>(engineRate)};
}

#ifdef LMMS_HAVE_OGGVORBIS
auto decodeSampleOggVorbis(const QString& audioFile) -> std::optional<SampleDecoder::Result>
{
	static auto s_read = [](void* buffer, size_t size, size_t count, void* stream) -> size_t {
		auto file = static_cast<QFile*>(stream);
		return file->read(static_cast<char*>(buffer), size * count);
	};

	static auto s_seek = [](void* stream, ogg_int64_t offset, int whence) -> int {
		auto file = static_cast<QFile*>(stream);
		if (whence == SEEK_SET) { file->seek(offset); }
		else if (whence == SEEK_CUR) { file->seek(file->pos() + offset); }
		else if (whence == SEEK_END) { file->seek(file->size() + offset); }
		else { return -1; }
		return 0;
	};

	static auto s_close = [](void* stream) -> int {
		auto file = static_cast<QFile*>(stream);
#ifdef LMMS_TESTING
		// libvorbisfile calls this callback exactly once per released decoder state (ov_clear),
		// so the count is the number of decoder states that were released.
		s_oggCloseCallsForTesting.fetch_add(1, std::memory_order_relaxed);
#endif
		file->close();
		return 0;
	};

	static auto s_tell = [](void* stream) -> long {
		auto file = static_cast<QFile*>(stream);
		return file->pos();
	};

	static ov_callbacks s_callbacks = {s_read, s_seek, s_close, s_tell};

	// TODO: Remove use of QFile
	auto file = QFile{audioFile};
	if (!file.open(QIODevice::ReadOnly)) { return std::nullopt; }

	auto vorbisFile = OggVorbis_File{};
	if (ov_open_callbacks(&file, &vorbisFile, nullptr, 0, s_callbacks) < 0) { return std::nullopt; }

	const auto vorbisInfo = ov_info(&vorbisFile, -1);
	if (vorbisInfo == nullptr) { return std::nullopt; }

	const auto numChannels = vorbisInfo->channels;
	const auto sampleRate = vorbisInfo->rate;
	const auto numSamples = ov_pcm_total(&vorbisFile, -1);
	if (numSamples < 0) { return std::nullopt; }

	auto buffer = std::vector<float>(numSamples);
	auto output = static_cast<float**>(nullptr);

	auto totalSamplesRead = 0;
	while (true)
	{
		auto samplesRead = ov_read_float(&vorbisFile, &output, numSamples, 0);
#ifdef LMMS_TESTING
		// A read error is what a corrupt file produces; the seam drives that path deterministically.
		if (s_failNextOggReadForTesting.exchange(false)) { samplesRead = -1; }
#endif

		if (samplesRead < 0) { return std::nullopt; }
		else if (samplesRead == 0) { break; }

		std::copy_n(*output, samplesRead, buffer.begin() + totalSamplesRead);
		totalSamplesRead += samplesRead;
	}

	auto result = std::vector<SampleFrame>(totalSamplesRead / numChannels);
	for (auto i = std::size_t{0}; i < result.size(); ++i)
	{
		if (numChannels == 1) { result[i] = {buffer[i], buffer[i]}; }
		else if (numChannels > 1) { result[i] = {buffer[i * numChannels], buffer[i * numChannels + 1]}; }
	}

	ov_clear(&vorbisFile);
	return SampleDecoder::Result{std::move(result), static_cast<int>(sampleRate)};
}
#endif // LMMS_HAVE_OGGVORBIS

// ---- Bounded decoding (Q050-048): memory and decode time follow the window, not the file. ----

constexpr sf_count_t SkipChunkFrames = 1 << 16;

auto boundedSF(const QString& audioFile, double maxSeconds) -> std::optional<SampleDecoder::BoundedResult>
{
	auto sfInfo = SF_INFO{};
	auto file = QFile{audioFile};
	if (!file.open(QIODevice::ReadOnly)) { return std::nullopt; }

	SNDFILE* sndFile = sf_open_fd(file.handle(), SFM_READ, &sfInfo, false);
	if (sf_error(sndFile) != 0) { return std::nullopt; }

	const int channels = sfInfo.channels;
	const auto maxFrames = static_cast<std::int64_t>(std::ceil(maxSeconds * sfInfo.samplerate));
	const std::int64_t wanted = sfInfo.frames > 0 ? std::min<std::int64_t>(sfInfo.frames, maxFrames) : maxFrames;
	auto buf = std::vector<sample_t>(static_cast<std::size_t>(std::max(channels, 1)) * static_cast<std::size_t>(wanted));
	const sf_count_t got = channels > 0 ? sf_readf_float(sndFile, buf.data(), wanted) : 0;

	std::int64_t total = got;
	if (sfInfo.frames > 0) { total = sfInfo.frames; }
	else if (got == wanted && channels > 0)
	{
		// No length in the header (streamed/raw): count the rest through a small scratch chunk.
		auto scratch = std::vector<sample_t>(static_cast<std::size_t>(channels) * SkipChunkFrames);
		for (sf_count_t n = 0; (n = sf_readf_float(sndFile, scratch.data(), SkipChunkFrames)) > 0;) { total += n; }
	}
	sf_close(sndFile);
	file.close();

	auto result = std::vector<SampleFrame>(static_cast<std::size_t>(got));
	for (std::size_t i = 0; i < result.size(); ++i)
	{
		if (channels == 1) { result[i] = {buf[i], buf[i]}; }
		else if (channels > 1) { result[i] = {buf[i * channels], buf[i * channels + 1]}; }
	}
	return SampleDecoder::BoundedResult{{std::move(result), static_cast<int>(sfInfo.samplerate)}, total};
}

#ifdef LMMS_HAVE_OGGVORBIS
auto boundedOggVorbis(const QString& audioFile, double maxSeconds) -> std::optional<SampleDecoder::BoundedResult>
{
	static auto s_read = [](void* buffer, size_t size, size_t count, void* stream) -> size_t {
		return static_cast<QFile*>(stream)->read(static_cast<char*>(buffer), size * count);
	};
	static auto s_seek = [](void* stream, ogg_int64_t offset, int whence) -> int {
		auto file = static_cast<QFile*>(stream);
		if (whence == SEEK_SET) { file->seek(offset); }
		else if (whence == SEEK_CUR) { file->seek(file->pos() + offset); }
		else if (whence == SEEK_END) { file->seek(file->size() + offset); }
		else { return -1; }
		return 0;
	};
	static auto s_close = [](void* stream) -> int {
		static_cast<QFile*>(stream)->close();
		return 0;
	};
	static auto s_tell = [](void* stream) -> long { return static_cast<QFile*>(stream)->pos(); };
	static ov_callbacks s_callbacks = {s_read, s_seek, s_close, s_tell};

	auto file = QFile{audioFile};
	if (!file.open(QIODevice::ReadOnly)) { return std::nullopt; }

	auto vorbisFile = OggVorbis_File{};
	if (ov_open_callbacks(&file, &vorbisFile, nullptr, 0, s_callbacks) < 0) { return std::nullopt; }

	const auto vorbisInfo = ov_info(&vorbisFile, -1);
	if (vorbisInfo == nullptr)
	{
		ov_clear(&vorbisFile);
		return std::nullopt;
	}
	const int channels = vorbisInfo->channels;
	const int rate = static_cast<int>(vorbisInfo->rate);
	// The Ogg container carries the length (the last page's granule position); ov_pcm_total reads it.
	const ogg_int64_t total = ov_pcm_total(&vorbisFile, -1);
	if (total < 0 || channels < 1)
	{
		ov_clear(&vorbisFile);
		return std::nullopt;
	}

	const auto maxFrames = static_cast<std::int64_t>(std::ceil(maxSeconds * rate));
	const std::int64_t wanted = std::min<std::int64_t>(total, maxFrames);
	auto result = std::vector<SampleFrame>();
	result.reserve(static_cast<std::size_t>(wanted));
	while (static_cast<std::int64_t>(result.size()) < wanted)
	{
		float** output = nullptr;
		const long want = static_cast<long>(std::min<std::int64_t>(wanted - static_cast<std::int64_t>(result.size()), 4096));
		const long read = ov_read_float(&vorbisFile, &output, static_cast<int>(want), nullptr);
		if (read < 0)
		{
			ov_clear(&vorbisFile);
			return std::nullopt;
		}
		if (read == 0) { break; }
		for (long i = 0; i < read; ++i)
		{
			const float left = output[0][i];
			result.push_back(SampleFrame{left, channels > 1 ? output[1][i] : left});
		}
	}
	ov_clear(&vorbisFile);
	return SampleDecoder::BoundedResult{{std::move(result), rate}, static_cast<std::int64_t>(total)};
}
#endif // LMMS_HAVE_OGGVORBIS
} // namespace

auto SampleDecoder::supportedAudioTypes() -> const std::vector<AudioType>&
{
	static const auto s_audioTypes = [] {
		auto types = std::vector<AudioType>();

		// Add DrumSynth by default since that support comes from us
		types.push_back(AudioType{"DrumSynth", "ds"});

		auto sfFormatInfo = SF_FORMAT_INFO{};
		auto simpleTypeCount = 0;
		sf_command(nullptr, SFC_GET_SIMPLE_FORMAT_COUNT, &simpleTypeCount, sizeof(int));

		// TODO: Ideally, this code should be iterating over the major formats, but some important extensions such as
		// *.ogg are not included. This is planned for future versions of sndfile.
		for (int simple = 0; simple < simpleTypeCount; ++simple)
		{
			sfFormatInfo.format = simple;
			sf_command(nullptr, SFC_GET_SIMPLE_FORMAT, &sfFormatInfo, sizeof(sfFormatInfo));

			auto it = std::find_if(types.begin(), types.end(),
				[&](const AudioType& type) { return sfFormatInfo.extension == type.extension; });
			if (it != types.end()) { continue; }

			auto name = std::string{sfFormatInfo.extension};
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return std::toupper(ch); });

			types.push_back(AudioType{std::move(name), sfFormatInfo.extension});
		}

		std::sort(types.begin(), types.end(), [&](const AudioType& a, const AudioType& b) { return a.name < b.name; });
		return types;
	}();
	return s_audioTypes;
}

auto SampleDecoder::decode(const QString& audioFile) -> std::optional<Result>
{
#ifdef LMMS_TESTING
	s_fullDecodeCallsForTesting.fetch_add(1, std::memory_order_relaxed);
#endif
	auto result = std::optional<Result>{};
	for (const auto& decoder : decoders)
	{
		result = decoder(audioFile);
		if (result) { break; }
	}

	return result;
}

auto SampleDecoder::decodeLimited(const QString& audioFile, double maxSeconds) -> std::optional<BoundedResult>
{
#ifdef LMMS_TESTING
	s_limitedDecodeCallsForTesting.fetch_add(1, std::memory_order_relaxed);
#endif
	if (!(maxSeconds > 0.0)) { return std::nullopt; }
	if (auto r = boundedSF(audioFile, maxSeconds)) { return r; }
#ifdef LMMS_HAVE_OGGVORBIS
	if (auto r = boundedOggVorbis(audioFile, maxSeconds)) { return r; }
#endif
	// DrumSynth renders the whole file; cut it to the window.
	auto whole = decodeSampleDS(audioFile);
	if (!whole) { return std::nullopt; }
	const auto total = static_cast<std::int64_t>(whole->data.size());
	const auto maxFrames = static_cast<std::int64_t>(std::ceil(maxSeconds * whole->sampleRate));
	if (total > maxFrames)
	{
		whole->data.resize(static_cast<std::size_t>(maxFrames));
		whole->data.shrink_to_fit();
	}
	return BoundedResult{std::move(*whole), total};
}

#ifdef LMMS_TESTING
void SampleDecoder::resetDecodeCallCountsForTesting()
{
	s_fullDecodeCallsForTesting.store(0, std::memory_order_relaxed);
	s_limitedDecodeCallsForTesting.store(0, std::memory_order_relaxed);
}

auto SampleDecoder::decodeCallCountsForTesting() -> DecodeCallCountsForTesting
{
	return {s_fullDecodeCallsForTesting.load(std::memory_order_relaxed), s_limitedDecodeCallsForTesting.load(std::memory_order_relaxed)};
}

auto SampleDecoder::decodeOggVorbisOnlyForTesting(const QString& audioFile) -> std::optional<Result>
{
#ifdef LMMS_HAVE_OGGVORBIS
	return decodeSampleOggVorbis(audioFile);
#else
	return std::nullopt;
#endif
}

void SampleDecoder::failNextOggReadForTesting()
{
	s_failNextOggReadForTesting.store(true, std::memory_order_relaxed);
}

void SampleDecoder::resetOggCloseCountForTesting()
{
	s_oggCloseCallsForTesting.store(0, std::memory_order_relaxed);
}

auto SampleDecoder::oggCloseCountForTesting() -> int
{
	return s_oggCloseCallsForTesting.load(std::memory_order_relaxed);
}
#endif

} // namespace lmms
