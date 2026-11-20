/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

/*! WinMM short-message packing. MidiWinMM::processOutEvent sends winmmShortMessage( event );
 *  the WinMM callback cannot be driven on this platform, so these tests reach the exact
 *  expression that output path uses. A 14-bit pitch-bend value must reach the wire as an
 *  LSB then an MSB, 7 bits each - the split the WinMM input path reverses. */

#include <QtTest>

#include <QFile>

#include <cstdint>

#include "MidiEvent.h"
#include "MidiWinMMShortMessage.h"

using lmms::MidiEvent;
using lmms::winmmShortMessage;

namespace
{

//! Byte of a packed short message: 0 = status, 1 and 2 = the two 7-bit data bytes (for pitch bend: low, high).
std::uint32_t byteAt(std::uint32_t msg, unsigned index)
{
	return (msg >> (8 * index)) & 0xFFu;
}

//! Builds an event through the (type, channel, param0, param1) constructor. A literal 0 in the
//! channel slot also matches the SysEx constructor's const char* overload, so the types are fixed here.
MidiEvent eventOf(lmms::MidiEventTypes type, int channel, int p0, int p1 = 0)
{
	return MidiEvent(type, static_cast<std::int8_t>(channel), static_cast<std::int16_t>(p0),
		static_cast<std::int16_t>(p1));
}

} // namespace

class MidiWinMMShortMessageTest : public QObject
{
	Q_OBJECT

private slots:
	//! The centre of the bend range is 8192: LSB 0x00, MSB 0x40.
	void centredBendIsCentre()
	{
		const std::uint32_t msg = winmmShortMessage(eventOf(lmms::MidiPitchBend, 0, 8192));
		QCOMPARE(byteAt(msg, 0), 0xE0u);
		QCOMPARE(byteAt(msg, 1), 0x00u);
		QCOMPARE(byteAt(msg, 2), 0x40u);
	}

	//! Whole-message equality at the range edges and at a mid-range value.
	void bendLowAndHighBytesForKnownValues()
	{
		struct Case
		{
			int value;
			std::uint32_t lsb;
			std::uint32_t msb;
		};
		static const Case cases[] = {
			{0, 0x00u, 0x00u},
			{8191, 0x7Fu, 0x3Fu},
			{8193, 0x01u, 0x40u},
			{16383, 0x7Fu, 0x7Fu},
			{12345, 0x39u, 0x60u},
		};
		for (const Case& c : cases)
		{
			const std::uint32_t msg = winmmShortMessage(eventOf(lmms::MidiPitchBend, 0, c.value));
			QCOMPARE(msg, 0xE0u | (c.lsb << 8) | (c.msb << 16));
		}
	}

	//! Every bend value survives the trip through the bytes the WinMM input path reads back.
	void everyBendRoundTripsThroughTheInputContract()
	{
		for (int v = 0; v <= 16383; ++v)
		{
			const std::uint32_t msg = winmmShortMessage(eventOf(lmms::MidiPitchBend, 0, v));
			const int data1 = static_cast<int>(byteAt(msg, 1));
			const int data2 = static_cast<int>(byteAt(msg, 2));
			QCOMPARE(data1, v & 0x7F);
			QCOMPARE(data2, (v >> 7) & 0x7F);
			QCOMPARE(data1 + data2 * 128, v);
		}
	}

	//! The bend branch must not disturb the generic path: other messages keep their data bytes.
	void otherChannelMessagesKeepTheirDataBytes()
	{
		// Status is type | channel; byte 1 and byte 2 are the two 7-bit data bytes (param(0), param(1) for non-bend messages).
		QCOMPARE(winmmShortMessage(eventOf(lmms::MidiNoteOn, 3, 60, 100)), 0x00643C93u);
		QCOMPARE(winmmShortMessage(eventOf(lmms::MidiControlChange, 5, 1, 127)), 0x7F01B5u);
		QCOMPARE(winmmShortMessage(eventOf(lmms::MidiChannelPressure, 2, 64)), 0x000040D2u);
	}

	//! The channel nibble goes into the status byte and the bend data is unchanged.
	void bendKeepsStatusAndChannel()
	{
		const std::uint32_t msg = winmmShortMessage(eventOf(lmms::MidiPitchBend, 15, 8192));
		QCOMPARE(byteAt(msg, 0), 0xEFu);
		QCOMPARE(byteAt(msg, 2), 0x40u);
	}

	//! The output path must keep calling the shared packer, so the helper is not dead code.
	void winmmOutputPathUsesTheSharedPacker()
	{
		QFile source(QStringLiteral(ZENE_SOURCE_DIR "/src/core/midi/MidiWinMM.cpp"));
		const bool opened = source.open(QIODevice::ReadOnly | QIODevice::Text);
		QVERIFY(opened);
		QVERIFY(source.readAll().contains("winmmShortMessage( event )"));
	}
};

QTEST_GUILESS_MAIN(MidiWinMMShortMessageTest)
#include "MidiWinMMShortMessageTest.moc"
