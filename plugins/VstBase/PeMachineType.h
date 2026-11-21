/*
 * PeMachineType.h - reads the COFF machine type of a PE image from its bytes
 *
 * Copyright (c) 2026 LMMS developers
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

#ifndef LMMS_VST_PE_MACHINE_TYPE_H
#define LMMS_VST_PE_MACHINE_TYPE_H

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <QtEndian>
#include <QtGlobal>

namespace lmms::vst::PE
{
// Utilities for reading PE file machine type
// See specification at https://msdn.microsoft.com/library/windows/desktop/ms680547(v=vs.85).aspx

// Work around name conflict
#ifdef i386
#	undef i386
#endif

enum class MachineType : uint16_t
{
	unknown = 0x0,
	amd64 = 0x8664,
	i386 = 0x14c,
};

// Returns the COFF machine type of the PE image held in image[0, size).
// Throws std::runtime_error when the image is too short to hold the DOS header field and the
// PE signature and machine field it names, or when there is no PE signature at that offset.
inline MachineType machineTypeOf(const uchar* image, qint64 size)
{
	// e_lfanew, the signed offset of the PE signature, is the last field of the DOS header at 0x3C.
	constexpr qint64 peOffsetField = 0x3C;
	constexpr qint64 signatureSize = 4;
	constexpr qint64 machineFieldSize = 2;
	if (size < peOffsetField + 4) {
		throw std::runtime_error("Invalid PE file");
	}
	const qint64 peOffset = qFromLittleEndian<int32_t>(image + peOffsetField);
	if (peOffset < 0 || peOffset > size - signatureSize - machineFieldSize) {
		throw std::runtime_error("Invalid PE file");
	}
	const uchar* peSignature = image + peOffset;
	if (memcmp(peSignature, "PE\0\0", signatureSize)) {
		throw std::runtime_error("Invalid PE file");
	}
	const uchar* coffHeader = peSignature + signatureSize;
	return static_cast<MachineType>(qFromLittleEndian<uint16_t>(coffHeader));
}

} // namespace lmms::vst::PE

#endif // LMMS_VST_PE_MACHINE_TYPE_H
