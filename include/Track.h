/*
 * Track.h - declaration of Track class
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

#ifndef LMMS_TRACK_H
#define LMMS_TRACK_H

#include <atomic>
#include <memory>
#include <vector>

#include <QColor>

#include "AutomatableModel.h"
#include "JournallingObject.h"
#include "LmmsTypes.h"
#include <optional>
#include "TakeLane.h"
#include "UnclaimedElements.h"


namespace lmms
{

class TimePos;
class TrackContainer;
class TrackFolder;
class Clip;
class SampleBuffer;
class IntModel;


namespace gui
{

class TrackView;
class TrackContainerView;

}


/*! The minimum track height in pixels
 *
 * Tracks can be resized by shift-dragging anywhere inside the track
 * display.  This sets the minimum size in pixels for a track.
 */
const int MINIMAL_TRACK_HEIGHT = 32;
const int DEFAULT_TRACK_HEIGHT = 32;

char const *const FILENAME_FILTER = "[\\0000-\x1f\"*/:<>?\\\\|\x7f]";


//! Base-class for all tracks
class LMMS_EXPORT Track : public Model, public JournallingObject
{
	Q_OBJECT
	mapPropertyFromModel(bool,isMuted,setMuted,m_mutedModel);
	mapPropertyFromModel(bool,isSolo,setSolo,m_soloModel);
public:
	using clipVector = std::vector<Clip*>;

	enum class Type
	{
		Instrument,
		Pattern,
		Sample,
		Event,
		Video,
		Automation,
		HiddenAutomation,
		/*! A FOLDER TRACK: a real container that holds other tracks
		 *  (docs/TRACK-FOLDER-DESIGN.md; owner items 3+20+21).
		 *
		 *  It sits IMMEDIATELY BEFORE Count for one reason that is checkable:
		 *  `type` is persisted as this enum's integer (saveTrack's `type`
		 *  attribute) and read back the same way, so every arm above keeps the
		 *  number it has always had. Only Count's own value moves, and Count
		 *  is used in-process only (TrackContainer::countTracks's "all types"
		 *  default).
		 *
		 *  A folder is a row of the SAME flat track list as every other track,
		 *  not a second container: iteration, document order, the save/load walk
		 *  and the trk-<n> addressing model all keep working unchanged. The
		 *  parent relation is an ATTRIBUTE (`folder`) on the child's own <track>
		 *  element, resolved after the load walk (Song::resolveTrackFolders).
		 */
		Folder,
		Count
	} ;

	Track( Type type, TrackContainer * tc );
	~Track() override;

	static Track * create( Type tt, TrackContainer * tc );
	static Track * create( const QDomElement & element,
							TrackContainer * tc );
	Track * clone();


	// pure virtual functions
	Type type() const
	{
		return m_type;
	}

	virtual bool play( const TimePos & start, const f_cnt_t frames,
						const f_cnt_t frameBase, int clipNum = -1 ) = 0;



	virtual gui::TrackView * createView( gui::TrackContainerView * view ) = 0;
	virtual Clip * createClip( const TimePos & pos ) = 0;

	virtual void saveTrackSpecificSettings(QDomDocument& doc, QDomElement& parent, bool presetMode) = 0;
	virtual void loadTrackSpecificSettings( const QDomElement & element ) = 0;

	// Saving and loading of presets which do not necessarily contain all the track information
	void savePreset(QDomDocument & doc, QDomElement & element);
	void loadPreset(const QDomElement & element);

	// Saving and loading of full tracks
	void saveSettings( QDomDocument & doc, QDomElement & element ) override;
	void loadSettings( const QDomElement & element ) override;

	// -- for usage by Clip only ---------------
	Clip * addClip( Clip * clip );
	void removeClip( Clip * clip );
	// -------------------------------------------------------
	void deleteClips();

	int numOfClips();
	auto getClip(std::size_t clipNum) -> Clip*;
	int getClipNum(const Clip* clip );

	const clipVector & getClips() const
	{
		return m_clips;
	}
	void getClipsInRange( clipVector & clipV, const TimePos & start,
							const TimePos & end );
	void swapPositionOfClips( int clipNum1, int clipNum2 );

