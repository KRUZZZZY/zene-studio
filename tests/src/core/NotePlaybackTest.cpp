/*
 * NotePlaybackTest.cpp - repeats, recurrence, articulations, chase, swing, lyrics and groups
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
 *
 */
/*! 0.5.0 L07 (W3): what a note carries beyond pitch and time, and how notes are played -
 *  - a note's repeats sound as that many more hits, its recurrence skips the passes it is not on,
 *    and its articulation sends the track's keyswitch first (ED-133, ED-121);
 *  - chase sounds a note playback starts inside, only when it is on (ED-075);
 *  - the global swing delays off-beats only, and the MIDI offset delays everything (ED-142, ED-076);
 *  - lyrics split into syllables, groups take ids, and all of it saves only when set (ED-088, ED-069). */

#include <QtTest>

#include <cmath>
#include <limits>

#include <QDomDocument>
#include <QJsonArray>
#include <QThread>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <barrier>
#include <cmath>
#include <new>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#if defined(Q_OS_LINUX)
#include <pthread.h>
#include <sched.h>
#endif

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiPlayback.h"
#include "Note.h"
#include "NoteConditions.h"
#include "NotePlayHandle.h"
#include "ProjectJournal.h"
#include "SampleFrame.h"
#include "Song.h"

using namespace lmms;

namespace
{

// volatile: operator new updates these, and the optimiser cannot see that across the test's calls.
// Without it the flag store and the count read are folded away, and every count reads zero.
thread_local volatile bool countAllocations = false;
thread_local volatile std::uint64_t allocationCount = 0;

//! A type whose `new` takes the align_val_t overloads rather than the plain ones.
struct alignas(64) OverAligned
{
	char bytes[64];
};

std::pair<InstrumentTrack*, Instrument*> createModulatedMonstroTrack()
{
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	if (track == nullptr) { return {nullptr, nullptr}; }
	auto* instrument = track->loadInstrument(QStringLiteral("monstro"));
	if (instrument == nullptr) { return {track, nullptr}; }
	QDomDocument document;
	QDomElement settings = document.createElement(QStringLiteral("monstro"));
	settings.setAttribute(QStringLiteral("l1wav"), 0);
	settings.setAttribute(QStringLiteral("v1l1"), 1.0);
	instrument->restoreState(settings);
	return {track, instrument};
}

//! Pins the dummy device's rate for one test. The original rate and the sampleRateChanged
//! listeners are restored however the test leaves.
class DeviceRatePin
{
public:
	explicit DeviceRatePin(sample_rate_t rate) :
		m_device(Engine::audioEngine()->audioDev()),
		m_original(m_device->sampleRate())
	{
		m_device->setSampleRateForTesting(rate);
		emit Engine::audioEngine()->sampleRateChanged();
	}

	~DeviceRatePin()
	{
		m_device->setSampleRateForTesting(m_original);
		emit Engine::audioEngine()->sampleRateChanged();
	}

	DeviceRatePin(const DeviceRatePin&) = delete;
	DeviceRatePin& operator=(const DeviceRatePin&) = delete;

private:
	AudioDevice* m_device;
	sample_rate_t m_original;
};

bool sameSamples(const std::vector<SampleFrame>& lhs, const std::vector<SampleFrame>& rhs)
{
	if (lhs.size() != rhs.size()) { return false; }
	for (std::size_t frame = 0; frame < lhs.size(); ++frame)
	{
		if (std::abs(lhs[frame][0] - rhs[frame][0]) > 1.0e-6f ||
			std::abs(lhs[frame][1] - rhs[frame][1]) > 1.0e-6f)
		{
			return false;
		}
	}
	return true;
}

int audioEngineRenderContextCount()
{
	int count = 0;
	// AudioEngine owns one QThread child for each worker plus the caller-side context.
	for (QObject* child : Engine::audioEngine()->children())
	{
		if (qobject_cast<QThread*>(child) != nullptr) { ++count; }
	}
	return count;
}

#if defined(Q_OS_LINUX)
class SingleCpuAffinity
{
public:
	~SingleCpuAffinity()
	{
		if (m_changed) { pthread_setaffinity_np(pthread_self(), sizeof(m_original), &m_original); }
	}

