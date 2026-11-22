/*
 * Song.h - class song - the root of the model-tree
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

#ifndef LMMS_SONG_H
#define LMMS_SONG_H

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <QString>
#include <QHash>  // IWYU pragma: keep
#include <QPointer>
#include <QStringList>

#include "AudioEngine.h"
#include "AutomationRenderScratch.h"
#include "Controller.h"
#include "ExportFilter.h"
#include "Metronome.h"
#include "lmms_constants.h"
#include "lmmsconfig.h"
#include "MeterModel.h"
#include "TempoMap.h"
#include "ModulationLayer.h"
#include "GroovePool.h"
#include "ChordTrack.h"

#include "ProjectKey.h"
#include "Timeline.h"
#include "TrackContainer.h"
#include "UnclaimedElements.h"
#include "VstSyncController.h"

#include "SessionModel.h"
#include "SessionScheduler.h"

namespace lmms
{

class AutomationTrack;
class DataFile;
class Keymap;
class MidiClip;
class Scale;

namespace gui
{
class SongEditor;
class ControllerRackView;
}


const bpm_t MinTempo = 10;
const bpm_t DefaultTempo = 140;
const bpm_t MaxTempo = 999;
const tick_t MaxSongLength = 9999 * DefaultTicksPerBar;


class LMMS_EXPORT Song : public TrackContainer
{
	Q_OBJECT
	mapPropertyFromModel( int,getTempo,setTempo,m_tempoModel );
	mapPropertyFromModel( int,masterPitch,setMasterPitch,m_masterPitchModel );
	mapPropertyFromModel( int,masterVolume,setMasterVolume, m_masterVolumeModel );
public:
	enum class PlayMode
	{
		None,
		Song,
		Pattern,
		MidiClip,
		AutomationClip,
		Count
	} ;
	constexpr static auto PlayModeCount = static_cast<std::size_t>(PlayMode::Count);

	struct SaveOptions {
		/**
		 * Should we discard MIDI ControllerConnections from project files?
		 */
		BoolModel discardMIDIConnections{false};
		/**
		 * Should we save the project as a project bundle? (with resources)
		 */
		BoolModel saveAsProjectBundle{false};
		/**
		 * Q050-007: write this save as a `.mmpz` v2 container instead of the
		 * legacy single blob. A one-shot choice like the two above.
		 */
		BoolModel saveAsV2Container{false};
		/**
		 * True once the Save As dialog was answered: saveAsV2Container then
		 * decides the format, ticked or not (unticking is the way a v2 project
		 * goes back to legacy). False for every other save, which takes
		 * Song::plannedSaveFormat().
		 */
		bool formatChosen = false;

		void setDefaultOptions() {
			discardMIDIConnections.setValue(false);
			saveAsProjectBundle.setValue(false);
			saveAsV2Container.setValue(false);
			formatChosen = false;
		}
	};

	/*! Q050-007: the two shapes a `.mmpz` project can be saved in. Legacy is the
	 *  single qCompress() blob every earlier build wrote and reads - the default
	 *  of every save. V2Container is the opt-in STORE-ZIP of one entry per
	 *  section (ProjectContainer.h): larger, and unreadable by builds that
	 *  predate it. */
	enum class SaveFormat { Legacy, V2Container };

	//! The config key (class "app") of the GLOBAL opt-in: "1" makes every save of
	//! a `.mmpz` project a v2 container. Absent or "0" - the default - saves legacy.
	static QString containerV2ConfigAttribute();
	//! True when the global opt-in above is on.
	static bool globalContainerV2();
	//! The one-sentence cost of the v2 container, shown wherever a user or an
	//! agent is offered or told about it: the larger file and the older builds.
	static QString containerV2Note();

	/*! The format a save to \a target takes when the caller named none: the
	 *  v2 container when the global opt-in is on OR this project was opened from
	 *  (or last explicitly saved as) a v2 container, and the target is a `.mmpz`
	 *  name; the legacy blob otherwise - which is every save of every project
	 *  that never opted in. */
	SaveFormat plannedSaveFormat(const QString & target) const;
	/*! True when \a target names a file a v2 container may be written to. EXACTLY the
	 *  lowercase `.mmpz` suffix, because that is the spelling DataFile::nameWithExtension()
	 *  and the writer recognise: a differently-cased suffix is treated as no suffix and gets
	 *  `.mmpz` appended, so accepting `X.MMPZ` here would save a different file than the one
	 *  named. One definition, used by the plan, project.save and the GUI save. */
	static bool isContainerPath(const QString & target);
	//! True when this session's project is a v2 container (opened as one, or last saved as one).
	bool isContainerV2Project() const { return m_containerV2Project; }
	//! Record the format the session's file now has (called after a successful save to the session's file).
	void setContainerV2Project(bool v2) { m_containerV2Project = v2; }
	//! The format the most recent saveProjectFile() actually wrote.
	SaveFormat lastSavedFormat() const { return m_lastSavedFormat; }

	void clearErrors();
	void collectError( const QString error );
	bool hasErrors();
	QString errorSummary();
	//! The messages collected by the last load, with how often each occurred
	//! (SPEC A13: a headless client reads which samples/plugins failed and why
	//! from a typed result instead of a dialog nobody can answer).
	const QHash<QString, int>& errors() const
	{
		return m_errors;
	}
	//! Why the most recent loadProject() refused the file, empty when it loaded.
	QString loadRefusal() const
	{
		return m_loadRefusal;
	}

	//! The sections a PARTIAL load did not load, in document order - the ones the
	//! caller named through loadProject()'s skip list and the reducer removed
	//! before anything parsed them (SPEC-ARCH-4 1.4, ARCH-4 S2b). Empty for every
	//! load that named none, which is every ordinary load.
	QStringList notLoadedSections() const
	{
		return m_notLoadedSections;
	}

	//! True when this session holds LESS than the file it was loaded from, so
	//! saving it would write that loss back and a render would render an
	//! incomplete project. Such a session is READ-ONLY: saveProjectFile() refuses
	//! and says why through saveRefusal().
	bool isPartialLoad() const
	{
		return !m_notLoadedSections.isEmpty();
	}

	//! Why the most recent saveProjectFile() refused, empty when it wrote.
	QString saveRefusal() const
	{
		return m_saveRefusal;
	}

	void processNextBuffer();

	/*! Sample-accurate automation (feature-list row 9,
	 *  docs/SAMPLE-ACCURATE-AUTOMATION.md): publish every opt-in automation
	 *  clip's curve to the parameters it drives as a per-sample ramp for the
	 *  block that starts at @a blockStart + @a frameOffsetInTick frames into its
	 *  tick. Called once per audio block from process() before any track renders
	 *  a sample of it, and callable directly - SampleAccurateAutomationTest's
	 *  allocation probe calls THIS entry over 64 blocks, so the probe measures
	 *  the function the engine runs and not a copy of it.
	 *
	 *  Realtime-safe: no allocation, no growth, one lock per clip (the clip's
	 *  own), and it returns at its first test when no clip has opted in. */
	void buildAutomationRamps(const TrackList& trackList, const TimePos& blockStart,
		f_cnt_t frames, int frameOffsetInTick);
	//! The same publish against one pinned AutomationLatency snapshot; an audio period passes its own.
	void buildAutomationRamps(const TrackList& trackList, const TimePos& blockStart,
		f_cnt_t frames, int frameOffsetInTick, const AutomationLatency::ReadSnapshot& latencySnapshot);

	inline int getLoadingTrackCount() const
	{
		return m_nLoadingTrack;
	}

	inline int getMilliseconds() const
	{
		return getMilliseconds(m_playMode);
	}

	//! Returns the elapsed milliseconds since the start of the song
	//! This function attempts to give the correct value even despite mid-song tempo changes
	inline int getMilliseconds(PlayMode playMode) const
	{
		return 1000 * getTimeline(playMode).getElapsedSeconds();
	}

	inline int getBars() const
	{
		return currentBar();
	}

	inline int ticksPerBar() const
	{
		return TimePos::ticksPerBar(m_timeSigModel);
	}

	// Returns the beat position inside the bar, 0-based
	inline int getBeat() const
	{
		return getPlayPos().getBeatWithinBar(m_timeSigModel);
	}
	// the remainder after bar and beat are removed
	inline int getBeatTicks() const
	{
		return getPlayPos().getTickWithinBeat(m_timeSigModel);
	}
	inline int getTicks() const
	{
		return currentTick();
	}
	inline f_cnt_t getFrames() const
	{
		return currentFrame();
	}
	inline bool isPaused() const
	{
		return m_paused;
	}

	inline bool isPlaying() const
	{
		return m_playing == true && m_exporting == false;
	}

	inline bool isStopped() const
	{
		return m_playing == false && m_paused == false;
	}

	inline bool isExporting() const
	{
		return m_exporting;
	}

	inline void setExportLoop( bool exportLoop )
	{
		m_exportLoop = exportLoop;
	}

	inline bool isRecording() const
	{
		return m_recording;
	}
	
	inline void setLoopRenderCount(int count)
	{
		if (count < 1)
			m_loopRenderCount = 1;
		else
			m_loopRenderCount = count;
		m_loopRenderRemaining = m_loopRenderCount;
	}
    
	inline int getLoopRenderCount() const
	{
		return m_loopRenderCount;
	}

	bool isExportDone() const;
	int getExportProgress() const;

	inline void setRenderBetweenMarkers( bool renderBetweenMarkers )
	{
		m_renderBetweenMarkers = renderBetweenMarkers;
	}

	/// Render length override, in bars, for a batch of renders of one project.
	///
	/// A whole-project render derives its length from the tracks (`updateLength()`),
	/// and during export that derivation *skips muted tracks* - which is exactly what a
	/// per-track/stem render does. Without an override each stem would therefore be
	/// trimmed to that one track's length, and the stems would line up neither with the
	/// mix nor with each other. 0 (the default) means "no override": the export length
	/// is derived from the tracks exactly as it always has been.
	inline void setExportLengthOverrideBars( int bars )
	{
		m_exportLengthOverrideBars = bars;
	}

	inline int exportLengthOverrideBars() const
	{
		return m_exportLengthOverrideBars;
	}

	/// Bars rendered past the export end, so an effect tail (reverb, delay) is not
	/// truncated. 1 by default: the whole-project render has always appended one bar
	/// (`startExport()`), so the default leaves every existing render's length
	/// exactly as it was.
	inline void setExportTailBars( int bars )
	{
		m_exportTailBars = bars;
	}

	inline int exportTailBars() const
	{
		return m_exportTailBars;
	}

	inline PlayMode playMode() const
	{
		return m_playMode;
	}

	const TimePos& getPlayPos(PlayMode pm) const
	{
		return getTimeline(pm).pos();
	}
	const TimePos& getPlayPos() const
	{
		return getPlayPos(m_playMode);
	}

	void setPlayPos(tick_t ticks, PlayMode playMode)
	{
		getTimeline(playMode).setTicks(ticks);
	}
	void setPlayPos(tick_t ticks)
	{
		setPlayPos(ticks, m_playMode);
	}

	auto getTimeline(PlayMode mode) -> Timeline& { return m_timelines[static_cast<std::size_t>(mode)]; }
	auto getTimeline(PlayMode mode) const -> const Timeline& { return m_timelines[static_cast<std::size_t>(mode)]; }
	auto getTimeline() -> Timeline& { return getTimeline(m_playMode); }
	auto getTimeline() const -> const Timeline& { return getTimeline(m_playMode); }

	/*! 0.5.0 W2: how many times playback has wrapped round a loop (the song's loop, a pattern's or a
	 *  looped clip's) - not a seek, which moves the position too. The audio thread bumps it before the
	 *  wrap's jump is announced, so a playbackPositionJumped listener can tell the two apart; any thread
	 *  may read it. It only counts up and starts at 0 with the song. */
	std::uint64_t loopWraps() const { return m_loopWraps.load(std::memory_order_acquire); }
	//! Test seam: sets the wrap count, so a test can start it near the 32-bit limit (2^32 wraps cannot be played).
	void setLoopWrapsForTest(std::uint64_t wraps) { m_loopWraps.store(wraps, std::memory_order_release); }

	void updateLength();
	bar_t length() const
	{
		return m_length;
	}


	bpm_t getTempo();

	AutomationTrack * globalAutomationTrack()
	{
		return m_globalAutomationTrack;
	}

	/*! Every place that adds automation work for the audio thread calls this AFTER it has added it:
	 *  Track::addClip (\a clips = 1, plus the clip's targets when it already has some),
	 *  AutomationClip::addObject and the AutomationClip copy constructor (\a targets). Load, undo and redo,
	 *  paste, duplicate and track add all reach the scratch through those three, and recording only writes
	 *  nodes into existing clips. The two bounds only ever grow here (removals are not subtracted), so they
	 *  are an upper bound of the true counts; when the larger passes the reserve, ensureAutomationScratch-
	 *  Capacity recounts exactly and reserves 2x under the engine change lock. That is O(1) per call and
	 *  O(project) once per doubling. On the render thread (or inside a RealtimeScope) only the bounds move:
	 *  nothing there may allocate, and the read's overflow redo covers it. */
	void noteAutomationGrowth(std::size_t clips, std::size_t targets) noexcept;
	//! Recounts the clips and automation targets of the song, the global track and the pattern store exactly
	//! and reserves the scratch for them (control thread; takes the engine change lock itself).
	void ensureAutomationScratchCapacity();
	//! Exact counts, for the tests that pin the bounds above.
	void countAutomationForTest(std::size_t* clips, std::size_t* targets) const;
	std::size_t automationBoundForTest() const noexcept
	{
		return std::max(m_automationClipBound.load(), m_automationTargetBound.load());
	}
	//! Test seam: replaces the scratch with one of \a capacity (call with the render locked out).
	void resizeAutomationScratchForTest(std::size_t capacity);
	//! How many ticks read more clips or values than the scratch had room for since the song was made.
	//! Each was redone on an allocating scratch, so the automation applied was still complete; a non-zero
	//! count means ensureAutomationScratchCapacity had not run for a change (e.g. a load) before it played.
	std::uint64_t automationScratchOverflows() const noexcept
	{
		return m_automationScratchOverflows.load(std::memory_order_relaxed);
	}

	//! Adds the song's global automation track to the read (see TrackContainer::fillAutomatedValues).
	//! The 4-argument form pins the current latency snapshot once; an audio period passes its own.
	using TrackContainer::fillAutomatedValues;
	void fillAutomatedValues(AutomationRenderScratch& level, AutomationRenderScratch* inner,
		TimePos time, int clipNum, const AutomationLatency::ReadSnapshot& snapshot) const override;

	/*! Forgets @p model in the automated-value cache of the live song, if there
	 *  is one (see `m_oldAutomatedValues`). The cache holds the RAW pointers of
	 *  the models the song automated on the last frame, and both
	 *  `Song::processAutomations()` and `Song::stop()` dereference every entry;
	 *  a model that is destroyed while it is cached (an instrument or an
	 *  automation track deleted while the transport runs) would be a
	 *  use-after-free in those two walks. `AutomatableModel`'s destructor calls
	 *  this, so the cache cannot end up naming a dead model.
	 *
	 *  Static because the model has no safe way to reach the song: resolving
	 *  `Engine::getSong()` would hand back a song whose cache may already be
	 *  destroyed, since the song's own models are destroyed after it (see the
	 *  declarations below). This pointer is cleared *before* any member of the
	 *  song is destroyed, and only the live song is ever resolvable through it
	 *  (there is one song per `Engine`).
	 */
	static void forgetAutomatedModel( AutomatableModel* model );

	/*! The seed MIDI depth randomisation rolls from. It lives in the project
	 *  header ("midiseed") and is written only when it is not the default 0,
	 *  so a project that does not use it serialises byte-identically. */
	uint32_t midiSeed() const
	{
		return m_midiSeed;
	}

	void setMidiSeed( uint32_t seed )
	{
		if( m_midiSeed != seed )
		{
			m_midiSeed = seed;
			setModified();
		}
	}

	// file management
	void createNewProject();
	void createNewProjectFromTemplate( const QString & templ );
	void loadProject( const QString & filename, const QStringList & skipSections = QStringList() );
	bool guiSaveProject();
	bool guiSaveProjectAs(const QString & filename);
	bool saveProjectFile(const QString & filename, bool withResources = false,
		SaveFormat format = SaveFormat::Legacy);

	/*! WF-033 (docs/UNDO-HISTORY-PERSISTENCE.md): the project as ONE document,
	 *  built exactly as saveProjectFile() builds it - the same writers, plug-in
	 *  state and sample references included - without writing a file. Save
	 *  callers check isPartialLoad(); an undo snapshot may retain a partial
	 *  session's skipped-section list out of band. */
	void buildProjectDocument( DataFile & dataFile );
	/*! WF-033: the state an undo step returns to, compressed - the document
	 *  above with the journal ids kept (so a restore gives every object back
	 *  the id later steps name) and without the append-only <z:provenance> log,
	 *  which an undo never rewinds. Null for a session that cannot be saved.
	 *  When \a skippedSections is supplied, a partial load can be captured too;
	 *  that list is needed to restore its read-only state. */
	std::shared_ptr<const QByteArray> captureUndoSnapshot(QStringList* skippedSections = nullptr);
	/*! WF-033: replaces the project with \a compressed IN PLACE - the undo of a
	 *  step loaded from a file. Keeps what a restore must not touch: the file
	 *  name, the undo journal around the step, the provenance log, the project's
	 *  undo-history setting and the id counter (never handed out twice). */
	bool restoreUndoSnapshot( const QByteArray & compressed );
	/*! Q050-044: the same restore, putting \a fileName back as the project's file name instead of
	 *  keeping the current one - undo of project.open and script.run restores the prior file. */
	bool restoreUndoSnapshot( const QByteArray & compressed, const QString & fileName,
		const QStringList& skippedSections = QStringList() );
	bool isRestoringUndoSnapshot() const { return m_restoringSnapshot; }

	/*! The session state a SUCCESSFUL save records: the file that was written,
	 *  and the clean modified flag.
	 *
	 *  One implementation on purpose. `saveProjectFile()` writes the document
	 *  and touches neither, so a caller of it had to remember both: the GUI's
	 *  guiSaveProjectAs() did, and the control surface's project.save did not -
	 *  it wrote the file and left `modified` true and the file name empty, so
	 *  its own bare form then refused for want of a project file and
	 *  control.quit {save:true} refused after a save that had just succeeded
	 *  (BUG-SAVE-01). Both paths call THIS. */
	void noteProjectSaved(const QString & projectFileName);

	const QString & projectFileName() const
	{
		return m_fileName;
	}

	//! Identity of this loaded document; changes whenever clearProject() replaces its contents.
	quint64 documentIdentity() const { return m_documentIdentity; }

	bool isLoadingProject() const
	{
		return m_loadingProject;
	}

	void loadingCancelled()
	{
		m_isCancelled = true;
		Engine::audioEngine()->clearNewPlayHandles();
	}

	bool isCancelled()
	{
		return m_isCancelled;
	}

	bool isModified() const
	{
		return m_modified;
	}

	QString nodeName() const override
	{
		return "song";
	}

	virtual bool fixedClips() const
	{
		return false;
	}

	void addController( Controller * c );
	void removeController( Controller * c );


	const ControllerVector & controllers() const
	{
		return m_controllers;
	}


	MeterModel & getTimeSigModel()
	{
		return m_timeSigModel;
	}

	IntModel& tempoModel()
	{
		return m_tempoModel;
	}

	//! 0.5.0: the master level and transposition, for the shell header's readouts
	//! (TransportReadouts) - the song editor reached them as a friend.
	IntModel& masterVolumeModel() { return m_masterVolumeModel; }
	IntModel& masterPitchModel() { return m_masterPitchModel; }

	/*! The tempo map: the ordered tempo and time-signature events the timeline
	 *  obeys (docs/TEMPO-MAP.md). The control thread edits it through the
	 *  transport.tempo_map_* commands; the audio thread reads it through the
	 *  publisher's lock-free snapshot in followTempoMap(). An EMPTY or INACTIVE
	 *  map is exactly today's single-tempo engine: it is not persisted, not
	 *  consulted per block, and every tick answers the global tempo. */
	TempoMapPublisher& tempoMap() { return m_tempoMap; }
	const TempoMapPublisher& tempoMap() const { return m_tempoMap; }

	/*! The modulation layer (#602, docs/MODULATION.md): song-level LFO
	 *  modulators that drive a SET of parameters by a relative amount. The
	 *  control thread edits it through the modulator.* commands; the audio
	 *  thread reads it once per block in processModulation(), through the
	 *  publisher's lock-free snapshot. An EMPTY layer is exactly today's
	 *  engine: it is not persisted, not consulted per block, and writes
	 *  nothing. */
	ModulationLayerPublisher& modulationLayer() { return m_modulationLayer; }
	const ModulationLayerPublisher& modulationLayer() const { return m_modulationLayer; }

	/*! The groove pool: the project's NAMED grooves - the timing and velocity
	 *  feel of a note pattern, captured from one clip and re-applied to another
	 *  (include/GrooveTemplate.h, include/GroovePool.h). The control thread
	 *  edits it through the groove.* commands and the notes a groove is applied
	 *  to through the clip's own note list, so nothing here is consulted from a
	 *  render path. An EMPTY pool is exactly the engine as it was before the
	 *  feature existed: it is not persisted and it changes nothing. */
	GroovePool& groovePool() { return m_groovePool; }
	const GroovePool& groovePool() const { return m_groovePool; }

	/*! The chord track: the project's harmony written down as an ENTITY - one
	 *  chord per position, named from the vocabulary this product already has
	 *  (include/ChordVocabulary.h is a view over ChordTable; the piano roll's
	 *  chord and scale selectors read the same table). It is what
	 *  chord.detect_to_track writes, what chord.track_write turns into notes,
	 *  and it persists as ONE <chord-track> element inside <song>, written only
	 *  when it holds a chord (include/ChordTrack.h). Nothing here is consulted
	 *  from a render path: the track is edited by commands and read by them. */
	ChordTrack& chordTrack() { return m_chordTrack; }
	const ChordTrack& chordTrack() const { return m_chordTrack; }

	/*! The project's DETECTED KEY (feature row 34, import detection;
	 *  include/ProjectKey.h): the tonic and the scale name a `detect.apply` wrote
	 *  after analysing an imported audio file, using the pre-existing scale
	 *  vocabulary. It is project state with no audio-thread reader and no
	 *  publisher - the tempo half of a detection goes into the tempo map above,
	 *  which DOES have one - so it is read only by the detect.* commands and the
	 *  project writer. An EMPTY key is exactly the state before the feature:
	 *  nothing is persisted and nothing is consulted. */
	ProjectKey& projectKey() { return m_projectKey; }
	const ProjectKey& projectKey() const { return m_projectKey; }

	/*! The document paths of everything the LAST load did not claim, in the
	 *  order the document carried them (SPEC-ARCH-4 1.6.1/1.6.2/1.6.4): the
	 *  <song> sections no reader matched, then each track's unclaimed children
	 *  as `/song/trackcontainer/track[i]/<name>`, then the <trackcontainer>
	 *  children this build could not CONSTRUCT as `/song/trackcontainer/
	 *  <name>[i]` (a <track> whose `type` has no class here, or an element that
	 *  is not a track at all). These elements are preserved verbatim and
	 *  re-emitted on save, so this list is the report of what a load preserved
	 *  rather than loaded - it is what `project.open` answers with, and it is
	 *  empty for a project this build understands completely.
	 *
	 *  RESET ON ABSENCE, the rule every other project-scoped element in the load
	 *  walk follows: the list describes ONE document and clearProject() empties
	 *  it, so a report can never describe the project before this one. */
	QStringList unclaimedElements() const;

	//! The tempo in force at \a tick: the map's event at or before it, else the
	//! global tempo model (the map's own out-of-range rule).
	int tempoAtTick(tick_t tick) const;
	//! Seconds from tick 0 to \a tick through the map's tempo segments.
	double secondsAtTick(tick_t tick) const;

	/*! Write the session's notes as a Standard MIDI File to \a exportFileName.
	 *
	 *  The file is created EXCLUSIVELY: a path that is already there belongs to someone
	 *  else and is never written to. The outcome says whether THIS call created the file,
	 *  so the caller knows whether it may remove it. */
	ExportOutcome exportProjectMidi(QString const & exportFileName) const;

	inline void setLoadOnLaunch(bool value) { m_loadOnLaunch = value; }
	SaveOptions &getSaveOptions() {
		return m_saveOptions;
	}

	bool isSavingProject() const;

	std::shared_ptr<const Scale> getScale(unsigned int index) const;
	std::shared_ptr<const Keymap> getKeymap(unsigned int index) const;
	void setScale(unsigned int index, std::shared_ptr<Scale> newScale);
	void setKeymap(unsigned int index, std::shared_ptr<Keymap> newMap);

	const std::string& syncKey() const noexcept { return m_vstSyncController.sharedMemoryKey(); }

	Metronome& metronome() { return m_metronome; }

	//! Session View data layer (SPEC-zene-studio A1): native engine code since
	//! ARCH-4 S10 removed WANT_SESSION_VIEW; persisted as the native <z:scenes>
	//! section (a legacy <session> block upconverts through the model).
	SessionModel& sessionModel() { return m_sessionModel; }
	const SessionModel& sessionModel() const { return m_sessionModel; }

	/*! Session View launch scheduler (task #595, SPEC A2/A3). The engine owns
	 *  the launch state; `processNextBuffer()` drives it on the audio thread
	 *  and takes a track over while its session clip plays (SPEC A1). */
	SessionScheduler& sessionScheduler() { return m_sessionScheduler; }
	const SessionScheduler& sessionScheduler() const { return m_sessionScheduler; }

