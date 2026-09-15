/*
 * AutomationClip.h - declaration of class AutomationClip, which contains
 *                       all information about an automation clip
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2006-2008 Javier Serrano Polo <jasp00/at/users.sourceforge.net>
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

#ifndef LMMS_AUTOMATION_CLIP_H
#define LMMS_AUTOMATION_CLIP_H

#include <QMap>
#include <QPointer>

#include "AutomationNode.h"
#include "Clip.h"


namespace lmms
{

class AutomationTrack;
class TimePos;

namespace gui
{
class AutomationClipView;
class AutomationEditor;
} // namespace gui



class LMMS_EXPORT AutomationClip : public Clip
{
	Q_OBJECT
public:
	enum class ProgressionType
	{
		Discrete,
		Linear,
		CubicHermite
	} ;

	using timeMap = QMap<int, AutomationNode>;
	using objectVector = std::vector<QPointer<AutomatableModel>>;

	using TimemapIterator = timeMap::const_iterator;

	AutomationClip( AutomationTrack * _auto_track );
	~AutomationClip() override = default;

	bool addObject( AutomatableModel * _obj, bool _search_dup = true );

	const AutomatableModel * firstObject() const;
	const objectVector& objects() const;

	// progression-type stuff
	inline ProgressionType progressionType() const
	{
		return m_progressionType;
	}
	void setProgressionType( ProgressionType _new_progression_type );

	inline float getTension() const
	{
		return m_tension;
	}
	void setTension( QString _new_tension );

	TimePos timeMapLength() const;
	void updateLength() override;

	TimePos putValue(
		const TimePos & time,
		const float value,
		const bool quantPos = true,
		const bool ignoreSurroundingPoints = true
	);

	TimePos putValues(
		const TimePos & time,
		const float inValue,
		const float outValue,
		const bool quantPos = true,
		const bool ignoreSurroundingPoints = true
	);

	void removeNode(const TimePos & time);
	void removeNodes(const int tick0, const int tick1);

	void resetNodes(const int tick0, const int tick1);

	/**
	 * @brief Resets the tangents from the nodes between the given ticks
	 * @param Int first tick of the range
	 * @param Int second tick of the range
	 */
	void resetTangents(const int tick0, const int tick1);

	void recordValue(TimePos time, float value);

	TimePos setDragValue( const TimePos & time,
				const float value,
				const bool quantPos = true,
				const bool controlKey = false );

	void applyDragValue();


	bool isDragging() const
	{
		return m_dragging;
	}

	inline const timeMap & getTimeMap() const
	{
		return m_timeMap;
	}

	inline timeMap & getTimeMap()
	{
		return m_timeMap;
	}

	inline float getMin() const
	{
		return firstObject()->minValue<float>();
	}

	inline float getMax() const
	{
		return firstObject()->maxValue<float>();
	}

	inline bool hasAutomation() const
	{
		return m_timeMap.isEmpty() == false;
	}

	static bool supportsTangentEditing(ProgressionType pType)
	{
		// Update function if we have new progression types that support tangent editing
		return pType == ProgressionType::CubicHermite;
	}

	inline bool canEditTangents() const
	{
		return supportsTangentEditing(m_progressionType);
	}

	float valueAt( const TimePos & _time ) const;
	float *valuesAfter( const TimePos & _time ) const;

	// -----------------------------------------------------------------------
	// Sample-accurate automation (feature-list row 9,
	// docs/SAMPLE-ACCURATE-AUTOMATION.md).
	//
	// A clip's curve is stored as nodes on integer ticks and is linear in ticks
	// between two nodes, so inside one audio block the curve has no more shape
	// than one straight line per tick. OFF by default (a project that never
	// asked for it renders byte-identically to before); when ON, the automation
	// evaluation writes the curve into an AutomationRamp at the start of every
	// audio block and the parameters this clip drives read their per-sample
	// buffer from that ramp instead of from the previous block's value.
	// -----------------------------------------------------------------------

	//! Is this clip rendered at sample precision inside a block?
	bool sampleAccurate() const { return m_sampleAccurate; }
	//! Turn it on or off. Journalled like any other clip edit (the flag is
	//! serialized, and loadSettings resets it on absence, so a checkpoint
	//! taken before the first edit takes it back).
	void setSampleAccurate( bool on );

	/*! Write this clip's curve into @a ramp for the block of @a frames frames
	 *  whose first sample sits at global tick @a blockStart + @a frameOffsetInTick
	 *  frames (the audio block grid and the tick grid do not line up, so a block
	 *  usually starts inside a tick).
	 *
	 *  One knot per tick boundary inside the block plus the block's two ends:
	 *  that is the curve itself at every frame, because the curve is linear
	 *  between two nodes and a node sits on a tick. REALTIME-SAFE: called from
	 *  the audio thread, allocates nothing (the ramp is the caller's own
	 *  fixed-capacity object), and takes the clip's own mutex ONCE for the whole
	 *  block rather than once per evaluation.
	 */
	void writeBlockRamp( AutomationRamp & ramp, const TimePos & blockStart, f_cnt_t frames,
		double framesPerTick, int frameOffsetInTick ) const;

	QString name() const;

	// settings-management
	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;

	static const QString classNodeName() { return "automationclip"; }
	QString nodeName() const override { return classNodeName(); }

	gui::ClipView * createView( gui::TrackView * _tv ) override;


	static bool isAutomated( const AutomatableModel * _m );
	static std::vector<AutomationClip*> clipsForModel(const AutomatableModel* _m);
	static AutomationClip * globalAutomationClip( AutomatableModel * _m );
	static void resolveAllIDs();

	bool isRecording() const { return m_isRecording; }
	void setRecording( const bool b ) { m_isRecording = b; }

	static int quantization() { return s_quantization; }
	static void setQuantization(int q) { s_quantization = q; }

	AutomationClip* clone() override
	{
		return new AutomationClip(*this);
	}

	void clearObjects() { m_objects.clear(); }