	/*! This track's take lanes and the composite they are comped into
	 *  (docs/COMPING.md, docs/CLIP-CAPTURE-DESIGN.md slices D/F).
	 *
	 *  The lanes are a CHILD RELATIONSHIP of the track, not a second track
	 *  type: the takes stay in this track's own clip list, tagged by
	 *  `Clip::laneIndex()`. The composite is a view over those tags - it holds
	 *  indices and tick ranges only, and nothing in it can write a take's audio.
	 *  Empty by default, and an empty one writes no element at all. */
	TakeLaneModel& takeLanes() { return m_takeLanes; }
	const TakeLaneModel& takeLanes() const { return m_takeLanes; }

	/*! A FROZEN TAKE: this track's own output rendered to audio, which the
	 *  engine then plays INSTEAD of the clips it was rendered from.
	 *
	 *  Freeze / bounce-in-place, docs/RELEASE-NOTES-v0.3.0-alpha.md. The take
	 *  carries the track's devices, fader, pan and sends - it is exactly the
	 *  signal the stems export produces (BounceInPlace) - so the engine plays it
	 *  at the mix level and NOT through the track's own chain a second time.
	 *  The source is disabled for as long as the take covers the timeline: the
	 *  concrete Track::play() overloads return the take for a pass inside the
	 *  take's window and never schedule the source's own playback.
	 *
	 *  The state is per-track project state: it is serialised as ATTRIBUTES on the
	 *  track's own element (frozenAudio / frozenStart / frozenEnd / frozenMuted),
	 *  written only when frozen and reset on absence by Track::loadTrack - the
	 *  rule every journal checkpoint restore depends on - so a frozen track
	 *  survives save/load and one `control.undo` can take the freeze off. */
	struct FrozenTake
	{
		QString path;           //!< the rendered audio file
		tick_t startTicks = 0;  //!< where the take begins on the timeline
		tick_t endTicks = 0;    //!< where it ends (0 = to its own end)
		//! A clip the freeze muted, recorded so unfreeze restores exactly what
		//! it muted and nothing else. `freeze.track` mutes nothing (the engine
		//! substitution already silences the source); `freeze.region` mutes the
		//! clips that start inside the region, which the substitution does not
		//! cover.
		struct MutedClip
		{
			tick_t startTicks = 0;
			tick_t lengthTicks = 0;
		};
		std::vector<MutedClip> mutedClips;

		bool isFrozen() const { return !path.isEmpty(); }
	};

	bool isFrozen() const { return m_frozen.isFrozen(); }
	const FrozenTake& frozenTake() const { return m_frozen; }
	//! True when the take's audio is in memory, i.e. it will actually sound.
	bool frozenAudioReady() const { return m_frozenBuffer != nullptr; }

	/*! SPEC-ARCH-4 1.6.1: the children of this track's own <track> element that
	 *  the last load did not claim - not this track type's own element, not
	 *  <takelanes>, not one of the two booleans the track reads itself, and not
	 *  a CLIP element (a clip belongs to the class createClip() builds, and
	 *  claimClipOrPreserveChild() hands it to that class). Each is kept verbatim
	 *  and re-emitted by saveTrack(), so a project written by a newer build
	 *  cannot lose a child this build never heard of by being opened and saved
	 *  here; Song::unclaimedElements() composes these into the report's paths.
	 *  (1.6.3: this branch used to make a real Clip of every unknown child.) */
	const QVector<UnclaimedElement>& unclaimedChildren() const { return m_unclaimedChildren; }

	/*! Loads \a path into memory and makes this track play it instead of its own
	 *  clips. The load happens HERE, on the calling (control or load) thread,
	 *  never on the audio thread. Returns false, with \a error filled, when the
	 *  file cannot be opened - in which case nothing is changed. */
	bool freezeTo(const QString& path, tick_t startTicks, tick_t endTicks,
			const std::vector<FrozenTake::MutedClip>& mutedClips, QString* error);
	//! Unmutes the clips this freeze muted and drops the take: the track plays
	//! its own clips again.
	void unfreeze();
	//! Drops the take WITHOUT touching any clip's mute state. Used where the
	//! incoming state is authoritative - Track::loadTrack's reset on absence,
	//! which is what makes a checkpoint restore (undo) unable to resurrect a
	//! take the file does not carry.
	void clearFrozenTake();

