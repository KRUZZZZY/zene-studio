/*
 * TrackConteintObject.h - declaration of Clip class
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

#ifndef LMMS_CLIP_H
#define LMMS_CLIP_H

#include <optional>

#include <QColor>
#include <QDomElement>

#include "AutomatableModel.h"
#include "ClipEdits.h"


namespace lmms
{

class QDomDocument;

class Track;

namespace gui
{

class ClipView;
class TrackView;

} // namespace gui


class LMMS_EXPORT Clip : public Model, public JournallingObject
{
	Q_OBJECT
	mapPropertyFromModel(bool,isMuted,setMuted,m_mutedModel);
	mapPropertyFromModel(bool,isSolo,setSolo,m_soloModel);
public:
	Clip( Track * track );
	~Clip() override;

	/*! The clip's STABLE ID - the number in `clip-<n>` (SPEC-stable-ids.md
	 *  slice 2). Assigned once, in the constructor, and never changed while the
	 *  clip is alive; written to the project file as an `id` ATTRIBUTE on the
	 *  clip's own element and taken back by restoreState, so a cached
	 *  `clip-<n>` still names this clip after a sibling clip is inserted,
	 *  deleted, reordered or undone - and after a save/open cycle.
	 *
	 *  An ATTRIBUTE and not a child element, for the reason Track::saveTrack
	 *  records for the track id: every clip loader walks its element's children
	 *  (MidiClip::loadSettings turns an unrecognised child into a Note), so a
	 *  child element would make every clip grow a phantom note on load.
	 */
	int id() const { return m_id; }
	//! Overrides the constructor's id with a file's value. Raises the project
	//! counter past \a id so a retired number is never handed out again.
	void setId(int id);

	//! SerializingObject: the clip's own element plus the persistent `id`
	//! attribute. ONE override for all four clip types (midiclip, sampleclip,
	//! patternclip, automationclip) - none of them overrides saveState, so
	//! every clip that is written to a project file carries its id.
	QDomElement saveState( QDomDocument & doc, QDomElement & parent ) override;
	//! SerializingObject: loadSettings() plus the `id` attribute. A file that
	//! carries one keeps it; a legacy file that does not keeps the number the
	//! constructor handed out - deterministic, because the load walks the
	//! containers and their clips in document order - and the assignment is
	//! COUNTED, so project.open reports it as `ids_assigned` instead of
	//! upgrading a file silently.
	void restoreState( const QDomElement & element ) override;

	inline Track * getTrack() const
	{
		return m_track;
	}

	inline const QString & name() const
	{
		return m_name;
	}

	inline void setName( const QString & name )
	{
		m_name = name;
		emit dataChanged();
	}

	QString displayName() const override
	{
		return name();
	}


	inline const TimePos & startPosition() const
	{
		return m_startPosition;
	}

	inline TimePos endPosition() const
	{
		const int sp = m_startPosition;
		return sp + m_length;
	}

	inline const TimePos & length() const
	{
		return m_length;
	}


	bool hasTrackContainer() const;

	bool isInPattern() const;

	bool manuallyResizable() const;

	/*! \brief Set whether a clip has been resized yet by the user or the knife tool.
	 *
	 *  If a clip has been resized previously, it will not automatically 
	 *  resize when editing it.
	 *
	 */
	void setAutoResize(const bool r)
	{
		m_autoResize = r;
	}

	bool getAutoResize() const
	{
		return m_autoResize;
	}

	auto color() const -> const std::optional<QColor>& { return m_color; }
	void setColor(const std::optional<QColor>& color);

	/*! The clip's fade ramps and its clip gain (fade/crossfade/clip-gain wave).
	 *
	 *  All defaults neutral, so a clip nobody has edited renders exactly as it
	 *  did before these values existed and writes no attribute to the project
	 *  file (docs/CLIP-CAPTURE-DESIGN.md §2.2 places them on the base type -
	 *  a MIDI clip can carry a fade too; §4.1 says the audio application lives
	 *  in the play handle, never in `Sample::render`, which the browser preview
	 *  and the metronome share). */
	const ClipEdits& clipEdits() const { return m_edits; }
	void setClipEdits(const ClipEdits& edits) { m_edits = edits; }

	/*! The take lane this clip belongs to (comping; docs/COMPING.md and
	 *  docs/CLIP-CAPTURE-DESIGN.md §2.2), 0 by default.
	 *
	 *  The tag lives on the base `Clip` for the design's reason: a lane is a
	 *  child relationship of the track, not a track type, and the tag is
	 *  type-agnostic. It is written with the clip's other non-default attributes
	 *  (Clip::saveClipEdits) and resets to 0 when the attribute is absent, so a
	 *  clip with no lane writes nothing at all. */
	int laneIndex() const { return m_laneIndex; }
	void setLaneIndex(int laneIndex) { m_laneIndex = laneIndex < 0 ? 0 : laneIndex; }

	/*! The link group this clip is a member of (linked / smart clips, row 6),
	 *  0 when it is not linked.
	 *
	 *  The relation is the clip's OWN attribute, written with the clip's other
	 *  non-default attributes (Clip::saveClipEdits) and reset to 0 when the
	 *  attribute is absent, so a group is rebuilt from its members on load with
	 *  no second registry in the file. What a group shares is the clip's
	 *  CONTENT (its note list - see include/ClipLinks.h and
	 *  docs/LINKED-CLIPS.md); position, length, offset, fades, gain and the rest
	 *  stay per-member. */
	int linkId() const { return m_linkId; }
	void setLinkId(int linkId) { m_linkId = linkId < 0 ? 0 : linkId; }

	virtual void movePosition( const TimePos & pos );
	virtual void changeLength( const TimePos & length );
	virtual void updateLength() {};

	virtual gui::ClipView * createView( gui::TrackView * tv ) = 0;

	inline void selectViewOnCreate( bool select )
	{
		m_selectViewOnCreate = select;
	}

	inline bool getSelectViewOnCreate()
	{
		return m_selectViewOnCreate;
	}

	/// Returns true if and only if a->startPosition() < b->startPosition()
	static bool comparePosition(const Clip* a, const Clip* b);

	TimePos startTimeOffset() const;
	virtual void setStartTimeOffset(const TimePos& startTimeOffset);

	/*! The clip's source position for a timeline position, in the source's own
	 *  units: frames for a clip whose source is audio (SampleClip overrides this),
	 *  ticks for a clip with no frame-domain source.
	 *
	 *  This pair is the mapping seam the clip-and-capture wave froze (task #611,
	 *  docs/CLIP-CAPTURE-DESIGN.md §2.4) so that warp/time-stretch (#597) can add a
	 *  non-linear mapping without touching the clip model: today's implementation is
	 *  linear, and it is the single place the clip's window relates to the timeline.
	 *
	 *  The result is always inside the clip's own source range, and the two
	 *  functions are inverses up to the frame-to-tick truncation.
	 */
	virtual f_cnt_t sourceFrameAt(TimePos timelinePos) const;

	/*! The inverse of sourceFrameAt(): the timeline position a source frame is
	 *  reached at, clamped to the clip's source range. Trimming is what needs it. */
	virtual TimePos timelinePosAt(f_cnt_t sourceFrame) const;

	// Will copy the state of a clip to another clip
	static void copyStateTo( Clip *src, Clip *dst );

	/**
	* Creates a copy of this clip
	* @return pointer to the new clip object
	*/
	virtual Clip* clone() = 0;

