/*
 * Song.cpp - root of the model tree
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "Song.h"
#include <QTextStream>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QMessageBox>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QThread>

#include <algorithm>
#include <functional>
#include <cmath>

#include "ControlRoom.h"
#include "ProjectSectionRegistry.h"
#include "AutomationLatency.h"
#include "AutomationRamp.h"
#include "AutomationSwitches.h"
#include "AutomationTrack.h"
#include "AutomationEditor.h"
#include "ConfigManager.h"
#include "DocumentIndex.h"
#include "ControllerRackView.h"
#include "ControllerConnection.h"
#include "UnattendedRun.h"
#include "UnclaimedElements.h"
#include "EnvelopeAndLfoParameters.h"
#include "Mixer.h"
#include "MixerView.h"
#include "GuiApplication.h"
#include "ExportFilter.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Keymap.h"
#include "NotePlayHandle.h"
#include "NoteRandom.h"
#include "MidiClip.h"
#include "MidiPlayback.h"
#include "MidiClock.h"
#include "LinkAudioFollower.h"
#include "PatternEditor.h"
#include "PatternStore.h"
#include "RealtimeScope.h"
#include "PatternTrack.h"
#include "PianoRoll.h"
#include "ProjectJournal.h"
#include "ProjectIds.h"
#include "ProjectNotes.h"
#include "ProvenanceSection.h"
#include "UndoHistory.h"
#include "RecordOptions.h"
#include "RetroMidiCapture.h"
#include "SessionAudioCells.h"
#include "ProjectSection.h"
#include "Scale.h"
#include "SongEditor.h"
#include "TakeLane.h"
#include "PeakController.h"


namespace lmms
{

// The load walk adopts at most ONE <z:provenance> element (restoreNamedSection). A second one - a file a
// 0.4 build wrote into after refusing the first as unclaimed (two provenance elements in one file) - is kept
// verbatim as unclaimed and re-emitted on save: adopting it would REPLACE the first's entries in memory
// (Section::load clears first), and the next save would lose them. Reset at the start of every load walk.
static bool s_provenanceAdoptedThisLoad = false;
static std::atomic<quint64> s_nextDocumentIdentity{0};

#ifdef LMMS_TESTING
namespace
{
void (*s_tempoMapMemoBoundaryHookForTest)(Song*) = nullptr;
}

void setTempoMapMemoBoundaryHookForTest(void (*hook)(Song*)) noexcept
{
	s_tempoMapMemoBoundaryHookForTest = hook;
}
#endif

static quint64 nextDocumentIdentity()
{
	return s_nextDocumentIdentity.fetch_add(1, std::memory_order_relaxed) + 1;
}

tick_t TimePos::s_ticksPerBar = DefaultTicksPerBar;


Song::Song() :
	TrackContainer(),
	m_globalAutomationTrack( dynamic_cast<AutomationTrack *>(
				Track::create( Track::Type::HiddenAutomation,
								this ) ) ),
	m_tempoModel( DefaultTempo, MinTempo, MaxTempo, this, tr( "Tempo" ) ),
	m_timeSigModel( this ),
	m_oldTicksPerBar( DefaultTicksPerBar ),
	m_masterVolumeModel( 100, 0, 200, this, tr( "Master volume" ) ),
	m_masterPitchModel( 0, -12, 12, this, tr( "Master pitch" ) ),
	m_nLoadingTrack( 0 ),
	m_documentIdentity(nextDocumentIdentity()),
	m_fileName(),
	m_oldFileName(),
	m_modified( false ),
	m_loadOnLaunch( true ),
	m_recording( false ),
	m_exporting( false ),
	m_exportLoop( false ),
	m_renderBetweenMarkers( false ),
	m_playing( false ),
	m_paused( false ),
	m_savingProject( false ),
	m_loadingProject( false ),
	m_isCancelled( false ),
	m_playMode( PlayMode::None ),
	m_length( 0 ),
	m_exportLengthOverrideBars( 0 ),
	m_exportTailBars( 1 ),
	m_midiClipToPlay( nullptr ),
	m_loopMidiClip( false ),
	m_loopRenderCount(1),
	m_loopRenderRemaining(1),
	m_oldAutomatedValues()
{
	auto* automationWriteTimer = new QTimer(this);
	automationWriteTimer->setObjectName(QStringLiteral("automationWriteDrainTimer"));
	automationWriteTimer->setInterval(10);
	connect(automationWriteTimer, &QTimer::timeout, this, &Song::applyPendingAutomationWrites);
	automationWriteTimer->start();

	// Frozen takes keep the committed tempo map in step with every publication (control thread, render period
	// held), so a pass whose pinned read loses to a publication still places the take on the current map.
	m_tempoMap.setPublishHook( []( void* owner, const TempoMap& map )
	{
		auto* song = static_cast<Song*>( owner );
		AudioEngine* audio = Engine::audioEngine();
		if( audio == nullptr ) { return; }
		const auto renderLock = audio->requestChangesGuard();
		for( Track* track : song->tracks() ) { track->adoptTempoMapForFrozenTake( map ); }
	}, this );

	// Reserved here, on the constructing thread, so processAutomations never has to grow them.
	m_oldAutomatedValues.reserve(AutomationRenderScratch::RenderCapacity);
	m_recordedModels.reserve(AutomationRenderScratch::RenderCapacity);
	// A model being destroyed reports itself here, before anything in this
	// object is torn down (see Song::forgetAutomatedModel and
	// s_automationCacheSong): the automation cache holds the models automated on
	// the last frame as raw pointers, and the hook needs a song it can reach.
	s_automationCacheSong = this;
	// Keep this runtime-only identity off disk but available to default output paths.
	setProperty("_zeneUnsavedProjectId", QUuid::createUuid().toString(QUuid::WithoutBraces));

	// 0.5.0 (review W3-batchA #1): the project sections and the singletons the audio thread reads, built here, on
	// the thread that builds the song, in every mode - not on the audio thread, and not only once the control
	// registry happens to exist (ProjectSectionRegistry.cpp).
	projectsections::registerAll();

	// R7.2 (RealtimeSanitizer, hosted run 36789477810): MidiClock is a lazily built singleton, and the
	// first processNextBuffer() used to build it - a `new` on the audio thread. It is built here, on
	// the thread that builds the song. m_periodTracks is the per-period track list's storage, reserved
	// once so a period reuses it instead of allocating a copy of tracks().
	LinkAudioFollower::initialize();
	MidiClock::instance();
	m_periodTracks.reserve(256);
	// ... and grown here, before a track past it could make a period's assign() allocate (review L19 #1): on the
	// thread adding the track, under the changes guard the period runs under, so no period sees it move.
	connect(this, &TrackContainer::trackAdded, this, [this] {
		if (tracks().size() <= m_periodTracks.capacity()) { return; }
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		m_periodTracks.reserve(2 * tracks().size());
	}, Qt::DirectConnection);

	connect( &m_tempoModel, SIGNAL(dataChanged()),
			this, SLOT(setTempo()), Qt::DirectConnection );
	connect( &m_tempoModel, SIGNAL(dataUnchanged()),
			this, SLOT(setTempo()), Qt::DirectConnection );
	connect( &m_timeSigModel, SIGNAL(dataChanged()),
			this, SLOT(setTimeSignature()), Qt::DirectConnection );


	connect( Engine::audioEngine(), SIGNAL(sampleRateChanged()), this,
						SLOT(updateFramesPerTick()));

	connect( &m_masterVolumeModel, SIGNAL(dataChanged()),
			this, SLOT(masterVolumeChanged()), Qt::DirectConnection );
/*	connect( &m_masterPitchModel, SIGNAL(dataChanged()),
			this, SLOT(masterPitchChanged()));*/

	qRegisterMetaType<lmms::Note>( "lmms::Note" );
	setType( Type::Song );

	for (auto& scale : m_scales) {scale = std::make_shared<Scale>();}
	for (auto& keymap : m_keymaps) {keymap = std::make_shared<Keymap>();}

	// Aggregate the `positionJumped` signals from all the timelines into a single `playbackPositionJumped` signal for
	// other objects (sample tracks, LFOs, etc) to use.
	for (auto i = std::size_t{0}; i < static_cast<std::size_t>(PlayMode::Count); ++i)
	{
		const auto onPositionJumped = [this, playMode = static_cast<PlayMode>(i)] {
			// Only emit the signal when the song is actually playing and the active timeline jumps
			// This prevents LFOs from changing phase when the user drags the timeline while paused
			if (m_playing && m_playMode == playMode) { emit playbackPositionJumped(); }
		};

		// DIRECT, on the thread that moved the timeline (BUGS_FOUND 11.28). The timeline
		// jumps on the RENDER thread too, and the default connection QUEUED this lambda to
		// the Song's thread: it then ran at an arbitrary later moment - mid-export, under
		// load - and every SampleClip's handler removed its track's play handles there, so
		// a clip restarted at whichever period the GUI thread happened to reach and an
		// offline render was not reproducible (MasteringTest, hosted macos-x86_64 run 13).
		// Every listener of playbackPositionJumped already connects to it directly.
		connect(&m_timelines[i], &Timeline::positionJumped, this, onPositionJumped, Qt::DirectConnection);
	}
	connect(&getTimeline(PlayMode::Song), &Timeline::loopPointsChanged, this,
		[this] { updateFrozenTakeVoiceCapacities(); }, Qt::DirectConnection);
	connect(&getTimeline(PlayMode::Song), &Timeline::loopEnabledChanged, this,
		[this](bool) { updateFrozenTakeVoiceCapacities(); }, Qt::DirectConnection);

	// R2.4: the song timeline's punch region, published to the render thread's
	// gate every time it changes (a set, a clear, a load, an undo).
	const auto publishPunch = [this] {
		const Timeline& timeline = getTimeline(PlayMode::Song);
		Engine::audioEngine()->setPunchWindow(timeline.punchArmed(), timeline.punchBegin(), timeline.punchEnd());
	};
	connect(&getTimeline(PlayMode::Song), &Timeline::punchChanged, this, publishPunch);

	// Inform VST plugins if the user moved the play head
	connect(this, &Song::playbackPositionJumped, this, [this](){ m_vstSyncController.setPlaybackJumped(true); }, Qt::DirectConnection);
}

void queueAutomationClipWrite(AutomationClip* clip, const TimePos& time, float value) noexcept
{
	clip->queuePendingWrite(static_cast<tick_t>(time.getTicks()), value);
}


//! Drops the queued writes of every clip: they belong to a project that is
//! being replaced and must not reach the project that replaces it.
void Song::discardPendingAutomationWrites()
{
	ProjectJournal* const journal = Engine::projectJournal();
	if (journal == nullptr) { return; }
	for (JournallingObject* object : journal->journallingObjects())
	{
		auto* clip = dynamic_cast<AutomationClip*>(object);
		if (clip != nullptr) { clip->discardPendingWrites(); }
	}
}




//! The control thread's half of the deferred automation write (cluster 5388).
//! The render thread queued the writes it wanted in the clips it wrote to;
//! here they are applied, in the order they were queued.
void Song::applyPendingAutomationWrites()
{
	// Nothing queued: the drain has no work and does not touch the engine.
	if (AutomationClip::pendingWriteTotal() <= 0) { return; }

	// The clips that have queued writes are found through the project journal,
	// which knows every clip of the session - the ones on the song's tracks,
	// the ones on the pattern store's, and the inline automation clip of a
	// note, which is on no track at all. The walk is a control-thread walk
	// over the journal's own map, so it runs without the render guard: only
	// the edits below need the render thread out of the way.
	ProjectJournal* const journal = Engine::projectJournal();
	if (journal == nullptr) { return; }

	std::vector<AutomationClip*> clips;
	for (JournallingObject* object : journal->journallingObjects())
	{
		auto* clip = dynamic_cast<AutomationClip*>(object);
		if (clip != nullptr && clip->pendingWriteCount() != 0) { clips.push_back(clip); }
	}
	if (clips.empty()) { return; }

	AudioEngine* const audio = Engine::audioEngine();
	if (audio == nullptr) { return; }
	const auto renderLock = audio->requestChangesGuard();
	for (AutomationClip* clip : clips) { clip->applyPendingWrites(); }
}

Song::~Song()
{
	// From this line on, a model that is destroyed cannot report into the
	// automation cache any more. It has to happen here, in the destructor body,
	// because the song's own models (m_tempoModel and friends, the metronome's)
	// are declared before `m_oldAutomatedValues` and are therefore destroyed
	// after it - their destructors would otherwise reach for a map that is
	// already gone. See `s_automationCacheSong`.
	s_automationCacheSong = nullptr;

	m_playing = false;
	delete m_globalAutomationTrack;
}



void Song::updateFrozenTakeVoiceCapacities()
{
	if (QThread::currentThread() != thread())
	{
		QMetaObject::invokeMethod(this, [this] { updateFrozenTakeVoiceCapacities(); }, Qt::QueuedConnection);
		return;
	}

	const auto renderLock = Engine::audioEngine()->requestChangesGuard();
	const Timeline& timeline = getTimeline(PlayMode::Song);
	for (Track* track : tracks())
	{
		track->updateFrozenVoiceCapacity(timeline.loopEnabled(), timeline.loopBegin().getTicks(),
			timeline.loopEnd().getTicks());
	}
}


Song* Song::s_automationCacheSong = nullptr;


void Song::forgetAutomatedModel( AutomatableModel* model )
{
	AudioEngine* const audio = Engine::audioEngine();
	const auto renderLock = audio == nullptr
		? AudioEngine::RequestChangesGuard{}
		: audio->requestChangesGuard();
	if( s_automationCacheSong != nullptr )
	{
		auto& cache = s_automationCacheSong->m_oldAutomatedValues;
		cache.erase( std::remove_if( cache.begin(), cache.end(),
			[model]( const auto& entry ) { return entry.first == model; } ), cache.end() );
	}
}




void Song::masterVolumeChanged()
{
	Engine::audioEngine()->setMasterGain( m_masterVolumeModel.value() / 100.0f );
}