public slots:
	void playSong();
	void record();
	//! A recording pass lost \a frames to a full capture ring (they are padded with silence in
	//! their take). Emits recordTakeDropped on the main thread.
	void reportRecordTakeDropped(qulonglong frames) { emit recordTakeDropped(frames); }
	void playAndRecord();
	void playPattern();
	void playMidiClip( const lmms::MidiClip * midiClipToPlay, bool loop = true );
	void togglePause();
	void stop();

	void startExport();
	void stopExport();
	//! Prepare every instrument's retained converters, then adopt or discard the set.
	//! Leading return type: the Qt 5.15.3 moc cannot parse a trailing one on a slot (ci-v041-1).
	bool prepareConverterModeForInstruments(AudioResampler::Mode mode) noexcept;
	void adoptPreparedConverterModeForInstruments() noexcept;
	void discardPreparedConverterModeForInstruments() noexcept;


	void setModified();

	void clearProject();

	void addPatternTrack();


private slots:
	void insertBar();
	void removeBar();
	void addSampleTrack();
	void addAutomationTrack();

	void setTempo();
	void setTimeSignature();

	void masterVolumeChanged();

	void savePlayStartPosition();

	void updateFramesPerTick();



private:
	//! The track list one period plays, kept to reuse its capacity (processNextBuffer; R7.2).
	TrackList m_periodTracks;
	//! playSong() and playAndRecord(): the transport starts under the model lock (BUGS_FOUND 11.17).
	void startSongPlayback( bool recording );
	Song();
	Song( const Song & );
	~Song() override;


	inline bar_t currentBar() const
	{
		return getPlayPos(m_playMode).getBar();
	}

	inline tick_t currentTick() const
	{
		return getPlayPos(m_playMode).getTicks();
	}

	inline f_cnt_t currentFrame() const
	{
		return getTimeline(m_playMode).ticks() * Engine::framesPerTick() + getTimeline(m_playMode).frameOffset();
	}

	void saveControllerStates( QDomDocument & doc, QDomElement & element );
	void restoreControllerStates( const QDomElement & element );

	void removeAllControllers();

	void saveScaleStates(QDomDocument &doc, QDomElement &element);
	void restoreScaleStates(const QDomElement &element);

	void saveKeymapStates(QDomDocument &doc, QDomElement &element);
	void restoreKeymapStates(const QDomElement &element);

	/*! The song-state elements behind a lock-free publisher, PLUS the groove
	 *  pool: the tempo map (D11), the modulation layer (#602) and the groove
	 *  pool (docs/GROOVE-POOL.md). Restores whichever \a node is and answers
	 *  false when it is none of them, so loadProject()'s element walk carries
	 *  one test for all of them rather than one per element (Gate 4).
	 *
	 *  The name is historical: the tempo map and the layer are publisher-backed
	 *  and the groove pool is plain project state with no audio-thread reader.
	 *  The pool joins them here because what this function dispatches on is
	 *  "a song-state element that restores itself from its own <element>", and
	 *  renaming a private helper that every branch in this line also edits
	 *  would buy no behaviour. */
	bool restorePublisherBackedSection(const QDomNode &node);
	//! The song-state sections beside the track container, and every ProjectSection (SongSections.cpp).
	void saveProjectSections( DataFile & dataFile );

	/*! SPEC-ARCH-4 1.6.1: the named sections of <song> this build does read.
	 *  Restores whichever \a element is and answers true when it was one of
	 *  them - the pair of answers is what lets loadProject()'s walk say "no
	 *  reader claimed this element" without repeating the name list, which is
	 *  what makes an element no branch here knows about preserved rather than
	 *  dropped. */
	bool restoreNamedSection(const QDomElement & element);

	/*! The five GUI window-state sections the project writer puts in <song>
	 *  (the controller rack, the piano roll, the automation editor, the project
	 *  notes and the timeline). Answers false with no GUI at all - in a headless
	 *  build the loader genuinely does not claim them, so the walk preserves and
	 *  reports them, the same build-capability gap the <session> block is
	 *  preserved for. */
	bool restoreGuiSection(const QDomElement & element);

	void processAutomations(const TrackList& tracks, TimePos timeStart, f_cnt_t frames,
		const AutomationLatency::ReadSnapshot& latencySnapshot);
	void noteRecordedModel(const AutomatableModel* model);
	/*! Applies the automation writes the render thread queued in the clips
	 *  (cluster 5388). Control thread; the render thread may not edit a clip's
	 *  node map, and a queued write is not lost when the control thread is
	 *  late - the clip keeps it until it is applied. */
	void applyPendingAutomationWrites();
	//! Drops the queued writes of every clip (the project is being replaced).
	void discardPendingAutomationWrites();
	void processMetronome(size_t bufferOffset);
	//! Recompute every frozen track's overlap allowance on the control thread after loop changes.
	void updateFrozenTakeVoiceCapacities();

	/*! The tempo-map follower (SPEC-zene-studio D11, docs/TEMPO-MAP.md). Runs
	 *  once per audio block, and does NOTHING unless the map is active - which
	 *  is every project that predates the feature, so the timing path of an
	 *  unmapped project is untouched (no snapshot, no arithmetic, no write).
	 *
	 *  When the map IS active it reads the tempo at the play head through the
	 *  publisher's lock-free snapshot and, when that differs from the tempo it
	 *  last applied, hands it to the engine's frame/tick scalar. Changes take
	 *  effect at the START of the block that contains the event; sample-accurate
	 *  tempo automation is a separate, still-open in-list item. */
	void followTempoMap();

	/*! The modulation layer's per-block pass (docs/MODULATION.md). Reads the
	 *  publisher's lock-free snapshot and writes each resolved route's target
	 *  once, from the LFO value at the block's own play position. Returns
	 *  immediately unless the layer is non-empty AND has a resolved route, so a
	 *  project that uses no modulator pays one empty-vector test per block and
	 *  nothing else - the byte-identity guarantee on the audio path. */
	void processModulation();

	/*! Puts the play position back at @a begin when it is outside [@a begin, @a end) and says whether
	 *  it moved. With @a wrapping (the tick loop's checks), a position that played on to @a end is a
	 *  loop wrap and m_loopWraps counts it; a position before @a begin is a seek's, never a wrap. */
	bool enforceLoop(const TimePos& begin, const TimePos& end, bool wrapping);

	void setModified(bool value);

	void setProjectFileName(QString const & projectFileName);

	AutomationTrack * m_globalAutomationTrack;

	IntModel m_tempoModel;
	MeterModel m_timeSigModel;
	//! The tempo map and its lock-free hand-off to the audio thread.
	TempoMapPublisher m_tempoMap;
	//! The tempo the follower last applied, so a block costs one comparison
	//! when nothing changed. -1 means "nothing applied yet". Atomic: the audio
	//! thread's followTempoMap() and the control thread's setTempo() both write it.
	std::atomic<int> m_tempoMapAppliedTempo{-1};
	//! The loop wraps the audio thread played (loopWraps()).
	std::atomic<std::uint64_t> m_loopWraps{0};
	//! The modulation layer and its lock-free hand-off to the audio thread
	//! (#602, docs/MODULATION.md).
	ModulationLayerPublisher m_modulationLayer;
	/*! The project's named grooves. Plain project state with no audio-thread
	 *  reader and no publisher: a groove is applied to a clip's notes once and
	 *  the notes are what plays, so the pool itself is read only by the
	 *  groove.* commands and the project writer. */
	GroovePool m_groovePool;
	/*! The project's chord track. Project state beside the groove pool, with no
	 *  audio-thread reader: a chord track is a way of writing harmony down (and
	 *  of generating notes FROM it), and the notes are what plays. Read only by
	 *  the chord.* commands and the project writer. */
	ChordTrack m_chordTrack;

	/*! The project's detected key (feature row 34, import detection). Plain
	 *  project state with no audio-thread reader and no publisher: it is written
	 *  by detect.apply, read by detect.get_state and the project writer, and
	 *  written to the file ONLY when it holds something, so a project that never
	 *  ran a detection re-saves byte for byte as before. */
	ProjectKey m_projectKey;
	int m_oldTicksPerBar;
	IntModel m_masterVolumeModel;
	IntModel m_masterPitchModel;

	uint32_t m_midiSeed = 0;

	ControllerVector m_controllers;

	int m_nLoadingTrack;
	//! Unique process-wide identity for the current document, distinct across Song instances and reloads.
	quint64 m_documentIdentity;

	QString m_fileName;
	QString m_oldFileName;
	bool m_modified;
	bool m_loadOnLaunch;

	volatile bool m_recording;
	//! Record was pressed (Song::record) and the next Play starts a record pass; a stop clears it.
	bool m_recordLatched = false;
	volatile bool m_exporting;
	volatile bool m_exportLoop;
	volatile bool m_renderBetweenMarkers;
	volatile bool m_playing;
	volatile bool m_paused;

	bool m_savingProject;
	bool m_loadingProject;
	//! WF-033: a snapshot restore's load is in progress (not a file the user opened).
	bool m_restoringSnapshot = false;
	bool m_isCancelled;

	SaveOptions m_saveOptions;

	QHash<QString, int> m_errors;
	//! Set by loadProject() when it refuses a file; empty after a good load.
	QString m_loadRefusal;

	//! The sections the most recent ACCEPTED load did not load, in document
	//! order (SPEC-ARCH-4 1.4, ARCH-4 S2b). A partial project.open names the
	//! sections it does not want and DataFile's reducer removes their bytes
	//! before anything parses them, so nothing in this session corresponds to
	//! them. Non-empty therefore means the session holds LESS than its file -
	//! which is what makes it read-only (see saveProjectFile()).
	QStringList m_notLoadedSections;
	//! Set by saveProjectFile() when it refuses; empty after a write.
	QString m_saveRefusal;
	//! Q050-007: the session's project file is a v2 container (see isContainerV2Project()).
	bool m_containerV2Project = false;
	SaveFormat m_lastSavedFormat = SaveFormat::Legacy;

	std::array<Timeline, PlayModeCount> m_timelines;

	PlayMode m_playMode;
	bar_t m_length;

	// Render-length control for batches of renders (stem export). See
	// setExportLengthOverrideBars()/setExportTailBars() above; both defaults
	// reproduce the historical export length exactly.
	volatile int m_exportLengthOverrideBars;
	volatile int m_exportTailBars;

	const MidiClip* m_midiClipToPlay;
	bool m_loopMidiClip;

	VstSyncController m_vstSyncController;
    
	int m_loopRenderCount;
	int m_loopRenderRemaining;
	TimePos m_exportSongBegin;
	TimePos m_exportLoopBegin;
	TimePos m_exportLoopEnd;
	TimePos m_exportSongEnd;
	TimePos m_exportEffectiveLength;

	std::shared_ptr<Scale> m_scales[MaxScaleCount];
	std::shared_ptr<Keymap> m_keymaps[MaxKeymapCount];

	//! The models the last rendered frame moved control away from, and the values
	//! to put back. GUARDED keys on purpose: the set is written by the render thread
	//! (processNextBuffer) and restored by stop() on another thread, and a model can
	//! be destroyed in between (a track deleted, a plugin unloaded). A raw pointer
	//! here is a use-after-free on the shutdown path - measured as
	//! `AutomationModesTest` aborting with signal 11 at Song.cpp:778, dereferencing a
	//! key whose model the test had already destroyed. QPointer nulls itself when the
	//! model goes, so the restore skips exactly the models that are gone.
	//! A flat list, reserved once in the constructor, and not a QMap: processAutomations rewrites it on
	//! every tick of the audio thread and a QMap allocates a node per entry (RealtimeSanitizer, hosted
	//! run 38707621703). Each model is in it at most once.
	std::vector<std::pair<QPointer<AutomatableModel>, float>> m_oldAutomatedValues;

	//! The audio thread's scratch for processAutomations, reserved in the constructor so a tick never
	//! allocates: the clips and values of the song's read, those of a pattern clip's nested read, the
	//! clips the recording pass looks at, and the models it wrote to. Past their capacity extra entries
	//! are dropped and counted in m_automationScratchOverflows.
	AutomationRenderScratch m_automationLevel{AutomationRenderScratch::RenderCapacity};
	AutomationRenderScratch m_automationInner{AutomationRenderScratch::RenderCapacity};
	AutomationRenderScratch m_automationRecordClips{AutomationRenderScratch::RenderCapacity};
	std::vector<const AutomatableModel*> m_recordedModels;
	std::atomic<std::uint64_t> m_automationScratchOverflows{0};
	std::atomic<std::size_t> m_automationClipBound{0};
	std::atomic<std::size_t> m_automationTargetBound{0};
	//! The smallest capacity of the lists above, as of the last reserve.
	std::atomic<std::size_t> m_automationReserved{AutomationRenderScratch::RenderCapacity};

	//! The song that `Song::forgetAutomatedModel()` reports into, or nullptr.
	//! Set by `Song::Song()`, cleared at the top of `Song::~Song()`.
	//!
	//! It exists because a destroyed model has to be able to reach the cache
	//! above, and the cache has to outlive every model that can report into it:
	//! the song's own models (`m_tempoModel` above, the metronome's below) are
	//! destroyed *after* `m_oldAutomatedValues` is, so a report that arrived
	//! during song teardown would reach a map that is already gone. Clearing this
	//! first is what makes the report a no-op from that point on, instead of
	//! relying on how the members happen to be ordered.
	static Song* s_automationCacheSong;

	Metronome m_metronome;

	SessionModel m_sessionModel;
	SessionScheduler m_sessionScheduler;

	//! The <song> sections the last load did not claim, kept verbatim and
	//! re-emitted on save (SPEC-ARCH-4 1.6.1). Each track keeps its own
	//! unclaimed children (Track::m_unclaimedChildren); unclaimedElements()
	//! composes both into the document paths the report names.
	QVector<UnclaimedElement> m_unclaimedElements;

	friend class Engine;
	friend class gui::SongEditor;
	friend class gui::ControllerRackView;

signals:
	//! A recording pass lost \a frames to capture overflow; the GUI shows a notice. Main thread.
	void recordTakeDropped(qulonglong frames);
	void projectLoaded();
	//! Q050-484: an export is about to start (the main thread, before its render thread does): an instrument that streams
	//! samples from disk loads them whole now, so the export never waits for a loader.
	void exportPreparing();
	//! Q050-484: that export's render thread has finished (the main thread).
	void exportFinished();
	void playbackStateChanged();
	void playbackPositionJumped();
	void lengthChanged( int bars );
	void tempoChanged( lmms::bpm_t newBPM );
	void timeSignatureChanged( int oldTicksPerBar, int ticksPerBar );
	void controllerAdded( lmms::Controller * );
	void controllerRemoved( lmms::Controller * );
	void stopped();
	void modified();
	void projectFileNameChanged();
	void scaleListChanged(int index);
	void keymapListChanged(int index);
} ;


} // namespace lmms

#endif // LMMS_SONG_H
