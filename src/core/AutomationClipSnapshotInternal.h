/*
 * AutomationClipSnapshotInternal.h - shared private snapshot representation
 *
 * This file is part of LMMS.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef LMMS_AUTOMATION_CLIP_SNAPSHOT_INTERNAL_H
#define LMMS_AUTOMATION_CLIP_SNAPSHOT_INTERNAL_H

#include <atomic>
#include <cstdint>
#include <vector>

#include <QMutex>

#include "AutomationClip.h"

namespace lmms
{

struct AutomationClipReadSnapshot
{
	using Node = AutomationRecordingQueue::OverlayNode;

	std::vector<Node> nodes;
	std::vector<std::uint64_t> appliedSequences;
	AutomationClip::ProgressionType progression = AutomationClip::ProgressionType::Discrete;
	float tension = 1.0f;
	float lastRecordedValue = 0.0f;
	int startPosition = 0;
	int startTimeOffset = 0;
	int length = 0;
	bool muted = false;
	bool inPattern = false;
	bool sampleAccurate = false;
	bool relative = false;
	AutomationClipReadSnapshot* retiredNext = nullptr;
};

namespace automation_snapshot_internal
{

extern std::atomic<std::uint32_t> s_activeAutomationSnapshotReaders;
extern QMutex s_retiredAutomationSnapshotMutex;
extern AutomationClipReadSnapshot* s_retiredAutomationSnapshots;

void collectRetiredAutomationSnapshots();

} // namespace automation_snapshot_internal
} // namespace lmms

#endif // LMMS_AUTOMATION_CLIP_SNAPSHOT_INTERNAL_H