void Song::setTempo()
{
	AudioEngine* const audio = Engine::audioEngine();
	audio->requestChangeInModel();
	const auto tempo = (bpm_t)m_tempoModel.value();
	PlayHandleList & playHandles = audio->playHandles();
	for (const auto& playHandle : playHandles)
	{
		auto nph = dynamic_cast<NotePlayHandle*>(playHandle);
		if( nph && !nph->isReleased() )
		{
			nph->lock();
			nph->resize( tempo );
			nph->unlock();
		}
	}
	// With a tempo map in force, the global model is only the tempo of the
	// region BEFORE the map's first event. Reset the follower's memo before
	// releasing the guard that excludes followTempoMap().
	if( m_tempoMap.map().active() ) { m_tempoMapAppliedTempo.store(-1, std::memory_order_relaxed); }
	Engine::updateFramesPerTick();
	audio->doneChangeInModel();

	#ifdef LMMS_TESTING
	if( s_tempoMapMemoBoundaryHookForTest != nullptr )
	{
		s_tempoMapMemoBoundaryHookForTest( this );
	}
	#endif

	m_vstSyncController.setTempo( tempo );

	emit tempoChanged( tempo );
}




void Song::setTimeSignature()
{
	TimePos::setTicksPerBar( ticksPerBar() );
	emit timeSignatureChanged( m_oldTicksPerBar, ticksPerBar() );
	emit dataChanged();
	m_oldTicksPerBar = ticksPerBar();

	m_vstSyncController.setTimeSignature(
		getTimeSigModel().getNumerator(), getTimeSigModel().getDenominator() );
}




void Song::savePlayStartPosition()
{
	getTimeline().setPlayStartPosition(getPlayPos());
}




void Song::processNextBuffer()
{
	// Automation modes (post-alpha/automation-modes): this is where the render
	// thread observes the transport's own start and stop edges. Both the mode
	// state machine (AutomatableModel) and the pass it arms are driven from the
	// token published here; an offline render reports "not running", so an
	// export can never write automation into a project.
	AutomatableModel::observeAutomationTransport( m_playing && !m_exporting );

	// MIDI clock (0.3.0, the `clock.*` group's engine half). The DAW as a clock
	// MASTER emits 24 pulses to the quarter note, START/STOP/CONTINUE and a Song
	// Position Pointer from the audio thread, and the SLAVE's measurement is
	// aged here so a clock that stops arriving loses its lock. It runs once per
	// audio period - BEFORE the transport gate below - because STOP is an EDGE
	// and a stopped transport is exactly when it has to be sent; the Session
	// View scheduler above runs before the same gate for the same reason. With
	// no master and no slave enabled this is one relaxed atomic load and a
	// return, so a session that does not use the clock runs the block it always
	// ran. The clock's own output goes through the engine's existing MIDI path
	// (MidiClient::processOutEvent through the clock's own MidiPort), which is
	// the path a track's MIDI output already uses from this same thread.
	if( AudioEngine* audioEngine = Engine::audioEngine() )
	{
		MidiClock::instance()->processAudioPeriod(
			audioEngine->framesPerPeriod(),
			m_playing && !m_exporting,
			static_cast<qint64>( getPlayPos( PlayMode::Song ).getTicks() ),
			MidiClock::nowNs() );
	}

	// Session View launch scheduling (task #595, SPEC-zene-studio A2/A3). The
	// session has its own clock domain, so this runs every audio period -
	// before the transport gate below - and launches can be scheduled while
	// the song is stopped. It is lock- and allocation-free: one bounded drain
	// of a fixed-size command queue and one pass over fixed slot storage (see
	// SessionSchedulerTest::audioThreadPathDoesNotAllocate).
	{
		SessionClockContext sessionClock;
		sessionClock.positionTicks = getPlayPos(PlayMode::Song).getTicks();
		sessionClock.ticksPerBar = ticksPerBar();
		sessionClock.framesPerTick = Engine::framesPerTick();
		sessionClock.transportRunning = m_playing && m_playMode == PlayMode::Song;
		m_sessionScheduler.processAudio(sessionClock,
			Engine::audioEngine()->framesPerPeriod());
		// 0.5.0 L02 (WF-055): a launcher cell recording audio takes this period's input, after the
		// launch events above decided whether it plays. One atomic load while nothing records.
		AudioEngine* engine = Engine::audioEngine();
		m_sessionScheduler.audioRecorder().push(engine->inputBuffer(), engine->inputBufferFrames(),
			engine->inputStreamStarted(), engine->framesPerPeriod());
	}

	// Ableton Link (docs/LINK-SYNC.md section 7): an engaged session follower sets this block's
	// frame/tick rate and keeps the play head on the session's beat grid, after the tempo map has had
	// its say. Hold is the count-in to the session's phase (nothing renders, nothing advances); Released
	// hands the rate back to the engine's own tempo. With sync off this is one relaxed atomic load.
	const auto followLink = [this](bool rolling) {
		const LinkPeriod verdict = followLinkSession(*this, rolling);
		if (verdict == LinkPeriod::Released) { m_tempoMapAppliedTempo = -1; Engine::updateFramesPerTick(); }
		return verdict != LinkPeriod::Hold;
	};

	// If nothing is playing, there is nothing to do (the Link follower still sees the stop)
	if (!m_playing) { followLink(false); return; }

	// The tempo map, if one is in force, sets the frame/tick rate this block
	// runs at - BEFORE the value is captured below (docs/TEMPO-MAP.md section 4).
	followTempoMap();
	if (!followLink(!m_exporting && m_playMode == PlayMode::Song)) { return; }

	// The modulation layer (#602, docs/MODULATION.md): one pass per block,
	// reading the publisher's lock-free snapshot. It returns before copying
	// anything unless the layer is non-empty, so a project with no modulator
	// runs exactly the block it has always run. Placed beside followTempoMap()
	// because both are per-block readers of control-thread state.
	processModulation();

	// Retrospective MIDI capture (owner item 14, docs/MIDI-RETRO-CAPTURE.md): publish
	// this period's transport tick once, so the raw MIDI clients can stamp a captured
	// event without reading live Song state from the MIDI thread - getPlayPos() hands
	// out a reference into that state and include/Song.h holds no atomics. One relaxed
	// store per rendered period, and the same value is not published while stopped, so
	// a capture that is armed but not rolling stamps nothing new. Placed after
	// followTempoMap() so the tick published is the one this block's tempo map sets.
	RetroMidiCapture::publishTick( getPlayPos().getTicks() );

	// At the beginning of the song, we have to reset the LFOs
	if (m_playMode == PlayMode::Song && getPlayPos() == 0)
	{
		EnvelopeAndLfoParameters::instances()->reset();
	}

	// Reused storage (see the constructor): clear() and assign keep the capacity, so a period does
	// not allocate and free a copy of the track list (RealtimeSanitizer finding, R7.2).
	TrackList& trackList = m_periodTracks;
	trackList.clear();
	int clipNum = -1; // The number of the clip that will be played

	// Determine the list of tracks to play and the clip number
	switch (m_playMode)
	{
		case PlayMode::Song:
			trackList.assign(tracks().begin(), tracks().end());
			break;

		case PlayMode::Pattern:
			if (Engine::patternStore()->numOfPatterns() > 0)
			{
				clipNum = Engine::patternStore()->currentPattern();
				trackList.push_back(PatternTrack::findPatternTrack(clipNum));
			}
			break;

		case PlayMode::MidiClip:
			if (m_midiClipToPlay)
			{
				clipNum = m_midiClipToPlay->getTrack()->getClipNum(m_midiClipToPlay);
				trackList.push_back(m_midiClipToPlay->getTrack());
			}
			break;

		default:
			return;
	}

	auto& timeline = getTimeline();

	const auto loopEnabled = !m_exporting && timeline.loopEnabled();

	// Ensure playback begins within the loop if it is enabled (a correction, not a wrap: a seek or a
	// start put the position outside the loop)
	if (loopEnabled) { enforceLoop(timeline.loopBegin(), timeline.loopEnd(), false); }

	const auto framesPerTick = Engine::framesPerTick();
	const auto framesPerPeriod = Engine::audioEngine()->framesPerPeriod();
	// 0.5.0 MX-160: one latency snapshot pins every clip's latency for the WHOLE period (ramps and
	// per-tick reads alike), so a refresh publishing new latencies mid-period cannot mix banks here.
	const auto latencySnapshot = AutomationLatency::instance().readSnapshot();

	// Sample-accurate automation (feature row 9). BEFORE the tick loop, because
	// the whole block's curve has to be in the parameters before the first
	// sample of it is rendered - that is the difference between a per-sample
	// ramp and the per-tick writes the loop below makes. It walks only the
	// clips that opted in (automation.ramp_set), plus default Discrete clips
	// whose latency compensation needs the frame offset that per-tick reads lose.
	buildAutomationRamps(trackList, getPlayPos(), framesPerPeriod,
		static_cast<int>(timeline.frameOffset()), latencySnapshot);

	f_cnt_t frameOffsetInPeriod = 0;

	while (frameOffsetInPeriod < framesPerPeriod)
	{
		auto frameOffsetInTick = timeline.frameOffset();

		// If a whole tick has elapsed, update the frame and tick count, and check any loops
		if (frameOffsetInTick >= framesPerTick)
		{
			// Transfer any whole ticks from the frame count to the tick count
			const auto elapsedTicks = static_cast<int>(frameOffsetInTick / framesPerTick);
			frameOffsetInTick -= elapsedTicks * framesPerTick;
			timeline.incrementTicks(elapsedTicks);
			timeline.setFrameOffset(frameOffsetInTick);

			// If we are playing a pattern track, or a MIDI clip with no loop enabled,
			// loop back to the beginning when we reach the end
			if (m_playMode == PlayMode::Pattern)
			{
				enforceLoop(TimePos{0}, TimePos{Engine::patternStore()->lengthOfCurrentPattern(), 0}, true);
			}
			else if (m_playMode == PlayMode::MidiClip && m_loopMidiClip && !loopEnabled)
			{
				enforceLoop(-m_midiClipToPlay->startTimeOffset(), m_midiClipToPlay->length() - m_midiClipToPlay->startTimeOffset(), true);
			}

			// Handle loop points, and inform VST plugins of the loop status
			if (loopEnabled || (m_loopRenderRemaining > 1 && getPlayPos() >= timeline.loopBegin()))
			{
				m_vstSyncController.startCycle(
					timeline.loopBegin().getTicks(), timeline.loopEnd().getTicks());

				// Loop if necessary, and decrement the remaining loops if we did
				if (enforceLoop(timeline.loopBegin(), timeline.loopEnd(), true)
					&& m_loopRenderRemaining > 1)
				{
					m_loopRenderRemaining--;
				}
			}
			else
			{
				m_vstSyncController.stopCycle();
			}
		}

		const f_cnt_t framesUntilNextPeriod = framesPerPeriod - frameOffsetInPeriod;
		const auto framesUntilNextTick = static_cast<f_cnt_t>(std::ceil(framesPerTick - frameOffsetInTick));

		// We want to proceed to the next buffer or tick, whichever is closer
		const auto framesToPlay = std::min(framesUntilNextPeriod, framesUntilNextTick);

		if (frameOffsetInPeriod == 0)
		{
			// First frame of buffer: update VST sync position.
			// This must be done after we've corrected the frame/tick count,
			// but before actually playing any frames.
			m_vstSyncController.setAbsolutePosition(getPlayPos().getTicks()
				+ timeline.frameOffset() / static_cast<double>(framesPerTick));
			m_vstSyncController.update();
		}

		if (static_cast<f_cnt_t>(frameOffsetInTick) == 0)
		{
			// First frame of tick: process automation and play tracks
			MidiPlayback::instance().tickPlayed(getPlayPos().getTicks(), m_loopWraps.load(std::memory_order_relaxed));  // 0.5.0 L07/W2: chase and the loop pass
			processAutomations(trackList, getPlayPos(), framesToPlay, latencySnapshot);
			processMetronome(frameOffsetInPeriod);

			// Column index of the next track in the song's own track list;
			// only meaningful in PlayMode::Song, which is the only mode this
			// test is applied in.
			int sessionTrackIndex = 0;
			for (const auto track : trackList)
			{
				// SPEC-zene-studio A1: a track's session and arrangement
				// content are mutually exclusive. While a session clip on this
				// column is playing, the track is taken over and its
				// arrangement content must not play.
				const int trackIndex = sessionTrackIndex++;
				if (m_playMode == PlayMode::Song
					&& m_sessionScheduler.trackIsSessionActive(trackIndex))
				{
					// Board card #597: the A1 take-over used to be the whole of
					// the session behaviour - the column was silenced and
					// nothing played in its place. Now the cell's published
					// content is rendered at its own loop position, through the
					// pattern store (the store owns the instruments a pattern's
					// notes belong to). A cell with no published content - an
					// empty slot - leaves patternId < 0 and keeps the old
					// silence, which is what makes "the launch fired" separable
					// from "there was content to play".
					const SessionSlotPlayback played =
						m_sessionScheduler.playbackForColumn(trackIndex);
					if (played.patternId >= 0)
					{
						Engine::patternStore()->play(TimePos(played.positionTicks),
							framesToPlay, frameOffsetInPeriod, played.patternId);
					}
					continue;
				}
				track->play(getPlayPos(), framesToPlay, frameOffsetInPeriod, clipNum);
			}
		}

		// Update frame counters
		frameOffsetInPeriod += framesToPlay;
		frameOffsetInTick += framesToPlay;
		timeline.setFrameOffset(frameOffsetInTick);
	}

	// Reset the jumped state after processing this buffer, since presumably it has now been handled.
	m_vstSyncController.setPlaybackJumped(false);
}


bool Song::enforceLoop(const TimePos& begin, const TimePos& end, bool wrapping)
{
	const bool playedToEnd = getPlayPos() >= end;
	if (!playedToEnd && getPlayPos() >= begin) { return false; }
	// 0.5.0 W2: counted BEFORE setTicks announces the jump, so a listener of that jump already sees the
	// wrap; an atomic add, nothing else, on the render thread.
	if (wrapping && playedToEnd) { m_loopWraps.fetch_add(1, std::memory_order_release); }
	getTimeline().setTicks(begin.getTicks());
	return true;
}


