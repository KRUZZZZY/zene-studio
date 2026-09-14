/*
 * ControlVcaTestSupport.h - the SHARED helpers of the `vca.*` command group's
 *                           acceptance tests: one definition for the two test
 *                           files that use them.
 *
 * The group's proof is two registered suites, split the way the repository's
 * file-length ratchet requires (a NEW file over 500 lines fails Gate 7):
 *
 *   tests/src/core/ControlVcaCommandsTest.cpp    the SURFACE half - the ids and
 *                                               their schemas, the contract rows,
 *                                               the typed refusals, and the
 *                                               measured effect of the fader and
 *                                               of membership (one undo each).
 *   tests/src/core/ControlVcaEditGroupsTest.cpp  the EDIT half - the phase-locked
 *                                               move across several tracks, the
 *                                               unlocked/skipped reporting, the
 *                                               lock's refusals, and the entity's
 *                                               own edit set + lock in the mixer
 *                                               element.
 *
 * Both drive the same registry, the same fixture builders and the same evidence
 * prefix, so those live here - free inline functions in a namespace, not statics
 * in each class: a second copy of a helper is exactly the drift the split exists
 * to prevent (the same argument ReversibilityTestSupport.h makes for its own
 * two halves).
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

#ifndef LMMS_CONTROL_VCA_TEST_SUPPORT_H
#define LMMS_CONTROL_VCA_TEST_SUPPORT_H

#include <cstdio>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ControlRegistry.h"
#include "Engine.h"
#include "Mixer.h"
#include "ReversibilityTestSupport.h"
#include "VcaGroup.h"

//! Shared helpers of the `vca.*` command group's two test files.
namespace vcatest
{
using namespace lmms;

//! The group's whole id list, in the order the group documents it.
inline const QStringList& vcaIds()
{
	static const QStringList ids = {
		QStringLiteral("vca.create"),
		QStringLiteral("vca.remove"),
		QStringLiteral("vca.list"),
		QStringLiteral("vca.get_state"),
		QStringLiteral("vca.rename"),
		QStringLiteral("vca.set_gain"),
		QStringLiteral("vca.set_mute"),
		QStringLiteral("vca.set_solo"),
		QStringLiteral("vca.assign"),
		QStringLiteral("vca.unassign"),
		QStringLiteral("vca.set_phase_lock"),
		QStringLiteral("vca.track_add"),
		QStringLiteral("vca.track_remove"),
		QStringLiteral("vca.edit_move"),
	};
	return ids;
}

//! The two reads, which the contract calls not_mutating.
inline const QStringList& vcaReads()
{
	static const QStringList reads = {
		QStringLiteral("vca.list"),
		QStringLiteral("vca.get_state"),
	};
	return reads;
}

//! A clip of `length` ticks at `position` on `track`; the clip-<n> id.
inline QString addClip(const QString& track, int position, int length)
{
	const ControlResult added = revtest::run(QStringLiteral("clip.add"),
		{{QStringLiteral("track"), track}, {QStringLiteral("position"), position},
			{QStringLiteral("length"), length}});
	return added.ok ? added.result.value(QStringLiteral("clip")).toString() : QString();
}

//! One evidence line, so a reader can paste the numbers into the report.
inline void evidence(const QString& line)
{
	std::fprintf(stdout, "VCA_EDIT_EVIDENCE %s\n", qPrintable(line));
	std::fflush(stdout);
}

//! Create a group and return its vca-<n> id, or empty when the command failed.
inline QString createGroup(const QString& name = QStringLiteral("Drums"))
{
	const ControlResult made = revtest::run(QStringLiteral("vca.create"),
		QJsonObject{{QStringLiteral("name"), name}});
	return made.ok ? made.result.value(QStringLiteral("group")).toString() : QString();
}

//! One group's `gain_for_member` for a channel, or -1 when the group does not
//! hold it.
inline double memberGain(const QString& group, const QString& channel)
{
	const ControlResult state = revtest::run(QStringLiteral("vca.get_state"),
		QJsonObject{{QStringLiteral("group"), group}});
	for (const QJsonValue& value : state.result.value(QStringLiteral("members")).toArray())
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("channel")).toString() == channel)
		{
			return entry.value(QStringLiteral("gain_for_member")).toDouble();
		}
	}
	return -1.0;
}

//! Drop every group the mix currently holds, so each test starts from a mix it
//! owns (the registry and the engine are process-wide).
inline void clearGroups()
{
	Mixer* mixer = Engine::mixer();
	while (!mixer->vcaGroups().empty())
	{
		if (!mixer->deleteVcaGroup(mixer->vcaGroups().front()->id())) { break; }
	}
}

} // namespace vcatest

#endif // LMMS_CONTROL_VCA_TEST_SUPPORT_H