public slots:
	void toggleMute();


signals:
	void lengthChanged();
	void positionChanged();
	void destroyedClip();
	void colorChanged();

protected:
	Clip(const Clip& other);

	/*! Writes the NON-DEFAULT part of `clipEdits()` onto the clip's own element,
	 *  and reads it back. Additive, invariant I9: a clip with no fade and unity
	 *  gain writes nothing at all, so a project that never used these attributes
	 *  serialises byte for byte as it did before (the same rule
	 *  `SampleClip::saveSettings` already follows for `srcin`/`srcout` and for
	 *  the `<warp>` child element). */
	void saveClipEdits(QDomElement& element) const;
	void loadClipEdits(const QDomElement& element);

private:
	Track * m_track;
	QString m_name;

	/*! The clip's stable id: the number in `clip-<n>`. Handed out once by the
	 *  constructor through ProjectIds - the same project-scoped counter the
	 *  track ids come from, so `next-id` on the project root covers both - and
	 *  replaced by the file's value on a project load (see saveState /
	 *  restoreState). */
	int m_id;

	TimePos m_startPosition;
	TimePos m_length;
	TimePos m_startTimeOffset;

	BoolModel m_mutedModel;
	BoolModel m_soloModel;
	bool m_autoResize = true;

	bool m_selectViewOnCreate;

	std::optional<QColor> m_color;

	//! The clip's fades and its gain. Neutral by default (see clipEdits()).
	ClipEdits m_edits;

	//! The take lane this clip is a take of (comping; docs/COMPING.md).
	int m_laneIndex = 0;

	//! The link group this clip shares its content with (row 6; 0 = unlinked).
	int m_linkId = 0;

	friend class ClipView;

} ;


/*! The base class's source unit is the tick, so a timeline position maps onto the
 *  source by the clip's own start and slip. MIDI and automation clips have no
 *  frame-domain source and inherit this unchanged; SampleClip overrides both with
 *  the frame-domain linear map (design §2.4). #597 replaces the override. */
inline f_cnt_t Clip::sourceFrameAt(TimePos timelinePos) const
{
	const auto relative = timelinePos.getTicks() - m_startPosition.getTicks() - m_startTimeOffset.getTicks();
	return relative > 0 ? static_cast<f_cnt_t>(relative) : 0;
}


inline TimePos Clip::timelinePosAt(f_cnt_t sourceFrame) const
{
	return TimePos(static_cast<int>(m_startPosition.getTicks() + m_startTimeOffset.getTicks() + sourceFrame));
}


} // namespace lmms

#endif // LMMS_CLIP_H