void Song::processAutomations(const TrackList &tracklist, TimePos timeStart, f_cnt_t,
	const AutomationLatency::ReadSnapshot& latencySnapshot)
{
	TrackContainer* container = this;
	int clipNum = -1;

	switch (m_playMode)
	{
	case PlayMode::Song:
		break;
	case PlayMode::Pattern:
	{
		if (tracklist.empty()) { return; }
		Q_ASSERT(tracklist.at(0)->type() == Track::Type::Pattern);
		auto patternTrack = dynamic_cast<PatternTrack*>(tracklist.at(0));
		container = Engine::patternStore();
		clipNum = patternTrack->patternIndex();
	}
		break;
	default:
		return;
	}

	// Everything this tick builds lives in the Song's scratch, reserved in the constructor: the read runs on
	// the audio thread, where RealtimeSanitizer reported the vector and map it used to allocate.
	// A read that overflowed the reserved scratch is incomplete. It is redone on an unbounded one (these
	// stay empty, and allocate nothing, unless that happens): late, but never wrong.
	AutomationRenderScratch spillValues;
	AutomationRenderScratch spillInner;
	AutomationRenderScratch spillClips;
	AutomationRenderScratch* valuesIn = &m_automationLevel;
	container->fillAutomatedValues(*valuesIn, &m_automationInner, timeStart, clipNum, latencySnapshot);
	if (valuesIn->overflows() != 0 || m_automationInner.overflows() != 0)
	{
		m_automationScratchOverflows.fetch_add(1, std::memory_order_relaxed);
		valuesIn->takeOverflows();
		m_automationInner.takeOverflows();
		container->fillAutomatedValues(spillValues, &spillInner, timeStart, clipNum, latencySnapshot);
		valuesIn = &spillValues;
	}
	AutomationRenderScratch& values = *valuesIn;
	const TrackList& tracks = container->tracks();

	AutomationRenderScratch* clipsIn = &m_automationRecordClips;
	clipsIn->clear();
	for (Track* track : tracks)
	{
		if (track->type() == Track::Type::Automation) {
			clipsIn->insertClipsInRange(*track, 0, timeStart);
		}
	}
	if (clipsIn->overflows() != 0)
	{
		m_automationScratchOverflows.fetch_add(1, std::memory_order_relaxed);
		clipsIn->takeOverflows();
		for (Track* track : tracks)
		{
			if (track->type() == Track::Type::Automation) { spillClips.insertClipsInRange(*track, 0, timeStart); }
		}
		clipsIn = &spillClips;
	}
	AutomationRenderScratch& clips = *clipsIn;
	m_recordedModels.clear();

	// Process recording
	//
	// Automation modes (post-alpha/automation-modes): a control in Touch, Latch
	// or Write mode writes its own moves while the transport runs, whether or
	// not the clip's recording flag is set. This runs on the render thread: the
	// decision is a read of relaxed atomics (AutomatableModel::automationWantsWrite),
	// the clock is read at most once per tick and only when some control is
	// actually in a writing mode, so a session that never leaves Read pays
	// nothing here.
	const quint64 automationRun = AutomatableModel::automationTransportRun();
	qint64 automationNowNs = 0;
	bool automationClockRead = false;
	for (Clip* clip : clips.clips())
	{
		auto p = dynamic_cast<AutomationClip *>(clip);
		TimePos relTime = timeStart - p->startPosition();
		if (relTime >= 0 && relTime < p->length())
		{
			const AutomatableModel* recordedModel = p->firstObject();
			bool writes = p->isRecording();
			if( !writes && recordedModel
				&& recordedModel->automationMode() != AutomatableModel::AutomationMode::Read )
			{
				if( !automationClockRead )
				{
					automationNowNs = AutomatableModel::automationClockNs();
					automationClockRead = true;
				}
				writes = recordedModel->automationWantsWrite( automationRun, automationNowNs );
			}
			if( writes && AutomationSwitches::instance().writesAt( timeStart.getTicks() ) )  // 0.5.0 L06 (ED-200/201)
			{
				// The values in an automation clip are stored in un-un-scaled format, so if you
				// are automating a log knob, when you draw an curve, the values being stored are not the actual values the
				// knob will take, but instead the unscaled version of the unscaled numbers. The tooltip shows the number you expect, but if you double-click,
				// you can see that the true values are stored by their inverse scaled value....which is wrong, since they weren't scaled in the first place...?
				// Anyhow, in the meantime before we redo the automation system, when recording automations, we have to get the inverseScaledValue
				// and store that so that when playing it back, it scales the value correctly.
				// The write is queued in the clip itself and applied by the
				// control thread (cluster 5388): the render thread may not
				// edit the clip's node map, and a queued write is not lost
				// when the control thread is late.
				p->queuePendingWrite(relTime.getTicks(),
					recordedModel->inverseScaledValue(recordedModel->value<float>()));

				noteRecordedModel(recordedModel);
			}
		}
	}

	// Checks if an automated model stopped being automated by automation clip
	// so we can move the control back to any connected controller again. A model
	// destroyed while it was cached has already dropped itself out of this map
	// (AutomatableModel's destructor calls Song::forgetAutomatedModel), so no key
	// here is a stale pointer.
	for (const auto& entry : m_oldAutomatedValues)
	{
		AutomatableModel * am = entry.first;
		// A guarded key can be null here: the model was destroyed between the frame
		// that automated it and this restore (see the member's comment). There is
		// nothing to move back to a controller for a model that no longer exists.
		if (am != nullptr && !values.contains(am))
		{
			am->setUseControllerValue(true);
		}
	}
	m_oldAutomatedValues.clear();
	for (const AutomationRenderScratch::Entry& entry : values.entries())
	{
		// Past the reserved capacity this grows (an allocation, counted): dropping an entry would leave
		// a model on the automation's control with nothing to hand it back.
		if (m_oldAutomatedValues.size() >= m_oldAutomatedValues.capacity())
		{
			m_automationScratchOverflows.fetch_add(1, std::memory_order_relaxed);
		}
		m_oldAutomatedValues.emplace_back(entry.model, entry.value);
	}

	// Apply values
	for (const AutomationRenderScratch::Entry& entry : values.entries())
	{
		AutomatableModel* model = entry.model;
		bool isRecording = std::binary_search(m_recordedModels.begin(), m_recordedModels.end(),
			static_cast<const AutomatableModel*>(model), std::less<const AutomatableModel*>{});
		model->setUseControllerValue(isRecording);

		// Automation modes: a control in Off IGNORES its written automation -
		// the manual value stands - so this frame's apply is skipped for it.
		// The clip itself is untouched, and the mode is the only gate here:
		// an Off control that a gesture or the clip's own record flag is
		// writing to is still recording (that is what those two are for).
		if (!isRecording && model->automationMode() != AutomatableModel::AutomationMode::Off && AutomationSwitches::instance().reads())
		{
			/* TODO
			 * Remove scaleValue() from here when automation editor's
			 * Y axis can be set to logarithmic, and automation clips store
			 * the actual values, and not the invertedScaledValue.
			 */
			// Automation modes (post-alpha/automation-modes): the trim offset is
			// applied here, where the written automation is read out to the
			// control, and is never written back into the clip - which is what
			// makes it non-destructive.
			model->setValue(model->effectiveAutomationValue(model->scaledValue(entry.value)), true);
		}
	}

}

//! Remembers \a model as written to this tick (kept sorted for the lookup in the apply pass). A full list drops it.
void Song::noteRecordedModel(const AutomatableModel* model)
{
	const auto at = std::lower_bound(m_recordedModels.begin(), m_recordedModels.end(), model,
		std::less<const AutomatableModel*>{});
	if (at != m_recordedModels.end() && *at == model) { return; }
	// Past the reserved capacity this grows (counted); a recorded model must never be forgotten.
	if (m_recordedModels.size() >= m_recordedModels.capacity())
	{
		m_automationScratchOverflows.fetch_add(1, std::memory_order_relaxed);
	}
	m_recordedModels.insert(at, model);
}

void Song::countAutomationForTest(std::size_t* clips, std::size_t* targets) const
{
	*clips = 0;
	*targets = 0;
	const auto countTrack = [clips, targets](Track* track) {
		*clips += track->getClips().size();
		if (track->type() != Track::Type::Automation && track->type() != Track::Type::HiddenAutomation) { return; }
		for (Clip* clip : track->getClips())
		{
			if (auto* automation = dynamic_cast<AutomationClip*>(clip)) { *targets += automation->objects().size(); }
		}
	};
	for (Track* track : tracks()) { countTrack(track); }
	if (m_globalAutomationTrack != nullptr) { countTrack(m_globalAutomationTrack); }
	if (PatternStore* patterns = Engine::patternStore())
	{
		for (Track* track : patterns->tracks()) { countTrack(track); }
	}
}

void Song::noteAutomationGrowth(std::size_t clips, std::size_t targets) noexcept
{
	const auto clipBound = m_automationClipBound.fetch_add(clips, std::memory_order_relaxed) + clips;
	const auto targetBound = m_automationTargetBound.fetch_add(targets, std::memory_order_relaxed) + targets;
	if (std::max(clipBound, targetBound) + 64 <= m_automationReserved.load(std::memory_order_relaxed)) { return; }
	if (RealtimeScope::isActive() || Engine::audioEngine() == nullptr || Engine::audioEngine()->isRenderThread()) { return; }
	try { ensureAutomationScratchCapacity(); }
	catch (...) { /* out of memory: the read's overflow redo keeps the automation right */ }
}

void Song::ensureAutomationScratchCapacity()
{
	if (RealtimeScope::isActive()) { return; }
	// With the render locked out nothing reads or rewrites the lists being resized.
	const auto changes = Engine::audioEngine()->requestChangesGuard();
	std::size_t clips = 0;
	std::size_t targets = 0;
	countAutomationForTest(&clips, &targets);
	m_automationClipBound.store(clips, std::memory_order_relaxed);
	m_automationTargetBound.store(targets, std::memory_order_relaxed);

	const std::size_t need = std::max(clips, targets) + 64;
	if (need <= m_automationReserved.load(std::memory_order_relaxed)) { return; }
	// Headroom, so a project that keeps growing re-counts once per doubling, not once per edit.
	const std::size_t target = 2 * need;
	m_automationLevel.reserve(target);
	m_automationInner.reserve(target);
	m_automationRecordClips.reserve(target);
	m_oldAutomatedValues.reserve(target);
	m_recordedModels.reserve(target);
	m_automationReserved.store(std::min({m_automationLevel.capacity(), m_automationInner.capacity(),
		m_automationRecordClips.capacity(), m_oldAutomatedValues.capacity(), m_recordedModels.capacity()}),
		std::memory_order_relaxed);
}

void Song::resizeAutomationScratchForTest(std::size_t capacity)
{
	m_automationLevel = AutomationRenderScratch(capacity);
	m_automationInner = AutomationRenderScratch(capacity);
	m_automationRecordClips = AutomationRenderScratch(capacity);
	m_oldAutomatedValues.clear();
	m_oldAutomatedValues.shrink_to_fit();
	m_oldAutomatedValues.reserve(capacity);
	m_recordedModels.clear();
	m_recordedModels.shrink_to_fit();
	m_recordedModels.reserve(capacity);
	m_automationReserved.store(capacity, std::memory_order_relaxed);
}

void Song::processMetronome(size_t bufferOffset)
{
	const auto currentPlayMode = playMode();
	const auto supported = currentPlayMode == PlayMode::MidiClip
		|| currentPlayMode == PlayMode::Song
		|| currentPlayMode == PlayMode::Pattern;

	if (!supported || m_exporting) { return; } 
	// 0.5.0 L03 (WF-080): the count-in clicks whether the metronome is on or not.
	m_metronome.processTick(currentTick(), ticksPerBar(), m_timeSigModel.getNumerator(), bufferOffset,
		recordoptions::countingIn(currentTick()));
}

void Song::setModified(bool value)
{
	if( !m_loadingProject && m_modified != value)
	{
		m_modified = value;
		emit modified();
	}
}

bool Song::isExportDone() const
{
	return !isExporting() || getPlayPos() >= m_exportSongEnd;
}

int Song::getExportProgress() const
{
	TimePos pos = getPlayPos();
    
	if (pos >= m_exportSongEnd)
	{
		return 100;
	}
	else if (pos <= m_exportSongBegin)
	{
		return 0;
	}
	else if (pos >= m_exportLoopEnd)
	{
		pos = (m_exportLoopBegin-m_exportSongBegin) + (m_exportLoopEnd - m_exportLoopBegin) *
			m_loopRenderCount + (pos - m_exportLoopEnd);
	}
	else if ( pos >= m_exportLoopBegin )
	{
		pos = (m_exportLoopBegin-m_exportSongBegin) + ((m_exportLoopEnd - m_exportLoopBegin) *
			(m_loopRenderCount - m_loopRenderRemaining)) + (pos - m_exportLoopBegin);
	}
	else
	{
		pos = (pos - m_exportSongBegin);
	}

	return (float)pos/(float)m_exportEffectiveLength*100.0f;
}

void Song::playSong()
{
	startSongPlayback( false );
}




void Song::startSongPlayback( bool recording )
{
	// Record pressed before this Play (BUGS P2): the press arms the pass this Play starts.
	const bool latched = m_recordLatched;
	m_recordLatched = false;
	m_recording = false;

	if( isStopped() == false )
	{
		stop();
	}

	// BUGS_FOUND 11.17: the render thread must not see the transport running before the
	// playbackStateChanged slots below have run - SampleClip's resets every sample clip's
	// playing flag and removes its play handles (Qt::DirectConnection), so a period that
	// rendered in between started the clips, lost them, and restarted them a tick later;
	// and playAndRecord's flag was set after this returned, so a period in THAT gap marked
	// a record clip as playing without starting its handle and the take stayed empty. The
	// model lock is recursive, so the slots' own requestChangeInModel() nests.
	Engine::audioEngine()->requestChangeInModel();
	if( recording || latched ) { m_recording = true; }

	m_playMode = PlayMode::Song;
	m_playing = true;
	m_paused = false;
	updateFrozenTakeVoiceCapacities();

	m_vstSyncController.setPlaybackState( true );

	savePlayStartPosition();

	emit playbackStateChanged();
	Engine::audioEngine()->doneChangeInModel();
}




void Song::record()
{
	m_recording = true;
	m_recordLatched = true;
	// TODO: Implement
}




void Song::playAndRecord()
{
	// A running transport stops first: startSongPlayback's own stop() would end the pass begun below (its
	// count-in, its punch gate) and move the play position off the pre-roll (review r4 L19 #22).
	if( isStopped() == false )
	{
		stop();
	}
	// 0.5.0 L03 (WF-080): a count-in or pre-roll starts the pass early, captured from here.
	const tick_t start = recordoptions::beginPass(this);
	startSongPlayback( true );
	// Stop goes back to where Record was pressed, not to the start of the count-in (review #4).
	if (start >= 0) { getTimeline(PlayMode::Song).setPlayStartPosition(TimePos(start)); }
}




void Song::playPattern()
{
	if( isStopped() == false )
	{
		stop();
	}

	m_playMode = PlayMode::Pattern;
	m_playing = true;
	m_paused = false;

	m_vstSyncController.setPlaybackState( true );

	savePlayStartPosition();

	emit playbackStateChanged();
}




