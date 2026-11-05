/*
 * LibraryCellAudio.h - the audio a launcher cell's source file holds, saved into the library
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

#ifndef LMMS_LIBRARY_CELL_AUDIO_H
#define LMMS_LIBRARY_CELL_AUDIO_H

#include <QString>

#include "lmms_export.h"

namespace lmms::libraryaudio
{

/*! The decoded audio of one launcher cell's source file, written to the shared library.
 *
 *  WHAT IS SAVED. An audio launcher cell holds a source PATH, not a Clip: its audio is
 *  the file that path names. The entry is that file's OWN frames at its own rate, as a
 *  stereo 32-bit float WAV - the format ClipConsolidate writes and the browser reads.
 *  The cell's gain, transpose, detune, loop region and warp are playback settings of the
 *  cell and are not baked into the entry; inserting the entry in another cell plays the
 *  same frames (Q050-273.1).
 *
 *  BOUNDED BY DECODED FRAMES, NOT BY THE FILE'S SIZE. The previous draft refused a source
 *  file larger than 512 MiB and then called SampleDecoder::decode(), which allocates the
 *  whole decoded buffer and then a second full interleaved copy - a compressed 20 KB file
 *  can decode to gigabytes, so an input-size limit was no memory bound at all. Here the
 *  limit is the DECODED size: `MaxDecodedBytes` of stereo float PCM (frames x 2 x 4
 *  bytes, which is also the output WAV's payload). A libsndfile-readable source is
 *  streamed block by block, so the peak is a block plus libsndfile's own state; a source
 *  the built-in decoder reads through a fallback (Ogg Vorbis, DrumSynth) is measured
 *  against the same bound BEFORE it is decoded in full. A source past the bound is a typed
 *  refusal, never an allocation.
 *
 *  ATOMIC, LIKE EVERY OTHER LIBRARY WRITE. The target is a QSaveFile: libsndfile writes
 *  through its handle() and does not close it (SF_FALSE), and only a complete, closed WAV
 *  is committed over the destination. Any failure cancels the write, so a refused save
 *  leaves no partial file and never replaces the bytes already there.
 *
 *  PORTABILITY. QSaveFile::handle() is the C file descriptor libsndfile takes on every
 *  platform this tree builds (the same pattern as ControlCommandsSampleStereo.cpp and
 *  RetroAudioCapture.cpp, both built on MSVC, mingw and macOS); libsndfile closes the
 *  stream, Qt closes and renames the file. Nothing here is POSIX-only and no Qt-6-only API
 *  is used.
 *
 *  REAL TIME. Decoding, encoding and file I/O all run on the calling (control / model)
 *  thread. No audio-thread code path reaches this header.
 *
 *  TEST SEAMS (compiled only under LMMS_TESTING, read per call, no effect in a release
 *  build - the same style as DataFile's ZENE_TEST_SAVE_FAULT):
 *    ZENE_TEST_LIBRARY_SAVE_MAX_DECODED_BYTES  lowers MaxDecodedBytes, so a save's frame
 *                                              bound can be measured with a small file;
 *    ZENE_TEST_LIBRARY_SAVE_ALLOC_FAULT        makes the decode allocation fail, so the
 *                                              out-of-memory refusal is deterministic;
 *    ZENE_TEST_LIBRARY_SAVE_WRITE_FAULT        "short-write" (a block is written one frame
 *                                              short) or "before-commit" (the WAV is
 *                                              complete but the destination is not
 *                                              replaced), so the failed-write cleanup is
 *                                              deterministic on every platform.
 */
class LMMS_EXPORT CellAudio
{
public:
	//! The decoded-PCM bound of one save: 256 MiB of stereo float frames (about 25 minutes
	//! at 44.1 kHz, 8 bytes a frame). The output WAV's payload is the same number of bytes.
	static constexpr qint64 MaxDecodedBytes = 256LL * 1024 * 1024;

	//! The bound this process applies; MaxDecodedBytes unless the test seam lowers it.
	static auto decodedByteBudget() -> qint64;

	struct Result
	{
		bool ok = false;
		QString error;      //!< why it was refused, when !ok
		qint64 frames = 0;  //!< the saved WAV's frames (equal to the source's)
		int sampleRate = 0;
		qint64 bytes = 0;   //!< the file's size on disk
	};

	/*! Writes @a source's audio to @a target as a stereo float WAV, replacing @a target only
	 *  when the whole file was written. @a source is an existing absolute path. */
	static auto save(const QString& source, const QString& target) -> Result;
};

} // namespace lmms::libraryaudio

#endif // LMMS_LIBRARY_CELL_AUDIO_H
