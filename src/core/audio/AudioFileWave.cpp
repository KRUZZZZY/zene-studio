/*
 * AudioFileWave.cpp - audio-device which encodes wave-stream and writes it
 *                     into a WAVE-file. This is used for song-export.
 *
 * Copyright (c) 2004-2013 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "AudioFileWave.h"
#include "endian_handling.h"
#include "AudioEngine.h"
#include "SurroundExport.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <memory>


namespace lmms
{

namespace
{

/*! The bit depth the TPDF dither is drawn against, or 0 for "do not dither".
 *
 *  Two conditions, and both have to hold: the render asked for dither
 *  (OutputSettings::dither(), off by default), and the depth actually written
 *  is integer. 32-bit WAV is IEEE float - it has no quantisation step, so there
 *  is nothing to dither and adding noise to it would only degrade it.
 */
int ditherBitsFor(OutputSettings::BitDepth bitDepth, bool ditherEnabled)
{
	if (!ditherEnabled) { return 0; }
	switch (bitDepth)
	{
	case OutputSettings::BitDepth::Depth16Bit: return 16;
	case OutputSettings::BitDepth::Depth24Bit: return 24;
	case OutputSettings::BitDepth::Depth32Bit:
	default: return 0;
	}
}

/*! Round-to-nearest variant of AudioDevice::convertToS16, used for the dithered
 *  16-bit path only.
 *
 *  convertToS16 truncates toward zero. Truncation discards every dither offset
 *  smaller than one LSB, so at digital silence - or at any input below half an
 *  LSB - the TPDF noise is quantised straight back out again and the exported
 *  bytes carry no dither at all. Rounding to nearest instead keeps the offsets
 *  that push a sample across the decision threshold. Clip and scale are
 *  identical to convertToS16, and the byte order matches, so the only change is
 *  where a half-way value lands.
 */
int sampleRoundedToS16(float value)
{
	return static_cast<int>(std::lround(AudioEngine::clip(value) * OUTPUT_SAMPLE_MULTIPLIER));
}

void convertDitheredToS16(const SampleFrame* _ab,
						  const f_cnt_t _frames,
						  const ch_cnt_t _channels,
						  int_sample_t * _output_buffer,
						  const bool _convert_endian)
{
	for( f_cnt_t frame = 0; frame < _frames; ++frame )
	{
		for( ch_cnt_t chnl = 0; chnl < _channels; ++chnl )
		{
			auto temp = static_cast<int_sample_t>(sampleRoundedToS16(_ab[frame][chnl]));

			( _output_buffer + frame * _channels )[chnl] = _convert_endian ?
					( ( temp & 0x00ff ) << 8 | ( temp & 0xff00 ) >> 8 ) : temp;
		}
	}
}

} // namespace

AudioFileWave::AudioFileWave( OutputSettings const & outputSettings,
				const ch_cnt_t channels, bool & successful,
				const QString & file,
				AudioEngine* audioEngine ) :
	AudioFileDevice( outputSettings, channels, file, audioEngine ),
	m_sf( nullptr ),
	m_dither(),
	m_noiseShaper( channels )
{
	successful = outputFileOpened() && startEncoding();
}



AudioFileWave::~AudioFileWave()
{
	finishEncoding();
}




