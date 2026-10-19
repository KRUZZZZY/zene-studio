/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

#include <QtTest>

#include <atomic>
#include <cstddef>
#include <type_traits>

#include <sndio.h>

#include <QThread>

#include "MidiClient.h"

#define private public
#include "MidiSndio.h"
#undef private

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
	//! The reader thread polls the stop flag while the destructor writes it, so
	//! the flag must be an atomic: a plain bool is a data race, volatile or not.
	void quitFlagIsAtomic()
	{
		QVERIFY((std::is_same_v<decltype(lmms::MidiSndio::m_quit), std::atomic_bool>));
	}

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