void Song::playMidiClip( const MidiClip* midiClipToPlay, bool loop )
{
	if( isStopped() == false )
	{
		stop();
	}

	m_midiClipToPlay = midiClipToPlay;
	m_loopMidiClip = loop;

	if( m_midiClipToPlay != nullptr )
	{
		m_playMode = PlayMode::MidiClip;
		m_playing = true;
		m_paused = false;
	}

	savePlayStartPosition();

	emit playbackStateChanged();
}




void Song::updateLength()
{
	if (m_loadingProject) { return; }

	m_length = 0;
	m_tracksMutex.lockForRead();
	for (auto track : tracks())
	{
		if (m_exporting && track->isMuted())
		{
			continue;
		}

		const bar_t cur = track->length();
		if( cur > m_length )
		{
			m_length = cur;
		}
	}
	m_tracksMutex.unlock();

	emit lengthChanged( m_length );
}




void Song::togglePause()
{
	// Pause/unpause only works when something is actually playing
	if (m_playMode == PlayMode::None)
	{
		return;
	}

	if( m_paused == true )
	{
		m_playing = true;
		m_paused = false;
	}
	else
	{
		m_playing = false;
		m_paused = true;
		Engine::audioEngine()->clear();
	}

	m_vstSyncController.setPlaybackState( m_playing );

	emit playbackStateChanged();
}




void Song::stop()
{
	// A stop ends a record request that Play has not yet taken - also when the transport is already
	// stopped (Record, then Stop, then Play must not record).
	m_recordLatched = false;
	// do not stop/reset things again if we're stopped already
	if( m_playMode == PlayMode::None )
	{
		return;
	}
	recordoptions::endPass(this);  // 0.5.0 L03: the timeline's own punch region back on the gate
	MidiPlayback::instance().stopped();  // 0.5.0 L07: the next tick is a start

	using gui::TimeLineWidget;

	// To avoid race conditions with the processing threads
	Engine::audioEngine()->requestChangeInModel();

	// The modulation layer (#602): a transport that stops hands every modulated
	// parameter back to the base it was modulated around, so a stopped song does
	// not leave a filter parked where the last block put it. The layer KEEPS its
	// resolved routes - the next play starts from the same bases.
	restoreModulationBases( m_modulationLayer.runtime() );

	auto& timeline = getTimeline();
	m_paused = false;
	m_recording = true;
	m_playing = false;

	switch (timeline.stopBehaviour())
	{
		case Timeline::StopBehaviour::BackToZero:
			if (m_playMode == PlayMode::MidiClip)
			{
				timeline.setTicks(std::max(0, -m_midiClipToPlay->startTimeOffset()));
			}
			else
			{
				timeline.setTicks(0);
			}
			break;

		case Timeline::StopBehaviour::BackToStart:
			if (timeline.playStartPosition() >= 0)
			{
				timeline.setTicks(timeline.playStartPosition().getTicks());

				timeline.setPlayStartPosition(-1);
			}
			break;

		case Timeline::StopBehaviour::KeepPosition:
			break;
	}

	getTimeline(PlayMode::None).setTicks(getPlayPos().getTicks());

	m_vstSyncController.setPlaybackState( m_exporting );
	m_vstSyncController.setAbsolutePosition(
		getPlayPos().getTicks()
		+ timeline.frameOffset()
		/ (double) Engine::framesPerTick() );

	// remove all note-play-handles that are active
	Engine::audioEngine()->clear();

	// Moves the control of the models that were processed on the last frame
	// back to their controllers. Every key in the map is still a live model: one
	// that is destroyed while it is cached removes itself from it (the model's
	// destructor calls Song::forgetAutomatedModel), so nothing here can be a
	// stale pointer.
	for (const auto& entry : m_oldAutomatedValues)
	{
		AutomatableModel * am = entry.first;
		// Null when the model was destroyed while the transport was playing (the
		// member's comment has the mechanism); skipping it is the whole point of
		// guarding the keys - this loop has no other way to tell.
		if (am != nullptr)
		{
			am->setUseControllerValue(true);
		}
	}
	m_oldAutomatedValues.clear();

	m_playMode = PlayMode::None;

	Engine::audioEngine()->doneChangeInModel();
	// The last points must be applied before a caller can save or replace the
	// project immediately after Stop.
	if (!RealtimeScope::isActive()) { applyPendingAutomationWrites(); }

	emit stopped();
	emit playbackStateChanged();
}




void Song::startExport()
{
	stop();

	m_exporting = true;
	controlroom::setExporting(true);  // 0.5.0 L09 (MX-038): the monitor section never reaches a render
	updateLength();

	auto& timeline = getTimeline(PlayMode::Song);

	if (m_renderBetweenMarkers)
	{
		m_exportSongBegin = m_exportLoopBegin = timeline.loopBegin();
		m_exportSongEnd = m_exportLoopEnd = timeline.loopEnd();

		timeline.setTicks(timeline.loopBegin().getTicks());
	}
	else
	{
		// A render-length override is what lets a stem export render every stem to
		// the same length as the mix (see setExportLengthOverrideBars).
		const bar_t exportBars = m_exportLengthOverrideBars > 0
			? static_cast<bar_t>(m_exportLengthOverrideBars)
			: m_length;
		m_exportSongEnd = TimePos(exportBars, 0);
        
		// Handle potentially ridiculous loop points gracefully.
		if (m_loopRenderCount > 1 && timeline.loopEnd() > m_exportSongEnd) 
		{
			m_exportSongEnd = timeline.loopEnd();
		}

		if (!m_exportLoop) 
			m_exportSongEnd += TimePos(m_exportTailBars, 0);
        
		m_exportSongBegin = TimePos(0,0);
		m_exportLoopBegin = timeline.loopBegin() < m_exportSongEnd && timeline.loopEnd() <= m_exportSongEnd
			? timeline.loopBegin()
			: TimePos{0};
		m_exportLoopEnd = timeline.loopBegin() < m_exportSongEnd && timeline.loopEnd() <= m_exportSongEnd
			? timeline.loopEnd()
			: TimePos{0};

		getTimeline(PlayMode::Song).setTicks(0);
	}

	m_exportEffectiveLength = (m_exportLoopBegin - m_exportSongBegin) + (m_exportLoopEnd - m_exportLoopBegin) 
		* m_loopRenderCount + (m_exportSongEnd - m_exportLoopEnd);
	m_loopRenderRemaining = m_loopRenderCount;

	playSong();

	m_vstSyncController.setPlaybackState( true );
}


bool Song::prepareConverterModeForInstruments(AudioResampler::Mode mode) noexcept
{
	for (Track* const track : tracks())
	{
		if (track == nullptr || track->type() != Track::Type::Instrument) { continue; }
		auto* const instrumentTrack = dynamic_cast<InstrumentTrack*>(track);
		if (instrumentTrack == nullptr) { continue; }
		Instrument* const instrument = instrumentTrack->instrument();
		if (instrument != nullptr && !instrument->prepareConverterMode(mode)) { return false; }
	}
	return true;
}

void Song::adoptPreparedConverterModeForInstruments() noexcept
{
	for (Track* const track : tracks())
	{
		if (track == nullptr || track->type() != Track::Type::Instrument) { continue; }
		auto* const instrumentTrack = dynamic_cast<InstrumentTrack*>(track);
		if (instrumentTrack == nullptr) { continue; }
		if (Instrument* const instrument = instrumentTrack->instrument())
		{
			instrument->adoptPreparedConverterMode();
		}
	}
}

void Song::discardPreparedConverterModeForInstruments() noexcept
{
	for (Track* const track : tracks())
	{
		if (track == nullptr || track->type() != Track::Type::Instrument) { continue; }
		auto* const instrumentTrack = dynamic_cast<InstrumentTrack*>(track);
		if (instrumentTrack == nullptr) { continue; }
		if (Instrument* const instrument = instrumentTrack->instrument())
		{
			instrument->discardPreparedConverterMode();
		}
	}
}




void Song::stopExport()
{
	stop();
	m_exporting = false;
	controlroom::setExporting(false);

	m_vstSyncController.setPlaybackState( m_playing );
}




void Song::insertBar()
{
	m_tracksMutex.lockForRead();
	for (Track* track: tracks())
	{
		// FIXME journal batch of tracks instead of each track individually
		if (track->numOfClips() > 0) { track->addJournalCheckPoint(); }
		track->insertBar(getPlayPos(PlayMode::Song));
	}
	m_tracksMutex.unlock();
}




void Song::removeBar()
{
	m_tracksMutex.lockForRead();
	for (Track* track: tracks())
	{
		// FIXME journal batch of tracks instead of each track individually
		if (track->numOfClips() > 0) { track->addJournalCheckPoint(); }
		track->removeBar(getPlayPos(PlayMode::Song));
	}
	m_tracksMutex.unlock();
}




void Song::addPatternTrack()
{
	Track * t = Track::create(Track::Type::Pattern, this);
	Engine::patternStore()->setCurrentPattern(dynamic_cast<PatternTrack*>(t)->patternIndex());
}




void Song::addSampleTrack()
{
	( void )Track::create( Track::Type::Sample, this );
}




void Song::addAutomationTrack()
{
	( void )Track::create( Track::Type::Automation, this );
}




bpm_t Song::getTempo()
{
	return ( bpm_t )m_tempoModel.value();
}




int Song::tempoAtTick( tick_t tick ) const
{
	// The global model is the map's own out-of-range answer (before its first
	// event, and for an empty or inactive map), so this is the one place the
	// default is chosen - docs/TEMPO-MAP.md section 2.
	return m_tempoMap.map().tempoAtTick( tick, static_cast<int>( m_tempoModel.value() ) );
}




double Song::secondsAtTick( tick_t tick ) const
{
	return m_tempoMap.map().secondsAtTick( tick, static_cast<int>( m_tempoModel.value() ) );
}




void Song::followTempoMap()
{
	// An empty OR inactive map is exactly today's engine, and the audio thread
	// must pay nothing for it: no snapshot copy, no arithmetic, no write to the
	// frame/tick scalar. This early return IS the byte-identity guarantee on the
	// timing path, and TempoMapTest measures it.
	// The map is read pinned (include/TempoMap.h, TempoMapPublisher::withRealtimeMap): bounded, no lock, no
	// allocation, no data race, and the active flag comes from the pinned map itself. A read that loses every
	// attempt to a publication keeps the tempo already applied; the next block reads again.
	bool active = false;
	int bpm = -1;
	const int globalTempo = static_cast<int>( m_tempoModel.value() );
	const tick_t tick = currentTick();
	const bool pinned = m_tempoMap.withRealtimeMap( [&]( const TempoMap& map )
	{
		active = map.active();
		if( active ) { bpm = map.tempoAtTick( tick, globalTempo ); }
	} );
	if( !pinned ) { return; }
	if( !active )
	{
		m_tempoMapAppliedTempo = -1;
		return;
	}
	if( bpm == m_tempoMapAppliedTempo ) { return; }

	m_tempoMapAppliedTempo = bpm;
	Engine::updateFramesPerTickForTempo( bpm );
}


void Song::processModulation()
{
	// An empty layer is exactly today's engine, and the audio thread must pay
	// nothing for it: no snapshot copy, no arithmetic, no model write. This
	// early return IS the byte-identity guarantee on the audio path, and
	// ModulationLayerTest measures it.
	// hasLayer() is the published flag: the authored layer itself is control
	// thread only and must not be read here.
	if( !m_modulationLayer.hasLayer() ) { return; }

	// The snapshot is a fixed-capacity plain-value copy with a version re-check
	// - no lock, no allocation, no growth, no QPointer (include/ModulationLayer.h).
	const ModulationAudioView view = m_modulationLayer.snapshot();
	if( !view.active() ) { return; }

	// The modulator runs on WALL-CLOCK seconds measured at the play head, so it
	// follows the timeline (including a tempo map's own rate) rather than the
	// transport's musical position: a rate in Hz means Hz. The float cast is
	// widened before the divide because f_cnt_t is 32-bit.
	const sample_rate_t rate = Engine::audioEngine()->baseSampleRate();
	if( rate == 0 ) { return; }
	const double frames = static_cast<double>( getPlayPos().getTicks() )
		* static_cast<double>( Engine::framesPerTick() );
	// R1.3: the block's size and rate, so a per-sample route can spread its knots across it.
	applyModulationBlock( m_modulationLayer, view, frames / static_cast<double>( rate ),
		ModulationBlock{ Engine::audioEngine()->framesPerPeriod(), rate } );
}


void Song::buildAutomationRamps( const TrackList & trackList, const TimePos & blockStart,
	f_cnt_t frames, int frameOffsetInTick )
{
	// A caller with no period's own snapshot pins one now (the tests' direct entry; the engine pins once).
	const auto latencySnapshot = AutomationLatency::instance().readSnapshot();
	buildAutomationRamps(trackList, blockStart, frames, frameOffsetInTick, latencySnapshot);
}

