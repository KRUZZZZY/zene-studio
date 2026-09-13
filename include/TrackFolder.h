/*
 * TrackFolder.h - a track that CONTAINS other tracks
 *                 (docs/TRACK-FOLDER-DESIGN.md; owner items 3+20+21).
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
 */

#ifndef LMMS_TRACK_FOLDER_H
#define LMMS_TRACK_FOLDER_H

#include <vector>

#include "Track.h"

namespace lmms
{

class IntModel;

/*! A track that holds other tracks: a REAL container, not a UI grouping.
 *
 *  The two structural decisions (the design document's sections 1, 2 and 9.1)
 *  are what make this small, and both are load-bearing:
 *
 *  - **A folder is a Track, not a second TrackContainer.** It is one more row
 *    of the SAME flat `TrackContainer::m_tracks` list, so iteration, document
 *    order, the whole save/load walk, the trk-<n> addressing model and every
 *    existing consumer of `tracks()` keep working untouched. Its own element is
 *    `<trackfolder>` and its relation to its children is one ATTRIBUTE on each
 *    CHILD's `<track>` element (`folder="<id>"`), never a nested container and
 *    never a child element - `Track::loadTrack` turns an unrecognised child
 *    element into a real Clip (SPEC-stable-ids.md section 3.1).
 *
 *  - **Children are REFERENCED, never owned.** `TrackContainer` is the only
 *    owner (removeTrack erases without deleting; `~TrackContainer`'s
 *    clearAllTracks deletes), so a folder that deleted a child would double-free
 *    it. Membership has exactly ONE source of truth - `Track::parentFolder()` -
 *    and `children()` derives the list by walking the container for it, which is
 *    what keeps a checkpoint restore from having two states to keep in step.
 *
 *  TWO MODES, and the organisational one is the default (the owner's recorded
 *  decision, 2026-09-12):
 *
 *  - `Mode::Group` - the tracks are organised under the folder and nothing about
 *    the audio path changes. Every child keeps its own mixer channel.
 *  - `Mode::Routing` - the children's output is SUMMED THROUGH the folder: each
 *    child's mixer-channel binding is pointed at one channel the folder owns, so
 *    the folder's channel receives `MixHelpers::add` of every child
 *    (`Mixer::mixToChannel`), runs its own effect chain on the sum and sends to
 *    master like any other channel. This is a regular `Mixer::createChannel()`
 *    channel and NOT `Mixer::createBusChannel()`: a bus refuses instrument
 *    output outright (Mixer.cpp, "a parallel bus never receives instrument
 *    output directly"), so a bus could not be the folder's summing point.
 *
 *    Pointing a child's binding at the folder channel needs no new
 *    delay-compensation code: a child's own AudioBusHandle aligns itself to
 *    `Mixer::channelInputLatency(nextMixerChannel)` (AudioBusHandle.cpp), so the
 *    sum lands in the folder's buffer BEFORE that channel's FX chain and before
 *    every compensation point downstream of it - the same structural answer the
 *    VCA lane gave for "on which side of the compensation point does the new
 *    operation sit" (docs/TRACK-FOLDER-DESIGN.md section 5.3).
 *
 *  Pinning and the collapse flag are persisted per-folder flags. 0.3.0 ships
 *  them as ENGINE state with no interface, which is what the UI-absence line in
 *  docs/RELEASE-NOTES-v0.3.0-alpha.md and docs/KNOWN-LIMITATIONS.md states.
 */
class LMMS_EXPORT TrackFolder : public Track
{
	Q_OBJECT
public:
	//! The two modes the owner's decision names; Group is the default.
	enum class Mode
	{
		Group,    //!< organisational: the children keep their own channels
		Routing,  //!< the children's output is summed through this folder
	};

	/*! One child's own mixer channel as it was BEFORE routing mode took it over.
	 *  Needed because turning routing off must restore where each child
	 *  actually was, and it is persisted (`prevch` on the `<trackfolder>`
	 *  element) so a folder that was saved in routing mode can still be turned
	 *  back to group mode in a later session. */
	struct ChannelBinding
	{
		int childId;
		int channel;
	};

