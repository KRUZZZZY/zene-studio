/*
 * ControlCommandsClipTrim.cpp - the clip group's EDGE-editing verbs (SPEC A11-A16):
 *                               clip.trim and clip.slip.
 *
 * Both verbs write the same four clip attributes clip.move / clip.resize already
 * write - `pos`, `len`, `off` and `autoresize` - and nothing else. That is the
 * design decision this file records, and it is not the obvious one.
 *
 * THE TRAP, stated where the reader needs it. `SampleClip::saveSettings` writes the
 * authored source window (`srcin` / `srcout`) ONLY when that window is not the whole
 * buffer, and `SampleClip::loadSettings` applies it ONLY
 * `if (_this.hasAttribute("srcin") || _this.hasAttribute("srcout"))` - there is NO
 * reset-on-absence for the window (compare `Note::loadSettings`, which reads
 * `attribute("prob", "1")` and therefore DOES reset on absence). A Clip checkpoint
 * captures the clip's XML BEFORE the write, so a trim or a slip authored through
 * `SampleClip::setSampleWindow()` would undo to the EDITED window: the pre-first-edit
 * XML carries no window at all, and loading it leaves the window alone. The four
 * attributes below are written unconditionally, so a checkpoint taken before a FIRST
 * edit is a live inverse in both directions. `tests/control-verb-inverses.py` proves
 * that for real, and its negative control neuters the same rule and shows the undo
 * fail.
 *
 * SCOPE. `clip.trim` is the song editor's own left-edge drag
 * (gui/clips/ClipView.cpp, the `positionOffset` branch): the clip's start, its length
 * and its source offset move together, so the audio the clip already carried stays at
 * the same SONG position. `clip.move` alone cannot do that (it slides the audio with
 * the clip) and `clip.resize` alone cannot either (it changes only the tail).
 * `clip.slip` is the other half: the clip rectangle does not move at all, and the
 * SOURCE slides inside it, which is the `off` attribute and nothing else.
 * Neither verb authors a pattern clip's offset modulo the pattern length - the song
 * editor does that as a GUI overflow guard and the engine moduluses at use time
 * (MidiClip), so the value this surface records is the value the model holds.
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

#include <QJsonObject>

#include "Clip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

/*! The mechanism a reader needs in order to believe the class, named once.
 *
 *  Every attribute a trim or a slip writes is written UNCONDITIONALLY by
 *  SampleClip::saveSettings (`pos`, `len`, `off`) and by MidiClip::exportToXML
 *  (`pos`, `len`), and read back unconditionally by the matching loadSettings - so
 *  the checkpoint the command takes before its first write already carries the
 *  pre-edit value of each, and restoring it is a real inverse rather than a
 *  reset-on-absence accident.
 */
const QString ClauseClipEdgeJournalled = QStringLiteral("ProjectJournal (Clip checkpoint: "
	"SampleClip::saveSettings and MidiClip::exportToXML write 'pos', 'len', 'off' and "
	"'autoresize' unconditionally and the matching loadSettings read all four unconditionally, "
	"so the checkpoint taken before the first edit already carries the pre-edit values)");

//! The state an edge edit is read back through: the same keys clipState() reports,
//! restricted to the four the two verbs can move.
QJsonObject edgeState(const ClipRef& ref)
{
	QJsonObject out;
	out.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	out.insert(QStringLiteral("position"), ref.clip->startPosition().getTicks());
	out.insert(QStringLiteral("length"), ref.clip->length().getTicks());
	out.insert(QStringLiteral("end"), ref.clip->endPosition().getTicks());
	out.insert(QStringLiteral("offset"), ref.clip->startTimeOffset().getTicks());
	return out;
}

//! The three values a trim wants, computed before anything is written.
struct TrimPlan
{
	tick_t start = 0;
	tick_t length = 0;
	tick_t offset = 0;
};

/*! Works out the post-trim edges, or refuses typed and leaves \p plan untouched.
 *
 *  The refusal is the rejected-edit rule and not a clamp (invariant I4's shape):
 *  `end <= start` would leave a zero-length clip, and `changeLength` on a
 *  SampleClip silently floors at one tick - so the caller asked for something the
 *  model cannot hold and is told so instead of getting a different edit back.
 */