void Song::buildAutomationRamps( const TrackList & trackList, const TimePos & blockStart,
	f_cnt_t frames, int frameOffsetInTick, const AutomationLatency::ReadSnapshot& latencySnapshot )
{
	/*! Sample-accurate automation (feature-list row 9,
	 *  docs/SAMPLE-ACCURATE-AUTOMATION.md). Runs ONCE PER AUDIO BLOCK, at the
	 *  block's own play position, and only over the automation clips that asked
	 *  for it (AutomationClip::sampleAccurate(), set through
	 *  automation.ramp_set): a project that never asked for one pays the two
	 *  type tests of the loops below and nothing else, which is what keeps
	 *  every existing project's render byte-identical.
	 *
	 *  REALTIME-SAFE by construction, and SampleAccurateAutomationTest measures
	 *  it over 64 blocks:
	 *   * the ramp is a stack object of fixed capacity (include/AutomationRamp.h)
	 *     and is COPIED into each model's own fixed-capacity member - no
	 *     allocation anywhere on this path;
	 *   * the traversal iterates the track list, each track's own clip vector
	 *     and each clip's own object vector by reference - no container is built
	 *     on the audio thread;
	 *   * the clip's mutex is taken ONCE per clip per block (already the case on
	 *     the per-tick path this replaces, and the only lock here);
	 *   * a block whose curve needs more knots than the capacity holds refuses
	 *     the extra knots and COUNTS them (`AutomationRamp::refusals()`), so
	 *     nothing grows with tempo or block size.
	 */
	const double framesPerTick = Engine::framesPerTick();
	// The song-wide read switch (ED-200) gates the ramp exactly as it gates the per-tick apply in
	// processAutomations(): with reads off, every control keeps its manual value, so nothing is published.
	if( frames == 0 || framesPerTick <= 0.0 || !AutomationSwitches::instance().reads() )
	{
		return;
	}

	for( Track* track : trackList )
	{
		const auto type = track->type();
		if( type != Track::Type::Automation && type != Track::Type::HiddenAutomation )
		{
			continue;
		}
		if( track->isMuted() )
		{
			continue;
		}
		for( Clip* clip : track->getClips() )
		{
			auto* automationClip = dynamic_cast<AutomationClip*>( clip );
			// Do not publish one sample-accurate curve at the wrong time to mixed-latency targets.
			if( automationClip != nullptr && latencySnapshot.latencyFrames(automationClip) < 0 )
			{
				continue;
			}
			const bool latencyNeedsRamp = automationClip != nullptr
				&& automationClip->rampSetting() == AutomationClip::RampSetting::Default
				&& latencySnapshot.enabled() && latencySnapshot.latencyFrames(automationClip) > 0;
			// 0.5.0 MX-160: the block as the latent audio sees it - the same start unless the project
			// turned latency-compensated automation on (moved before the song's start, it reads nothing).
			TimePos readStart = blockStart;
			int readOffsetInTick = frameOffsetInTick;
			AutomationLatency::instance().shiftBlock(automationClip, framesPerTick, &readStart,
				&readOffsetInTick, latencySnapshot);
			if( automationClip == nullptr || (!automationClip->sampleAccurate() && !latencyNeedsRamp)
				|| !automationClip->hasAutomation() || automationClip->isMuted()
				|| automationClip->startPosition() > readStart )
			{
				continue;
			}

			// The clip's targets, read once through the existing accessor. The
			// ramp is then built under ONE clip lock, as before: a target whose
			// authored initial value differs gets a COPY of the ramp with only
			// the pre-first knots overridden, never a second rebuild.
			const AutomationClip::objectVector& targets = automationClip->objects();
			const AutomatableModel* baseTarget = nullptr;
			for( const QPointer<AutomatableModel>& candidate : targets )
			{
				if( candidate != nullptr ) { baseTarget = candidate; break; }
			}
			const float basePreFirst = baseTarget == nullptr ? 0.0f
				: baseTarget->inverseScaledValue( baseTarget->initValue<float>() );

			AutomationRamp ramp;
			f_cnt_t firstNodeFrame = 0;
			automationClip->writeBlockRamp( ramp, readStart, frames, framesPerTick,
				readOffsetInTick, baseTarget, &firstNodeFrame );
			for( const QPointer<AutomatableModel>& model : targets )
			{
				// Automation modes: a control in Off IGNORES its written
				// automation - the manual value stands - and this publish is a
				// WRITE to the model, so it must obey the same gate the per-tick
				// apply pass obeys (Song::processAutomations). Without it the
				// ramp reaches an Off parameter before the apply pass can skip
				// it, and that is the one place the two features meet.
				if( model == nullptr
					|| model->automationMode() == AutomatableModel::AutomationMode::Off )
				{
					continue;
				}
				if( firstNodeFrame == 0 )
				{
					// The first node is at or before the block: every frame is
					// on the curve, so all targets share this ramp.
					model->publishAutomationRamp( ramp );
					continue;
				}
				const float targetPreFirst =
					model->inverseScaledValue( model->initValue<float>() );
				if( targetPreFirst == basePreFirst )
				{
					model->publishAutomationRamp( ramp );
					continue;
				}
				AutomationRamp own = ramp;
				own.setValueBefore( firstNodeFrame, targetPreFirst );
				model->publishAutomationRamp( own );
			}
		}
	}
}


void Song::fillAutomatedValues(AutomationRenderScratch& level, AutomationRenderScratch* inner,
	TimePos time, int clipNum, const AutomationLatency::ReadSnapshot& snapshot) const
{
	// The global automation track first, then the song's own tracks: the same order the clips were always
	// gathered in, and with no track list built on the way (that vector was an audio-thread allocation).
	level.clear();
	gatherAutomationClips(level, m_globalAutomationTrack, time, clipNum);
	for (Track* track : tracks()) { gatherAutomationClips(level, track, time, clipNum); }
	collectAutomatedValues(level, inner, time, snapshot);
}




void Song::clearProject()
{
	using gui::getGUI;

	m_containerV2Project = false;

	m_documentIdentity = nextDocumentIdentity();
	Engine::projectJournal()->setJournalling( false );
	// The Song object survives project replacement, so issue a fresh path identity here.
	setProperty("_zeneUnsavedProjectId", QUuid::createUuid().toString(QUuid::WithoutBraces));

	if( m_playing )
	{
		stop();
	}
	// The writes the render thread queued belong to the project that is being
	// cleared: they are dropped with it, not carried into the project that
	// replaces it.
	discardPendingAutomationWrites();

	// A pending Record press (or an active recording pass) belongs to the
	// project being cleared - the latch must not survive into the next one
	// (BUGS new-p12-257 BUG 8).
	m_recordLatched = false;
	m_recording = false;

	for (auto i = std::size_t{0}; i < PlayModeCount; i++)
	{
		setPlayPos( 0, ( PlayMode )i );
	}


	Engine::audioEngine()->requestChangeInModel();

	if( getGUI() != nullptr && getGUI()->patternEditor() )
	{
		getGUI()->patternEditor()->m_editor->clearAllTracks();
	}
	if( getGUI() != nullptr && getGUI()->songEditor() )
	{
		getGUI()->songEditor()->m_editor->clearAllTracks();
	}
	if( getGUI() != nullptr && getGUI()->mixerView() )
	{
		getGUI()->mixerView()->clear();
	}
	QCoreApplication::sendPostedEvents();
	Engine::patternStore()->clearAllTracks();
	clearAllTracks();

	Engine::mixer()->clear();

	if( getGUI() != nullptr && getGUI()->automationEditor() )
	{
		getGUI()->automationEditor()->setCurrentClip( nullptr );
	}

	if( getGUI() != nullptr && getGUI()->pianoRoll() )
	{
		getGUI()->pianoRoll()->reset();
	}

	m_tempoModel.reset();
	m_masterVolumeModel.reset();
	m_masterPitchModel.reset();
	m_timeSigModel.reset();

	// Clear the m_oldAutomatedValues AutomatedValueMap
	m_oldAutomatedValues.clear();

	AutomationClip::globalAutomationClip( &m_tempoModel )->clear();
	AutomationClip::globalAutomationClip( &m_masterVolumeModel )->
									clear();
	AutomationClip::globalAutomationClip( &m_masterPitchModel )->
									clear();
	// WF-055: a launcher cell's audio take belongs to the project being closed (no period renders here).
	m_sessionScheduler.audioRecorder().cancel();
	sessionaudio::retire();  // and the audio cells' clips (freed at the next close: SessionAudioCells.h)

	Engine::audioEngine()->doneChangeInModel();

	if( getGUI() != nullptr && getGUI()->getProjectNotes() )
	{
		getGUI()->getProjectNotes()->clear();
	}

	removeAllControllers();

	m_sessionModel.clear();
	ProjectSection::clearAll();
	m_sessionScheduler.reset();

	// ...and neither may the sections the previous project left unclaimed. The
	// list describes ONE document (SPEC-ARCH-4 1.6.4), and the walk below only
	// APPENDS to it, so this is the one place the next project starts from
	// empty.
	m_unclaimedElements.clear();

	// ...and neither may the sections a PARTIAL load left out (ARCH-4 S2b). The
	// list describes one document and names bytes that are not in this one, so a
	// new project must start whole - leaving it set would mark a fresh, complete
	// session read-only.
	m_notLoadedSections.clear();

	// The tempo map is project state, so a new project starts with none: a map
	// from one project must not retime the next (docs/TEMPO-MAP.md).
	m_tempoMap.edit([](TempoMap& map) { map.clear(); return true; });
	m_tempoMapAppliedTempo = -1;

	// The groove pool is project state too. A groove from one project must not
	// be applicable to the next one's clips, and the pool is written to the
	// file ONLY when it is non-empty, so the empty state has to be reachable
	// here as well as from the (absent) element in loadProject
	// (docs/GROOVE-POOL.md section 4).
	m_groovePool.clear();

	// The chord track is project state too, and the chords of one project must
	// not be writable into the next one's clips. It is written to the file ONLY
	// when it holds a chord, so the empty state has to be reachable here as
	// well as from the (absent) element in loadProject.
	m_chordTrack.clear();

	// The detected key is project state too: a key from one project must not
	// describe the next one, and the field is written to the file ONLY when it
	// holds something, so the empty state has to be reachable here as well as
	// from the (absent) element in loadProject (the groove pool's own rule).
	m_projectKey.clear();

	// The modulation layer is project state too, and a layer from one project
	// must not keep driving the next one's parameters. CLEARING the layer drops
	// its resolved routes with it, so no write target outlives the project it
	// was bound in (the lifetime rule docs/MODULATION.md section 4 states).
	m_modulationLayer.edit([](ModulationLayer& layer, ModulationRuntime& runtime) {
		layer.clear();
		runtime = ModulationRuntime{};
		return true;
	});

	emit dataChanged();

	Engine::projectJournal()->clearJournal();

	Engine::projectJournal()->setJournalling( true );
}




// create new file
void Song::createNewProject()
{

	QString defaultTemplate = ConfigManager::inst()->userTemplateDir()
						+ "default.mpt";


	if( QFile::exists( defaultTemplate ) )
	{
		createNewProjectFromTemplate( defaultTemplate );
		return;
	}

	defaultTemplate = ConfigManager::inst()->factoryProjectsDir()
						+ "templates/default.mpt";
	if( QFile::exists( defaultTemplate ) )
	{
		createNewProjectFromTemplate( defaultTemplate );
		return;
	}

	m_loadingProject = true;

	clearProject();

	// A brand-new document starts its id counter at 0, so the first object an
	// agent creates after project.new is <prefix>-0 rather than a number left
	// over from whatever was loaded before (SPEC-stable-ids.md 2.1).
	ProjectIds::reset();

	Engine::projectJournal()->setJournalling( false );

	m_oldFileName = "";
	setProjectFileName("");

	auto tripleOscTrack = Track::create(Track::Type::Instrument, this);
	dynamic_cast<InstrumentTrack*>(tripleOscTrack)->loadInstrument("tripleoscillator");

	auto kickerTrack = Track::create(Track::Type::Instrument, Engine::patternStore());
	dynamic_cast<InstrumentTrack*>(kickerTrack)->loadInstrument("kicker");

	Track::create( Track::Type::Sample, this );
	Track::create( Track::Type::Pattern, this );
	Track::create( Track::Type::Automation, this );

	m_tempoModel.setInitValue( DefaultTempo );
	m_timeSigModel.reset();
	m_masterVolumeModel.setInitValue( 100 );
	m_masterPitchModel.setInitValue( 0 );
	m_midiSeed = 0;

	QCoreApplication::instance()->processEvents();

	m_loadingProject = false;
	updateLength();
	Engine::patternStore()->updateAfterTrackAdd();

	Engine::projectJournal()->setJournalling( true );

	QCoreApplication::sendPostedEvents();

	setModified(false);
	m_loadOnLaunch = false;
}




void Song::createNewProjectFromTemplate( const QString & templ )
{
	loadProject( templ );
	// clear file-name so that user doesn't overwrite template when
	// saving...
	m_oldFileName = "";
	setProjectFileName("");
	// update window title
	m_loadOnLaunch = false;
}




namespace
{

//! ARCH-4 S3 (SPEC-ARCH-4 1.5 R4), note family: a note whose file id is already
//! taken by an earlier note in document order is handed a fresh one from the
//! high-water mark and the replacement is COUNTED, exactly as the track and
//! clip passes above do. Without this, `note-<n>` could name two live notes at
//! once after a branch merge. Track and clip ids are untouched, and the
//! addresses are per family (`trk-7`, `clip-7` and `note-7` are distinct), so
//! this keeps its own seen-set.
void repairDuplicateNoteIds( const TrackContainer::TrackList& loaded )
{
	QSet<int> seenNotes;
	for( const Track* track : loaded )
	{
		for( const Clip* clip : track->getClips() )
		{
			auto* midi = dynamic_cast<const MidiClip*>( clip );
			if( !midi ) { continue; }
			for( Note* note : midi->notes() )
			{
				if( seenNotes.contains( note->id() ) )
				{
					note->setId( ProjectIds::allocate() );
					ProjectIds::noteLoadAssignment( note->id() );
				}
				else
				{
					seenNotes.insert( note->id() );
				}
			}
		}
	}
}

} // namespace


