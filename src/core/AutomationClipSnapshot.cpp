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
#include <limits>

#include "AutomationClip.h"
#include "AutomationRamp.h"
#include "AutomationClipSnapshotInternal.h"
#include "Engine.h"
#include "JournallingObject.h"
#include "Note.h"
#include "Song.h"

namespace lmms
{

namespace
{

using namespace automation_snapshot_internal;

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

using OverlayScratch = AutomationRecordingQueue::OverlayScratch;
using SnapshotNode = AutomationClipReadSnapshot::Node;
const SnapshotNode s_emptyOverlayNode{};

enum class VirtualNodeSource { Base, Edit, None };

std::vector<SnapshotNode>::const_iterator nextBaseNode(const AutomationClipReadSnapshot& snapshot,
	int tick, bool inclusive)
{
	if (inclusive)
	{
		return std::lower_bound(snapshot.nodes.begin(), snapshot.nodes.end(), tick,
			[](const SnapshotNode& node, int pos) { return node.tick < pos; });
	}
	return std::upper_bound(snapshot.nodes.begin(), snapshot.nodes.end(), tick,
		[](int pos, const SnapshotNode& node) { return pos < node.tick; });
}

const SnapshotNode* nextEditNode(const OverlayScratch* overlay, const SnapshotNode* begin,
	const SnapshotNode* end, int tick, bool inclusive)
{
	if (inclusive)
	{
		return std::lower_bound(begin, end, tick,
			[](const SnapshotNode& node, int pos) { return node.tick < pos; });
	}
	return std::upper_bound(begin, end, tick,
		[](int pos, const SnapshotNode& node) { return pos < node.tick; });
}

std::vector<SnapshotNode>::const_iterator previousBaseNode(const AutomationClipReadSnapshot& snapshot,
	int tick, bool inclusive)
{
	if (inclusive)
	{
		return std::upper_bound(snapshot.nodes.begin(), snapshot.nodes.end(), tick,
			[](int pos, const SnapshotNode& node) { return pos < node.tick; });
	}
	return std::lower_bound(snapshot.nodes.begin(), snapshot.nodes.end(), tick,
		[](const SnapshotNode& node, int pos) { return node.tick < pos; });
}

const SnapshotNode* previousEditNode(const SnapshotNode* begin, const SnapshotNode* end,
	int tick, bool inclusive)
{
	if (inclusive)
	{
		return std::upper_bound(begin, end, tick,
			[](int pos, const SnapshotNode& node) { return pos < node.tick; });
	}
	return std::lower_bound(begin, end, tick,
		[](const SnapshotNode& node, int pos) { return node.tick < pos; });
}

VirtualNodeSource nextNodeSource(std::vector<SnapshotNode>::const_iterator base,
	std::vector<SnapshotNode>::const_iterator baseEnd, const SnapshotNode* edit,
	const SnapshotNode* editEnd)
{
	if (edit == editEnd) { return base == baseEnd ? VirtualNodeSource::None : VirtualNodeSource::Base; }
	if (base == baseEnd) { return VirtualNodeSource::Edit; }
	return base->tick < edit->tick ? VirtualNodeSource::Base : VirtualNodeSource::Edit;
}

VirtualNodeSource previousNodeSource(bool hasBase, std::vector<SnapshotNode>::const_iterator base,
	bool hasEdit, const SnapshotNode* edit)
{
	if (!hasEdit) { return hasBase ? VirtualNodeSource::Base : VirtualNodeSource::None; }
	if (!hasBase) { return VirtualNodeSource::Edit; }
	return base->tick > edit->tick ? VirtualNodeSource::Base : VirtualNodeSource::Edit;
}

bool stepPreviousBase(std::vector<SnapshotNode>::const_iterator& base,
	std::vector<SnapshotNode>::const_iterator begin)
{
	if (base == begin) { return false; }
	--base;
	return true;
}

bool stepPreviousEdit(const SnapshotNode*& edit, const SnapshotNode* begin)
{
	if (edit == begin) { return false; }
	--edit;
	return true;
}

bool nextVirtualNode(const AutomationClipReadSnapshot& snapshot, const OverlayScratch* overlay,
	int tick, bool inclusive, SnapshotNode& result)
{
	auto base = nextBaseNode(snapshot, tick, inclusive);
	const SnapshotNode* editBegin = overlay == nullptr ? &s_emptyOverlayNode : overlay->nodes.get();
	const SnapshotNode* editEnd = editBegin + (overlay == nullptr ? 0 : overlay->nodeCount);
	auto edit = nextEditNode(overlay, editBegin, editEnd, tick, inclusive);
	for (;;)
	{
		switch (nextNodeSource(base, snapshot.nodes.end(), edit, editEnd))
		{
		case VirtualNodeSource::Base:
			result = *base++;
			return true;
		case VirtualNodeSource::Edit:
		{
			const auto candidate = *edit++;
			if (base != snapshot.nodes.end() && base->tick == candidate.tick) { ++base; }
			if (candidate.present) { result = candidate; return true; }
			break;
		}
		case VirtualNodeSource::None:
			return false;
	}
	}
}

bool previousVirtualNode(const AutomationClipReadSnapshot& snapshot, const OverlayScratch* overlay,
	int tick, bool inclusive, SnapshotNode& result)
{
	auto base = previousBaseNode(snapshot, tick, inclusive);
	bool hasBase = stepPreviousBase(base, snapshot.nodes.begin());
	const SnapshotNode* editBegin = overlay == nullptr ? &s_emptyOverlayNode : overlay->nodes.get();
	const SnapshotNode* editEnd = editBegin + (overlay == nullptr ? 0 : overlay->nodeCount);
	auto edit = previousEditNode(editBegin, editEnd, tick, inclusive);
	bool hasEdit = overlay != nullptr && stepPreviousEdit(edit, editBegin);
	while (hasBase || hasEdit)
	{
		if (previousNodeSource(hasBase, base, hasEdit, edit) == VirtualNodeSource::Base)
		{
			result = *base;
			return true;
		}
		const auto candidate = *edit;
		if (hasBase && base->tick == edit->tick)
		{
			hasBase = stepPreviousBase(base, snapshot.nodes.begin());
		}
		hasEdit = stepPreviousEdit(edit, editBegin);
		if (candidate.present) { result = candidate; return true; }
	}
	return false;
}

bool exactVirtualNode(const AutomationClipReadSnapshot& snapshot, const OverlayScratch* overlay,
	int tick, SnapshotNode& result)
{
	if (overlay != nullptr)
	{
		const auto edit = std::lower_bound(overlay->nodes.get(), overlay->nodes.get() + overlay->nodeCount, tick,
			[](const SnapshotNode& node, int pos) { return node.tick < pos; });
		if (edit != overlay->nodes.get() + overlay->nodeCount && edit->tick == tick)
		{
			if (edit->present) { result = *edit; return true; }
			return false;
		}
	}
	const auto base = std::lower_bound(snapshot.nodes.begin(), snapshot.nodes.end(), tick,
		[](const SnapshotNode& node, int pos) { return node.tick < pos; });
	if (base == snapshot.nodes.end() || base->tick != tick) { return false; }
	result = *base;
	return true;
}

float virtualValueAt(const AutomationClipReadSnapshot& snapshot, int tick, const OverlayScratch* overlay)
{
	SnapshotNode exact, previous, next;
	if (exactVirtualNode(snapshot, overlay, tick, exact)) { return exact.inValue; }
	if (!previousVirtualNode(snapshot, overlay, tick, false, previous)) { return 0.0f; }
	if (!nextVirtualNode(snapshot, overlay, tick, false, next)) { return previous.outValue; }
	return snapshotSegmentValueAtOffset(snapshot, previous, next, tick - previous.tick);
}

bool setVirtualNode(OverlayScratch& scratch, const SnapshotNode& node)
{
	auto* begin = scratch.nodes.get();
	auto* end = begin + scratch.nodeCount;
	auto* edit = std::lower_bound(begin, end, node.tick,
		[](const SnapshotNode& current, int tick) { return current.tick < tick; });
	if (edit != end && edit->tick == node.tick) { *edit = node; return true; }
	if (scratch.nodeCount == scratch.nodeCapacity) { scratch.capacityExceeded = true; return false; }
	const auto index = static_cast<std::size_t>(edit - begin);
	for (std::size_t move = scratch.nodeCount; move > index; --move)
	{
		scratch.nodes[move] = scratch.nodes[move - 1];
	}
	scratch.nodes[index] = node;
	++scratch.nodeCount;
	return true;
}

void recalculateVirtualTangents(const AutomationClipReadSnapshot& snapshot, OverlayScratch& scratch, int changedTick)
{
	SnapshotNode current;
	if (!previousVirtualNode(snapshot, &scratch, changedTick, false, current)
		&& !nextVirtualNode(snapshot, &scratch, changedTick, true, current)) { return; }
	for (int count = 0; count < 3; ++count)
	{
		SnapshotNode previous, next;
		const bool hasPrevious = previousVirtualNode(snapshot, &scratch, current.tick, false, previous);
		const bool hasNext = nextVirtualNode(snapshot, &scratch, current.tick, false, next);
		if (!current.lockedTangents)
		{
			if (!hasNext)
			{
				current.inTangent = 0.0f;
				current.outTangent = 0.0f;
			}
			else if (!hasPrevious)
			{
				current.inTangent = 0.0f;
				current.outTangent = (next.inValue - current.outValue) / (next.tick - current.tick);
			}
			else if (current.outValue - current.inValue == 0.0f)
			{
				const float tangent = (next.inValue - previous.outValue) / (next.tick - previous.tick);
				current.inTangent = tangent;
				current.outTangent = tangent;
			}
			else
			{
				current.inTangent = (current.inValue - previous.outValue) / (current.tick - previous.tick);
				current.outTangent = (next.inValue - current.outValue) / (next.tick - current.tick);
			}
			if (!setVirtualNode(scratch, current)) { return; }
		}
		if (!hasNext) { return; }
		current = next;
	}
}

void removeVirtualNode(const AutomationClipReadSnapshot& snapshot, OverlayScratch& scratch, int tick)
{
	SnapshotNode node;
	if (exactVirtualNode(snapshot, &scratch, tick, node))
	{
		node.present = false;
		if (!setVirtualNode(scratch, node)) { return; }
	}
	recalculateVirtualTangents(snapshot, scratch, tick);
}

void replayPendingEvents(const AutomationClipReadSnapshot& snapshot, OverlayScratch& scratch)
{
	float lastRecordedValue = snapshot.lastRecordedValue;
	for (std::size_t index = 0; index < scratch.eventCount; ++index)
	{
		const auto& event = scratch.events[index];
		const int tick = event.time.getTicks();
		if (event.value != lastRecordedValue)
		{
			const SnapshotNode node{tick, event.value, event.value, 0.0f, 0.0f, false, 0.0f,
				AutomationNode::Curve::Clip, true};
			if (!setVirtualNode(scratch, node)) { return; }
			lastRecordedValue = event.value;
			recalculateVirtualTangents(snapshot, scratch, tick);
		}
		else if (virtualValueAt(snapshot, tick, &scratch) != event.value)
		{
			removeVirtualNode(snapshot, scratch, tick);
		}
	}
}

void pendingRecordingOverlay(const AutomationClipReadSnapshot& snapshot, jo_id_t clipId,
	AutomationClip::RecordingOverlay& overlay)
{
	auto* song = Engine::getSong();
	if (song == nullptr) { return; }
	auto* scratch = song->pendingAutomationRecordingEvents(clipId, snapshot.appliedSequences);
	if (scratch == nullptr || scratch->eventCount == 0) { return; }
	replayPendingEvents(snapshot, *scratch);
	if (!scratch->capacityExceeded) { overlay.scratch = scratch; }
	else { song->noteAutomationOverlayOverflow(); }
}

bool hasVirtualNodes(const AutomationClipReadSnapshot& snapshot, const OverlayScratch* overlay)
{
	SnapshotNode first;
	return nextVirtualNode(snapshot, overlay, std::numeric_limits<int>::min(), true, first);
}

bool lastVirtualNode(const AutomationClipReadSnapshot& snapshot, const OverlayScratch* overlay,
	SnapshotNode& result)
{
	return previousVirtualNode(snapshot, overlay, std::numeric_limits<int>::max(), true, result);
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

TimePos AutomationClip::recordingMapTime(const TimePos& clipRelativeTime) const noexcept
{
	AutomationSnapshotReadScope read(m_readSnapshot);
	const int startOffset = read.get() == nullptr ? 0 : read.get()->startTimeOffset;
	const TimePos relative = clipRelativeTime - TimePos(startOffset);
	return std::max(TimePos(0), Note::quantized(relative, quantization()));
}


bool AutomationClip::hasAutomation() const
{
	AutomationSnapshotReadScope read(m_readSnapshot);
	const auto* snapshot = read.get();
	if (snapshot == nullptr) { return false; }
	RecordingOverlay overlay;
	pendingRecordingOverlay(*snapshot, static_cast<const JournallingObject&>(*this).id(), overlay);
	return hasVirtualNodes(*snapshot, overlay.scratch);
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
		overlay);
	return virtualValueAt(*read.get(), _time.getTicks(), overlay.scratch);
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
	const auto* scratch = overlay == nullptr ? nullptr : overlay->scratch;
	if (rel < 0 || !hasVirtualNodes(snapshot, scratch)) { return 0.0f; }
	const int ticks = static_cast<int>( rel.getTicks() );
	AutomationClipReadSnapshot::Node last;
	if (lastVirtualNode(snapshot, scratch, last) && ticks >= last.tick)
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
	if (!previousVirtualNode(snapshot, scratch, ticks, true, previous))
	{
		// Before the FIRST node no segment has started, and stepping back from
		// upperBound() would step off the map: the clip's value there is the one
		// valueAt(TimePos) returns for the same ticks, which is no value at all.
		return 0.0f;
	}
	if (!nextVirtualNode(snapshot, scratch, ticks, false, next)) { return previous.outValue; }
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
		pendingRecordingOverlay(snapshot, static_cast<const JournallingObject&>(*this).id(), overlay);
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

	RecordingOverlay overlay;
	pendingRecordingOverlay(*snapshot, static_cast<const JournallingObject&>(*this).id(), overlay);
	// The block's own geometry travels with each knot call.
	// Knot 0 is the block's first sample, at its own position inside its tick.
	addRampKnot(*snapshot, ramp, relStart, 0, frameOffsetInTick, framesPerTick, &overlay);

	// Tick-boundary pairs preserve discrete jumps and the line between them.
	addRampBoundaryKnots(*snapshot, ramp, relStart, frames, frameOffsetInTick, framesPerTick, &overlay);

	// ...and the block's end, so the samples after the last boundary
	// interpolate to where the curve actually is when the block runs out,
	// instead of holding the last boundary's value.
	addRampKnot(*snapshot, ramp, relStart, static_cast<int>(frames), frameOffsetInTick, framesPerTick, &overlay);
}



} // namespace lmms