bool planTrim(const ClipRef& ref, const QJsonObject& args, TrimPlan* plan, ControlResult* error)
{
	const tick_t oldStart = ref.clip->startPosition().getTicks();
	const tick_t oldEnd = ref.clip->endPosition().getTicks();
	const tick_t start = static_cast<tick_t>(args.value(QStringLiteral("start")).toDouble());
	const tick_t end = args.contains(QStringLiteral("end"))
		? static_cast<tick_t>(args.value(QStringLiteral("end")).toDouble())
		: oldEnd;
	if (end <= start)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'end' (%1) must be after 'start' (%2): a trim cannot leave an "
				"empty clip").arg(end).arg(start));
		return false;
	}
	plan->start = start;
	plan->length = end - start;
	// ClipView's head-trim rule (the song editor's own left-edge drag): the source
	// offset moves by the same delta as the clip's start. srcin frames and `off`
	// ticks both fold into SampleClip::sourceFrameAt's single origin, which is why
	// moving the two together holds the audio still on the timeline.
	plan->offset = ref.clip->startTimeOffset().getTicks() + (oldStart - start);
	return true;
}

void registerClipTrim(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.trim");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("trim");
	cmd.description = QStringLiteral("Move a clip's start edge to an absolute tick, holding the "
		"audio the clip already carried at the same song position: the start, the length and the "
		"source offset move together, which is what the song editor's own left-edge drag does. An "
		"optional 'end' trims the tail in the same step. Reversible through the ProjectJournal "
		"(Clip checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("start"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("end"), integerProperty(1, MaxSongLength)},
	}, {QStringLiteral("clip"), QStringLiteral("start")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("position"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("length"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("end"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("offset"), integerProperty(-MaxSongLength, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ClipRef ref;
		if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }

		TrimPlan plan;
		const QJsonObject before = edgeState(ref);
		if (!planTrim(ref, args, &plan, &error)) { return error; }

		ref.clip->addJournalCheckPoint();
		ref.clip->movePosition(TimePos(plan.start));
		ref.clip->changeLength(TimePos(plan.length));
		ref.clip->setStartTimeOffset(TimePos(plan.offset));
		ref.clip->setAutoResize(false);

		QJsonObject result = edgeState(ref);
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), clipId(ref.ordinal));
		inverseArgs.insert(QStringLiteral("start"), before.value(QStringLiteral("position")));
		inverseArgs.insert(QStringLiteral("end"), before.value(QStringLiteral("end")));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(before, QStringLiteral("clip.trim"), inverseArgs, true,
				ClauseClipEdgeJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerClipSlip(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.slip");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("slip");
	cmd.description = QStringLiteral("Slip a clip's content inside its own rectangle: the clip's "
		"position and its length do not move, and the part of the source that plays at the clip's "
		"start becomes 'offset' ticks into it. Slip is the only verb that moves the audio without "
		"moving the clip. Reversible through the ProjectJournal (Clip checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("offset"), integerProperty(-MaxSongLength, MaxSongLength)},
	}, {QStringLiteral("clip"), QStringLiteral("offset")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("position"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("length"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("offset"), integerProperty(-MaxSongLength, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ClipRef ref;
		if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }

		// The engine accepts the whole signed range and CLAMPS the rendered frame
		// into the clip's own source window (SampleClip::sourceFrameAt), so there is
		// no state to refuse before the checkpoint: an offset past the end of the
		// source is a silent region, not a corrupt clip. The `off` attribute is
		// written unconditionally, so an offset of 0 is serialized as 0 rather than
		// as an absence - which is exactly what makes the first slip reversible.
		const QJsonObject before = edgeState(ref);
		const tick_t offset = static_cast<tick_t>(
			args.value(QStringLiteral("offset")).toDouble());

		ref.clip->addJournalCheckPoint();
		ref.clip->setStartTimeOffset(TimePos(offset));
		ref.clip->setAutoResize(false);

		QJsonObject result = edgeState(ref);
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), clipId(ref.ordinal));
		inverseArgs.insert(QStringLiteral("offset"), before.value(QStringLiteral("offset")));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(before, QStringLiteral("clip.slip"), inverseArgs, true,
				ClauseClipEdgeJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerClipTrimCommands(ControlRegistry& registry)
{
	registerClipTrim(registry);
	registerClipSlip(registry);
}

} // namespace lmms