public slots:
	void clear();
	void objectDestroyed( lmms::jo_id_t );
	void flipY( int min, int max );
	void flipY();
	void flipX(int start = -1, int end = -1);

protected:
	AutomationClip( const AutomationClip & _clip_to_copy );

private:
	void cleanObjects();
	void generateTangents();
	void generateTangents(timeMap::iterator it, int numToGenerate);
	float valueAt( timeMap::const_iterator v, int offset ) const;

	/*! The curve's value at clip-relative tick @a rel (plus @a fraction of the
	 *  segment that starts there) - the segment, not the node, so a node whose
	 *  in- and out-values differ does not make the ramp step early. Caller
	 *  holds m_clipMutex. Allocates nothing. */
	float rampValueAt( const TimePos & rel, float fraction ) const;
	//! One knot: the curve at @a frame of the block (whose first sample sits
	//! @a frameOffsetInTick frames into its tick). Caller holds m_clipMutex.
	void addRampKnot( AutomationRamp & ramp, const TimePos & relClipStart, int frame,
		int frameOffsetInTick, double framesPerTick ) const;

	/**
	 * @brief
	 * This function combines the song tracks, pattern store tracks,
	 * and the global automation track all in one vector.
	 *
	 * @return std::vector<Track*>
	 */
	static std::vector<Track*> combineAllTracks();

	// Mutex to make methods involving automation clips thread safe
	// Mutable so we can lock it from const objects
	mutable QRecursiveMutex m_clipMutex;

	AutomationTrack * m_autoTrack;
	std::vector<jo_id_t> m_idsToResolve;
	objectVector m_objects;
	timeMap m_timeMap;	// actual values
	timeMap m_oldTimeMap;	// old values for storing the values before setDragValue() is called.
	float m_tension;
	bool m_hasAutomation;
	ProgressionType m_progressionType;

	//! Sample-accurate rendering of this clip's curve (feature-list row 9).
	//! Serialized only when it is ON, so a project that never asked for it
	//! saves the bytes it always saved; loadSettings resets it on absence.
	bool m_sampleAccurate = false;

	bool m_dragging;
	bool m_dragKeepOutValue; // Should we keep the current dragged node's outValue?
	float m_dragOutValue; // The outValue of the dragged node's
	bool m_dragLockedTan; // If the dragged node has it's tangents locked
	float m_dragInTan; // The dragged node's inTangent
	float m_dragOutTan; // The dragged node's outTangent

	bool m_isRecording;
	float m_lastRecordedValue;

	static int s_quantization;

	static const float DEFAULT_MIN_VALUE;
	static const float DEFAULT_MAX_VALUE;

	friend class gui::AutomationClipView;
	friend class AutomationNode;
	friend class gui::AutomationEditor;

} ;

//Short-hand functions to access node values in an automation clip;
// replacement for CPP macros with the same purpose; could be refactored
// further in the future.
inline float INVAL(AutomationClip::TimemapIterator it)
{
	return it->getInValue();
}

inline float OUTVAL(AutomationClip::TimemapIterator it)
{
	return it->getOutValue();
}

inline float OFFSET(AutomationClip::TimemapIterator it)
{
	return it->getValueOffset();
}

inline float INTAN(AutomationClip::TimemapIterator it)
{
	return it->getInTangent();
}

inline float OUTTAN(AutomationClip::TimemapIterator it)
{
	return it->getOutTangent();
}

inline float LOCKEDTAN(AutomationClip::TimemapIterator it)
{
	return it->lockedTangents();
}

inline int POS(AutomationClip::TimemapIterator it)
{
	return it.key();
}


} // namespace lmms

#endif // LMMS_AUTOMATION_CLIP_H
