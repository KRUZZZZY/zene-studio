/*
 * VideoAudioExtract.h - a video's sound decoded to a stereo WAV through FFmpeg (libswresample)
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

#ifndef LMMS_VIDEO_AUDIO_EXTRACT_H
#define LMMS_VIDEO_AUDIO_EXTRACT_H

#include <atomic>

#include <QDir>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

//! What an extraction wrote.
struct VideoAudio
{
	QString path;
	int sampleRate = 0;
	long long frames = 0;
};

/*! 0.5.0 theme 24: how a background extraction reports and is stopped - written by the decoding thread (progress)
 *  and by the main thread (cancel), so both are atomics and neither side waits on the other. */
struct VideoExtractControl
{
	std::atomic<float> progress{-1.0f};  //!< 0..1 of the video's length decoded, or -1 while its length is unknown
	std::atomic<bool> cancel{false};     //!< stop at the next decoded frame; discard partial output and preserve the destination
};

/*! 0.5.0 ED-054 (video.extract_audio): decode @a video's best audio stream, convert it with libswresample to
 *  32-bit float stereo at the stream's own rate (mono is spread to both sides, more channels are mixed down), and
 *  write it to @a wav. False, with @a error, when the video has no audio, the file cannot be written, nothing
 *  decodes, or the build has no FFmpeg; a failed or cancelled run leaves @a wav unchanged, and success replaces it.
 *  Runs on the calling thread. */
LMMS_EXPORT bool extractVideoAudio(const QString& video, const QString& wav, VideoAudio* out, QString* error);
//! The same, reporting its progress to @a control and stopping when it asks (a cancelled run fails, leaving @a wav unchanged).
LMMS_EXPORT bool extractVideoAudio(const QString& video, const QString& wav, VideoAudio* out, QString* error,
	VideoExtractControl* control);

/*! The default WAV for an extraction: "<stem>.wav" in @a dir, then "<stem>-2.wav" up to "<stem>-<maxNames>.wav", the first
 *  name no file takes. Empty when all of them are taken; the caller refuses rather than reuse one. */
LMMS_EXPORT QString firstFreeAudioPath(const QDir& dir, const QString& stem, int maxNames);

} // namespace lmms

#endif // LMMS_VIDEO_AUDIO_EXTRACT_H
