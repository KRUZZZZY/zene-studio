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

#include <QDir>
#include <QDomDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>
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

namespace lmms
{
LMMS_EXPORT void resetInternLyricLookupWorkForTest() noexcept;
LMMS_EXPORT std::uint64_t internLyricLookupWorkForTest() noexcept;
}

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
	void initTestCase();

	void cleanupTestCase();

	void importingDistinctLyricsUsesIndexedLookup();

	void monstroNoteStartDoesNotAllocate();

	void nesNoteStartDoesNotAllocate();

	void overAlignedAllocationsAreCounted();

	void plainAllocationsAreCounted();

	void monstroConcurrentVoicesKeepIndependentModulation();

	void monstroRandomLfoAtLowSampleRateRenders();

	void repeatsSoundAsMoreHits();

	//! Q050-294.1: the step view's ratchet and chance are PLAYED, not display values. note.step_set writes the same
	//! fields the engine reads at trigger time: ratchet 4 is four attacks, chance 0 is none, one undo is one attack.
	void stepSetRatchetAndChanceArePlayed();

	void upDownArpeggioHandlesRepeatedSinglePitch();

	//! Cluster new-p14-121: with range 3 and repeats 2 the up/down and down/up arpeggios reach every step of the range.
	void upDownArpeggioWithRepeatsReachesEveryStep();

	//! Note repeats are not rounded to their step, so 1.5 repeats reach the arpeggio. The slot arithmetic
	//! then divided by range / repeats = 1 / 1.5, which truncates to zero: a cycled arpeggio took the
	//! modulo by zero in the audio path. 1.5 plays as the whole number below it (1), a single pitch.
	void cycledArpeggioWithFractionalRepeatsDoesNotDivideByZero();

	//! The downward inversion uses the same slot count as the cycle: with 1.5 repeats and no cycle it
	//! produced index -1, which is key 48 (an octave down) rather than the chord's only pitch.
	void downArpeggioWithFractionalRepeatsStaysOnTheChord();

	void aRecurrenceSkipsThePassesItIsNotOn();

	void malformedRecurrenceMaskLeavesNoteOnEveryPass();

	//! 0.5.0 W2: the song counts the loop wraps its render plays, and a seek back while playing is no wrap
	//! and no pass (the pass used to count every jump back).
	void aLoopWrapIsAPassAndASeekBackIsNot();

	//! The wrap count only counts up: past 2^32 wraps it keeps going instead of rolling back to 0 (Song.h
	//! loopWraps). The count starts at the 32-bit limit through the test seam, as 2^32 wraps cannot be played.
	void aLoopWrapCountPassesTheThirtyTwoBitLimit();

	void anArticulationPlaysItsKeyswitchFirst();

	void aMalformedArticulationJsonWarnsAndLoadsWithNone();

	void anInvalidArticulationLeavesNoPrefixLoaded();

	void aLoopedClipPlaysItsStartAgain();

	/*! Q050-242: recurrence through a LOCALLY LOOPED clip - the combination no test covered. The
	 *  clip is 96 ticks long and looped every 24 ticks, so within ONE song pass it folds its note
	 *  onto ticks 0, 24, 48 and 72 (four local repeats inside a 96-tick song pass). The recurrence mask counts SONG passes
	 *  (MidiPlayback::pass(), advanced only by a transport loop wrap), not the clip's own repeats:
	 *  with length 2 and mask 1 (the first of every two passes) the note sounds on every local
	 *  repeat of pass 0 and on none of pass 1, while the clip keeps folding. The tempo is set to
	 *  180 BPM so the note handle ends before each local repeat, then restored by the guard below.
	 *
	 *  The three cases are the ones a length/mask pair can be wrong about: the selected passes
	 *  sound and the others do not (2/1), a positive length with mask 0 plays NONE, and length 0
	 *  plays every pass.
	 */
	void recurrenceSelectsThePassesOfALocallyLoopedClip();

	/*! Q050-242, acceptance 1: the pair the properties box writes survives a real project save and
	 *  load, and a note with no pattern saves exactly as before (no recl/recm attributes at all). */
	void aRecurrencePairRoundTripsThroughTheProjectFile();

	void chaseSoundsANotePlaybackStartsInside();

	void swingDelaysTheOffBeatsOnly();

	void perNoteLyricsRequireText();

	void perNoteLyricsRequireStringText();

	//! 'text' and 'lyrics' are two spellings of the same request: both together is ambiguous, so the
	//! command refuses it instead of silently dropping one of them.
	void lyricsRejectBothInputFormsTogether();

	//! Every note in scope is written: the ones under the syllables get one, the rest are cleared.
	//! The reply's 'set' counts every note the command changed, not only the syllables.
	void lyricsReportEveryNoteTheyWrite();

	//! A non-finite swing must not survive setSwing: std::clamp leaves a NaN
	//! untouched (every comparison against it is false), and the value reaches
	//! delayFrames(), save() and shouldPersist() from there.
	void nonFiniteSwingLoadsBounded();

	//! delayFrames() returns a frame count. A frames-per-tick that is negative or not finite has no
	//! swing to give, and a negative delay must never reach the unsigned result: at full swing,
	//! -1 frame per tick used to come back as -9 frames, which is 2^64 - 9 frames.
	void delayFramesStaysAFrameCount();

	void swingRejectsValuesJustAboveMaximum();

	void swingRejectsTinyNegativeValues();

	void clipControllerUsesTheTrackOutputChannel();

	void lyricsAndGroups();

	void emptyPerNoteLyricsDoNotCreateAnUndoStep();
};

#include "NotePlaybackTestParts1.h"
#include "NotePlaybackTestParts2.h"

QTEST_MAIN(NotePlaybackTest)
#include "NotePlaybackTest.moc"
