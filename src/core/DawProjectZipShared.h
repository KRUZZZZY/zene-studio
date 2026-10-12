/*
 * DawProjectZipShared.h - what the DAWproject container's writer and reader share.
 *
 * An internal header (src/core, not include/): nothing outside DawProjectZip.cpp
 * and DawProjectZipRead.cpp uses it. It carries the CRC-32 every ZIP entry
 * carries and the two little-endian readers the record parsing needs, in a named
 * namespace so both translation units see one definition.
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

#ifndef LMMS_DAWPROJECT_ZIP_SHARED_H
#define LMMS_DAWPROJECT_ZIP_SHARED_H

#include <QByteArray>
#include <QtGlobal>

namespace lmms
{
namespace interchange
{
namespace zipdetail
{

//! The CRC-32 register's start value (and the value it is finally xored with).
constexpr quint32 CrcInitial = 0xFFFFFFFFu;

//! Folds \a size bytes at \a data into the running CRC register \a state
//! (reflected, polynomial 0xEDB88320). Start from CrcInitial; xor the result with
//! CrcInitial to finish.
quint32 crc32Update(quint32 state, const char* data, qsizetype size);

//! The finished CRC-32 of \a bytes.
quint32 crc32Of(const QByteArray& bytes);

inline quint16 readU16(const QByteArray& bytes, int offset)
{
	return static_cast<quint16>(static_cast<unsigned char>(bytes[offset]))
		| static_cast<quint16>(static_cast<unsigned char>(bytes[offset + 1]) << 8);
}

inline quint32 readU32(const QByteArray& bytes, int offset)
{
	return static_cast<quint32>(static_cast<unsigned char>(bytes[offset]))
		| (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 1])) << 8)
		| (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 2])) << 16)
		| (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 3])) << 24);
}

} // namespace zipdetail
} // namespace interchange
} // namespace lmms

#endif // LMMS_DAWPROJECT_ZIP_SHARED_H