// load given song. \a skipSections names sections the caller does not want
// loaded (SPEC-ARCH-4 1.4, ARCH-4 S2b): their bytes are removed before anything
// parses them, and what was removed becomes this session's notLoadedSections() -
// which makes the session read-only (see saveProjectFile). An empty list is an
// ordinary, whole load.
void Song::loadProject( const QString & fileName, const QStringList & skipSections )
{
	using gui::getGUI;

	QDomNode node;

	m_loadingProject = true;

	// A load must never leave a modal dialog open in an agent instance (task
	// #625): record why a file was refused so project.open can answer with a
	// typed error instead.
	m_loadRefusal.clear();

	Engine::projectJournal()->setJournalling( false );

	m_oldFileName = m_fileName;
	setProjectFileName(fileName);

	// The report is carried in a LOCAL list and only becomes this session's
	// state once the load has been ACCEPTED, a few lines below. Assigning the
	// member directly would let a refused load - unparseable, no <head>, or the
	// local-plugin refusal - leave the PREVIOUS project looking partial, and
	// the previous project is still the loaded one on that path.
	QStringList skippedSections;
	DataFile dataFile( m_fileName, skipSections, &skippedSections );

	bool cantLoadProject = false;
	// if file could not be opened, head-node is null and we create
	// new project
	if( dataFile.head().isNull() )
	{
		cantLoadProject = true;
		m_loadRefusal = tr( "the project file could not be read or parsed "
			"(see the log for the parser's line/column report)" );
	}
	else
	{
		// We check if plugins contain local paths to prevent malicious code being
		// added to project bundles and loaded with "local:" paths
		if (dataFile.hasLocalPlugins())
		{
			cantLoadProject = true;
			m_loadRefusal = tr("Project file contains local paths to plugins, which could be used to "
					"run malicious code.");

			// In an agent instance nobody can answer this box (task #625), so
			// the refusal becomes the typed error project.open returns.
			if (getGUI() != nullptr && !lmms::isUnattendedRun())
			{
				QMessageBox::critical(nullptr, tr("Aborting project load"),
					m_loadRefusal);
			}
			else
			{
				QTextStream(stderr) << tr("Can't load project: "
					"Project file contains local paths to plugins.")
					<< Qt::endl;
			}
		}
	}

	if (cantLoadProject)
	{
		if( m_loadOnLaunch )
		{
			// A failed fallback template must not ask this branch to fall back again.
			m_loadOnLaunch = false;
			createNewProject();
		}
		setProjectFileName(m_oldFileName);
		// A failed open must not leave the document half-initialised. This
		// branch returns before the success path re-enables them, so both
		// flags used to stay off for the rest of the session: setModified()
		// is a no-op while m_loadingProject is set (see its body) and
		// MainWindow::autoSave() refuses to run while isLoadingProject() is
		// true. The user who opened one bad file - or hit the local-plugin
		// refusal - then carried on with autosave, undo journalling and
		// modified-tracking all disabled until a restart, with nothing said.
		// The previous project is still loaded here (clearProject() has not
		// run), so restoring the two flags restores exactly its state.
		m_loadingProject = false;
		Engine::projectJournal()->setJournalling( true );
		return;
	}

	m_oldFileName = m_fileName;

	clearProject();

	// The load was ACCEPTED, so what it skipped is now this session's state
	// (ARCH-4 S2b). clearProject() runs first because it is what resets the rest
	// of the per-document state and this belongs with it.
	m_notLoadedSections = skippedSections;
	// Q050-007: a project opened from a v2 container stays one on an ordinary
	// save - silently turning it back into a legacy blob would be the surprise.
	m_containerV2Project = dataFile.loadedFromContainer();

	// WF-033: the undo history's fingerprint is taken over the document as the
	// FILE has it - before any reader below runs (docs/UNDO-HISTORY-PERSISTENCE.md).
	UndoHistory::instance().beginLoad( dataFile );

	clearErrors();

	// The id pass, load half (SPEC-stable-ids.md R2/R3). Reset the counter so
	// assignment is deterministic - the container walk below creates the tracks
	// in document order and the Track constructor allocates in that order, so
	// the same legacy document always yields the same ids - and clear the
	// assignment count project.open reports as `ids_assigned`.
	ProjectIds::reset();
	ProjectIds::beginLoad();

	Engine::audioEngine()->requestChangeInModel();

	// get the header information from the DOM
	m_tempoModel.loadSettings( dataFile.head(), "bpm" );
	m_timeSigModel.loadSettings( dataFile.head(), "timesig" );
	m_masterVolumeModel.loadSettings( dataFile.head(), "mastervol" );
	m_masterPitchModel.loadSettings( dataFile.head(), "masterpitch" );
	// MIDI depth: the seed the note probability / velocity jitter rolls use.
	// Absent (every project saved before this existed) reads as 0.
	m_midiSeed = NoteRandom::readProjectSeed( dataFile.head() );

	getTimeline(PlayMode::Song).setLoopEnabled(false);

	//Backward compatibility for LMMS <= 0.4.15
	PeakController::initGetControllerBySetting();

	// Load mixer first to be able to set the correct range for mixer channels
	node = dataFile.content().firstChildElement( Engine::mixer()->nodeName() );
	if( !node.isNull() )
	{
		Engine::mixer()->restoreState( node.toElement() );
		if( getGUI() != nullptr )
		{
			// refresh MixerView
			getGUI()->mixerView()->refreshDisplay();
		}
	}

	node = dataFile.content().firstChild();

	QDomNodeList tclist=dataFile.content().elementsByTagName("trackcontainer");
	m_nLoadingTrack=0;
	for( int i=0,n=tclist.count(); i<n; ++i )
	{
		QDomNode nd=tclist.at(i).firstChild();
		while(!nd.isNull())
		{
			if( nd.isElement() && nd.nodeName() == "track" )
			{
				++m_nLoadingTrack;
				if (static_cast<Track::Type>(nd.toElement().attribute("type").toInt()) == Track::Type::Pattern)
				{
					n += nd.toElement().elementsByTagName("patterntrack").at(0)
						.toElement().firstChildElement().childNodes().count();
				}
			}
			// The advance belongs HERE, not inside the branch above (ARCH-4 S1c,
			// measured 2026-09-21). Inside it, the walk only moved on when the
			// child it was looking at WAS a <track>: the first child of
			// <trackcontainer> that was anything else - a <track> this build's
			// factory refuses is still named `track`, so the trigger is an
			// element a NEWER writer put there, or a hand-edited file - left
			// `nd` where it was, `while( !nd.isNull() )` never terminated, and
			// Song::loadProject spun at 100% CPU before the load proper had
			// begun. It is a latent defect rather than a live one only because
			// every file this tree writes has <track> children alone.
			nd=nd.nextSibling();
		}
	}

	// RESET ON ABSENCE for the named visibility sets (owner items 3+20+21): a
	// project with no <visibilitysets> element holds none, whatever the object
	// carried before the load. This is the ONE place that decides it - the
	// container's own walk cannot, because the element is not its child.
	clearVisibilitySets();

	// RESET ON ABSENCE for the ARCH-4 S6 provenance section (SPEC-ARCH-4 1.9),
	// for the same reason and in the same place: the change log belongs to the
	// document being opened. The walk below either claims <z:provenance> and
	// adopts its entries, or this session holds none - which is also why a
	// project that never carried a section still saves without one.
	provenance::Section::instance().clear();
	s_provenanceAdoptedThisLoad = false;

	while( !node.isNull() && !isCancelled() )
	{
		if( node.isElement() )
		{
			const QDomElement element = node.toElement();
			// SPEC-ARCH-4 1.6.1: an element NO reader claims is kept verbatim
			// and re-emitted on save, so a document written by a newer build -
			// or by a build of this one with more features compiled in - cannot
			// lose a section merely by being opened and saved here. The two
			// helpers answer "did I claim this element?", and the walk keeps
			// only the dispatch (which is also what keeps its own complexity
			// down; the branches that were here moved into them unchanged).
			if( !restoreNamedSection( element ) && !restoreGuiSection( element ) )
			{
				captureUnclaimed( element, element.nodeName(), m_unclaimedElements );
			}
		}
		node = node.nextSibling();
	}

	// quirk for fixing projects with broken positions of Clips inside pattern tracks
	Engine::patternStore()->fixIncorrectPositions();

	// The id pass, post-walk half (SPEC-stable-ids.md R3). The objects exist and
	// carry ids now, so this is the one place the document's own counter can be
	// honoured and repaired:
	//
	//  * `next-id` is taken from the root. A file written by this build carries
	//    it; a legacy file (or a file that came back through an older build,
	//    which drops it) does not, and then the counter is already at
	//    max(id seen)+1 because every setId() observed its value.
	//  * Two live tracks sharing an id is only reachable from an external merge
	//    of two projects (GIT-FRIENDLY-MMPZ.md documents real .mmpz merges). The
	//    second in document order is re-assigned from the high-water mark and
	//    counted, so the repair shows up in `ids_assigned` instead of hiding.
	if (dataFile.documentElement().hasAttribute(QStringLiteral("next-id")))
	{
		bool ok = false;
		const int stored = dataFile.documentElement().attribute(QStringLiteral("next-id")).toInt(&ok);
		// restoreFromDocument, NOT observeNext: the walk above allocated a
		// placeholder for every object it constructed, so the counter is ABOVE
		// the document's own value here, and a max-merge would keep it there -
		// which made every save/load/save round trip write a larger `next-id`
		// than the file it was loaded from (round1 `next-id="3"`, round2
		// `next-id="5"`, StableTrackIdsTest / TempoMapPersistenceTest). The
		// document's counter is restored, floored by the ids the document
		// carries and the ones an id-less element kept (R3).
		if (ok) { ProjectIds::restoreFromDocument(stored); }
	}
	{
		QSet<int> seen;
		const TrackContainer::TrackList& loaded = tracks();
		for (int i = 0; i < static_cast<int>(loaded.size()); ++i)
		{
			Track* track = loaded[i];
			if (seen.contains(track->id()))
			{
				track->setId(ProjectIds::allocate());
				ProjectIds::noteLoadAssignment(track->id());
			}
			else
			{
				seen.insert(track->id());
			}
		}

		// ARCH-4 S3 (SPEC-ARCH-4 1.5 R4, census row 8): the SAME rule for the
		// clip family. Duplicated ids arrive exactly like the tracks' - a
		// merge of two branches that each edited a copy of the file - and the
		// addresses are per family (`trk-7` and `clip-7` are distinct), so the
		// clip family gets its own seen-set over every clip of every loaded
		// track. Track ids are untouched; this block runs only where it always
		// ran, after the walk and before endLoad().
		QSet<int> seenClips;
		for (int i = 0; i < static_cast<int>(loaded.size()); ++i)
		{
			for (Clip* clip : loaded[i]->getClips())
			{
				if (seenClips.contains(clip->id()))
				{
					clip->setId(ProjectIds::allocate());
					ProjectIds::noteLoadAssignment(clip->id());
				}
				else
				{
					seenClips.insert(clip->id());
				}
			}
		}

		// ...and the SAME rule for the note family, over every note of every midi
		// clip of every loaded track. Two notes claiming one id arrive exactly
		// like duplicated tracks or clips - a merge of two branches that each
		// edited a copy - and `note-<n>` would then name two live notes, so a
		// command addressing one could edit or remove the other.
		repairDuplicateNoteIds( loaded );
	}

	// The walk is over: from here on allocate() hands out ids to objects that
	// really keep them, so it counts against the counter again.
	ProjectIds::endLoad();

	// Connect controller links to their controllers
	// now that everything is loaded
	ControllerConnection::finalizeConnections();

	// Finish the folder relation (docs/TRACK-FOLDER-DESIGN.md section 4.3; owner
	// items 3+20+21) in the same after-the-walk pass, for the same reason: a
	// child's `folder` attribute names a track by id, and a folder created after
	// the tracks it holds is constructed after them. The count is the children
	// whose folder is gone - reported, not hidden, the way the id pass above
	// reports every id it re-assigned.
	const int danglingFolders = tracks().empty() ? 0 : resolveTrackFolders();
	if (danglingFolders > 0)
	{
		qWarning("project load: %d track(s) named a folder that is not in the project; "
			"they are at the container root", danglingFolders);
	}

	// Remove dummy controllers that was added for correct connections
	m_controllers.erase(std::remove_if(m_controllers.begin(), m_controllers.end(),
		[](Controller* c){return c->type() == Controller::ControllerType::Dummy;}),
		m_controllers.end());

	// resolve all IDs so that autoModels are automated
	AutomationClip::resolveAllIDs();

	// Publish PDC and its automation offsets before the first period can render this project.
	Engine::mixer()->publishLatencyCompensationForProjectLoad();
	AutomationLatency::instance().refreshAfterProjectLoad();

	Engine::audioEngine()->doneChangeInModel();

	// WF-033: an undo-snapshot restore loads a temporary file; it is not a
	// project the user opened.
	if( !m_restoringSnapshot ) { ConfigManager::inst()->addRecentlyOpenedProject( fileName ); }

	Engine::projectJournal()->setJournalling( true );

	emit projectLoaded();

	if( isCancelled() )
	{
		m_isCancelled = false;
		createNewProject();
		return;
	}

	if ( hasErrors())
	{
		// The "LMMS Error report" box is a question for a human.  In an agent
		// instance (--control-socket) or a run with no display it would park
		// the UI thread in a nested event loop - project.open never returns,
		// and the client is told nothing (task #625).  The same report goes to
		// stderr, and project.open hands the caller the per-item list.
		if ( getGUI() != nullptr && !lmms::isUnattendedRun() )
		{
			QMessageBox::warning( nullptr, tr("Zene Studio Error report"), errorSummary(),
							QMessageBox::Ok );
		}
		else
		{
			QTextStream(stderr) << Engine::getSong()->errorSummary() << Qt::endl;
		}
	}

	m_loadingProject = false;
	// Board card #597: publish what the loaded project's session cells hold, or
	// a launch of a cell that came from the FILE would take its track over and
	// render nothing. Here rather than in the per-element reader, so every
	// section that fills the model - the native <z:scenes> and the legacy
	// <session> upconverter alike - has already run.
	publishSessionSlotContent(m_sessionScheduler, m_sessionModel);
	updateLength();
	setModified(false);
	m_loadOnLaunch = false;
}


// only save current song as filename and do nothing else

/*! ARCH-4 S7 (SPEC-ARCH-4 1.1 sketch / migration upconversion row): the lane
 *  entities as ONE top-level <z:lanes> section, written ONLY when some track's
 *  TakeLaneModel is non-empty - the additive rule at document scope (a project
 *  that never comped gains no section, no `z` binding and byte-identical
 *  output) and the compatibility rule (SPEC-ARCH-4 2: new state only in
 *  top-level namespaced sections, never as a child of <track>, because an older
 *  build's loadTrack materialises an unrecognised <track> child as a phantom
 *  Clip - 5.2 risk 1). Each non-empty track contributes a <z:tracklanes>
 *  group keyed by its track id, filled by TakeLaneModel::saveSettings: the same
 *  z:lane/z:segment children a journal checkpoint gets under the track's own
 *  <takelanes>, the two contexts docs/COMPING.md section 3 records. */
static void writeLanesSection( DataFile & dataFile, const TrackContainer::TrackList & tracks )
{
	bool any = false;
	for( const Track * track : tracks )
	{
		if( track->takeLanes().hasPersistentState() ) { any = true; break; }
	}
	if( ! any ) { return; }

	// ONE spelling of the `z` binding - the same attribute and URI the
	// <z:provenance> writer below sets (DocumentIndex.h). setAttribute through
	// a document handle is idempotent, so a document carrying two sections
	// binds the prefix once and its bytes do not depend on write order.
	QDomElement & content = dataFile.content();
	QDomElement root = dataFile.documentElement();
	root.setAttribute( documentIndexNamespaceAttribute(), documentIndexNamespaceUri() );

	QDomDocument document = content.ownerDocument();
	QDomElement section = document.createElement( QStringLiteral( "z:lanes" ) );
	section.setAttribute( QStringLiteral( "v" ), QStringLiteral( "1" ) );
	for( Track * track : tracks )
	{
		if( ! track->takeLanes().hasPersistentState() ) { continue; }
		QDomElement group = document.createElement( QStringLiteral( "z:tracklanes" ) );
		group.setAttribute( QStringLiteral( "track" ), track->id() );
		track->takeLanes().saveSettings( document, group );
		section.appendChild( group );
	}
	content.appendChild( section );
}