bool AudioFileWave::startEncoding()
{
	m_si.samplerate = sampleRate();
	m_si.channels = channels();
	m_si.frames = audioEngine()->framesPerPeriod();
	m_si.sections = 1;
	m_si.seekable = 0;

	// 0.5.0 F7: more than two channels (a surround export) is WAVE_FORMAT_EXTENSIBLE, which carries the
	// speakers' mask; libsndfile's SF_FORMAT_WAV would write a plain PCM header with no mask. A stereo
	// file is the SF_FORMAT_WAV it always was.
	m_si.format = channels() > DEFAULT_CHANNELS ? SF_FORMAT_WAVEX : SF_FORMAT_WAV;

	switch( getOutputSettings().getBitDepth() )
	{
	case OutputSettings::BitDepth::Depth32Bit:
		m_si.format |= SF_FORMAT_FLOAT;
		break;
	case OutputSettings::BitDepth::Depth24Bit:
		m_si.format |= SF_FORMAT_PCM_24;
		break;
	case OutputSettings::BitDepth::Depth16Bit:
	default:
		m_si.format |= SF_FORMAT_PCM_16;
		break;
	}

	// Use file handle to handle unicode file name on Windows
	m_sf = sf_open_fd( outputFileHandle(), SFM_WRITE, &m_si, false );

	if (!m_sf)
	{
		qWarning("Error: AudioFileWave::startEncoding: %s", sf_strerror(nullptr));
		return false;
	}
	if (!setChannelMap())
	{
		qWarning("Error: AudioFileWave::startEncoding: no channel map for %d channels", static_cast<int>(channels()));
		return false;
	}

	// Prevent fold overs when encountering clipped data
	sf_command(m_sf, SFC_SET_CLIPPING, nullptr, SF_TRUE);

	sf_set_string ( m_sf, SF_STR_SOFTWARE, "Zene Studio" );

	return true;
}

bool AudioFileWave::writeBuffer(const SampleFrame* _ab, const f_cnt_t _frames)
{
	OutputSettings::BitDepth bitDepth = getOutputSettings().getBitDepth();
	const int ditherBits = ditherBitsFor(bitDepth, getOutputSettings().dither());

	bool ok = true;

	if( bitDepth == OutputSettings::BitDepth::Depth32Bit || bitDepth == OutputSettings::BitDepth::Depth24Bit )
	{
		auto buf = new float[_frames * channels()];
		for( f_cnt_t frame = 0; frame < _frames; ++frame )
		{
			for( ch_cnt_t chnl = 0; chnl < channels(); ++chnl )
			{
				buf[frame * channels() + chnl] = _ab[frame][chnl];
			}
		}
		// The dither goes in BEFORE libsndfile quantises the floats to 24-bit
		// integer, which is the only order that removes the correlation between
		// the quantisation error and the signal. Off by default, so with no
		// dither request this call is absent and the bytes are what they were.
		if (ditherBits > 0)
		{
			m_dither.ditherInterleaved(buf, static_cast<std::size_t>(_frames) * channels(), ditherBits);
		}
		const sf_count_t written = sf_writef_float( m_sf, buf, _frames );
		m_framesWritten += written;
		if (written != static_cast<sf_count_t>(_frames))
		{
			m_writeError = true;
			ok = false;
		}
		delete[] buf;
	}
	else
	{
		auto buf = new int_sample_t[_frames * channels()];
		if (getOutputSettings().ditherMode() == DitherMode::NoiseShaped)
		{
			// Noise-shaped TPDF: the shaper requantises (same scale, clip and byte order
			// as convertToS16) and feeds each sample's error back into the next.
			m_noiseShaper.quantiseFrames(_ab, _frames, channels(), buf, !isLittleEndian());
		}
		else if (ditherBits > 0)
		{
			// The 16-bit path converts a whole SampleFrame array, so the dither
			// is applied to a staged copy and the conversion then reads the
			// dithered value - the same order as the 24-bit path above.
			auto staged = std::make_unique<SampleFrame[]>(_frames);
			std::copy_n(_ab, _frames, staged.get());
			m_dither.ditherFrames(staged.get(), _frames, ditherBits);
			convertDitheredToS16(staged.get(), _frames, channels(), buf, !isLittleEndian());
		}
		else
		{
			convertToS16(_ab, _frames, buf, !isLittleEndian());
		}

		const sf_count_t written = sf_writef_short( m_sf, buf, _frames );
		m_framesWritten += written;
		if (written != static_cast<sf_count_t>(_frames))
		{
			m_writeError = true;
			ok = false;
		}
		delete[] buf;
	}

	return ok;
}




