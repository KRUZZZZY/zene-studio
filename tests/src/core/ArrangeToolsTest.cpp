/*
 * ArrangeToolsTest.cpp - the clip list, sections, remove silence, tab to transient, note rotation
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
/*! 0.5.0 L04 (W3): clip.list finds clips by track and name (ED-036); sections are added, changed,
 *  removed and undone, saved only when there is one, and selected as the range (ED-047); a quiet middle
 *  stretch of a sample clip is cut out, one undo step (ED-040); tab to transient refuses where there
 *  is no audio and moves the play head to the next or previous burst where there is (ED-025); a MIDI
 *  clip is written as a MusicXML file (ED-086); track versions are listed and forgotten; notes rotate
 *  inside their clip (ED-028). */

#include <QtTest>

#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTemporaryDir>

#include <cstdlib>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "ArrangementSections.h"
#include "AudioBurstSupport.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "ScoreExport.h"
#include "Song.h"
#include "TrackVersions.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A sample clip of three equal parts: loud, silent, loud.
SampleClip* loudQuietLoud(Track* track)
{
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	std::vector<SampleFrame> data(static_cast<std::size_t>(rate * 3), SampleFrame(0.5f, 0.5f));
	for (int f = rate; f < 2 * rate; ++f) { data[static_cast<std::size_t>(f)] = SampleFrame(0.f, 0.f); }
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	clip->movePosition(TimePos(0));
	return clip;
}

/*! A sample clip of @a rate (NOT the engine's) whose middle @a quietFrames source frames are silent -
 *  the loud part either side is @a loudFrames. The rate is the point: source frames are counted in
 *  the SAMPLE's frames, so a stretch's length in ticks depends on it. */
SampleClip* loudQuietLoudAtRate(Track* track, int rate, int loudFrames, int quietFrames)
{
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
	std::vector<SampleFrame> data(static_cast<std::size_t>(loudFrames * 2 + quietFrames), SampleFrame(0.5f, 0.5f));
	for (int f = loudFrames; f < loudFrames + quietFrames; ++f) { data[static_cast<std::size_t>(f)] = SampleFrame(0.f, 0.f); }
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	clip->movePosition(TimePos(0));
	return clip;
}

//! Tells whether the audio engine's change mutex is held right now. The mutex
//! is the lock the audio thread takes around a render period, so a model edit
//! that runs while it is held cannot be walked by the audio thread.
using ChangeMutexProbe = std::function<bool()>;

class GuardProbeSampleClip : public SampleClip
{
public:
	GuardProbeSampleClip(Track* track, ChangeMutexProbe probe) : SampleClip(track), m_probe(std::move(probe)) {}

	void restoreState(const QDomElement& element) override
	{
		m_restoreRanInsideGuard = m_probe();
		SampleClip::restoreState(element);
	}

	//! True when the change mutex was held while this clip was restored.
	bool restoreRanInsideGuard() const { return m_restoreRanInsideGuard; }

private:
	ChangeMutexProbe m_probe;
	bool m_restoreRanInsideGuard = false;
};

class GuardProbeSampleTrack : public SampleTrack
{
public:
	GuardProbeSampleTrack(Song* song, ChangeMutexProbe probe) : SampleTrack(song), m_probe(std::move(probe)) {}

	Clip* createClip(const TimePos& pos) override
	{
		auto* clip = new GuardProbeSampleClip(this, m_probe);
		clip->movePosition(pos);
		m_createRanInsideGuard = m_probe();
		m_lastClip = clip;
		return clip;
	}

	//! True when the change mutex was held while the replacement clip was made.
	bool createRanInsideGuard() const { return m_createRanInsideGuard; }
	//! True when the change mutex was held while the replacement clip was restored.
	bool restoreRanInsideGuard() const { return m_lastClip != nullptr && m_lastClip->restoreRanInsideGuard(); }
	void resetGuardObservations()
	{
		m_lastClip = nullptr;
		m_createRanInsideGuard = false;
	}

private:
	ChangeMutexProbe m_probe;
	GuardProbeSampleClip* m_lastClip = nullptr;
	bool m_createRanInsideGuard = false;
};

} // namespace

class ArrangeToolsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void cleanupTestCase();

	void theClipListFindsByName();

	void aTrackSwitchesBetweenVersions();

	void switchingTrackVersionTrimsTheName();

	void replacingTrackVersionKeepsModelGuardThroughClipRestore();

	void undoAndRedoOfTrackVersionSwitchKeepModelGuardThroughClipRestore();

	void savingAnotherVersionPreservesEditsToTheActiveVersion();

	void aVersionIsListedAndForgotten();

	void removingTrackVersionTrimsTheName();

	void sectionsAreSpansWithUndo();

	//! A replacement enforces the span checks add() does: a reversed or empty span, or one starting
	//! before the song, is refused and the stored section is left alone.
	void aSectionReplacementRefusesAnInvalidSpan();

	//! A negative span is refused at the command boundary and never reaches the store: 'start' has a
	//! schema minimum of 0, so section.add and section.set fail InvalidArgs and the section is unchanged.
	void aNegativeSectionSpanIsRefusedOverTheCommands();

	//! A section whose start or end is missing or not a number is refused, not silently read as tick
	//! zero: a hand-edited <section start="oops" end="192"/> used to load as a section from 0.
	void aMalformedSectionSpanIsNotReadAsTickZero();

	void aQuietStretchIsCutOut();

	//! ED-040: min_length counts TICKS, so it must be compared in the clip's own sample frames. A 96 kHz
	//! sample in a 48 kHz engine holds twice the frames per tick, and the engine's scalar halved the
	//! threshold - cutting a stretch half as long as was asked for.
	void aQuietStretchIsMeasuredInTheClipsOwnRate();

	//! ED-040: a minimum duration in ticks must include a partial source frame.
	void aQuietStretchShorterThanOneTickIsNotRemoved();

	//! ED-036: a 'track' that names nothing must not read as an empty track. automation.get_state
	//! (ControlCommandsAutomation.cpp:129) already refuses a filter that matched nothing.
	void aClipListRefusesATrackThatIsNotThere();

	void noTransientWithoutAudio();

	void tabMovesToTheNextTransient();

	void nextTransientDoesNotLeaveTheVisibleClip();

	void aClipBecomesAScore();

	//! The suffix check must not care about case: '.MUSICXML' and '.XML' are the same
	//! file types, and a client that names its score 'score.XML' is not wrong.
	void theScoreSuffixIsCaseInsensitive();

	//! W6: notes that start together but end apart are written as voices - the short one in voice 1, the long one in
	//! voice 2 after a <backup> to the bar line - so each keeps its own duration (it used to be cut to the chord's).
	void aChordOfMixedLengthsBecomesVoices();

	void notesRotateInsideTheClip();

private:
	//! Tells whether the audio engine's change mutex is held. A second thread
	//! tries to take it without blocking: only a free mutex can be taken, so
	//! the answer is the state of the mutex at that instant - the probe never
	//! waits and never races a timer.
	bool changeMutexIsHeld()
	{
		std::recursive_mutex& changeMutex = Engine::audioEngine()->m_changeMutex;
		std::promise<bool> taken;
		std::future<bool> answer = taken.get_future();
		std::thread worker([&changeMutex, &taken] {
			const bool locked = changeMutex.try_lock();
			if (locked) { changeMutex.unlock(); }
			taken.set_value(locked);
		});
		const bool locked = answer.get();
		worker.join();
		return !locked;
	}
};

#include "ArrangeToolsTestParts1.h"

QTEST_MAIN(ArrangeToolsTest)
#include "ArrangeToolsTest.moc"
