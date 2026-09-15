/*
 * ControlStemWav.cpp - the one WAV writer of the stem.* result verb.
 *
 * Split out of ControlStemSupport.cpp because the file-length ratchet reads a
 * file as a unit, the same reason the A16 table and this fork's other support
 * files are split: what lives here is one self-contained job - turn a
 * SampleBuffer into the float32 RIFF/WAVE a separation result is written as, and
 * hash a file - and nothing else in the tree writes a stem.
 *
 * The format is the reference CLI's own (tools/stem_split_cli.py, write_wav:
 * audio_format 3, 32-bit, stereo), so a stem this surface produced and a stem the
 * CLI produced are the same file format; the header is fixed at 44 bytes and the
 * writer asserts the frame layout it depends on.
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

#include "ControlStemSupport.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>

#include "SampleBuffer.h"
#include "SampleFrame.h"

namespace lmms
{
namespace control
{

namespace
{

//! The one WAV header this surface writes: RIFF/WAVE, IEEE float, stereo. 44
//! bytes, the same header the reference CLI writes
//! (tools/stem_split_cli.py, write_wav: audio_format 3, 32-bit), so a stem this
//! surface produced and a stem the CLI produced are the same file format.
constexpr int kWavHeaderBytes = 44;
//! channel count and bits per sample of that header.
constexpr int kWavChannels = 2;
constexpr int kWavBits = 32;

} // namespace


/*! Writes \a buffer as float32 RIFF/WAVE. The buffer IS the interleaved stereo
 *  stream: SampleFrame is two sample_t (float) with no padding
 *  (include/SampleFrame.h:50-53, include/LmmsTypes.h:39), which the static
 *  assertion below pins so a future frame layout cannot silently corrupt a stem.
 */
bool stemWriteWavFile(const QString& path, const SampleBuffer& buffer, int sampleRate, QString* error)
{
	static_assert(sizeof(SampleFrame) == 2 * sizeof(float),
		"a stem is written as the buffer's raw interleaved stereo float32 stream");

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		*error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
		return false;
	}
	const quint64 frames = static_cast<quint64>(buffer.size());
	const quint64 dataBytes = frames * kWavChannels * (kWavBits / 8);

	QByteArray header;
	QDataStream out(&header, QIODevice::WriteOnly);
	out.setByteOrder(QDataStream::LittleEndian);
	out.writeRawData("RIFF", 4);
	out << static_cast<quint32>(36 + dataBytes);
	out.writeRawData("WAVEfmt ", 8);
	out << static_cast<quint32>(16) << static_cast<quint16>(3) << static_cast<quint16>(kWavChannels)
		<< static_cast<quint32>(sampleRate)
		<< static_cast<quint32>(sampleRate * kWavChannels * (kWavBits / 8))
		<< static_cast<quint16>(kWavChannels * (kWavBits / 8)) << static_cast<quint16>(kWavBits);
	out.writeRawData("data", 4);
	out << static_cast<quint32>(dataBytes);
	if (header.size() != kWavHeaderBytes)
	{
		*error = QStringLiteral("internal error: the WAV header is %1 bytes, not %2")
			.arg(header.size()).arg(kWavHeaderBytes);
		return false;
	}
	if (file.write(header) != header.size())
	{
		*error = QStringLiteral("cannot write the WAV header of %1").arg(path);
		return false;
	}
	const qint64 payload = static_cast<qint64>(dataBytes);
	if (file.write(reinterpret_cast<const char*>(buffer.data()), payload) != payload)
	{
		*error = QStringLiteral("cannot write %1 (%2 bytes of audio)").arg(path).arg(payload);
		return false;
	}
	return true;
}


//! Lowercase hex SHA-256 of \a file, empty when it cannot be read.
QString stemSha256OfFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file)) { return QString(); }
	return QString::fromLatin1(hash.result().toHex());
}

} // namespace control
} // namespace lmms