bool AudioFileWave::setChannelMap()
{
	if (channels() <= DEFAULT_CHANNELS) { return true; }
	const surround::Format format = surroundexport::formatForChannels(channels(), surroundexport::masterFormat());
	if (format == surround::Format::Stereo) { return false; }
	// A discrete layout has no speaker mask. libsndfile refuses an all-INVALID map and writes a
	// count-based default mask for some counts (4/6/8), so setChannelMap lets it write and
	// writeZeroChannelMask() clears the field after the samples are flushed.
	if (surround::isDiscrete(format)) { m_zeroChannelMask = true; return true; }
	// Before any audio: libsndfile writes the mask into the header it has not written yet.
	auto map = surroundexport::sndfileChannelMap(format);
	return sf_command(m_sf, SFC_SET_CHANNEL_MAP_INFO, map.data(),
		static_cast<int>(channels() * sizeof(int))) == SF_TRUE;
}

/*! 0.5.0 F7 (MX-060): a surround export's interleaved frames. The same depths, the same dither and the
 *  same 16-bit quantisation as writeBuffer() - per sample instead of per SampleFrame - except that the
 *  16-bit samples go to sf_writef_short in the host's own byte order, which is what it takes. */
bool AudioFileWave::writeInterleaved(float* samples, const f_cnt_t frames)
{
	const auto count = static_cast<std::size_t>(frames) * channels();
	const OutputSettings::BitDepth bitDepth = getOutputSettings().getBitDepth();
	const int ditherBits = ditherBitsFor(bitDepth, getOutputSettings().dither());
	const bool shaped = bitDepth == OutputSettings::BitDepth::Depth16Bit
		&& getOutputSettings().ditherMode() == DitherMode::NoiseShaped;
	if (ditherBits > 0 && !shaped) { m_dither.ditherInterleaved(samples, count, ditherBits); }
	if (bitDepth == OutputSettings::BitDepth::Depth32Bit || bitDepth == OutputSettings::BitDepth::Depth24Bit)
	{
		const sf_count_t written = sf_writef_float(m_sf, samples, static_cast<sf_count_t>(frames));
		m_framesWritten += written;
		const bool ok = written == static_cast<sf_count_t>(frames);
		if (!ok) { m_writeError = true; }
		return ok;
	}
	m_interleaved16.resize(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		m_interleaved16[i] = shaped ? m_noiseShaper.quantise16(samples[i], static_cast<int>(i % channels()))
			: static_cast<int_sample_t>(AudioEngine::clip(samples[i]) * OUTPUT_SAMPLE_MULTIPLIER);
	}
	const sf_count_t written = sf_writef_short(m_sf, m_interleaved16.data(), static_cast<sf_count_t>(frames));
	m_framesWritten += written;
	const bool ok = written == static_cast<sf_count_t>(frames);
	if (!ok) { m_writeError = true; }
	return ok;
}




sf_count_t AudioFileWave::expectedDataBytes() const
{
	sf_count_t bytesPerSample = 2;
	switch (getOutputSettings().getBitDepth())
	{
	case OutputSettings::BitDepth::Depth24Bit: bytesPerSample = 3; break;
	case OutputSettings::BitDepth::Depth32Bit: bytesPerSample = 4; break;
	case OutputSettings::BitDepth::Depth16Bit:
	default: break;
	}
	return m_framesWritten * channels() * bytesPerSample;
}


namespace
{

//! Little-endian u32 at @a at, the field order every RIFF chunk uses.
quint32 riffU32(const QByteArray& bytes, int at)
{
	return static_cast<quint32>(static_cast<unsigned char>(bytes.at(at)))
		| (static_cast<quint32>(static_cast<unsigned char>(bytes.at(at + 1))) << 8)
		| (static_cast<quint32>(static_cast<unsigned char>(bytes.at(at + 2))) << 16)
		| (static_cast<quint32>(static_cast<unsigned char>(bytes.at(at + 3))) << 24);
}

} // namespace


