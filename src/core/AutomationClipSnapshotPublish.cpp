/*
 * AutomationClipSnapshotPublish.cpp - publish and reclaim immutable read snapshots
 *
 * This file is part of LMMS.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <algorithm>
#include <new>

#include <QMutexLocker>

#include "AutomationClip.h"
#include "AutomationClipSnapshotInternal.h"
#include "JournallingObject.h"

namespace lmms::automation_snapshot_internal
{

std::atomic<std::uint32_t> s_activeAutomationSnapshotReaders{0};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
	"automation snapshot reader count must be lock-free");
static_assert(std::atomic<AutomationClipReadSnapshot*>::is_always_lock_free,
	"automation snapshot pointer must be lock-free");
QMutex s_retiredAutomationSnapshotMutex;
AutomationClipReadSnapshot* s_retiredAutomationSnapshots = nullptr;

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

} // namespace lmms::automation_snapshot_internal

namespace lmms
{

using namespace automation_snapshot_internal;

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
				node.getInTangent(), node.getOutTangent(), node.lockedTangents(), node.bend(), node.curve(), true});
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
	snapshot->lastRecordedValue = m_lastRecordedValue;
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

} // namespace lmms