	bool restrictToOneCpu() noexcept
	{
		if (pthread_getaffinity_np(pthread_self(), sizeof(m_original), &m_original) != 0) { return false; }
		for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
		{
			if (!CPU_ISSET(cpu, &m_original)) { continue; }
			cpu_set_t singleCpu;
			CPU_ZERO(&singleCpu);
			CPU_SET(cpu, &singleCpu);
			m_changed = pthread_setaffinity_np(pthread_self(), sizeof(singleCpu), &singleCpu) == 0;
			return m_changed;
		}
		return false;
	}

private:
	cpu_set_t m_original{};
	bool m_changed = false;
};
#endif

struct MonstroVoiceSet
{
	std::vector<NotePlayHandle*> handles;
	std::vector<std::vector<SampleFrame>> outputs;
};

MonstroVoiceSet createMonstroVoiceSet(InstrumentTrack* track, std::size_t count, f_cnt_t frames)
{
	MonstroVoiceSet voices;
	voices.handles.reserve(count);
	voices.outputs.resize(count, std::vector<SampleFrame>(static_cast<std::size_t>(frames)));
	for (std::size_t voice = 0; voice < count; ++voice)
	{
		const Note note(TimePos(384), TimePos(0), 36 + static_cast<int>(voice % 48));
		voices.handles.push_back(NotePlayHandleManager::acquire(track, 0, frames, note));
	}
	return voices;
}

void primeMonstroVoiceSet(Instrument* instrument, MonstroVoiceSet& voices)
{
	for (std::size_t voice = 0; voice < voices.handles.size(); ++voice)
	{
		instrument->playNote(voices.handles[voice], voices.outputs[voice]);
	}
}

void renderMonstroVoiceSetSerial(Instrument* instrument, MonstroVoiceSet& voices)
{
	for (int period = 0; period < 64; ++period)
	{
		for (std::size_t voice = 0; voice < voices.handles.size(); ++voice)
		{
			instrument->playNote(voices.handles[voice], voices.outputs[voice]);
		}
	}
}

void renderMonstroVoiceSetConcurrent(Instrument* instrument, MonstroVoiceSet& voices)
{
	std::barrier startVoices{static_cast<std::ptrdiff_t>(voices.handles.size())};
	std::vector<std::thread> workers;
	workers.reserve(voices.handles.size());
	for (std::size_t voice = 0; voice < voices.handles.size(); ++voice)
	{
		workers.emplace_back([&, voice]
		{
			for (int period = 0; period < 64; ++period)
			{
				startVoices.arrive_and_wait();
				instrument->playNote(voices.handles[voice], voices.outputs[voice]);
			}
		});
	}
	for (auto& worker : workers) { worker.join(); }
}

void releaseMonstroVoiceSet(MonstroVoiceSet& voices)
{
	for (NotePlayHandle* handle : voices.handles) { NotePlayHandleManager::release(handle); }
}

bool monstroVoiceSetsMatch(const MonstroVoiceSet& lhs, const MonstroVoiceSet& rhs)
{
	if (lhs.outputs.size() != rhs.outputs.size()) { return false; }
	for (std::size_t voice = 0; voice < lhs.outputs.size(); ++voice)
	{
		if (!sameSamples(lhs.outputs[voice], rhs.outputs[voice])) { return false; }
	}
	return true;
}

std::pair<InstrumentTrack*, Instrument*> createParallelMonstroTrack(int renderContexts,
	bool* affinityMismatchEstablished)
{
	*affinityMismatchEstablished = true;
#if defined(Q_OS_LINUX)
	SingleCpuAffinity affinity;
	if (!affinity.restrictToOneCpu() || QThread::idealThreadCount() >= renderContexts)
	{
		*affinityMismatchEstablished = false;
		return {nullptr, nullptr};
	}
#else
	Q_UNUSED(renderContexts);
#endif
	return createModulatedMonstroTrack();
}

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The keys of @a track's note play handles alive now.
QList<int> handleKeys(const InstrumentTrack* track)
{
	QList<int> keys;
	for (PlayHandle* handle : Engine::audioEngine()->playHandles())
	{
		auto* note = dynamic_cast<NotePlayHandle*>(handle);
		if (note != nullptr && note->instrumentTrack() == track) { keys << note->key(); }
	}
	std::sort(keys.begin(), keys.end());
	return keys;
}

//! The warnings qWarning printed while it is alive.
class WarningCapture
{
public:
	WarningCapture()
	{
		s_texts.clear();
		qInstallMessageHandler(&collect);
	}

	~WarningCapture() { qInstallMessageHandler(nullptr); }
	QStringList texts() const { return s_texts; }

private:
	static void collect(QtMsgType, const QMessageLogContext&, const QString& text) { s_texts << text; }
	static QStringList s_texts;
};

QStringList WarningCapture::s_texts;

} // namespace

namespace
{

//! Over-aligned memory from malloc, with the pointer malloc returned stored just below the aligned address.
void* allocateAligned(std::size_t size, std::size_t alignment)
{
	if (countAllocations) { allocationCount = allocationCount + 1; }
	const std::size_t align = std::max(alignment, sizeof(void*));
	void* raw = std::malloc((size != 0 ? size : 1) + align + sizeof(void*));
	if (raw == nullptr) { throw std::bad_alloc{}; }
	const auto start = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
	void* memory = reinterpret_cast<void*>((start + align - 1) & ~(static_cast<std::uintptr_t>(align) - 1));
	std::memcpy(static_cast<char*>(memory) - sizeof(void*), &raw, sizeof(raw));
	return memory;
}

void freeAligned(void* memory) noexcept
{
	if (memory == nullptr) { return; }
	void* raw = nullptr;
	std::memcpy(&raw, static_cast<char*>(memory) - sizeof(void*), sizeof(raw));
	std::free(raw);
}

} // namespace

