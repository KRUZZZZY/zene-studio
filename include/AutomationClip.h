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

#include <atomic>
#include <cstdint>

#include <QMap>
#include <QPointer>

#include "AutomationNode.h"
#include "Clip.h"

class AutomationClipSnapshotTest;

namespace lmms
{

class AutomationTrack;
class TimePos;
struct AutomationClipReadSnapshot;

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

	//! ED-184: the shapes drawShape writes.
	enum class Shape { Sine, Triangle, Square, Saw, Random, Adsr };

	using timeMap = QMap<int, AutomationNode>;
	using objectVector = std::vector<QPointer<AutomatableModel>>;

	using TimemapIterator = timeMap::const_iterator;

	AutomationClip( AutomationTrack * _auto_track );
	~AutomationClip() override;

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
	//! Edit an existing point without exposing a mutable map or node reference.
	bool setNodeInValue(const TimePos& time, float value);
	bool setNodeOutValue(const TimePos& time, float value);
	bool setNodeTangent(const TimePos& time, bool outgoing, float tangent);
	bool setNodeTangents(const TimePos& time, float inTangent, float outTangent, bool locked);
	bool setNodeLockedTangents(const TimePos& time, bool locked);
	bool setNodeCurve(const TimePos& time, AutomationNode::Curve curve, float bend = 0.f);
	//! Move one point while retaining its curve and bend; refuses an occupied destination.
	bool moveNode(const TimePos& from, const TimePos& to, float inValue);
	//! Rescale all point values after the controlled model's range changes.
	bool rescaleValuesToRange(float oldMin, float oldMax, float newMin, float newMax);

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

	inline float getMin() const
	{
		return firstObject()->minValue<float>();
	}

	inline float getMax() const
	{
		return firstObject()->maxValue<float>();
	}

	bool hasAutomation() const;

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
	// than one straight line per tick. ON by default for a clip with a curve since R1.2
	// (see RampSetting below); when ON, the automation
	// evaluation writes the curve into an AutomationRamp at the start of every
	// audio block and the parameters this clip drives read their per-sample
	// buffer from that ramp instead of from the previous block's value.
	// -----------------------------------------------------------------------

	/*! R1.2: the ramp is the DEFAULT for a clip carrying a curve. The flag is three-valued:
	 *  explicitly on, explicitly off, or not set - and not set resolves to ON for a Linear
	 *  or CubicHermite clip and OFF for a Discrete one (a step has no curve inside a block
	 *  to follow). Only an explicit setting is saved ("1" as before, "0" for off), so every
	 *  project file keeps its bytes; what changes is that an old file's curves now render
	 *  at sample precision. */
	enum class RampSetting : std::uint8_t { Default, On, Off };

	//! Is this clip rendered at sample precision inside a block? (The resolved setting.)
	bool sampleAccurate() const;
	RampSetting rampSetting() const { return m_rampSetting; }
	//! Turn it explicitly on or off. Journalled like any other clip edit (the setting is
	//! serialized, and loadSettings resets it to Default on absence, so a checkpoint
	//! taken before the first edit takes it back).
	void setSampleAccurate( bool on );
	//! The three-valued form (Default included), for the loader's reset and the tests.
	void setRampSetting( RampSetting setting );

	/*! Write this clip's curve into @a ramp for the block of @a frames frames
	 *  whose first sample sits at global tick @a blockStart + @a frameOffsetInTick
	 *  frames (the audio block grid and the tick grid do not line up, so a block
	 *  usually starts inside a tick).
	 *
	 *  One knot per tick boundary inside the block plus the block's two ends:
	 *  that is the curve itself at every frame, because the curve is linear
	 *  between two nodes and a node sits on a tick. REALTIME-SAFE: called from
	 *  the audio thread, allocates nothing (the ramp is the caller's own
	 *  fixed-capacity object), and reads one immutable snapshot under the
	 *  lock-free automation reader count. No clip mutex is taken here.
	 */
	void writeBlockRamp( AutomationRamp & ramp, const TimePos & blockStart, f_cnt_t frames,
		double framesPerTick, int frameOffsetInTick ) const;
	void movePosition(const TimePos& pos) override;
	void changeLength(const TimePos& length) override;
	void setStartTimeOffset(const TimePos& startTimeOffset) override;
	bool isMuted() const override;
	void setMuted(bool muted) override;