	void createClipsForPattern(int pattern);


	void insertBar( const TimePos & pos );
	void removeBar( const TimePos & pos );

	bar_t length() const;


	inline TrackContainer* trackContainer() const
	{
		return m_trackContainer;
	}

	/*! The FOLDER this track sits in, or nullptr when it is in none
	 *  (docs/TRACK-FOLDER-DESIGN.md section 1.2; owner items 3+20+21).
	 *
	 *  The relation is REFERENCE-ONLY in both directions and has exactly ONE
	 *  source of truth: this pointer. A TrackFolder never owns its children -
	 *  TrackContainer is the only owner (removeTrack erases without deleting,
	 *  ~TrackContainer's clearAllTracks deletes) - so a folder that deleted a
	 *  child would double-free it. A folder's child list is DERIVED by walking
	 *  the container for this pointer, which is what keeps a checkpoint restore
	 *  from having two states to keep in step.
	 */
	TrackFolder* parentFolder() const
	{
		return m_parentFolder;
	}
	//! Links this track into \a folder (nullptr = the container root) and keeps
	//! the folder's routing mode coherent. Defined in Track.cpp, which is where
	//! the folder's own invariants live.
	void setParentFolder( TrackFolder* folder );

	/*! The id a project file named as this track's folder, or -1 when the file
	 *  named none.
	 *
	 *  A file may name a folder that is constructed LATER in the same walk (a
	 *  folder created after the tracks it holds sits after them in the
	 *  container, and therefore after them in the file), so the link is
	 *  finished after the walk by TrackContainer::resolveTrackFolders - the
	 *  same shape ControllerConnection::finalizeConnections and
	 *  AutomationClip::resolveAllIDs already use.
	 */
	int pendingFolderId() const
	{
		return m_pendingFolderId;
	}
	void setPendingFolderId( int id )
	{
		m_pendingFolderId = id;
	}

	/*! Whether this track is part of the VISIBLE set - the state a named
	 *  visibility set applies (TrackContainer::applyVisibilitySet).
	 *
	 *  A persisted per-track flag with no audio meaning: hiding a track in
	 *  0.3.0 does NOT mute it and does not change a render (proved by the
	 *  registered transcript's negative control). It is written to the file
	 *  only when it is false, so a project that never hid a track re-saves the
	 *  bytes it always had, and Track::loadTrack RESETS it on absence - the
	 *  rule every reset-on-absence follows, without which a checkpoint restore
	 *  could not take a hide back off.
	 */
	bool isVisible() const
	{
		return m_visible;
	}
	void setVisible( bool visible )
	{
		m_visible = visible;
	}

	/*! The mixer channel this track's own output is bound to, or nullptr for a
	 *  track type that has no channel of its own (a folder, an automation
	 *  track).
	 *
	 *  A folder in ROUTING mode points each child's this-way at the folder's
	 *  own channel - the model side of "its children's output summed through
	 *  it" - so the rewire is one virtual call rather than a dynamic_cast to
	 *  the two types that happen to have a channel today.
	 */
	virtual IntModel* mixerChannelModel()
	{
		return nullptr;
	}

	// name-stuff
	virtual const QString & name() const
	{
		return m_name;
	}

	QString displayName() const override
	{
		return name();
	}

	/*! The track's stable id, the number in "trk-<n>" (SPEC-stable-ids.md).
	 *
	 * Assigned ONCE, in the constructor, and never changed while the track is
	 * alive: it names the object, not its position in Song::tracks(). The only
	 * other writer is loadTrack(), which takes the value the project file
	 * carries so a re-save keeps it; a file element with no id leaves the
	 * constructor's value in place, which is what makes legacy assignment
	 * deterministic (construction order == document order).
	 */
	int id() const
	{
		return m_id;
	}

	//! Take \a id from a project file. Raises the project counter above it, so
	//! the number can never be handed to a new object (see ProjectIds).
	void setId(int id);

	using Model::dataChanged;

	inline int getHeight()
	{
		return m_height >= MINIMAL_TRACK_HEIGHT
			? m_height
			: DEFAULT_TRACK_HEIGHT;
	}
	inline void setHeight( int height )
	{
		m_height = height;
	}

