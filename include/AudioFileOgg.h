/*
 * AudioFileOgg.h - Audio-device which encodes wave-stream and writes it
 *                  into an OGG-file. This is used for song-export.
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_AUDIO_FILE_OGG_H
#define LMMS_AUDIO_FILE_OGG_H

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_OGGVORBIS

#include <vorbis/codec.h>

#include "AudioFileDevice.h"

namespace lmms
{

class AudioFileOgg : public AudioFileDevice
{
public:
	AudioFileOgg( OutputSettings const & outputSettings,
			const ch_cnt_t _channels,
			bool & _success_ful,
			const QString & _file,
			AudioEngine* audioEngine );
	~AudioFileOgg() override;

	static AudioFileDevice * getInst( const QString & outputFilename,
					  OutputSettings const & outputSettings,
					  const ch_cnt_t channels,
					  AudioEngine* audioEngine,
					  bool & successful )
	{
		return new AudioFileOgg( outputSettings, channels, successful, outputFilename, audioEngine );
	}

public:
	/*! Flushes the tail of the stream and reports whether the whole OGG was written: false when a
	 *  page (the final one included) or the close failed. The render calls this before it reads the
	 *  result; the destructor calls it again, which is then a no-op. */
	bool finishEncoding() override;

#ifdef LMMS_TESTING
	/*! Test seam: the next page write stores only @a bytes and answers with that SHORT count, the
	 *  way a nearly-full disk answers a page that does not fit (a positive count, not -1). QFile
	 *  buffers pages under its 16 KiB buffer, so a real short page write cannot be provoked through
	 *  a file-size cap alone; this drives the comparison writePage() actually makes. -1 disables. */
	void setNextPageWriteShortForTest(int bytes) { m_nextPageWriteShort = bytes; }

	/*! Test seam for the construction path: the next page write on ANY OGG device stores only
	 *  @a bytes. A device only exists after its constructor wrote the header pages, so the header
	 *  case has to arm the failure before the object is built. -1 disables. */
	static void setFirstPageWriteShortForTest(int bytes) { s_firstPageWriteShort = bytes; }
#endif

private:
	bool writeBuffer(const SampleFrame* _ab, const f_cnt_t _frames) override;
	//! Stores the current page in full; false (and the device flagged) when part of it did not land.
	bool writePage();
	//! writeData() for a page, honouring the test seam; answers with the bytes actually stored.
	int writePageBytes(const void* data, int len);
#ifdef LMMS_TESTING
	//! One-shot: the next page write stores only this many bytes (-1: off). See the setter.
	int m_nextPageWriteShort = -1;
	//! One-shot across devices: armed before a device is constructed, consumed by its first page.
	static int s_firstPageWriteShort;
#endif
	//! True once finishEncoding() has flushed the tail, so the destructor cannot flush twice.
	bool m_finished = false;
	vorbis_info m_vi;
	vorbis_dsp_state m_vds;
	vorbis_comment m_vc;
	vorbis_block m_vb;
	ogg_stream_state m_oss;
	ogg_packet m_packet;
	ogg_page m_page;
	bool m_vorbisInfoInitialized{};
	bool m_vorbisDspInitialized{};
	bool m_vorbisCommentInitialized{};
	bool m_vorbisBlockInitialized{};
	bool m_oggStreamInitialized{};
};

} // namespace lmms

#endif // LMMS_HAVE_OGGVORBIS

#endif // LMMS_AUDIO_FILE_OGG_H
