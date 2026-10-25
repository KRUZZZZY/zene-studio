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

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <QMap>
#include <QPointer>

#include "AutomationNode.h"
#include "Clip.h"


struct AutomationClipRampSettingsTestAccess;

namespace lmms
{

class AutomationTrack;
class AutomationLatency;
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
	static_assert(std::atomic<ProgressionType>::is_always_lock_free,
		"The audio thread reads automation progression without a mutex.");

	//! ED-184: the shapes drawShape writes.
	enum class Shape { Sine, Triangle, Square, Saw, Random, Adsr };

	using timeMap = QMap<int, AutomationNode>;
	using objectVector = std::vector<QPointer<AutomatableModel>>;

	using TimemapIterator = timeMap::const_iterator;

	AutomationClip( AutomationTrack * _auto_track );
	~AutomationClip() override;

	bool addObject( AutomatableModel * _obj, bool _search_dup = true );
	bool removeObject( AutomatableModel * _obj );

#ifdef LMMS_TESTING
	/*! Test seam: the points of a target-list change - a connection
	 *  (addObject()) or a removal (removeObject()) - where the changing
	 *  thread reports to the test:
	 *  - Before: the thread is inside the change, before it takes the engine's
	 *    change guard (the lock the render thread holds for a whole period).
	 *  - Holding: the thread is HOLDING ON - the acquisition of the engine's
	 *    change guard was CONTENDED, i.e. the render thread held the lock when
	 *    the acquisition was attempted, so the change waits for the render to
	 *    let go.
	 *
	 *  The report is made AT the acquisition, on the acquiring thread: it is
	 *  positive evidence that the changing thread really reached the guard,
	 *  where a report sent before the attempt could not tell a blocked thread
	 *  from one that has not run yet. Null in product use.
	 */
	enum class TargetChangePoint { Before, Holding };
	using TargetChangeProbe = void (*)(TargetChangePoint);
	static void setTargetChangeProbeForTest( TargetChangeProbe probe )
	{
		s_targetChangeProbe = probe;
	}
	inline static TargetChangeProbe s_targetChangeProbe = nullptr;
#endif

	const AutomatableModel * firstObject() const;
	const objectVector& objects() const;
	objectVector objectsSnapshot() const;
	bool hasObjects() const;
#ifdef LMMS_TESTING
	using ObjectsReadProbe = void (*)(const AutomationClip*);
	static void setObjectsReadProbeForTest(ObjectsReadProbe probe) { s_objectsReadProbe = probe; }
	inline static ObjectsReadProbe s_objectsReadProbe = nullptr;
#endif

	// progression-type stuff
	inline ProgressionType progressionType() const
	{
		return m_progressionType.load(std::memory_order_relaxed);
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

	/*! The pending writes the render thread hands to the control thread
	 *  (cluster 5388).
	 *
	 *  Writing an automation point edits the clip's node map, and that edits
	 *  allocate - so the render thread may not do it. The render thread queues
	 *  the write here instead, and the control thread applies it (see
	 *  Song::applyPendingAutomationWrites()).
	 *
	 *  The storage lives in the clip, so one clip's backlog can never take
	 *  another clip's place, and the room a write needs grows with the project
	 *  instead of with a fixed constant. A write that does not fit is NOT
	 *  dropped: it takes the place of the clip's newest pending write, so the
	 *  value the control is at when the store fills is the one that survives.
	 *  The write it replaced is counted (pendingWriteMerges()), never dropped
	 *  silently. */
	static constexpr std::size_t PendingWriteCapacity = 64;

	//! Queues one write for the control thread. Render thread; never allocates.
	void queuePendingWrite(tick_t tick, float value) noexcept;

	//! Applies the queued writes, oldest first. Control thread. Returns how many it applied.
	std::size_t applyPendingWrites();

	//! How many writes this clip has queued and not applied yet.
	std::size_t pendingWriteCount() const noexcept;

	//! Drops the queued writes (control thread): they belong to a project that
	//! is being replaced, and must not reach the project that replaces it.
	void discardPendingWrites();

	//! How many queued writes were merged away because the clip's store was full.
	std::uint64_t pendingWriteMerges() const noexcept
	{
		return m_pendingWriteMerges.load(std::memory_order_relaxed);
	}

	//! How many writes are queued in every clip of the session. The control
	//! thread's drain reads it to learn whether it has anything to do at all.
	static std::int64_t pendingWriteTotal() noexcept
	{
		return s_pendingWriteTotal.load(std::memory_order_relaxed);
	}

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
		return supportsTangentEditing(m_progressionType.load(std::memory_order_relaxed));
	}

