/*
 * AudioFileMP3.h - Audio-device which encodes a wave stream into
 *                  an MP3 file. This is used for song export.
 *
 * Copyright (c) 2017 to present Michael Gregorius <michael.gregorius.git/at/arcor[dot]de>
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

#ifndef LMMS_AUDIO_FILE_MP3_H
#define LMMS_AUDIO_FILE_MP3_H

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_MP3LAME

#include "AudioFileDevice.h"

#include "lame/lame.h"

namespace lmms
{

class AudioFileMP3 : public AudioFileDevice
{
public:
	AudioFileMP3( OutputSettings const & outputSettings,
			const ch_cnt_t _channels,
			bool & successful,
			const QString & _file,
			AudioEngine* audioEngine );
	~AudioFileMP3() override;

	static AudioFileDevice * getInst( const QString & outputFilename,
					  OutputSettings const & outputSettings,
					  const ch_cnt_t channels,
					  AudioEngine* audioEngine,
					  bool & successful )
	{
		return new AudioFileMP3( outputSettings, channels, successful,
					 outputFilename, audioEngine );
	}

public:
	/*! Flushes the LAME tail into the file and reports whether the whole MP3 was written: false when
	 *  the tail did not reach it. The render calls this before it reads the result; the destructor
	 *  calls it again, which is then a no-op. */
	bool finishEncoding() override;

#ifdef LMMS_TESTING
	//! TEST SEAM: the next encoder created sees lame_init() return null, once.
	static void failNextLameInitForTest() { s_failNextLameInitForTest = true; }
	//! TEST SEAM: how many times lame_encode_flush() has been called in this process.
	static int flushCallsForTest() { return s_flushCallsForTest; }
#endif

protected:
	bool writeBuffer(const SampleFrame* /* _buf*/, const f_cnt_t /*_frames*/) override;

private:
	//! Flushes the LAME tail and records whether it reached the file. Idempotent.
	void flushRemainingBuffers();
	bool initEncoder();
	void tearDownEncoder();

private:
	lame_t m_lame = nullptr;
	//! True once the LAME tail has been flushed (finishEncoding(), or the destructor without it).
	bool m_flushed = false;
#ifdef LMMS_TESTING
	inline static bool s_failNextLameInitForTest = false;
	inline static int s_flushCallsForTest = 0;
#endif
};

} // namespace lmms

#endif // LMMS_HAVE_MP3LAME

#endif // LMMS_AUDIO_FILE_MP3_H
