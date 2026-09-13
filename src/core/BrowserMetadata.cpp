/*
 * BrowserMetadata.cpp - what an audio file can be probed for.
 *
 * The probe is libsndfile's header parse plus its embedded-tag read: no
 * decoding of the audio, no database of our own, and no invented field. The
 * library the engine already decodes with is the authority here - if it cannot
 * read the file, the probe says so with the library's own message rather than
 * guessing from the extension.
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
 */

#include "BrowserCatalog.h"

#include <QFile>

#include <sndfile.h>

namespace lmms
{

namespace
{

struct NameRow
{
	int value;
	const char* name;
};

//! libsndfile's major formats. A table, not a switch: the complexity ratchet
//! counts every `case` as a branch, and a 15-case switch is a 16-CCN function.
//!
//! The last four entries are behind #ifdef because their constants arrived
//! AFTER the 1.0.18 this project's CMakeLists.txt accepts: MP3/MPEG and RF64
//! are 1.1.0, Opus is 1.0.29. This box has 1.2.2 and CI's ubuntu-22.04 has
//! 1.0.31, so an unguarded table compiles here and fails there - the exact
//! "compiles locally, dies in CI" class the release prompt warns about.
const NameRow kMajorFormats[] = {
	{SF_FORMAT_WAV, "wav"},
	{SF_FORMAT_WAVEX, "wavex"},
	{SF_FORMAT_AIFF, "aiff"},
	{SF_FORMAT_AU, "au"},
	{SF_FORMAT_CAF, "caf"},
	{SF_FORMAT_FLAC, "flac"},
	{SF_FORMAT_OGG, "ogg"},
	{SF_FORMAT_RAW, "raw"},
#ifdef SF_FORMAT_MPEG
	{SF_FORMAT_MPEG, "mp3"},
#endif
#ifdef SF_FORMAT_RF64
	{SF_FORMAT_RF64, "rf64"},
#endif
};

//! The encodings a browser user will actually meet.
const NameRow kEncodings[] = {
	{SF_FORMAT_PCM_S8, "pcm_s8"},
	{SF_FORMAT_PCM_16, "pcm_16"},
	{SF_FORMAT_PCM_24, "pcm_24"},
	{SF_FORMAT_PCM_32, "pcm_32"},
	{SF_FORMAT_PCM_U8, "pcm_u8"},
	{SF_FORMAT_FLOAT, "float"},
	{SF_FORMAT_DOUBLE, "double"},
	{SF_FORMAT_ULAW, "ulaw"},
	{SF_FORMAT_ALAW, "alaw"},
	{SF_FORMAT_VORBIS, "vorbis"},
#ifdef SF_FORMAT_OPUS
	{SF_FORMAT_OPUS, "opus"},
#endif
#ifdef SF_FORMAT_MPEG_LAYER_III
	{SF_FORMAT_MPEG_LAYER_III, "mpeg_layer_3"},
#endif
};

QString nameOf(const NameRow* rows, int count, int value)
{
	for (int i = 0; i < count; ++i)
	{
		if (rows[i].value == value) { return QString::fromLatin1(rows[i].name); }
	}
	return QString();
}

//! An embedded string tag, or an empty string when the file carries none.
//! sf_get_string answers NULL for "no such tag", which must not become a
//! QString built from a null pointer.
QString embeddedTag(SNDFILE* file, int which)
{
	const char* value = sf_get_string(file, which);
	return value != nullptr ? QString::fromUtf8(value).trimmed() : QString();
}

QString subformatName(int format)
{
	return nameOf(kEncodings, static_cast<int>(sizeof(kEncodings) / sizeof(kEncodings[0])),
		format & SF_FORMAT_SUBMASK);
}

QString majorFormatName(int format)
{
	return nameOf(kMajorFormats, static_cast<int>(sizeof(kMajorFormats) / sizeof(kMajorFormats[0])),
		format & SF_FORMAT_TYPEMASK);
}

//! The text fields the probe fills, in the order searchableText() reports them.
QStringList metadataTexts(const BrowserMetadata& metadata)
{
	return QStringList{metadata.title, metadata.artist, metadata.album, metadata.comment,
		metadata.genre, metadata.date, metadata.software, metadata.copyright, metadata.format,
		metadata.encoding, QString::number(metadata.sampleRate), QString::number(metadata.channels),
		QString::number(metadata.frames), QString::number(metadata.durationSeconds, 'f', 3)};
}

} // namespace

QStringList BrowserMetadata::searchableText() const
{
	QStringList out;
	if (!valid) { return out; }
	for (const QString& text : metadataTexts(*this))
	{
		if (!text.isEmpty()) { out.append(text.toLower()); }
	}
	return out;
}

BrowserMetadata probeAudioMetadata(const QString& path, QString* error)
{
	BrowserMetadata out;
	SF_INFO info{};
	const QByteArray encoded = QFile::encodeName(path);
	SNDFILE* file = sf_open(encoded.constData(), SFM_READ, &info);
	if (file == nullptr)
	{
		// The library's own message: "File contains data in an unknown format",
		// "System error", ... A caller must not have to guess which it was.
		out.error = QString::fromUtf8(sf_strerror(nullptr));
		if (error != nullptr) { *error = out.error; }
		return out;
	}

	out.valid = true;
	out.sampleRate = info.samplerate;
	out.channels = info.channels;
	out.frames = static_cast<qint64>(info.frames);
	out.durationSeconds = info.samplerate > 0
		? static_cast<double>(info.frames) / static_cast<double>(info.samplerate) : 0.0;
	out.format = majorFormatName(info.format);
	out.encoding = subformatName(info.format);

	out.title = embeddedTag(file, SF_STR_TITLE);
	out.artist = embeddedTag(file, SF_STR_ARTIST);
	out.album = embeddedTag(file, SF_STR_ALBUM);
	out.comment = embeddedTag(file, SF_STR_COMMENT);
	out.genre = embeddedTag(file, SF_STR_GENRE);
	out.date = embeddedTag(file, SF_STR_DATE);
	out.software = embeddedTag(file, SF_STR_SOFTWARE);
	out.copyright = embeddedTag(file, SF_STR_COPYRIGHT);

	sf_close(file);
	return out;
}

} // namespace lmms
