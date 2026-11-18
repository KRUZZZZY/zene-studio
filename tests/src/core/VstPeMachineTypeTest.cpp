/*
 * VstPeMachineTypeTest.cpp - the VST host's PE machine-type probe reads only
 *                            bytes that lie inside the file it was given
 *
 * A truncated or malformed VST2 binary must be rejected with the "Invalid PE file"
 * error, not read past the mapping. The probe is pinned here on the header it uses
 * (plugins/VstBase/PeMachineType.h). Each malformed case builds the PE header in
 * memory beyond the declared file size, so a probe that ignores the size reads a
 * valid-looking header and returns a machine type instead of throwing.
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

#include <QtEndian>
#include <QtTest>

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "../../../plugins/VstBase/PeMachineType.h"

namespace PE = lmms::vst::PE;

namespace {

constexpr qint32 peHeaderOffset = 0x40;
constexpr uint16_t amd64Machine = 0x8664;
constexpr uint16_t i386Machine = 0x14c;

// Writes e_lfanew (the DOS header field at 0x3C) naming peOffset, then the PE
// signature and COFF machine field at that offset. peOffset may be negative; the
// caller leaves room before the image in that case.
void writePeHeader(uchar* image, qint32 peOffset, uint16_t machine)
{
	qToLittleEndian<qint32>(peOffset, image + 0x3C);
	uchar* signature = image + peOffset;
	std::memcpy(signature, "PE\0\0", 4);
	qToLittleEndian<uint16_t>(machine, signature + 4);
}

uint16_t machineOf(const uchar* image, qint64 size)
{
	return static_cast<uint16_t>(PE::machineTypeOf(image, size));
}

} // namespace

class VstPeMachineTypeTest : public QObject
{
	Q_OBJECT

private slots:
	void acceptsWellFormedImages();
	void acceptsSignatureEndingExactlyAtFileEnd();
	void rejectsEmptyFile();
	void rejectsFileShorterThanDosHeaderField();
	void rejectsSignatureBeyondFileEnd();
	void rejectsNegativePeOffset();
	void rejectsMachineFieldBeyondFileEnd();
	void rejectsMissingSignature();
};

void VstPeMachineTypeTest::acceptsWellFormedImages()
{
	uchar amd64Image[0x80] = {};
	writePeHeader(amd64Image, peHeaderOffset, amd64Machine);
	QCOMPARE(machineOf(amd64Image, sizeof amd64Image), amd64Machine);

	uchar win32Image[0x80] = {};
	writePeHeader(win32Image, peHeaderOffset, i386Machine);
	QCOMPARE(machineOf(win32Image, sizeof win32Image), i386Machine);
}

void VstPeMachineTypeTest::acceptsSignatureEndingExactlyAtFileEnd()
{
	// Signature at 0x40..0x43 and machine field at 0x44..0x45: the last byte is the file's last byte.
	uchar image[peHeaderOffset + 6] = {};
	writePeHeader(image, peHeaderOffset, amd64Machine);
	QCOMPARE(machineOf(image, sizeof image), amd64Machine);
}

void VstPeMachineTypeTest::rejectsEmptyFile()
{
	uchar image[0x80] = {};
	writePeHeader(image, peHeaderOffset, amd64Machine);
	QVERIFY_EXCEPTION_THROWN(PE::machineTypeOf(image, 0), std::runtime_error);
}

void VstPeMachineTypeTest::rejectsFileShorterThanDosHeaderField()
{
	// e_lfanew occupies 0x3C..0x3F, so a 0x3F-byte file has no PE offset at all.
	uchar image[0x80] = {};
	writePeHeader(image, peHeaderOffset, amd64Machine);
	QVERIFY_EXCEPTION_THROWN(PE::machineTypeOf(image, 0x3F), std::runtime_error);
}

void VstPeMachineTypeTest::rejectsSignatureBeyondFileEnd()
{
	// The DOS header is inside the 0x80-byte file, but it names a signature at 0x1000.
	uchar image[0x2000] = {};
	writePeHeader(image, 0x1000, amd64Machine);
	QVERIFY_EXCEPTION_THROWN(PE::machineTypeOf(image, 0x80), std::runtime_error);
}

void VstPeMachineTypeTest::rejectsNegativePeOffset()
{
	// e_lfanew is signed: -0x40 would point before the start of the file.
	uchar backing[0x100] = {};
	uchar* image = backing + 0x80;
	writePeHeader(image, -0x40, amd64Machine);
	QVERIFY_EXCEPTION_THROWN(PE::machineTypeOf(image, 0x80), std::runtime_error);
}

void VstPeMachineTypeTest::rejectsMachineFieldBeyondFileEnd()
{
	// The signature fits in 0x45 bytes but the second byte of the machine field does not.
	uchar image[0x80] = {};
	writePeHeader(image, peHeaderOffset, amd64Machine);
	QVERIFY_EXCEPTION_THROWN(PE::machineTypeOf(image, peHeaderOffset + 5), std::runtime_error);
}

void VstPeMachineTypeTest::rejectsMissingSignature()
{
	uchar image[0x80] = {};
	qToLittleEndian<qint32>(peHeaderOffset, image + 0x3C);
	QVERIFY_EXCEPTION_THROWN(PE::machineTypeOf(image, sizeof image), std::runtime_error);
}

QTEST_APPLESS_MAIN(VstPeMachineTypeTest)

#include "VstPeMachineTypeTest.moc"
