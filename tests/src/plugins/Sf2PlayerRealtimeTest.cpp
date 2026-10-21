/*
 * Sf2PlayerRealtimeTest.cpp - the SF2 player's note path must not allocate.
 *
 * The SF2 player hands a note from the note stage to the render stage. That
 * hand-off used to be a QVector of note handles behind a mutex: the first note
 * (or any note that grew the vector past its capacity) allocated on the audio
 * thread, and both the note stage and the render stage took a lock to do it.
 *
 * This test drives the product's own plugin module: it plays a note through
 * the instrument and counts the allocations the note path makes. The count
 * must be zero - the note's data comes from a preallocated slot and the note
 * event is published into a bounded, preallocated queue.
 *
 * The count is taken with a replacement global operator new, the same
 * mechanism the plugin migration harness uses (tests/src/plugins/
 * PluginPortsHarness.h): the module's allocations are counted too, because the
 * executable's replacement is the one the loaded module resolves.
 */

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include <QLibrary>
#include <QtTest>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "NotePlayHandle.h"
#include "Plugin.h"
#include "SampleFrame.h"
#include "Song.h"

namespace
{

//! Allocations counted while `counting` is true.
std::atomic_bool counting{false};
std::atomic<std::uint64_t> allocationCount{0};

} // namespace

void* operator new(std::size_t size)
{
	if (counting.load(std::memory_order_relaxed))
	{
		allocationCount.fetch_add(1, std::memory_order_relaxed);
	}
	if (void* memory = std::malloc(size == 0 ? 1 : size)) { return memory; }
	throw std::bad_alloc{};
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void operator delete(void* memory) noexcept
{
	std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
	::operator delete(memory);
}

void operator delete[](void* memory) noexcept
{
	::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
	::operator delete(memory);
}


class Sf2PlayerRealtimeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void notePathAllocatesNothing();
	void renderSurvivesAPendingReload();
};

void Sf2PlayerRealtimeTest::initTestCase()
{
	lmms::Engine::init(true);
	lmms::NotePlayHandleManager::init();
	QVERIFY2(lmms::Engine::audioEngine() != nullptr, "the engine did not initialise");
	// The test drives the instrument itself; the device must not render it too.
	lmms::Engine::audioEngine()->audioDev()->stopProcessing();
}


void Sf2PlayerRealtimeTest::notePathAllocatesNothing()
{
	if (QString::fromUtf8(SF2PLAYER_MODULE).isEmpty())
	{
		QSKIP("this build has no SF2 player module (no FluidSynth)");
	}

	QLibrary library{QString::fromUtf8(SF2PLAYER_MODULE)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));

	auto entry = reinterpret_cast<lmms::Plugin* (*)(lmms::Model*, void*)>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};

	{
		// The note the player is asked to play. It is created before the count
		// is armed: the note handle itself is not part of the note path.
		const lmms::Note note{lmms::TimePos{0}, lmms::TimePos{0}, lmms::DefaultKey};
		lmms::NotePlayHandle handle{track.get(), 0, frames, note, nullptr, -1,
			lmms::NotePlayHandle::Origin::MidiClip};

		// The render path is warmed up first: the synth allocates its own
		// buffers when it is created and when it renders for the first time,
		// and that is not what this test is about.
		instrument->play(span);

		// From here on the note path must not allocate: the note's data comes
		// from a preallocated slot and the note event goes into a bounded
		// queue.
		allocationCount.store(0, std::memory_order_relaxed);
		counting.store(true, std::memory_order_relaxed);
		instrument->playNote(&handle, span);
		counting.store(false, std::memory_order_relaxed);

		const auto allocations = allocationCount.load(std::memory_order_relaxed);
		QVERIFY2(allocations == 0,
			qPrintable(QStringLiteral("the SF2 note path allocated %1 time(s) on the audio thread")
				.arg(allocations)));

		// The note is still played: the hand-off to the render stage happened.
		instrument->play(span);
	}
	// The note gave its data back here; the instrument may go now.

	delete instrument;
}



void Sf2PlayerRealtimeTest::renderSurvivesAPendingReload()
{
	if (QString::fromUtf8(SF2PLAYER_MODULE).isEmpty())
	{
		QSKIP("this build has no SF2 player module (no FluidSynth)");
	}

	QLibrary library{QString::fromUtf8(SF2PLAYER_MODULE)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));

	auto entry = reinterpret_cast<lmms::Plugin* (*)(lmms::Model*, void*)>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};

	// The UI publishes a replacement synth; the render thread adopts it on the
	// next period instead of waiting for a mutex the UI might hold.
	QVERIFY(QMetaObject::invokeMethod(instrument, "reloadSynth", Qt::DirectConnection));
	QVERIFY(QMetaObject::invokeMethod(instrument, "updateGain", Qt::DirectConnection));
	QVERIFY(QMetaObject::invokeMethod(instrument, "updateTuning", Qt::DirectConnection));
	instrument->play(span);

	// The old synth is retired for the UI to delete, and the player keeps
	// rendering: nothing on the audio path blocked or crashed.
	QVERIFY(QMetaObject::invokeMethod(instrument, "reloadSynth", Qt::DirectConnection));
	instrument->play(span);

	delete instrument;
}

QTEST_MAIN(Sf2PlayerRealtimeTest)

#include "Sf2PlayerRealtimeTest.moc"