	//! 0.5.0 MX-160: the latency (frames) upstream of the parameter this clip drives, which the
	//! clip's reading lags by while latency-compensated automation is on (AutomationLatency).
	//! Written by the control thread, read by the audio thread; never saved (it is derived).
	int latencyFrames() const { return m_latencyFrames.load( std::memory_order_relaxed ); }
	void setLatencyFrames( int frames ) { m_latencyFrames.store( frames, std::memory_order_relaxed ); }

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

public:
	//! 0.5.0 L06 (AutomationClipTransforms.cpp). ED-184: replace the points in [@a start, @a end]
	//! with @a shape (@a cycles periods, @a low..@a high, a point every @a step ticks and one at @a end;
	//! @a seed makes Random repeatable).
	void drawShape(int start, int end, Shape shape, int cycles, float low, float high, int step, unsigned seed = 1);
	//! ED-186: the points in [@a start, @a end] rescaled about @a start in time (then shifted by
	//! @a timeOffset) and about the range's middle in value (then shifted); how many moved.
	int transformRange(int start, int end, double timeScale, int timeOffset, double valueScale, double valueOffset);
	//! ED-187: remove the points within @a tolerance (value units) of the line through their kept
	//! neighbours (Douglas-Peucker; the first and last stay); how many were removed.
	int thin(float tolerance);

	/*! ED-199: a RELATIVE (trim) clip does not set its parameter, it offsets it: its value's
	 *  distance from the middle of the parameter's range is added to whatever the absolute clips
	 *  (or, without one, the parameter's own value when playback reached it) say. */
	bool isRelative() const { return m_relative.load(std::memory_order_relaxed); }
	void setRelative(bool relative);

protected:
	AutomationClip( const AutomationClip & _clip_to_copy );

private:
	void cleanObjects();
	void generateTangents();
	void generateTangents(timeMap::iterator it, int numToGenerate);
	float editableValueAt(const TimePos& time) const;
	void publishReadSnapshot();
	static void retireReadSnapshot(AutomationClipReadSnapshot* snapshot);

	/*! The curve's value at clip-relative tick @a rel (plus @a fraction of the
	 *  segment that starts there) - the segment, not the node, so a node whose
	 *  in- and out-values differ does not make the ramp step early. */
	static float rampValueAt(const AutomationClipReadSnapshot& snapshot, const TimePos& rel, float fraction);
	//! One knot: the curve at @a frame of the block (whose first sample sits
	//! @a frameOffsetInTick frames into its tick).
	static void addRampKnot(const AutomationClipReadSnapshot& snapshot, AutomationRamp& ramp,
		const TimePos& relClipStart, int frame, int frameOffsetInTick, double framesPerTick);
	static void addRampBoundaryKnots(const AutomationClipReadSnapshot& snapshot, AutomationRamp& ramp,
		const TimePos& relClipStart, f_cnt_t frames, int frameOffsetInTick, double framesPerTick);

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
	std::atomic<AutomationClipReadSnapshot*> m_readSnapshot{nullptr};
	bool m_snapshotPublicationSuppressed = false;

	AutomationTrack * m_autoTrack;
	std::vector<jo_id_t> m_idsToResolve;
	objectVector m_objects;
	timeMap m_timeMap;	// actual values
	timeMap m_oldTimeMap;	// old values for storing the values before setDragValue() is called.
	float m_tension;
	bool m_hasAutomation;
	ProgressionType m_progressionType;
	std::atomic<bool> m_relative{false};  //!< ED-199; atomic: the audio thread reads it

	//! Sample-accurate rendering of this clip's curve (feature-list row 9).
	//! Serialized only when it is ON, so a project that never asked for it
	//! saves the bytes it always saved; loadSettings resets it on absence.
	RampSetting m_rampSetting = RampSetting::Default;
	//! 0.5.0 MX-160: see latencyFrames(); a copy starts at 0 and the next refresh sets it.
	std::atomic<int> m_latencyFrames{ 0 };

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
	friend class ::AutomationClipSnapshotTest;

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