	/*! The curve's value at clip-relative time @a _time.
	 *
	 *  Q050-037: BEFORE the first node the curve has not begun, so the clip
	 *  holds the authored parameter's initial value (inverse-scaled, the domain
	 *  the time map stores) instead of 0. A point drawn at the clip's start is
	 *  the first node and therefore the explicit start value. When @a
	 *  beforeFirstNode is not null it is set to true for that pre-first read, so
	 *  a playback pass can apply each target's OWN initial value without taking
	 *  the clip mutex a second time. An empty map keeps returning 0: with no
	 *  curve there is no start value to define. A RELATIVE (trim) clip is
	 *  unchanged: its value is an offset, so it keeps reading 0 before its first
	 *  node and never sets @a beforeFirstNode. */
	float valueAt( const TimePos & _time, bool * beforeFirstNode = nullptr ) const;
	float *valuesAfter( const TimePos & _time, std::int64_t firstOffset, std::size_t count ) const;

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
	static_assert(std::atomic<RampSetting>::is_always_lock_free,
		"The audio thread reads ramp settings without a mutex.");

	//! Is this clip rendered at sample precision inside a block? (The resolved setting.)
	bool sampleAccurate() const
	{
		const RampSetting setting = m_rampSetting.load(std::memory_order_relaxed);
		return setting == RampSetting::Default
			? m_progressionType.load(std::memory_order_relaxed) != ProgressionType::Discrete
			: setting == RampSetting::On;
	}
	RampSetting rampSetting() const { return m_rampSetting.load(std::memory_order_relaxed); }
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
	 *  fixed-capacity object), and takes the clip's own mutex ONCE for the whole
	 *  block rather than once per evaluation.
	 *
	 *  @a target selects whose authored initial value the pre-first frames hold
	 *  (null = the clip's first object, what a single-valued read answers with).
	 *  When @a firstNodeFrame is not null it receives the first frame of the
	 *  block whose curve time is at or after the clip's first node (0 when the
	 *  node is at or before the block, frames+1 when it lies past the block's
	 *  last frame). Every knot before it belongs to the pre-first region, so a
	 *  caller publishing to a target with a different initial value can copy the
	 *  ramp and override only that prefix (`AutomationRamp::setValueBefore`)
	 *  instead of rebuilding the whole ramp under a second clip lock. */
	void writeBlockRamp( AutomationRamp & ramp, const TimePos & blockStart, f_cnt_t frames,
		double framesPerTick, int frameOffsetInTick,
		const AutomatableModel * target = nullptr, f_cnt_t * firstNodeFrame = nullptr ) const;

	//! 0.5.0 MX-160: the latency (frames) upstream of this clip's target, which its reading lags by
	//! while latency-compensated automation is on. Returns -1 when live targets have incompatible
	//! upstream latencies and the clip is rejected for automation; derived data, never saved.
	int latencyFrames() const;
	//! Read only while holding the matching AutomationLatency::ReadSnapshot; may be -1 for incompatible targets.
	int latencyFramesForSnapshot(unsigned bank) const { return m_latencyFrames[bank]; }

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
	void setRelative(bool relative) { m_relative.store(relative, std::memory_order_relaxed); }

protected:
	AutomationClip( const AutomationClip & _clip_to_copy );

