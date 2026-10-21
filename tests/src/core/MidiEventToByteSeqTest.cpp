/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

#include "MidiEvent.h"
#include "MidiEventToByteSeq.h"

#include <QtTest>
#include <cstdint>

class MidiEventToByteSeqTest : public QObject
{
	Q_OBJECT

private slots:
	void validMaximumKeyPressureIsEncoded()
	{
		const lmms::MidiEvent event(lmms::MidiKeyPressure, 3, lmms::MidiMaxKey, 91);
		std::uint8_t bytes[3]{};

		QCOMPARE(lmms::writeToByteSeq(event, bytes, sizeof(bytes)), std::size_t{3});
		QCOMPARE(bytes[0], static_cast<std::uint8_t>(lmms::MidiKeyPressure | 3));
		QCOMPARE(bytes[1], static_cast<std::uint8_t>(lmms::MidiMaxKey));
		QCOMPARE(bytes[2], std::uint8_t{91});
	}

	void outOfRangeKeyPressureIsRejected()
	{
		const lmms::MidiEvent event(lmms::MidiKeyPressure, 0, 256, 91);
		std::uint8_t bytes[3]{};

		QCOMPARE(lmms::writeToByteSeq(event, bytes, sizeof(bytes)), std::size_t{0});
	}

	//! A control change inside its ranges is written as it is.
	void validControlChangeIsEncoded()
	{
		const lmms::MidiEvent event(lmms::MidiControlChange, 2, 127, 127);
		std::uint8_t bytes[3]{};

		QCOMPARE(lmms::writeToByteSeq(event, bytes, sizeof(bytes)), std::size_t{3});
		QCOMPARE(bytes[0], static_cast<std::uint8_t>(lmms::MidiControlChange | 2));
		QCOMPARE(bytes[1], std::uint8_t{127});
		QCOMPARE(bytes[2], std::uint8_t{127});
	}

	//! A controller number is a 7-bit field: 128 is not one, and masking it to 0
	//! would write a different message (another controller) than the caller asked for.
	void outOfRangeControlNumberIsRejected()
	{
		const lmms::MidiEvent event(lmms::MidiControlChange, 0, 128, 64);
		std::uint8_t bytes[3]{};

		QCOMPARE(lmms::writeToByteSeq(event, bytes, sizeof(bytes)), std::size_t{0});
	}

	//! A control value is a 7-bit data byte: 200 has its high bit set and is not
	//! a MIDI byte at all.
	void outOfRangeControlValueIsRejected()
	{
		const lmms::MidiEvent event(lmms::MidiControlChange, 0, 7, 200);
		std::uint8_t bytes[3]{};

		QCOMPARE(lmms::writeToByteSeq(event, bytes, sizeof(bytes)), std::size_t{0});
	}

	//! The program number is a 7-bit data byte too.
	void outOfRangeProgramIsRejected()
	{
		const lmms::MidiEvent event(lmms::MidiProgramChange, 0, 200, 0);
		std::uint8_t bytes[3]{};

		QCOMPARE(lmms::writeToByteSeq(event, bytes, sizeof(bytes)), std::size_t{0});
	}

	//! The channel pressure (aftertouch) is a 7-bit data byte.
	void outOfRangeChannelPressureIsRejected()
	{
		const lmms::MidiEvent event(lmms::MidiChannelPressure, 0, 200, 0);
		std::uint8_t bytes[3]{};

		QCOMPARE(lmms::writeToByteSeq(event, bytes, sizeof(bytes)), std::size_t{0});
	}

	//! The velocity is a 7-bit data byte: 200 must not be masked to 72.
	void outOfRangeVelocityIsRejected()
	{
		const lmms::MidiEvent event(lmms::MidiNoteOn, 0, 60, 200);
		std::uint8_t bytes[3]{};

		QCOMPARE(lmms::writeToByteSeq(event, bytes, sizeof(bytes)), std::size_t{0});
	}

	//! The maximum pitch bend (16383) is a 14-bit value and stays encodable.
	void maximumPitchBendIsEncoded()
	{
		const lmms::MidiEvent event(lmms::MidiPitchBend, 1, lmms::MidiMaxPitchBend, 0);
		std::uint8_t bytes[3]{};

		QCOMPARE(lmms::writeToByteSeq(event, bytes, sizeof(bytes)), std::size_t{3});
		QCOMPARE(bytes[0], static_cast<std::uint8_t>(lmms::MidiPitchBend | 1));
		QCOMPARE(bytes[1], std::uint8_t{0x7F});
		QCOMPARE(bytes[2], std::uint8_t{0x7F});
	}

	//! The pitch bend is 14 bits: 16384 does not fit and its high byte would be 128.
	void outOfRangePitchBendIsRejected()
	{
		const lmms::MidiEvent event(lmms::MidiPitchBend, 0, lmms::MidiMaxPitchBend + 1, 0);
		std::uint8_t bytes[3]{};

		QCOMPARE(lmms::writeToByteSeq(event, bytes, sizeof(bytes)), std::size_t{0});
	}
};

QTEST_APPLESS_MAIN(MidiEventToByteSeqTest)
#include "MidiEventToByteSeqTest.moc"
