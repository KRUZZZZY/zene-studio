/*
 * AutomationClipSnapshot.cpp - immutable, lock-free automation readers
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

#include <algorithm>
#include <cmath>
#include <new>
#include <vector>

#include <QMutexLocker>

#include "AutomationClip.h"
#include "AutomationRamp.h"
#include "Engine.h"
#include "JournallingObject.h"
#include "Note.h"
#include "Song.h"

namespace lmms
{

struct AutomationClipReadSnapshot
{
	struct Node
	{
		int tick;
		float inValue;
		float outValue;
		float inTangent;
		float outTangent;
		bool lockedTangents;
		float bend;
		AutomationNode::Curve curve;
	};

	std::vector<Node> nodes;
	std::vector<std::uint64_t> appliedSequences;
	AutomationClip::ProgressionType progression = AutomationClip::ProgressionType::Discrete;
	float tension = 1.0f;
	int startPosition = 0;
	int startTimeOffset = 0;
	int length = 0;
	bool muted = false;
	bool inPattern = false;
	bool sampleAccurate = false;
	bool relative = false;
	AutomationClipReadSnapshot* retiredNext = nullptr;
};

namespace
{

std::atomic<std::uint32_t> s_activeAutomationSnapshotReaders{0};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
	"automation snapshot reader count must be lock-free");
static_assert(std::atomic<AutomationClipReadSnapshot*>::is_always_lock_free,
	"automation snapshot pointer must be lock-free");
QMutex s_retiredAutomationSnapshotMutex;
AutomationClipReadSnapshot* s_retiredAutomationSnapshots = nullptr;

class AutomationSnapshotReadScope
{
public:
	explicit AutomationSnapshotReadScope(const std::atomic<AutomationClipReadSnapshot*>& published)
	{
		s_activeAutomationSnapshotReaders.fetch_add(1, std::memory_order_seq_cst);
		m_snapshot = published.load(std::memory_order_seq_cst);
	}

	~AutomationSnapshotReadScope()
	{
		s_activeAutomationSnapshotReaders.fetch_sub(1, std::memory_order_seq_cst);
	}

	const AutomationClipReadSnapshot* get() const { return m_snapshot; }

private:
	const AutomationClipReadSnapshot* m_snapshot = nullptr;
};

void collectRetiredAutomationSnapshots()
{
	if (s_activeAutomationSnapshotReaders.load(std::memory_order_seq_cst) != 0) { return; }
	AutomationClipReadSnapshot* retired = nullptr;
	{
		QMutexLocker lock(&s_retiredAutomationSnapshotMutex);
		if (s_activeAutomationSnapshotReaders.load(std::memory_order_seq_cst) != 0) { return; }
		retired = s_retiredAutomationSnapshots;
		s_retiredAutomationSnapshots = nullptr;
	}
	while (retired != nullptr)
	{
		auto* next = retired->retiredNext;
		delete retired;
		retired = next;
	}
}

float snapshotSegmentValueAtOffset(const AutomationClipReadSnapshot& snapshot,
	const AutomationClipReadSnapshot::Node& node, const AutomationClipReadSnapshot::Node& next, int offset)
{
	if (offset == 0) { return node.inValue; }
	if (node.curve == AutomationNode::Curve::Bend)
	{
		const double t = std::clamp(static_cast<double>(offset) / std::max(1, next.tick - node.tick), 0.0, 1.0);
		const double bend = node.bend;
		const double shaped = bend >= 0.0 ? std::pow(t, 1.0 + 4.0 * bend)
			: 1.0 - std::pow(1.0 - t, 1.0 - 4.0 * bend);
		return static_cast<float>(node.outValue + (next.inValue - node.outValue) * shaped);
	}
	const auto type = node.curve == AutomationNode::Curve::Clip ? snapshot.progression
		: node.curve == AutomationNode::Curve::Hold ? AutomationClip::ProgressionType::Discrete
		: node.curve == AutomationNode::Curve::Linear ? AutomationClip::ProgressionType::Linear
		: AutomationClip::ProgressionType::CubicHermite;
	if (type == AutomationClip::ProgressionType::Discrete) { return node.outValue; }
	if (type == AutomationClip::ProgressionType::Linear)
	{
		const float slope = (next.inValue - node.outValue) / (next.tick - node.tick);
		return node.outValue + offset * slope;
	}
	const int numValues = next.tick - node.tick;
	const float t = static_cast<float>(offset) / static_cast<float>(numValues);
	const float m1 = node.outTangent * numValues * snapshot.tension;
	const float m2 = next.inTangent * numValues * snapshot.tension;
	const auto t2 = t * t, t3 = t2 * t;
	return (2 * t3 - 3 * t2 + 1) * node.outValue
		+ (t3 - 2 * t2 + t) * m1
		+ (-2 * t3 + 3 * t2) * next.inValue
		+ (t3 - t2) * m2;
}

float snapshotValueAtOffset(const AutomationClipReadSnapshot& snapshot, std::size_t index, int offset)
{
	return snapshotSegmentValueAtOffset(snapshot, snapshot.nodes[index], snapshot.nodes[index + 1], offset);
}

float snapshotValueAt(const AutomationClipReadSnapshot& snapshot, int tick)
{
	if (snapshot.nodes.empty()) { return 0.0f; }
	const auto next = std::lower_bound(snapshot.nodes.begin(), snapshot.nodes.end(), tick,
		[](const AutomationClipReadSnapshot::Node& node, int pos) { return node.tick < pos; });
	if (next != snapshot.nodes.end() && next->tick == tick) { return next->inValue; }
	if (next == snapshot.nodes.begin()) { return 0.0f; }
	const auto previous = std::prev(next);
	if (next == snapshot.nodes.end()) { return previous->outValue; }
	return snapshotValueAtOffset(snapshot, static_cast<std::size_t>(previous - snapshot.nodes.begin()), tick - previous->tick);
}

AutomationClipReadSnapshot::Node overlayNode(const AutomationClip::RecordingOverlayPoint& point)
{
	return {point.tick, point.value, point.value, 0.0f, 0.0f, false, 0.0f, AutomationNode::Curve::Clip};
}

void considerNextOverlayNode(const AutomationClip::RecordingOverlayPoint* point, int tick, bool inclusive,
	bool& found, AutomationClipReadSnapshot::Node& result)
{
	if (point == nullptr) { return; }
	if (inclusive ? point->tick < tick : point->tick <= tick) { return; }
	if (found && point->tick > result.tick) { return; }
	result = overlayNode(*point);
	found = true;
}

void considerPreviousOverlayNode(const AutomationClip::RecordingOverlayPoint* point, int tick, bool inclusive,
	bool& found, AutomationClipReadSnapshot::Node& result)
{
	if (point == nullptr) { return; }
	if (inclusive ? point->tick > tick : point->tick >= tick) { return; }
	if (found && point->tick < result.tick) { return; }
	result = overlayNode(*point);
	found = true;
}

bool nextVirtualNode(const AutomationClipReadSnapshot& snapshot, const AutomationClip::RecordingOverlay* overlay,
	int tick, bool inclusive, AutomationClipReadSnapshot::Node& result)
{
	const auto base = inclusive
		? std::lower_bound(snapshot.nodes.begin(), snapshot.nodes.end(), tick,
			[](const AutomationClipReadSnapshot::Node& node, int pos) { return node.tick < pos; })
		: std::upper_bound(snapshot.nodes.begin(), snapshot.nodes.end(), tick,
			[](int pos, const AutomationClipReadSnapshot::Node& node) { return pos < node.tick; });
	bool found = base != snapshot.nodes.end();
	if (found) { result = *base; }
	if (overlay != nullptr)
	{
		considerNextOverlayNode(overlay->hasBefore ? &overlay->before : nullptr, tick, inclusive, found, result);
		considerNextOverlayNode(overlay->hasAfter ? &overlay->after : nullptr, tick, inclusive, found, result);
	}
	return found;
}

bool previousVirtualNode(const AutomationClipReadSnapshot& snapshot, const AutomationClip::RecordingOverlay* overlay,
	int tick, bool inclusive, AutomationClipReadSnapshot::Node& result)
{
	const auto base = inclusive
		? std::upper_bound(snapshot.nodes.begin(), snapshot.nodes.end(), tick,
			[](int pos, const AutomationClipReadSnapshot::Node& node) { return pos < node.tick; })
		: std::lower_bound(snapshot.nodes.begin(), snapshot.nodes.end(), tick,
			[](const AutomationClipReadSnapshot::Node& node, int pos) { return node.tick < pos; });
	bool found = base != snapshot.nodes.begin();
	if (found) { result = *std::prev(base); }
	if (overlay != nullptr)
	{
		considerPreviousOverlayNode(overlay->hasBefore ? &overlay->before : nullptr, tick, inclusive, found, result);
		considerPreviousOverlayNode(overlay->hasAfter ? &overlay->after : nullptr, tick, inclusive, found, result);
	}
	return found;
}

float snapshotValueAt(const AutomationClipReadSnapshot& snapshot, int tick,
	const AutomationClip::RecordingOverlay* overlay)
{
	if (overlay == nullptr || (!overlay->hasBefore && !overlay->hasAfter)) { return snapshotValueAt(snapshot, tick); }
	AutomationClipReadSnapshot::Node next, previous;
	if (nextVirtualNode(snapshot, overlay, tick, true, next) && next.tick == tick) { return next.inValue; }
	if (!previousVirtualNode(snapshot, overlay, tick, false, previous)) { return 0.0f; }
	if (!nextVirtualNode(snapshot, overlay, tick, false, next)) { return previous.outValue; }
	return snapshotSegmentValueAtOffset(snapshot, previous, next, tick - previous.tick);
}

void pendingRecordingOverlay(const AutomationClipReadSnapshot& snapshot, jo_id_t clipId,
	const TimePos& mapTime, AutomationClip::RecordingOverlay& overlay)
{
	auto* song = Engine::getSong();
	if (song == nullptr) { return; }
	AutomationRecordingQueue::Event before, after;
	song->pendingAutomationRecordBounds(clipId, snapshot.appliedSequences, mapTime,
		before, overlay.hasBefore, after, overlay.hasAfter);
	if (overlay.hasBefore)
	{
		overlay.before = {before.time.getTicks(), before.value};
	}
	if (overlay.hasAfter)
	{
		overlay.after = {after.time.getTicks(), after.value};
	}
}

bool hasRecordingOverlayPoints(const AutomationClip::RecordingOverlay* overlay)
{
	return overlay != nullptr && (overlay->hasBefore || overlay->hasAfter);
}

bool lastVirtualNode(const AutomationClipReadSnapshot& snapshot, const AutomationClip::RecordingOverlay* overlay,
	AutomationClipReadSnapshot::Node& result)
{
	bool found = !snapshot.nodes.empty();
	if (found) { result = snapshot.nodes.back(); }
	if (overlay == nullptr) { return found; }
	if (overlay->hasBefore && (!found || overlay->before.tick > result.tick))
	{
		result = overlayNode(overlay->before);
		found = true;
	}
	if (overlay->hasAfter && (!found || overlay->after.tick > result.tick))
	{
		result = overlayNode(overlay->after);
		found = true;
	}
	return found;
}

float rampSegmentValue(const AutomationClipReadSnapshot& snapshot,
	const AutomationClipReadSnapshot::Node& previous, const AutomationClipReadSnapshot::Node& next,
	int ticks, float fraction)
{
	const int offset = ticks - previous.tick;
	const float start = offset == 0 ? previous.outValue
		: snapshotSegmentValueAtOffset(snapshot, previous, next, offset);
	const float end = snapshotSegmentValueAtOffset(snapshot, previous, next, offset + 1);
	return start + fraction * (end - start);
}

} // namespace

AutomationClip::~AutomationClip()
{
	retireReadSnapshot(m_readSnapshot.exchange(nullptr, std::memory_order_seq_cst));
}


void AutomationClip::retireReadSnapshot(AutomationClipReadSnapshot* snapshot)
{
	if (snapshot == nullptr) { return; }
	{
		QMutexLocker lock(&s_retiredAutomationSnapshotMutex);
		snapshot->retiredNext = s_retiredAutomationSnapshots;
		s_retiredAutomationSnapshots = snapshot;
	}
	collectRetiredAutomationSnapshots();
}


void AutomationClip::collectRetiredReadSnapshots()
{
	collectRetiredAutomationSnapshots();
}


bool AutomationClip::snapshotReadersActive() noexcept
{
	return s_activeAutomationSnapshotReaders.load(std::memory_order_seq_cst) != 0;
}


TimePos AutomationClip::recordingMapTime(const TimePos& clipRelativeTime) const noexcept
{
	AutomationSnapshotReadScope read(m_readSnapshot);
	const int startOffset = read.get() == nullptr ? 0 : read.get()->startTimeOffset;
	const TimePos relative = clipRelativeTime - TimePos(startOffset);
	return std::max(TimePos(0), Note::quantized(relative, quantization()));
}


void AutomationClip::beginSnapshotReaderForTest()
{
	s_activeAutomationSnapshotReaders.fetch_add(1, std::memory_order_seq_cst);
}


void AutomationClip::endSnapshotReaderForTest()
{
	s_activeAutomationSnapshotReaders.fetch_sub(1, std::memory_order_seq_cst);
}


std::size_t AutomationClip::retiredReadSnapshotCountForTest()
{
	QMutexLocker lock(&s_retiredAutomationSnapshotMutex);
	std::size_t count = 0;
	for (auto* retired = s_retiredAutomationSnapshots; retired != nullptr; retired = retired->retiredNext)
	{
		++count;
	}
	return count;
}


void AutomationClip::publishReadSnapshot()
{
	if (m_snapshotPublicationSuppressed) { return; }
	AutomationClipReadSnapshot* snapshot = new (std::nothrow) AutomationClipReadSnapshot;
	if (snapshot == nullptr) { qFatal("could not allocate AutomationClip read snapshot"); }
	try
	{
		snapshot->nodes.reserve(static_cast<std::size_t>(m_timeMap.size()));
		for (auto it = m_timeMap.cbegin(); it != m_timeMap.cend(); ++it)
		{
			const AutomationNode& node = it.value();
			snapshot->nodes.push_back({it.key(), node.getInValue(), node.getOutValue(),
				node.getInTangent(), node.getOutTangent(), node.lockedTangents(), node.bend(), node.curve()});
		}
		snapshot->appliedSequences = m_appliedRecordingSequences;
	}
	catch (const std::bad_alloc&)
	{
		delete snapshot;
		qFatal("could not allocate AutomationClip read snapshot nodes");
	}
	snapshot->progression = m_progressionType;
	snapshot->tension = m_tension;
	snapshot->startPosition = startPosition().getTicks();
	snapshot->startTimeOffset = startTimeOffset().getTicks();
	snapshot->length = length().getTicks();
	snapshot->muted = Clip::isMuted();
	snapshot->inPattern = isInPattern();
	snapshot->sampleAccurate = m_rampSetting == RampSetting::Default
		? m_progressionType != ProgressionType::Discrete : m_rampSetting == RampSetting::On;
	snapshot->relative = isRelative();
	retireReadSnapshot(m_readSnapshot.exchange(snapshot, std::memory_order_seq_cst));
	++m_readSnapshotPublicationCount;
}


bool AutomationClip::hasAutomation() const
{
	AutomationSnapshotReadScope read(m_readSnapshot);
	return read.get() != nullptr && !read.get()->nodes.empty();
}


bool AutomationClip::sampleAccurate() const
{
	AutomationSnapshotReadScope read(m_readSnapshot);
	return read.get() != nullptr && read.get()->sampleAccurate;
}


bool AutomationClip::isMuted() const
{
	AutomationSnapshotReadScope read(m_readSnapshot);
	return read.get() != nullptr && read.get()->muted;
}


void AutomationClip::movePosition(const TimePos& pos)
{
	Clip::movePosition(pos);
	QMutexLocker lock(&m_clipMutex);
	publishReadSnapshot();
}


void AutomationClip::changeLength(const TimePos& newLength)
{
	Clip::changeLength(newLength);
	QMutexLocker lock(&m_clipMutex);
	publishReadSnapshot();
}


void AutomationClip::setStartTimeOffset(const TimePos& offset)
{
	Clip::setStartTimeOffset(offset);
	QMutexLocker lock(&m_clipMutex);
	publishReadSnapshot();
}


void AutomationClip::setMuted(bool muted)
{
	Clip::setMuted(muted);
	QMutexLocker lock(&m_clipMutex);
	publishReadSnapshot();
}


void AutomationClip::setRelative(bool relative)
{
	m_relative.store(relative, std::memory_order_relaxed);
	QMutexLocker lock(&m_clipMutex);
	publishReadSnapshot();
}




float AutomationClip::valueAt( const TimePos & _time ) const
{
	AutomationSnapshotReadScope read(m_readSnapshot);
	if (read.get() == nullptr) { return 0.0f; }
	RecordingOverlay overlay;
	pendingRecordingOverlay(*read.get(), static_cast<const JournallingObject&>(*this).id(),
		_time, overlay);
	return snapshotValueAt(*read.get(), _time.getTicks(), &overlay);
}




void AutomationClip::setSampleAccurate( bool on )
{
	setRampSetting( on ? RampSetting::On : RampSetting::Off );
}


void AutomationClip::setRampSetting( RampSetting setting )
{
	QMutexLocker m(&m_clipMutex);
	m_rampSetting = setting;
	publishReadSnapshot();
}


float AutomationClip::rampValueAt(const AutomationClipReadSnapshot& snapshot, const TimePos& rel, float fraction,
	const RecordingOverlay* overlay)
{
	if (rel < 0 || (snapshot.nodes.empty() && !hasRecordingOverlayPoints(overlay))) { return 0.0f; }
	const int ticks = static_cast<int>( rel.getTicks() );
	AutomationClipReadSnapshot::Node last;
	if (lastVirtualNode(snapshot, overlay, last) && ticks >= last.tick)
	{
		// At or after the last node the curve holds the last node's out value -
		// exactly what valueAt(TimePos) returns past the end of the map.
		return last.outValue;
	}

	// The segment that starts at the node at or before this tick is the one the
	// value moves along, so the value at the node itself is its OUT value:
	// valueAt(v, 0) returns the IN value, which is the limit the PREVIOUS
	// segment reaches, not the one this one starts from.
	AutomationClipReadSnapshot::Node previous, next;
	if (!previousVirtualNode(snapshot, overlay, ticks, true, previous))
	{
		// Before the FIRST node no segment has started, and stepping back from
		// upperBound() would step off the map: the clip's value there is the one
		// valueAt(TimePos) returns for the same ticks, which is no value at all.
		return 0.0f;
	}
	if (!nextVirtualNode(snapshot, overlay, ticks, false, next)) { return previous.outValue; }
	return rampSegmentValue(snapshot, previous, next, ticks, fraction);
}


void AutomationClip::addRampKnot(const AutomationClipReadSnapshot& snapshot, AutomationRamp& ramp,
	const TimePos& relClipStart, int frame, int frameOffsetInTick, double framesPerTick,
	const RecordingOverlay* pendingOverlay) const
{
	// The knot's own POSITION inside its tick, from the frame it is written on,
	// so a knot is exactly on the curve rather than at a rounded boundary.
	const double position = ( static_cast<double>( frameOffsetInTick ) + frame ) / framesPerTick;
	const int tickOffset = static_cast<int>( std::floor( position ) );
	const float fraction = static_cast<float>( position - tickOffset );

	TimePos rel = relClipStart + tickOffset;
	if( !snapshot.inPattern )
	{
		rel = std::min(static_cast<int>(rel), snapshot.length - snapshot.startTimeOffset);
	}
	RecordingOverlay overlay;
	if (pendingOverlay == nullptr)
	{
		pendingRecordingOverlay(snapshot, static_cast<const JournallingObject&>(*this).id(),
			rel, overlay);
		pendingOverlay = &overlay;
	}
	ramp.addKnot(static_cast<f_cnt_t>(frame), rampValueAt(snapshot, rel, fraction, pendingOverlay));
}


void AutomationClip::addRampBoundaryKnots(const AutomationClipReadSnapshot& snapshot, AutomationRamp& ramp,
	const TimePos& relClipStart, f_cnt_t frames, int frameOffsetInTick, double framesPerTick,
	const RecordingOverlay* overlay) const
{
	for( int tick = 1; ; ++tick )
	{
		const double knotFrame = tick * framesPerTick - frameOffsetInTick;
		if( knotFrame > static_cast<double>( frames ) ) { break; }
		int before = static_cast<int>( std::floor( knotFrame ) );
		const int after = static_cast<int>( std::ceil( knotFrame ) );
		if( before == after ) { before = after - 1; }
		if( before > 0 ) { addRampKnot(snapshot, ramp, relClipStart, before, frameOffsetInTick, framesPerTick, overlay); }
		if( after > 0 && after < static_cast<int>( frames ) )
		{
			addRampKnot(snapshot, ramp, relClipStart, after, frameOffsetInTick, framesPerTick, overlay);
		}
	}
}


void AutomationClip::writeBlockRamp( AutomationRamp & ramp, const TimePos & blockStart,
	f_cnt_t frames, double framesPerTick, int frameOffsetInTick ) const
{
	AutomationSnapshotReadScope read(m_readSnapshot);
	const auto* snapshot = read.get();

	ramp.reset( frames );
	if( snapshot == nullptr || frames == 0 || framesPerTick <= 0.0 )
	{
		return;
	}

	const TimePos relStart = blockStart - TimePos(snapshot->startPosition) - TimePos(snapshot->startTimeOffset);
	if( relStart < 0 )
	{
		return; // the block starts before this clip does
	}

	// The block's own geometry travels with each knot call.
	// Knot 0 is the block's first sample, at its own position inside its tick.
	addRampKnot(*snapshot, ramp, relStart, 0, frameOffsetInTick, framesPerTick, nullptr);

	// Tick-boundary pairs preserve discrete jumps and the line between them.
	addRampBoundaryKnots(*snapshot, ramp, relStart, frames, frameOffsetInTick, framesPerTick, nullptr);

	// ...and the block's end, so the samples after the last boundary
	// interpolate to where the curve actually is when the block runs out,
	// instead of holding the last boundary's value.
	addRampKnot(*snapshot, ramp, relStart, static_cast<int>(frames), frameOffsetInTick, framesPerTick, nullptr);
}



} // namespace lmms