private:
	void cleanObjects();
	void generateTangents();
	void generateTangents(timeMap::iterator it, int numToGenerate);
	float valueAt( timeMap::const_iterator v, std::int64_t offset ) const;
	//! ED-185: a Bend segment's value (AutomationClipTransforms.cpp).
	float bendValueAt( timeMap::const_iterator v, std::int64_t offset ) const;

	/*! Q050-037: the value the curve holds before its first node - @a target's
	 *  authored initial value, inverse-scaled to the domain the time map stores;
	 *  a null @a target uses the clip's first object. Caller holds m_clipMutex,
	 *  so this takes no lock of its own. A clip with no object answers 0. */
	float preFirstValueLocked( const AutomatableModel * target ) const;

	/*! The curve's value at clip-relative tick @a rel (plus @a fraction of the
	 *  segment that starts there) - the segment, not the node, so a node whose
	 *  in- and out-values differ does not make the ramp step early. Before the
	 *  first node it returns @a preFirstValue (Q050-037). Caller holds
	 *  m_clipMutex. Allocates nothing. */
	float rampValueAt( const TimePos & rel, float fraction, float preFirstValue ) const;
	//! One knot: the curve at @a frame of the block (whose first sample sits
	//! @a frameOffsetInTick frames into its tick). Caller holds m_clipMutex.
	void addRampKnot( AutomationRamp & ramp, const TimePos & relClipStart, int frame,
		int frameOffsetInTick, double framesPerTick, float preFirstValue ) const;

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
	std::atomic<ProgressionType> m_progressionType;
	std::atomic<bool> m_relative{false};  //!< ED-199; atomic: the audio thread reads it

	//! Sample-accurate rendering of this clip's curve (feature-list row 9).
	//! Serialized only when it is ON, so a project that never asked for it
	//! saves the bytes it always saved; loadSettings resets it on absence.
	std::atomic<RampSetting> m_rampSetting{ RampSetting::Default };
	//! 0.5.0 MX-160: double-buffered; refresh writes the inactive bank before publishing it to readers.
	int m_latencyFrames[2]{ 0, 0 };
	void setLatencyFramesForSnapshot(unsigned bank, int frames) { m_latencyFrames[bank] = frames; }
	friend class AutomationLatency;

	bool m_dragging;
	bool m_dragKeepOutValue; // Should we keep the current dragged node's outValue?
	float m_dragOutValue; // The outValue of the dragged node's
	bool m_dragLockedTan; // If the dragged node has it's tangents locked
	float m_dragInTan; // The dragged node's inTangent
	float m_dragOutTan; // The dragged node's outTangent

	bool m_isRecording;
	float m_lastRecordedValue;

	//! The writes the render thread queued for the control thread (cluster
	//! 5388). Each slot holds one packed write (the tick in the high half, the
	//! value's bits in the low half), so the writer and the reader can never
	//! see half of one write and half of another. */
	std::array<std::atomic<std::uint64_t>, PendingWriteCapacity> m_pendingWrites{};
	//! The newest write, kept for the clip while the slots above are full.
	std::atomic<std::uint64_t> m_pendingLatest{ 0 };
	std::atomic<bool> m_pendingLatestSet{ false };
	//! How many writes were published to the slots above, and how many of them
	//! the control thread has applied.
	std::atomic<std::uint64_t> m_pendingWriteCount{ 0 };
	std::atomic<std::uint64_t> m_pendingWriteApplied{ 0 };
	//! How many queued writes were merged away because the clip's store was full.
	std::atomic<std::uint64_t> m_pendingWriteMerges{ 0 };

	//! How many writes are queued in every clip of the session.
	static std::atomic<std::int64_t> s_pendingWriteTotal;

	static int s_quantization;

	static const float DEFAULT_MIN_VALUE;
	static const float DEFAULT_MAX_VALUE;

	friend struct ::AutomationClipRampSettingsTestAccess;
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
