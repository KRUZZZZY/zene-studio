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
};

QTEST_APPLESS_MAIN(MidiEventToByteSeqTest)
#include "MidiEventToByteSeqTest.moc"