QString Song::containerV2ConfigAttribute()
{
	return QStringLiteral("savecontainerv2");
}




bool Song::globalContainerV2()
{
	return ConfigManager::inst()->value(QStringLiteral("app"), containerV2ConfigAttribute()).toInt() != 0;
}




QString Song::containerV2Note()
{
	return tr("A v2 container is a larger file than the legacy .mmpz (its sections are stored, "
		"not compressed), and builds older than this one cannot open it. Ordinary saves stay "
		"legacy unless you opt in.");
}




bool Song::isContainerPath(const QString & target)
{
	return target.endsWith(QStringLiteral(".mmpz"), Qt::CaseSensitive);
}




Song::SaveFormat Song::plannedSaveFormat(const QString & target) const
{
	// Only a `.mmpz` name can hold a container (a loader tells the two apart by
	// content, but the extension is the user's hint that a file is a compressed
	// project), and only an opt-in - the global setting, or a project that already
	// IS a container - asks for one. Nothing else changes what a save writes.
	if (!isContainerPath(target)) { return SaveFormat::Legacy; }
	return (m_containerV2Project || globalContainerV2()) ? SaveFormat::V2Container : SaveFormat::Legacy;
}




bool Song::saveProjectFile(const QString & filename, bool withResources, SaveFormat format)
{
	// ARCH-4 S2b, and the whole of the read-only policy for a partial load. A
	// session that holds LESS than the file it came from must not write that
	// loss back: its <song> is missing the sections a partial project.open
	// named, so a save would replace a whole document with a subset of it and
	// nothing downstream could tell the result from a document that merely lost
	// those sections.
	//
	// There is deliberately no "save what you have" mode. The skipped sections
	// are not in this session's DOM at all - that is the mechanism's point - so
	// there is nothing to write them from, and inventing a carrier for them is a
	// FORMAT decision (SPEC-ARCH-4's `z:unclaimed`, or the v2 container) rather
	// than this slice's. Every writer therefore refuses, including the temporary
	// project files render.stems and the mastering run take: a render of an
	// incomplete session is an incomplete render, and refusing is the honest
	// answer rather than a quietly short one.
	m_saveRefusal.clear();
	if (isPartialLoad())
	{
		m_saveRefusal = tr("this session was loaded PARTIALLY: %1 section(s) are not in memory "
			"(%2), so saving would write a document missing them. Reopen the project without "
			"\"sections\" to save it.")
			.arg(m_notLoadedSections.size())
			.arg(m_notLoadedSections.join(QStringLiteral(", ")));
		return false;
	}

	// Include every queued write in an immediate save made during playback.
	if (!RealtimeScope::isActive()) { applyPendingAutomationWrites(); }

	DataFile dataFile( DataFile::Type::SongProject );
	buildProjectDocument( dataFile );
	// WF-033: the undo history, written only by a real save that holds a
	// UndoHistory::SaveScope and only when the project opted in; sealed last so
	// its fingerprint covers the document exactly as it is about to be written.
	UndoHistory::instance().seal( dataFile, withResources );
	dataFile.setContainerV2(format == SaveFormat::V2Container);
	const bool written = dataFile.writeFile(filename, withResources);
	m_lastSavedFormat = written && dataFile.containerV2Written() ? SaveFormat::V2Container : SaveFormat::Legacy;
	return written;
}



void Song::buildProjectDocument( DataFile & dataFile )
{
	using gui::getGUI;

	m_savingProject = true;
	// ARCH-4 S3: nothing has stamped a revision in THIS save yet; writeRevision
	// raises the flag per object and the <head> writer attribute is gated on it
	// at the far end of this function (SPEC-ARCH-4 1.5 R4).
	ProjectIds::beginSave();

	m_tempoModel.saveSettings( dataFile, dataFile.head(), "bpm" );
	m_timeSigModel.saveSettings( dataFile, dataFile.head(), "timesig" );
	m_masterVolumeModel.saveSettings( dataFile, dataFile.head(), "mastervol" );
	m_masterPitchModel.saveSettings( dataFile, dataFile.head(), "masterpitch" );
	// Written only when it is not the default, so a project that does not use
	// MIDI depth keeps the header it has always had.
	NoteRandom::writeProjectSeed( dataFile.head(), m_midiSeed );

	saveState( dataFile, dataFile.content() );

	Engine::mixer()->saveState( dataFile, dataFile.content() );
	if( getGUI() != nullptr )
	{
		getGUI()->getControllerRackView()->saveState( dataFile, dataFile.content() );
		getGUI()->pianoRoll()->saveState( dataFile, dataFile.content() );
		getGUI()->automationEditor()->m_editor->saveState( dataFile, dataFile.content() );
		getGUI()->getProjectNotes()->SerializingObject::saveState( dataFile, dataFile.content() );
		getTimeline(PlayMode::Song).saveState(dataFile, dataFile.content());
	}

	saveControllerStates( dataFile, dataFile.content() );

	saveScaleStates(dataFile, dataFile.content());
	saveKeymapStates(dataFile, dataFile.content());

	// The song-state sections beside the track container (tempo map, modulation layer, groove
	// pool, chord track, detected key, visibility sets, and every registered ProjectSection):
	// SongSections.cpp.
	saveProjectSections( dataFile );

	// Only projects that use the session view carry scene state; a pre-session
	// project re-saves without a section (see SessionModel::shouldPersist).
	// ARCH-4 S8 (SPEC-ARCH-4 5.1 row S8, migration row :403): the section is
	// the NATIVE top-level <z:scenes>, not the legacy <session> block - the
	// <session> READER below stays as the upconverter for files carrying the
	// old form, and SessionModel::saveState keeps the <session> shape for the
	// undo checkpoint payload (ControlCommandsSession.cpp sessionBlockXml).
	// Written before the <z:lanes> section so the unclaimed tail and the
	// provenance write stay last (S7's ordering comment). Nothing is appended
	// for a project that never used the session view: the additive rule at
	// document scope, and the byte-identity the S8 row owes.
	if( m_sessionModel.shouldPersist() )
	{
		m_sessionModel.saveScenesSection( dataFile, dataFile.content() );
	}

	// ARCH-4 S7 (SPEC-ARCH-4 1.1 / migration row): the lane entities, before
	// the unclaimed tail so 1.6.1's re-emission stays last, and before the
	// provenance write so that section's comment about being placed before the
	// tail still holds. Writes nothing when no track holds lanes (I9 at
	// document scope).
	writeLanesSection( dataFile, tracks() );

	// ARCH-4 S6 (SPEC-ARCH-4 1.9): the append-only <z:provenance> section,
	// written ONLY when this session has recorded a change - the additive rule
	// (a project that never carried one saves the exact bytes it always did) -
	// and bounded exactly as ProjectJournal is bounded (Section::trim quotes
	// the journal's own count cap and byte budget). Every entry loaded from
	// the file is re-emitted before this session's own, so a second save
	// APPENDS rather than rewrites. Placed before the unclaimed tail because
	// 1.6.1's re-emission must stay the last thing this writer emits.
	provenance::writeTo( dataFile );

	// SPEC-ARCH-4 1.6.1: the <song> sections this build did not claim, put back
	// in capture order AFTER everything this build writes. That is the position
	// the <session> block above has always been written at, and the only one
	// available to a build that cannot know where a newer writer put them (this
	// writer has never preserved section ORDER - it emits a fixed sequence, so
	// trackcontainer comes first whatever the file said). An empty list appends
	// nothing, which is why a project this build understands completely still
	// round-trips byte-identically.
	reemitUnclaimed( m_unclaimedElements, dataFile, dataFile.content() );

	m_savingProject = false;

	// The project-scoped id counter, on the root element beside version /
	// creatorversion (SPEC-stable-ids.md 3.2). The root is already a mutable
	// document header: DataFile's constructor writes five sibling attributes
	// there and DataFile::upgrade() rewrites them. This is the ONLY place ids
	// reach the disk - load-time assignment is in memory only, which is what
	// keeps the agent_surface gate's fixture byte-identical across a sweep.
	dataFile.documentElement().setAttribute( "next-id", ProjectIds::next() );

	// ARCH-4 S3 (SPEC-ARCH-4 1.5 R4): `writer`, the instance identifier behind
	// the per-object `rev`/`writer` pair, in <head> where the spec puts it -
	// written ONLY when this save actually carries a revision (the flag
	// ProjectIds::beginSave() cleared and writeRevision() raises). A project
	// nobody revised re-saves exactly the bytes it always had: no `rev` on any
	// object, no `writer` in the head. A file that does carry revisions names
	// here the instance whose `writer` attributes its objects wear, which is
	// how a three-way merge can say WHICH SIDE changed an object instead of
	// rebuilding the answer from XML diffs.
	if (ProjectIds::revisionWritten())
	{
		dataFile.head().setAttribute( "writer", ProjectIds::writerInstance() );
	}

	// ARCH-4 S2a (SPEC-ARCH-4 1.4): the document index, and the ONE condition
	// under which it may be written. The index names and digests the sections of
	// <song> so a reader can be asked for a subset of them, which is only
	// meaningful for a document that carries a section this build did not claim
	// - and writing it for a document that carries none would change the bytes of
	// every project 0.4.0 saves, which is what the additive rule forbids
	// (SPEC-ARCH-4 5.2 risk 2 calls it "a release invariant, not a guideline").
	//
	// So the gate is the S1 preserved set, and it is asked through the SAME two
	// answers the load report is built from, so the index and the report cannot
	// disagree about what a load preserved: unclaimedElements() covers the <song>
	// sections no reader matched and each track's unclaimed children,
	// unclaimedChildren() covers the <trackcontainer> children this build could
	// not construct. Everything re-emitted above is therefore inside the
	// digests, which is the point - the index describes the document as it is
	// saved, not as it was loaded.
	//
	// It is set as a FLAG rather than written here, and DataFile::write builds
	// the index from it, because the digests must be taken AFTER the write-time
	// DOM prune and only the writer knows when that has run. Measured, not
	// assumed: computing the digests here put <journallingObject id="…"
	// metadata="1"/> children into every digest - the elements
	// DataFile::cleanMetaNodes deletes on the way out - so every recorded digest
	// described a document the file never contained. The gate stays here; the
	// ordering cannot.
	//
	// Deliberately conservative. A document whose only preserved content is an
	// unknown-version <session> block, which the session model ignores without
	// reporting it as preserved, gets NO index: a missing index is a missed
	// optimisation, an unearned one breaks a release invariant.
	dataFile.setDocumentIndexEnabled(
		!unclaimedElements().isEmpty() || !unclaimedChildren().isEmpty() );
}



// Save the current song
bool Song::guiSaveProject()
{
	return guiSaveProjectAs(m_fileName);
}




// Save the current song with the given filename
bool Song::guiSaveProjectAs(const QString & filename)
{
	DataFile dataFile(DataFile::Type::SongProject);
	QString fileNameWithExtension = dataFile.nameWithExtension(filename);

	bool withResources = m_saveOptions.saveAsProjectBundle.value();

	// Q050-007: the Save As dialog's answer decides when it was given; every other
	// save takes the plan (global opt-in, or a project that already is a v2
	// container). A v2 container asked for by name is refused for a name that is
	// not `.mmpz` rather than written as something the user did not choose.
	const bool chosen = m_saveOptions.formatChosen;
	const SaveFormat format = chosen
		? (m_saveOptions.saveAsV2Container.value() ? SaveFormat::V2Container : SaveFormat::Legacy)
		: plannedSaveFormat(fileNameWithExtension);
	if (format == SaveFormat::V2Container
		&& !isContainerPath(fileNameWithExtension))
	{
		m_saveRefusal = tr("a v2 container needs a .mmpz file name, and %1 is not one.").arg(fileNameWithExtension);
		qWarning().noquote() << m_saveRefusal;
		m_saveOptions.setDefaultOptions();
		return false;
	}

	// WF-033: Save / Save As is a real save - it carries the undo history when
	// the project opted in (UndoHistory::SaveScope; templates, autosave and
	// render temporaries hold none and never do).
	const UndoHistory::SaveScope historyScope;
	bool const saveResult = saveProjectFile(fileNameWithExtension, withResources, format);

	// After saving, restore default save options.
	m_saveOptions.setDefaultOptions();

	// If we saved a bundle, we keep the project on the original
	// file and still keep it as modified
	if (saveResult && !withResources)
	{
		noteProjectSaved(fileNameWithExtension);
		m_containerV2Project = m_lastSavedFormat == SaveFormat::V2Container;
	}

	return saveResult;
}




void Song::noteProjectSaved(const QString & fileNameWithExtension)
{
	setModified(false);
	setProjectFileName(fileNameWithExtension);
}




void Song::saveControllerStates( QDomDocument & doc, QDomElement & element )
{
	// save settings of controllers
	QDomElement controllersNode = doc.createElement( "controllers" );
	element.appendChild( controllersNode );
	for (const auto& controller : m_controllers)
	{
		controller->saveState(doc, controllersNode);
	}
}




void Song::restoreControllerStates( const QDomElement & element )
{
	QDomNode node = element.firstChild();
	while( !node.isNull() && !isCancelled() )
	{
		Controller * c = Controller::create( node.toElement(), this );
		if (c) {addController(c);}
		else
		{
			// Fix indices to ensure correct connections
			m_controllers.push_back(Controller::create(Controller::ControllerType::Dummy, this));
		}

		node = node.nextSibling();
	}
}



void Song::removeAllControllers()
{
	while (m_controllers.size() != 0)
	{
		removeController(m_controllers.at(0));
	}

	m_controllers.clear();
}



void Song::saveScaleStates(QDomDocument &doc, QDomElement &element)
{
	QDomElement scalesNode = doc.createElement("scales");
	element.appendChild(scalesNode);

	for (const auto& scale : m_scales)
	{
		scale->saveState(doc, scalesNode);
	}
}


void Song::restoreScaleStates(const QDomElement &element)
{
	QDomNode node = element.firstChild();

	for (auto i = std::size_t{0}; i < MaxScaleCount && !node.isNull() && !isCancelled(); i++)
	{
		m_scales[i]->restoreState(node.toElement());
		node = node.nextSibling();
	}
	emit scaleListChanged(-1);
}


void Song::saveKeymapStates(QDomDocument &doc, QDomElement &element)
{
	QDomElement keymapsNode = doc.createElement("keymaps");
	element.appendChild(keymapsNode);

	for (const auto& keymap : m_keymaps)
	{
		keymap->saveState(doc, keymapsNode);
	}
}