	explicit TrackFolder( TrackContainer * tc );
	~TrackFolder() override = default;

	// ---- Track's pure virtuals ------------------------------------------
	//! A folder makes no sound of its own: its children play themselves, and in
	//! routing mode they are summed at the mixer, not here.
	bool play( const TimePos & start, const f_cnt_t frames, const f_cnt_t frameBase, int clipNum = -1 ) override;
	//! The folder's row is an ordinary TrackView: one more row of the same track
	//! list. 0.3.0 draws no folder affordance on it (no chevron, no pin, no
	//! colour header) - that is the UI-absence line this release states.
	gui::TrackView * createView( gui::TrackContainerView * view ) override;
	//! A folder carries no clips of its own, so there is nothing to create.
	Clip * createClip( const TimePos & pos ) override;

	void saveTrackSpecificSettings(QDomDocument& doc, QDomElement& parent, bool presetMode) override;
	void loadTrackSpecificSettings( const QDomElement & element ) override;

	QString nodeName() const override
	{
		return QStringLiteral("trackfolder");
	}

	// ---- membership (DERIVED, never stored twice) ------------------------
	//! Every track of this folder's container whose parent folder is this one, in
	//! container (document) order.
	std::vector<Track*> children() const;
	int childCount() const;
	//! Whether \a child may be parented here; false with \a reason filled when it
	//! may not (a folder may not hold itself, and two folders may not hold each
	//! other).
	bool canHold( const Track* child, QString * reason ) const;

	// ---- state ----------------------------------------------------------
	Mode mode() const { return m_mode; }
	bool isRouting() const { return m_mode == Mode::Routing; }
	//! Switches the mode. Routing mode needs at least one child to sum and a
	//! mixer channel to sum into; false with \a error filled changes NOTHING.
	bool setMode( Mode mode, QString * error );

	bool isCollapsed() const { return m_collapsed; }
	void setCollapsed( bool collapsed );
	bool isPinned() const { return m_pinned; }
	void setPinned( bool pinned );

	//! The folder's own mixer channel, or -1 when it has none (group mode, or a
	//! routing folder whose channel has been released). A plain `int`, not
	//! `mix_ch_t`: mix_ch_t is UNSIGNED (short unsigned int), so it cannot carry
	//! the "no channel" sentinel this needs, and the mixer's own accessors take
	//! an int.
	int mixerChannel() const { return m_mixerChannel; }
	void setMixerChannel( int channel ) { m_mixerChannel = channel; }

	const std::vector<ChannelBinding>& savedChildChannels() const { return m_savedChildChannels; }
	void setSavedChildChannels( const std::vector<ChannelBinding>& bindings ) { m_savedChildChannels = bindings; }
	//! Where \a child's own channel was before routing mode took it over, or -1
	//! when no binding was recorded for it.
	int savedChannelFor( int childId ) const;

	//! Told by Track::setParentFolder when a child joins (or leaves): routing
	//! mode's invariant is that EVERY child of a routing folder points at the
	//! folder's channel, so a child that joins later is routed here rather than
	//! waiting for the next mode switch.
	void childLinked( Track * child );
	void childUnlinked( Track * child );

	//! The routing half, as two operations, so the command layer can record one
	//! A16 step that restores the mode AND every child's channel.
	bool enableRouting( QString * error );
	bool releaseRouting();

private:
	void routeChild( Track * child );
	//! Puts \a child back on its own recorded channel (shared by
	//! releaseRouting() and childUnlinked()).
	void unrouteChild( Track * child );
	bool alreadyRouted( Track * child ) const;

	Mode m_mode{Mode::Group};
	bool m_collapsed{false};
	bool m_pinned{false};
	int m_mixerChannel{-1};
	//! Transient mirror of `prevch` (the file's record of the same thing): the
	//! channel each child was on before routing mode moved it.
	std::vector<ChannelBinding> m_savedChildChannels;
};

} // namespace lmms

#endif // LMMS_TRACK_FOLDER_H