void* operator new(std::size_t size)
{
	if (countAllocations) { allocationCount = allocationCount + 1; }
	if (void* memory = std::malloc(size != 0 ? size : 1)) { return memory; }
	throw std::bad_alloc{};
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
	return allocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
	return ::operator new(size, alignment);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { freeAligned(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { freeAligned(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { freeAligned(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { freeAligned(memory); }

class NotePlaybackTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;

	//! A fresh project holding one clip with one note, and that note.
	Note* oneNote(int key = 60, int length = 384)
	{
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		m_track->loadInstrument(QStringLiteral("tripleoscillator"));
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		return m_clip->addNote(Note(TimePos(length), TimePos(0), key), false);
	}

	//! Play from @a tick for @a periods periods; the keys of the handles the track has sounding then.
	QList<int> playFrom(tick_t tick, int periods = 3)
	{
		Song* song = Engine::getSong();
		song->setPlayPos(tick, Song::PlayMode::Song);
		song->playSong();
		for (int period = 0; period < periods; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		const QList<int> keys = handleKeys(m_track);
		song->stop();
		for (int period = 0; period < 64 && !handleKeys(m_track).isEmpty(); ++period) { Engine::audioEngine()->renderNextPeriod(); }
		return keys;
	}

	QJsonObject edit(const Note* note, const QJsonObject& values)
	{
		QJsonObject entry = values;
		entry.insert(QStringLiteral("note"), control::noteIdOf(note));
		return {{QStringLiteral("clip"), control::clipIdOf(m_clip)}, {QStringLiteral("edits"), QJsonArray{entry}}};
	}

private slots:
	void initTestCase()
	{
		#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // a real instrument, so notes start (SessionTuningTest's pattern)
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void monstroNoteStartDoesNotAllocate()
	{
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track != nullptr);
		Instrument* instrument = m_track->loadInstrument(QStringLiteral("monstro"));
		QVERIFY(instrument != nullptr);
		QCOMPARE(instrument->nodeName(), QStringLiteral("monstro"));

		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> output(static_cast<std::size_t>(frames));
		const Note note(TimePos(384), TimePos(0), 60);
		NotePlayHandle* nph = NotePlayHandleManager::acquire(m_track, 0, frames, note);

		allocationCount = 0;
		countAllocations = true;
		instrument->playNote(nph, std::span<SampleFrame>{output});
		countAllocations = false;
		const std::uint64_t allocations = allocationCount;
		NotePlayHandleManager::release(nph);

		QCOMPARE(static_cast<qulonglong>(allocations), 0ull);
	}

	void nesNoteStartDoesNotAllocate()
	{
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track != nullptr);
		Instrument* instrument = m_track->loadInstrument(QStringLiteral("nes"));
		QVERIFY(instrument != nullptr);
		QCOMPARE(instrument->nodeName(), QStringLiteral("nes"));

		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> output(static_cast<std::size_t>(frames));
		const Note note(TimePos(384), TimePos(0), 60);
		NotePlayHandle* nph = NotePlayHandleManager::acquire(m_track, 0, frames, note);
		QVERIFY(nph->m_pluginData == nullptr);

		allocationCount = 0;
		countAllocations = true;
		instrument->playNote(nph, std::span<SampleFrame>{output});
		countAllocations = false;
		const std::uint64_t allocations = allocationCount;
		QVERIFY(nph->m_pluginData != nullptr);
		NotePlayHandleManager::release(nph);

		QCOMPARE(static_cast<qulonglong>(allocations), 0ull);
	}

	void overAlignedAllocationsAreCounted()
	{
		allocationCount = 0;
		countAllocations = true;
		auto* single = new OverAligned{};
		auto* array = new OverAligned[2]{};
		countAllocations = false;
		const std::uint64_t allocations = allocationCount;

		QVERIFY(reinterpret_cast<std::uintptr_t>(single) % alignof(OverAligned) == 0);
		QVERIFY(reinterpret_cast<std::uintptr_t>(array) % alignof(OverAligned) == 0);
		delete[] array;
		delete single;

		QCOMPARE(static_cast<qulonglong>(allocations), 2ull);
	}

	void plainAllocationsAreCounted()
	{
		// Positive control: a zero from monstroNoteStartDoesNotAllocate only means something if this counts.
		allocationCount = 0;
		countAllocations = true;
		auto* single = new int{0};
		countAllocations = false;
		const std::uint64_t allocations = allocationCount;
		delete single;

		QCOMPARE(static_cast<qulonglong>(allocations), 1ull);
	}

	void monstroConcurrentVoicesKeepIndependentModulation()
	{
		const auto [serialTrack, serialInstrument] = createModulatedMonstroTrack();
		QVERIFY(serialTrack != nullptr && serialInstrument != nullptr);
		const int renderContexts = audioEngineRenderContextCount();
		if (renderContexts < 2) { QSKIP("the audio engine has no parallel render worker"); }

		bool affinityMismatchEstablished = false;
		const auto parallel = createParallelMonstroTrack(renderContexts, &affinityMismatchEstablished);
		if (!affinityMismatchEstablished) { QSKIP("could not constrain Qt's ideal thread count for this test"); }
		InstrumentTrack* parallelTrack = parallel.first;
		Instrument* parallelInstrument = parallel.second;
		QVERIFY(parallelTrack != nullptr && parallelInstrument != nullptr);

		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		auto serialVoices = createMonstroVoiceSet(serialTrack, static_cast<std::size_t>(renderContexts), frames);
		auto parallelVoices = createMonstroVoiceSet(parallelTrack, static_cast<std::size_t>(renderContexts), frames);
		primeMonstroVoiceSet(serialInstrument, serialVoices);
		primeMonstroVoiceSet(parallelInstrument, parallelVoices);
		renderMonstroVoiceSetSerial(serialInstrument, serialVoices);
		renderMonstroVoiceSetConcurrent(parallelInstrument, parallelVoices);
		releaseMonstroVoiceSet(serialVoices);
		releaseMonstroVoiceSet(parallelVoices);
		QVERIFY2(monstroVoiceSetsMatch(serialVoices, parallelVoices),
			"a voice was dropped or its modulation changed at engine render concurrency");
	}

	void monstroRandomLfoAtLowSampleRateRenders()
	{
		// At 8 kHz the 0.1 ms LFO rate is 0.8 frames. The random LFO modes take the period as
		// whole frames for their modulo, and it truncated to zero: a division by zero on the
		// first period.
		DeviceRatePin pin(8000);
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		// WAVE_RANDOM (9) and WAVE_RANDOM_SMOOTH (10): both take the period from the same LFO rate
		for (const int wave : {9, 10})
		{
			Engine::getSong()->clearProject();
			auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
			QVERIFY(track != nullptr);
			Instrument* instrument = track->loadInstrument(QStringLiteral("monstro"));
			QVERIFY(instrument != nullptr);
			QDomDocument document;
			QDomElement settings = document.createElement(QStringLiteral("monstro"));
			settings.setAttribute(QStringLiteral("l1wav"), wave);
			settings.setAttribute(QStringLiteral("l1rat"), 0.1);   // LFO 1 rate in ms, the minimum
			settings.setAttribute(QStringLiteral("v1l1"), 1.0);
			instrument->restoreState(settings);

			std::vector<SampleFrame> output(static_cast<std::size_t>(frames));
			const Note note(TimePos(384), TimePos(0), 60);
			NotePlayHandle* handle = NotePlayHandleManager::acquire(track, 0, frames, note);
			for (int period = 0; period < 16; ++period) { instrument->playNote(handle, output); }
			NotePlayHandleManager::release(handle);

			float peak = 0.0f;
			for (const SampleFrame& frame : output)
			{
				QVERIFY(std::isfinite(frame[0]) && std::isfinite(frame[1]));
				peak = std::max(peak, std::max(std::abs(frame[0]), std::abs(frame[1])));
			}
			QVERIFY2(peak > 0.0f, "the note rendered silence, so the LFO path proves nothing");
		}
		Engine::getSong()->clearProject();
	}

	void repeatsSoundAsMoreHits()
	{
		Note* note = oneNote();
		QVERIFY(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("repeats"), 3}})).ok);
		QCOMPARE(note->conditions().repeats, 3);
		QCOMPARE(playFrom(0), (QList<int>{60, 60, 60, 60}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_clip->notes().front()->conditions().repeats, 0);
	}

	void upDownArpeggioHandlesRepeatedSinglePitch()
	{
		oneNote();
		QDomDocument doc;
		QDomElement trackSettings = doc.createElement(QStringLiteral("instrumenttrack"));
		QDomElement arpeggio = doc.createElement(QStringLiteral("arpeggiator"));
		arpeggio.setAttribute(QStringLiteral("arp-enabled"), 1);
		arpeggio.setAttribute(QStringLiteral("arp"), 0); // The "octave" chord contains one pitch.
		arpeggio.setAttribute(QStringLiteral("arprange"), 1);
		arpeggio.setAttribute(QStringLiteral("arprepeats"), 2);
		arpeggio.setAttribute(QStringLiteral("arpdir"), static_cast<int>(InstrumentFunctionArpeggio::ArpDirection::UpAndDown));
		arpeggio.setAttribute(QStringLiteral("arptime"), 25);
		trackSettings.appendChild(arpeggio);
		m_track->loadTrackSpecificSettings(trackSettings);
		QVERIFY(m_track->isArpeggioEnabled());

		const QList<int> keys = playFrom(0, 3);
		QVERIFY2(keys.size() >= 2, qPrintable(QString::number(keys.size())));
		QVERIFY(std::all_of(keys.cbegin(), keys.cend(), [](int key) { return key == 60; }));
	}

	void aRecurrenceSkipsThePassesItIsNotOn()
	{
		Note* note = oneNote();
		QCOMPARE(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("recur_mask"), 2}})).errorKind, ControlErrorKind::InvalidArgs);
		// The second of every two passes: not on the first.
		QVERIFY(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("recur_length"), 2}, {QStringLiteral("recur_mask"), 2}})).ok);
		QVERIFY(playFrom(0).isEmpty());
		NoteConditions conditions = m_clip->notes().front()->conditions();
		QVERIFY(!conditions.recursOnPass(0) && conditions.recursOnPass(1) && !conditions.recursOnPass(2));
		// The pass counts a loop wrap (Song::loopWraps) - not a jump back - and a stop starts it again (0.5.0 W2).
		MidiPlayback& playback = MidiPlayback::instance();
		playback.stopped();
		playback.tickPlayed(10, 5);
		QVERIFY(playback.chasingNow());
		QCOMPARE(playback.pass(), 0);  // the wraps before the start are no pass
		playback.tickPlayed(11, 5);
		QVERIFY(!playback.chasingNow());
		playback.tickPlayed(0, 5);     // a seek back
		QVERIFY(playback.chasingNow());
		QCOMPARE(playback.pass(), 0);
		playback.tickPlayed(0, 6);     // a wrap
		QCOMPARE(playback.pass(), 1);
		playback.stopped();
		playback.tickPlayed(0, 6);
		QCOMPARE(playback.pass(), 0);
	}

	void malformedRecurrenceMaskLeavesNoteOnEveryPass()
	{
		QDomDocument document;
		QDomElement note = document.createElement(QStringLiteral("note"));
		note.setAttribute(QStringLiteral("recl"), QStringLiteral("2"));
		note.setAttribute(QStringLiteral("recm"), QStringLiteral("broken"));

		NoteConditions conditions;
		conditions.load(note);
		QCOMPARE(conditions.recurLength, 0);
		QCOMPARE(conditions.recurMask, 0);
		QVERIFY(conditions.recursOnPass(0));
		QVERIFY(conditions.recursOnPass(1));
	}

	//! 0.5.0 W2: the song counts the loop wraps its render plays, and a seek back while playing is no wrap
	//! and no pass (the pass used to count every jump back).
	void aLoopWrapIsAPassAndASeekBackIsNot()
	{
		oneNote();
		Song* song = Engine::getSong();
		Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
		timeline.setLoopPoints(TimePos(0), TimePos(48));
		timeline.setLoopEnabled(true);
		song->setPlayPos(0, Song::PlayMode::Song);
		MidiPlayback::instance().stopped();  // the test above fed it by hand: a fresh start, as after a stop
		song->playSong();
		const auto before = song->loopWraps();
		for (int period = 0; period < 2000 && song->loopWraps() == before; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		QCOMPARE(song->loopWraps(), before + 1);
		QCOMPARE(MidiPlayback::instance().pass(), 1);
		for (int period = 0; period < 2000 && song->getPlayPos().getTicks() < 24; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		song->setPlayPos(4, Song::PlayMode::Song);  // a seek back inside the loop
		Engine::audioEngine()->renderNextPeriod();
		QCOMPARE(song->loopWraps(), before + 1);
		QCOMPARE(MidiPlayback::instance().pass(), 1);
		song->setPlayPos(100, Song::PlayMode::Song);  // a seek past the loop: put back inside, no wrap either
		Engine::audioEngine()->renderNextPeriod();
		QVERIFY(song->getPlayPos().getTicks() < 48);
		QCOMPARE(song->loopWraps(), before + 1);
		QCOMPARE(MidiPlayback::instance().pass(), 1);
		song->stop();
		timeline.setLoopEnabled(false);
	}

	//! The wrap count only counts up: past 2^32 wraps it keeps going instead of rolling back to 0 (Song.h
	//! loopWraps). The count starts at the 32-bit limit through the test seam, as 2^32 wraps cannot be played.
	void aLoopWrapCountPassesTheThirtyTwoBitLimit()
	{
		oneNote();
		Song* song = Engine::getSong();
		Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
		// A failed QCOMPARE returns from this function before its last lines, and a loop left on or a count left at
		// the 32-bit limit would fail the tests that run after it: the guard puts both back on every return path.
		struct RestoreLoop
		{
			Song* song;
			Timeline& timeline;
			std::uint64_t wraps;
			~RestoreLoop()
			{
				song->stop();
				timeline.setLoopEnabled(false);
				song->setLoopWrapsForTest(wraps);
			}
		} restore{song, timeline, song->loopWraps()};
		timeline.setLoopPoints(TimePos(0), TimePos(48));
		timeline.setLoopEnabled(true);
		song->setPlayPos(0, Song::PlayMode::Song);
		MidiPlayback::instance().stopped();
		const std::uint64_t limit = 0xFFFFFFFFull;
		song->setLoopWrapsForTest(limit);
		song->playSong();
		QCOMPARE(static_cast<std::uint64_t>(song->loopWraps()), limit);
		for (int period = 0; period < 2000 && song->loopWraps() == limit; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		QCOMPARE(static_cast<std::uint64_t>(song->loopWraps()), limit + 1);
		QCOMPARE(MidiPlayback::instance().pass(), 1);
	}

	void anArticulationPlaysItsKeyswitchFirst()
	{
		Note* note = oneNote();
		const QString track = control::trackIdOf(m_track);
		QCOMPARE(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), track},
			{QStringLiteral("articulations"), QJsonArray{QJsonObject{{QStringLiteral("keyswitch"), 200}}}}}).errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), track}, {QStringLiteral("articulations"),
			QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("legato")}}, QJsonObject{{QStringLiteral("name"), QStringLiteral("staccato")},
				{QStringLiteral("keyswitch"), 25}}}}}).ok);
		QCOMPARE(m_track->articulations().indexOf(QStringLiteral("Staccato")), 2);
		const ControlResult listed = run(QStringLiteral("track.articulations"), {{QStringLiteral("track"), track}});
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		const QJsonArray map = listed.result.value(QStringLiteral("articulations")).toArray();
		QCOMPARE(map.size(), 2);  // in order: a note's 'articulation' is the 1-based index into it
		QCOMPARE(map.at(1).toObject().value(QStringLiteral("keyswitch")).toInt(), 25);
		QVERIFY(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("articulation"), 2}})).ok);
		// Q050-015: the keyswitch is a MIDI key sent to the instrument ahead of the note, no play handle of its own
		// (ArticulationPlaybackTest reads what the instrument is handed); the note's handle is the only one.
		QCOMPARE(playFrom(0, 1), (QList<int>{60}));
		// Saved with the track, and one undo takes the map away.
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("track"));
		m_track->articulations().save(element);
		QVERIFY(element.attribute(QStringLiteral("articulations")).contains(QStringLiteral("staccato")));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the note's articulation
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the map
		QVERIFY(m_track->articulations().empty());
	}

	void aMalformedArticulationJsonWarnsAndLoadsWithNone()
	{
		oneNote();
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), control::trackIdOf(m_track)},
			{QStringLiteral("articulations"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("legato")}}}}}).ok);
		QCOMPARE(m_track->articulations().indexOf(QStringLiteral("Legato")), 1);
		QDomDocument doc;
		QDomElement truncated = doc.createElement(QStringLiteral("track"));
		truncated.setAttribute(QStringLiteral("articulations"), QStringLiteral("[{\"name\":\"legato\","));
		QDomElement notAnArray = doc.createElement(QStringLiteral("track"));
		notAnArray.setAttribute(QStringLiteral("articulations"), QStringLiteral("{\"name\":\"legato\"}"));
		WarningCapture warnings;
		m_track->articulations().load(truncated);
		QVERIFY(m_track->articulations().empty());
		QVERIFY2(warnings.texts().join(QLatin1Char('\n')).contains(QStringLiteral("could not be read")),
			qPrintable(warnings.texts().join(QLatin1Char('\n'))));
		m_track->articulations().load(notAnArray);
		QVERIFY(m_track->articulations().empty());
		QVERIFY2(warnings.texts().join(QLatin1Char('\n')).contains(QStringLiteral("could not be read")),
			qPrintable(warnings.texts().join(QLatin1Char('\n'))));
	}

	void anInvalidArticulationLeavesNoPrefixLoaded()
	{
		oneNote();
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("track"));
		element.setAttribute(QStringLiteral("articulations"),
			QStringLiteral("[{\"name\":\"legato\"},{\"keyswitch\":25}]"));  // the second entry has no name
		WarningCapture warnings;
		m_track->articulations().load(element);
		QVERIFY(m_track->articulations().empty());  // not the valid prefix alone, which would save back as the whole map
		QVERIFY2(warnings.texts().join(QLatin1Char('\n')).contains(QStringLiteral("could not be read")),
			qPrintable(warnings.texts().join(QLatin1Char('\n'))));
	}

	void aLoopedClipPlaysItsStartAgain()
	{
		Note* note = oneNote(60, 24);
		m_clip->changeLength(TimePos(384));
		QVERIFY(playFrom(96).isEmpty());  // no loop: nothing starts at 96
		QVERIFY(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)}, {QStringLiteral("length"), 96}}).ok);
		QCOMPARE(playFrom(96), (QList<int>{60}));  // 96 folds to 0, where the note is
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_clip->clipEdits().loopTicks, 0);
		(void) note;
	}

	void chaseSoundsANotePlaybackStartsInside()
	{
		oneNote();
		QVERIFY(playFrom(96).isEmpty());  // off by default: playback as it always was
		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), true}}).ok);
		QCOMPARE(playFrom(96), (QList<int>{60}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!MidiPlayback::instance().chase());
	}

	void swingDelaysTheOffBeatsOnly()
	{
		MidiPlayback& playback = MidiPlayback::instance();
		QCOMPARE(playback.delayFrames(12, 100.f, 48000), f_cnt_t{0});
		QVERIFY(run(QStringLiteral("transport.groove"), {{QStringLiteral("swing"), 0.5}}).ok);
		QCOMPARE(playback.delayFrames(0, 100.f, 48000), f_cnt_t{0});
		QCOMPARE(playback.delayFrames(12, 100.f, 48000), f_cnt_t{600});  // half of a 12-tick step
		QCOMPARE(playback.delayFrames(24, 100.f, 48000), f_cnt_t{0});
		QVERIFY(playback.shouldPersist());
		QVERIFY(run(QStringLiteral("transport.groove"), {{QStringLiteral("grid"), QStringLiteral("8th")}}).ok);
		QCOMPARE(playback.delayFrames(12, 100.f, 48000), f_cnt_t{0});
		QCOMPARE(playback.delayFrames(24, 100.f, 48000), f_cnt_t{1200});
		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("offset_ms"), 10}}).ok);
		QCOMPARE(playback.delayFrames(0, 100.f, 48000), f_cnt_t{480});
		for (int i = 0; i < 3; ++i) { QVERIFY(run(QStringLiteral("control.undo")).ok); }
		QCOMPARE(playback.swing(), 0.f);
		QCOMPARE(playback.offsetMs(), 0);
		QVERIFY(!playback.shouldPersist());
		QCOMPARE(run(QStringLiteral("transport.groove"), {{QStringLiteral("swing"), 0.9}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void perNoteLyricsRequireText()
	{
		Note* note = oneNote();
		const QString clip = control::clipIdOf(m_clip);
		QVERIFY(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}, {QStringLiteral("text"), QStringLiteral("original")}}).ok);
		const ControlResult result = run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip},
			{QStringLiteral("lyrics"), QJsonArray{QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}}}}});
		QCOMPARE(lyricText(note->conditions().lyric), QStringLiteral("original"));
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
	}

	void perNoteLyricsRequireStringText()
	{
		Note* note = oneNote();
		const QString clip = control::clipIdOf(m_clip);
		QVERIFY(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}, {QStringLiteral("text"), QStringLiteral("original")}}).ok);
		const ControlResult result = run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip},
			{QStringLiteral("lyrics"), QJsonArray{QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}, {QStringLiteral("text"), 42}}}}});
		QCOMPARE(lyricText(note->conditions().lyric), QStringLiteral("original"));
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! 'text' and 'lyrics' are two spellings of the same request: both together is ambiguous, so the
	//! command refuses it instead of silently dropping one of them.
	void lyricsRejectBothInputFormsTogether()
	{
		Note* note = oneNote();
		const QString clip = control::clipIdOf(m_clip);
		QVERIFY(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}, {QStringLiteral("text"), QStringLiteral("original")}}).ok);
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int depthBefore = journal->undoDepth();
		const ControlResult result = run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip},
			{QStringLiteral("text"), QStringLiteral("ignored")},
			{QStringLiteral("lyrics"), QJsonArray{QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)},
				{QStringLiteral("text"), QStringLiteral("per note")}}}}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(lyricText(note->conditions().lyric), QStringLiteral("original"));
		QCOMPARE(journal->undoDepth(), depthBefore);
	}

	//! Every note in scope is written: the ones under the syllables get one, the rest are cleared.
	//! The reply's 'set' counts every note the command changed, not only the syllables.
	void lyricsReportEveryNoteTheyWrite()
	{
		oneNote();
		for (int i = 1; i < 3; ++i) { m_clip->addNote(Note(TimePos(24), TimePos(i * 48), 60 + i), false); }
		const QString clip = control::clipIdOf(m_clip);
		QVERIFY(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}, {QStringLiteral("text"), QStringLiteral("a b c")}}).ok);
		const ControlResult result = run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip},
			{QStringLiteral("text"), QStringLiteral("solo")}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(result.result.value(QStringLiteral("set")).toInt(), 3);
		QCOMPARE(result.result.value(QStringLiteral("syllables")).toInt(), 1);
		QStringList sung;
		for (const Note* note : m_clip->notes()) { sung << lyricText(note->conditions().lyric); }
		QCOMPARE(sung, (QStringList{QStringLiteral("solo"), QString(), QString()}));
	}

	//! A non-finite swing must not survive setSwing: std::clamp leaves a NaN
	//! untouched (every comparison against it is false), and the value reaches
	//! delayFrames(), save() and shouldPersist() from there.
	void nonFiniteSwingLoadsBounded()
	{
		MidiPlayback& playback = MidiPlayback::instance();
		const float nan = std::numeric_limits<float>::quiet_NaN();
		playback.setSwing(nan, 2 * MidiPlayback::DefaultSwingGrid);
		QVERIFY(std::isfinite(playback.swing()));
		QVERIFY(playback.swing() >= 0.f);
		QVERIFY(playback.swing() <= MidiPlayback::MaxSwing);
		playback.setSwing(std::numeric_limits<float>::infinity(), MidiPlayback::DefaultSwingGrid);
		QVERIFY(std::isfinite(playback.swing()));
		QVERIFY(playback.swing() <= MidiPlayback::MaxSwing);
		playback.setSwing(std::numeric_limits<float>::lowest(), MidiPlayback::DefaultSwingGrid);
		QVERIFY(std::isfinite(playback.swing()));
		QVERIFY(playback.swing() >= 0.f);
		// The delayFrames() result must stay a real frame count throughout.
		QVERIFY(playback.delayFrames(12, 100.f, 48000) >= 0);
		playback.setSwing(0.f, MidiPlayback::DefaultSwingGrid);
		QCOMPARE(playback.swing(), 0.f);
	}

	void swingRejectsValuesJustAboveMaximum()
	{
		MidiPlayback& playback = MidiPlayback::instance();
		playback.setSwing(0.f, MidiPlayback::DefaultSwingGrid);
		const ControlResult result = run(QStringLiteral("transport.groove"), {{QStringLiteral("swing"), 0.75000001}});
		if (result.ok) { QVERIFY(run(QStringLiteral("control.undo")).ok); }
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(playback.swing(), 0.f);
		QCOMPARE(playback.swingGrid(), MidiPlayback::DefaultSwingGrid);
	}

	void swingRejectsTinyNegativeValues()
	{
		MidiPlayback& playback = MidiPlayback::instance();
		playback.setSwing(0.f, MidiPlayback::DefaultSwingGrid);
		const ControlResult result = run(QStringLiteral("transport.groove"), {{QStringLiteral("swing"), -1e-50}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(playback.swing(), 0.f);
		QCOMPARE(playback.swingGrid(), MidiPlayback::DefaultSwingGrid);
	}

	void clipControllerUsesTheTrackOutputChannel()
	{
		oneNote();
		m_clip->setControllerPoints(ControllerKind::ControlChange, 7, {{0, 64}});
		MidiPort* port = m_track->midiPort();
		port->setOutputChannel(6);
		port->setMode(MidiPort::Mode::Output);
		const quint64 written = port->outputEventsWritten();

		m_track->playClipControllers(m_clip, 0, 0);

		QCOMPARE(port->outputEventsWritten(), written + 1);
	}

	void lyricsAndGroups()
	{
		oneNote();
		for (int i = 1; i < 5; ++i) { m_clip->addNote(Note(TimePos(24), TimePos(i * 48), 60 + i), false); }
		const QString clip = control::clipIdOf(m_clip);
		QCOMPARE(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}}).errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}, {QStringLiteral("text"), QStringLiteral("Hal-le-lu-jah sing")}}).ok);
		QStringList sung;
		for (const Note* note : m_clip->notes()) { sung << lyricText(note->conditions().lyric); }
		QCOMPARE(sung, (QStringList{QStringLiteral("Hal-"), QStringLiteral("le-"), QStringLiteral("lu-"), QStringLiteral("jah"), QStringLiteral("sing")}));
		for (Note* note : m_clip->notes()) { note->setSelected(note->key() >= 62); }
		const ControlResult grouped = run(QStringLiteral("note.group"), {{QStringLiteral("clip"), clip}});
		QVERIFY2(grouped.ok, qPrintable(grouped.errorMessage));
		QCOMPARE(grouped.result.value(QStringLiteral("notes")).toInt(), 3);
		Note* last = m_clip->notes().back();
		QCOMPARE(last->conditions().group, 1);
		// Saved only when set, and read back whole.
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement saved = last->saveState(doc, root);
		QCOMPARE(saved.attribute(QStringLiteral("lyr")), QStringLiteral("sing"));
		Note restored;
		restored.restoreState(saved);
		QVERIFY(restored.conditions() == last->conditions());
		QVERIFY(!m_clip->notes().front()->saveState(doc, root).hasAttribute(QStringLiteral("grp")));
		QVERIFY(run(QStringLiteral("note.ungroup"), {{QStringLiteral("clip"), clip}}).ok);
		QCOMPARE(m_clip->notes().back()->conditions().group, 0);
		QCOMPARE(run(QStringLiteral("note.group"), {{QStringLiteral("clip"), clip}, {QStringLiteral("scope"), QStringLiteral("clip")}}).result
			.value(QStringLiteral("group")).toInt(), 1);
	}

	void emptyPerNoteLyricsDoNotCreateAnUndoStep()
	{
		oneNote();
		const QString clip = control::clipIdOf(m_clip);
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int depthBefore = journal->undoDepth();

		const ControlResult result = run(QStringLiteral("note.lyrics"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("lyrics"), QJsonArray{}}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(result.result.value(QStringLiteral("set")).toInt(), 0);
		QCOMPARE(journal->undoDepth(), depthBefore);
	}
};

QTEST_MAIN(NotePlaybackTest)
#include "NotePlaybackTest.moc"