void Song::restoreKeymapStates(const QDomElement &element)
{
	QDomNode node = element.firstChild();

	for (auto i = std::size_t{0}; i < MaxKeymapCount && !node.isNull() && !isCancelled(); i++)
	{
		m_keymaps[i]->restoreState(node.toElement());
		node = node.nextSibling();
	}
	emit keymapListChanged(-1);
}

/*! ARCH-4 S7 (SPEC-ARCH-4 1.1/2): the lane section, claimed only in the exact
 *  shape this writer produces - v="1", every child a <z:tracklanes> naming a
 *  track the file also contains. Anything else answers false, so the walk
 *  preserves the element verbatim as unclaimed instead of letting a write-back
 *  rewrite it (the provenance branch's contract). Load order is the writer's
 *  order: <trackcontainer> is emitted first, so by the time the walk reaches
 *  this section the track ids resolve; a file that puts it earlier declines
 *  rather than half-loads. TakeLaneModel::loadSettings clears each model
 *  before filling it, so a section that names one track and not another leaves
 *  the other empty - reset-on-absence rides s4's rule, unchanged. */
static bool loadLanesSection( const Song & song, const QDomElement & section )
{
	if( section.attribute( QStringLiteral( "v" ) ) != QLatin1String( "1" ) ) { return false; }
	for( QDomNode node = section.firstChild(); ! node.isNull(); node = node.nextSibling() )
	{
		if( ! node.isElement() || node.nodeName() != QLatin1String( "z:tracklanes" ) )
		{
			return false;
		}
		const QDomElement group = node.toElement();
		bool ok = false;
		const int trackId = group.attribute( QStringLiteral( "track" ) ).toInt( & ok );
		Track * track = ok ? song.findTrackById( trackId ) : nullptr;
		if( track == nullptr ) { return false; }
		track->takeLanes().loadSettings( group );
		track->takeLanes().synchronizeTakePlayback( track->getClips() );
	}
	return true;
}

/*! The named <song> sections this build reads (SPEC-ARCH-4 1.6.1). Extracted
 *  from loadProject()'s walk when the walk's job became "claim it, or preserve
 *  it": every branch below is what it was inline, one answer added. That answer
 *  is what lets the walk treat anything else as unclaimed rather than dropped.
 *  The five GUI window-state sections answer for themselves, in
 *  restoreGuiSection(), because whether they have a reader depends on the
 *  build. */
bool Song::restoreNamedSection(const QDomElement & element)
{
	const QString name = element.nodeName();

	// The mixer. loadProject() restores it BEFORE this walk on purpose - its
	// channels set the range the track walk needs - so by the time the walk
	// reaches the element it has already been read, and the only thing the walk
	// owes it is the answer "claimed". Without this branch the mixer was an
	// unclaimed section: it was captured and re-emitted beside the writer's own
	// <mixer>, so the SECOND save of any project carried two of them (measured:
	// SessionModelTest::preSessionProjectLoadsAndSavesUnchanged gained exactly
	// one <mixer> block on its second save). The pre-walk lookup and this test
	// both name the element through Engine::mixer()->nodeName(), so they cannot
	// drift apart.
	if( name == Engine::mixer()->nodeName() )
	{
		return true;
	}
	if( name == "trackcontainer" )
	{
		( (JournallingObject *)( this ) )->restoreState( element );
		return true;
	}
	if( name == TrackContainer::visibilitySetsNodeName() )
	{
		// The named visibility sets (owner items 3+20+21; the same project-state
		// shape the tempo map and the modulation layer use). A build without
		// this feature leaves the element alone and re-emits it, exactly as it
		// does for an unknown section.
		loadVisibilitySetState( element );
		return true;
	}
	if( name == "controllers" )
	{
		restoreControllerStates( element );
		return true;
	}
	if( name == "scales" )
	{
		restoreScaleStates( element );
		return true;
	}
	if( name == "keymaps" )
	{
		restoreKeymapStates( element );
		return true;
	}
	// ARCH-4 S6 (SPEC-ARCH-4 1.9): the append-only provenance section, adopted
	// only in the exact shape this writer produces (v="1", well-formed
	// <z:change> children - Section::load's contract). A future version or a
	// foreign child answers false here, so the walk preserves the element
	// verbatim as unclaimed instead of letting a write-back rewrite it.
	if( name == provenance::nodeName() && !s_provenanceAdoptedThisLoad )
	{
		if( provenance::Section::instance().load( element ) )
		{
			s_provenanceAdoptedThisLoad = true;
			return true;
		}
	}
	// ARCH-4 S7 (SPEC-ARCH-4 1.1/2): the lane entities, top-level <z:lanes>.
	// Claim-or-preserve like the provenance branch: the exact shape this
	// writer produces claims it and fills the named tracks' models; anything
	// else answers false so 1.6.1 keeps the element verbatim as unclaimed.
	if( name == QLatin1String( "z:lanes" ) )
	{
		if( loadLanesSection( *this, element ) ) { return true; }
	}
	// The two song-state elements that live behind a lock-free publisher: the
	// tempo map (D11, docs/TEMPO-MAP.md) and the modulation layer (#602,
	// docs/MODULATION.md). Each restores itself from its own <element>; the pair
	// shares ONE helper so this walk carries one test for both instead of one
	// each.
	if( restorePublisherBackedSection( element ) ) { return true; }

	// ARCH-4 S8 (SPEC-ARCH-4 5.1 row S8 / migration row :403): the NATIVE
	// scene cells, top-level <z:scenes> - claim-or-preserve like the
	// <z:lanes> branch above: the exact shape saveScenesSection produces
	// claims it and fills the model; anything else answers false so 1.6.1
	// keeps the element verbatim as unclaimed (a future v="2", a foreign
	// child, an out-of-grid coordinate). No ordering constraint: the section
	// references nothing outside itself, so it claims wherever the walk
	// meets it.
	if( name == QLatin1String( "z:scenes" ) )
	{
		if( m_sessionModel.restoreScenesSection( element ) ) { return true; }
	}
	if( name == "session" )
	{
		// Versioned <session> block (SPEC-zene-studio A1). Unknown or future
		// versions are ignored and preserved by the model. Since ARCH-4 S8
		// this reader is the UPCONVERTER (migration row :403): an old block
		// fills the same model the native section fills, and the next save
		// writes <z:scenes> - the legacy form is the migration's input,
		// never its output again.
		if( m_sessionModel.restoreState( element ) ) { return true; }
		return false;
	}

	return false;
}

/*! The five GUI window-state sections the project writer puts in <song>: the
 *  controller rack, the piano roll, the automation editor, the project notes and
 *  the timeline. With no GUI this answers false for all five, which is the point:
 *  the loader genuinely does not claim them (GuiApplication::getGUI() is null, so
 *  nothing could restore them), and the walk then preserves and reports them the
 *  way it preserves an unknown section - the same build-capability gap the
 *  <session> block is preserved for. A GUI build claims them and never preserves
 *  them, so the two builds agree about what the FORMAT knows while disagreeing
 *  about what this BUILD can read. */
bool Song::restoreGuiSection(const QDomElement & element)
{
	using gui::getGUI;

	if( getGUI() == nullptr ) { return false; }

	const QString name = element.nodeName();
	if( name == getGUI()->getControllerRackView()->nodeName() )
	{
		getGUI()->getControllerRackView()->restoreState( element );
		return true;
	}
	if( name == getGUI()->pianoRoll()->nodeName() )
	{
		getGUI()->pianoRoll()->restoreState( element );
		return true;
	}
	if( name == getGUI()->automationEditor()->m_editor->nodeName() )
	{
		getGUI()->automationEditor()->m_editor->restoreState( element );
		return true;
	}
	if( name == getGUI()->getProjectNotes()->nodeName() )
	{
		getGUI()->getProjectNotes()->SerializingObject::restoreState( element );
		return true;
	}
	if( name == getTimeline(PlayMode::Song).nodeName() )
	{
		getTimeline(PlayMode::Song).restoreState( element );
		return true;
	}
	return false;
}

QStringList Song::unclaimedElements() const
{
	QStringList paths;
	paths.reserve( m_unclaimedElements.size() );

	for( const UnclaimedElement & element : m_unclaimedElements )
	{
		// The walk's parent element IS the document's <song>, so a captured
		// section sits one level below it.
		paths.append( QStringLiteral( "/song/" ) + element.key );
	}

	// The tracks are written FLAT under <trackcontainer> - TrackContainer::
	// saveSettings writes one child element per track, a folder's children
	// included - so a track's index in tracks() is its index in the document.
	const TrackList& all = tracks();
	for( int i = 0; i < static_cast<int>( all.size() ); ++i )
	{
		for( const UnclaimedElement & child : all.at( i )->unclaimedChildren() )
		{
			paths.append( QStringLiteral( "/song/trackcontainer/track[%1]/%2" )
				.arg( i ).arg( child.key ) );
		}
	}

	// And the children of <trackcontainer> this build could not construct
	// (ARCH-4 S1c): a <track> whose `type` has no class here, or an element that
	// is not a track at all. They are re-emitted AFTER the tracks above, so the
	// j-th of them is child number tracks().size()+j of the element - which is
	// what the index in the path means, exactly as it does one line up. The name
	// is the element's OWN, so a refused track reports as `track[n]` and
	// anything else reports under the name the newer writer gave it.
	const QVector<UnclaimedElement>& refused = unclaimedChildren();
	for( int j = 0; j < static_cast<int>( refused.size() ); ++j )
	{
		paths.append( QStringLiteral( "/song/trackcontainer/%1[%2]" )
			.arg( refused.at( j ).key )
			.arg( static_cast<int>( all.size() ) + j ) );
	}

	return paths;
}

/*! The song-state elements that live behind a lock-free publisher and carry no
 *  GUI state: the tempo map (D11, docs/TEMPO-MAP.md) and the modulation layer
 *  (#602, docs/MODULATION.md). Each restores itself from its own <element> -
 *  exactly as loadProject() used to inline it. The pair was extracted from that
 *  walk when the modulation lane's branch put loadProject one over its recorded
 *  CCN target, and the fix is the extraction rather than a re-anchor (Gate 4,
 *  tests/complexity-gate.sh). Returns true when \a node was one of them, so the
 *  walk carries one test for both elements instead of one each. */


ExportOutcome Song::exportProjectMidi(QString const & exportFileName) const
{
	// instantiate midi export plugin
	TrackContainer::TrackList const & tracks = this->tracks();
	TrackContainer::TrackList const & patternStoreTracks = Engine::patternStore()->tracks();

	ExportFilter *exf = dynamic_cast<ExportFilter *> (Plugin::instantiate("midiexport", nullptr, nullptr));
	if (exf)
	{
		return exf->tryExport(tracks, patternStoreTracks, getTempo(), m_masterPitchModel.value(), exportFileName);
	}
	qDebug() << "failed to load midi export filter!";
	return ExportOutcome::NotCreated;

}

namespace
{
//! A TEST hook (see setExportCloseFailureHook): empty in every real run.
ExportCloseFailureHook s_exportCloseFailureHook;
}

ExportOutcome exportOutcomeAfterClose(const QFileDevice& file, bool writeOk)
{
	// A TEST hook: the close is forced to count as failed, the way a flush error or an
	// error the close itself reports would.
	if (s_exportCloseFailureHook && s_exportCloseFailureHook()) { return ExportOutcome::Incomplete; }
	// The file's state AFTER the close: a flush error (the bytes the device still held)
	// and an error the close itself reports only show up then.
	if (file.error() != QFileDevice::NoError) { return ExportOutcome::Incomplete; }
	return writeOk ? ExportOutcome::Written : ExportOutcome::Incomplete;
}

void setExportCloseFailureHook(ExportCloseFailureHook hook)
{
	s_exportCloseFailureHook = std::move(hook);
}



void Song::updateFramesPerTick()
{
	Engine::updateFramesPerTick();
}




void Song::setModified()
{
	setModified(true);
}

void Song::setProjectFileName(QString const & projectFileName)
{
	if (m_fileName != projectFileName)
	{
		m_fileName = projectFileName;
		emit projectFileNameChanged();
	}
}




void Song::addController( Controller * controller )
{
	bool containsController = std::find(m_controllers.begin(), m_controllers.end(), controller) != m_controllers.end();
	if (controller && !containsController)
	{
		m_controllers.push_back(controller);
		emit controllerAdded( controller );

		this->setModified();
	}
}




void Song::removeController( Controller * controller )
{
	auto it = std::find(m_controllers.begin(), m_controllers.end(), controller);
	if (it != m_controllers.end())
	{
		m_controllers.erase(it);

		emit controllerRemoved( controller );
		delete controller;

		this->setModified();
	}
}




void Song::clearErrors()
{
	m_errors.clear();
}



void Song::collectError( const QString error )
{
	if (!m_errors.contains(error)) { m_errors[error] = 1; }
	else { m_errors[ error ]++; }
}



bool Song::hasErrors()
{
	return !(m_errors.isEmpty());
}



QString Song::errorSummary()
{
	QString errors;

	auto i = m_errors.constBegin();
	while (i != m_errors.constEnd())
	{
		errors.append( i.key() );
		if( i.value() > 1 )
		{
			errors.append( tr(" (repeated %1 times)").arg( i.value() ) );
		}
		errors.append("\n");
		++i;
	}

	errors.prepend( "\n\n" );
	errors.prepend( tr( "The following errors occurred while loading: " ) );

	return errors;
}

bool Song::isSavingProject() const {
	return m_savingProject;
}


std::shared_ptr<const Scale> Song::getScale(unsigned int index) const
{
	if (index >= MaxScaleCount) {index = 0;}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	return std::atomic_load(&m_scales[index]);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
}


std::shared_ptr<const Keymap> Song::getKeymap(unsigned int index) const
{
	if (index >= MaxKeymapCount) {index = 0;}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	return std::atomic_load(&m_keymaps[index]);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
}


void Song::setScale(unsigned int index, std::shared_ptr<Scale> newScale)
{
	if (index >= MaxScaleCount) {index = 0;}

	Engine::audioEngine()->requestChangeInModel();
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	std::atomic_store(&m_scales[index], newScale);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
	emit scaleListChanged(index);
	Engine::audioEngine()->doneChangeInModel();
}


void Song::setKeymap(unsigned int index, std::shared_ptr<Keymap> newMap)
{
	if (index >= MaxKeymapCount) {index = 0;}

	Engine::audioEngine()->requestChangeInModel();
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	std::atomic_store(&m_keymaps[index], newMap);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
	emit keymapListChanged(index);
	Engine::audioEngine()->doneChangeInModel();
}
} // namespace lmms