	void lock()
	{
		m_processingLock.lock();
	}
	void unlock()
	{
		m_processingLock.unlock();
	}
	bool tryLock()
	{
		return m_processingLock.tryLock();
	}

	auto color() const -> const std::optional<QColor>& { return m_color; }
	void setColor(const std::optional<QColor>& color);

	bool isMutedBeforeSolo() const
	{
		return m_mutedBeforeSolo;
	}
	
	BoolModel* getMutedModel();

public slots:
	virtual void setName(const QString& newName);

	void setMutedBeforeSolo(const bool muted)
	{
		m_mutedBeforeSolo = muted;
	}

	void toggleSolo();

private:
	void saveTrack(QDomDocument& doc, QDomElement& element, bool presetMode);
	void loadTrack(const QDomElement& element, bool presetMode);
	//! Reads the frozen-take attributes a project file carries (freeze /
	//! bounce-in-place) and opens the take's audio. A no-op on an element that
	//! carries no frozenAudio attribute.
	void loadFrozenTake(const QDomElement& element);
	//! Connects (once) the transport signals that end a pass through a take: a
	//! stop, a seek or loop, and a mute change. Called when a take is installed.
	void subscribeFrozenTakeSignals();

private:
	TrackContainer* m_trackContainer;
	Type m_type;
	int m_id;
	QString m_name;
	int m_height;

protected:
	BoolModel m_mutedModel;
	BoolModel m_soloModel;

	/*! Queues the frozen take for a pass that starts at \a start, on its own
	 *  mix-level bus handle: the take already carries this track's devices,
	 *  fader, pan and sends, so it is summed into the mix and NOT through the
	 *  track's chain a second time. Returns true when a handle was queued.
	 *
	 *  Called from the AUDIO thread by InstrumentTrack::play and
	 *  SampleTrack::play - the two audio-producing track types - and reads only
	 *  state the control thread published before the take became visible
	 *  (the buffer and the take's window), plus one atomic flag that keeps one
	 *  pass through the take to one play handle. */
	bool playFrozenTake(const TimePos& start, f_cnt_t frames, f_cnt_t offset);

private:
	bool m_mutedBeforeSolo;

	clipVector m_clips;

	//! Take lanes + composite (comping; docs/COMPING.md). Serialised by
	//! Track::saveTrack as a <takelanes> child, absent when empty.
	TakeLaneModel m_takeLanes;

	//! The children of this track's <track> element that the last load did not
	//! claim, kept verbatim and re-emitted by saveTrack() (SPEC-ARCH-4 1.6.1).
	//! Cleared per load, like every other reset-on-absence member here.
	QVector<UnclaimedElement> m_unclaimedChildren;

	//! The frozen take (freeze / bounce-in-place). `m_frozenBuffer` is loaded
	//! on the control thread and never touched by the audio thread while the
	//! take is frozen; `m_frozenTakePlaying` is the one member the audio thread
	//! writes, so that one pass through the take queues one play handle.
	FrozenTake m_frozen;
	std::shared_ptr<const SampleBuffer> m_frozenBuffer;
	std::atomic<bool> m_frozenTakePlaying{false};
	//! Control-thread only: subscribeFrozenTakeSignals() runs once per track.
	bool m_frozenSignalsConnected = false;

	QMutex m_processingLock;
	
	std::optional<QColor> m_color;

	//! The folder relation (docs/TRACK-FOLDER-DESIGN.md section 1.2): the ONLY
	//! source of truth for membership, in the child. A folder derives its child
	//! list from these pointers, so a checkpoint restore that re-reads this
	//! track's XML cannot leave a folder holding a stale list.
	TrackFolder* m_parentFolder{nullptr};
	//! The folder id the last load named, -1 when it named none; cleared by the
	//! post-walk resolution.
	int m_pendingFolderId{-1};
	//! The visibility flag a named visibility set writes; true unless the file
	//! says otherwise (reset on absence in loadTrack).
	bool m_visible{true};

	friend class gui::TrackView;
	friend class TrackFolder;


signals:
	void destroyedTrack();
	void nameChanged();
	void clipAdded( lmms::Clip * );
	void colorChanged();
} ;


} // namespace lmms

#endif // LMMS_TRACK_H