bool AudioFileWave::headerHoldsEveryFrame(sf_count_t expectedData) const
{
	QFile file(outputFile());
	if (!file.open(QIODevice::ReadOnly)) { return false; }
	const QByteArray head = file.read(12);
	if (head.size() < 12 || head.left(4) != "RIFF" || head.mid(8, 4) != "WAVE") { return false; }
	const quint32 riffSize = riffU32(head, 4);

	// Walk the chunk list to the data chunk, the way a reader does. The RIFF size and the data
	// chunk's own size are exactly the two fields sf_close() rewrites in place; a partial rewrite
	// leaves either stale while the file's LENGTH is untouched, so the fields - not the size - are
	// the evidence. Bounded: a real WAV has a handful of chunks before data.
	quint32 dataOffset = 0;
	quint32 dataSize = 0;
	bool sawData = false;
	qint64 at = 12;
	for (int chunks = 0; chunks < 64; ++chunks)
	{
		if (!file.seek(at)) { break; }
		const QByteArray chunk = file.read(8);
		if (chunk.size() < 8) { break; }
		const quint32 size = riffU32(chunk, 4);
		if (chunk.left(4) == "data")
		{
			dataOffset = static_cast<quint32>(at);
			dataSize = size;
			sawData = true;
			break;
		}
		if (size == 0) { break; }  // a chunk list a reader would reject: nothing to prove
		at += 8 + static_cast<qint64>(size) + static_cast<qint64>(size & 1u);
	}
	if (!sawData) { return false; }

	// The data chunk's size must account for every frame the writes reported; a stale field is short.
	if (static_cast<sf_count_t>(dataSize) < expectedData) { return false; }
	// And the RIFF field must cover the data chunk header plus its payload.
	const quint64 riffNeeds = static_cast<quint64>(dataOffset) + 8u + static_cast<quint64>(expectedData);
	return static_cast<quint64>(riffSize) + 8u >= riffNeeds;
}


bool AudioFileWave::finishEncoding()
{
	// Already finalised (a render called this, the destructor calls it again): report the state
	// the file was left in, and touch nothing - the file, when it was broken, is already gone.
	if (!m_sf) { return !m_writeError; }

	const sf_count_t expectedData = expectedDataBytes();
	if (sf_close( m_sf ) != 0)
	{
		m_writeError = true;
	}
	m_sf = nullptr;
	if (m_zeroChannelMask && !writeZeroChannelMask())
	{
		// The speaker-mask patch is part of finalising a discrete WAVEX file: a patch that did not
		// land leaves a header a reader would take for a layout the export never wrote.
		m_writeError = true;
	}

	// sf_close() reports success even for a header it could not rewrite completely
	// (psf_fwrite() counts a partial write as a whole one), and such a partial rewrite does not
	// change the file's length, so the header's own fields - not its size - are the evidence.
	if (!headerHoldsEveryFrame(expectedData))
	{
		m_writeError = true;
	}

	if (m_writeError)
	{
		removeOutputFile();
		return false;
	}
	return true;
}

bool AudioFileWave::writeZeroChannelMask()
{
	QFile file(outputFile());
	if (!file.open(QIODevice::ReadWrite)) { return false; }
	const QByteArray header = file.read(4096);
	const int fmt = header.indexOf("fmt ");
	// fmt chunk data begins at fmt + 8; dwChannelMask is field 20 of WAVE_FORMAT_EXTENSIBLE's fmt data.
	if (fmt < 0 || fmt + 32 > header.size()) { return false; }
	if (!file.seek(fmt + 28)) { return false; }
	// QFile buffers, so write() alone is not evidence: a short write that QFile accepts into its
	// buffer only fails at flush(). Both results are the answer.
	const QByteArray zero(4, '\0');
	if (file.write(zero) != zero.size()) { return false; }
	return file.flush();
}

} // namespace lmms