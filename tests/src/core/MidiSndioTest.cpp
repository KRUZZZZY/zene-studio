/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

#include <QtTest>

#include <cstddef>

#include <sndio.h>

#include "MidiSndio.h"

namespace
{
int mioWriteCalls = 0;
}

extern "C" mio_hdl* mio_open(const char*, unsigned int, int)
{
	return nullptr;
}

extern "C" std::size_t mio_write(mio_hdl*, const void*, std::size_t size)
{
	++mioWriteCalls;
	return size;
}

class TestMidiSndio final : public lmms::MidiSndio
{
public:
	using lmms::MidiSndio::sendByte;
};

class MidiSndioTest : public QObject
{
	Q_OBJECT

private slots:
	void failedOpenDoesNotWrite()
	{
		mioWriteCalls = 0;
		TestMidiSndio midi;
		midi.sendByte(0x90);
		QCOMPARE(mioWriteCalls, 0);
	}
};

QTEST_APPLESS_MAIN(MidiSndioTest)
#include "MidiSndioTest.moc"
