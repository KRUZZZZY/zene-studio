/*
 * ControlCommandsPunch.cpp - the punch in/out verbs of the transport.* group
 *                            (SPEC A11-A16, 0.3.0).
 *
 * The engine half is Timeline's punch region (include/Timeline.h): a tick range
 * plus an arm flag on the transport, saved with the timeline and gated by
 * Timeline::punchCapturesAt(). A punch region is the transport's, not a clip's
 * or a track's, which is why these ids keep the `transport.` prefix that
 * transport.play / transport.seek / transport.tempo_map_* already use - the
 * same decision the tempo map's half of the group made (an agent finds the
 * region where it finds the transport). AGENT-TOOLING.md lists this gap as
 * `record.punch_set`; the range lives on the timeline, so the ids land here and
 * that boarded name is superseded.
 *
 * What was missing is the AGENT SURFACE: with no commands a punch region could
 * only be authored by editing a project file's <timeline> element, which is
 * exactly the gap AGENT-TOOLING.md section 1 makes a defect.
 *
 * THE STATED LIMIT, in the same place as the claim: 0.3.0 ships the region and
 * its predicate, and NOT the audio-side gate. Nothing in this build feeds the
 * capture path a transport position (ALSA has no capture path and the two-track
 * prototype is fed by tests), so wiring a gate would be a change no test here
 * could exercise - and an unexercised gate on the audio thread is a claim, not
 * a feature. docs/KNOWN-LIMITATIONS.md carries the one line.
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
#include <QString>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "Timeline.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! Why a Timeline checkpoint is a real inverse here: the region is part of the
//! timeline's own serialized state (`<timeline>`'s punch0pos/punch1pos/
//! punchstate), and Timeline::loadSettings CLEARS it when those attributes are
//! absent - which is what lets a checkpoint taken before the first punch call
//! take the region back off.
const QString ClausePunchRegion = QStringLiteral("ProjectJournal (Timeline checkpoint: "
	"Timeline::saveState writes the region onto the <timeline> element and "
	"Timeline::loadSettings clears it when the element carries no punch attribute, "
	"so restoring the pre-punch checkpoint removes the region rather than leaving it "
	"in place)");

//! The recorded inverse of a punch call: the op and args that put the previous
//! region back.
struct PunchInverse
{
	QString op;
	QJsonObject args;
};

//! The transport a punch region belongs to: the SONG timeline, not whichever
//! play mode happens to be current.
//!
//! WHY THIS IS NOT `song->getTimeline()`. `Song::m_playMode` starts as
//! `PlayMode::None` and only becomes `PlayMode::Song` when playback starts, so
//! the no-argument accessor addresses `m_timelines[None]` in a fresh instance -
//! a DIFFERENT object. A region written there survives exactly nothing: the
//! project carries `getTimeline(PlayMode::Song)` (Song::saveProjectFile) and
//! restores that one (Song::loadProject), which is also the transport a
//! recording runs on (`Song::playAndRecord()` sets the mode to Song). A region
//! that vanishes on save would be the worst kind of punch: present until you
//! need it.
//!
//! The position the gate is evaluated at is still the one the rest of the
//! transport group reports (`transport.get_state`'s `position_ticks`), so
//! `punch_active` answers "would a capture starting where the transport is
//! report as being, be inside the region?" for the position an agent actually
//! drives with `transport.seek`.
Timeline& songTransport(Song* song)
{
	return song->getTimeline(Song::PlayMode::Song);
}

//! The transport's punch state, plus the one thing a caller cannot derive from
//! it: whether the play head is inside the region RIGHT NOW. `punch_active` is
//! Timeline::punchCapturesAt() at the current position - the gate's own answer,
//! reported so a caller can see the region take effect without an audio device.
QJsonObject punchStateJson(const Timeline& timeline, const Song* song)
{
	const tick_t position = song->getPlayPos().getTicks();
	QJsonObject result;
	result.insert(QStringLiteral("punch_begin"), static_cast<qint64>(timeline.punchBegin()));
	result.insert(QStringLiteral("punch_end"), static_cast<qint64>(timeline.punchEnd()));
	result.insert(QStringLiteral("punch_enabled"), timeline.punchEnabled());
	result.insert(QStringLiteral("punch_armed"), timeline.punchArmed());
	result.insert(QStringLiteral("position_ticks"), static_cast<qint64>(position));
	result.insert(QStringLiteral("punch_active"), timeline.punchCapturesAt(position));
	return result;
}

//! The `start`/`end` pair, validated. The returned string is empty when they are
//! usable, otherwise it is the refusal's message.
QString parsePunchRange(const QJsonObject& args, tick_t* start, tick_t* end)
{
	if (!args.value(QStringLiteral("start")).isDouble()
		|| !args.value(QStringLiteral("end")).isDouble())
	{
		return QStringLiteral("'start' and 'end' are tick positions, in integers");
	}
	*start = static_cast<tick_t>(args.value(QStringLiteral("start")).toDouble());
	*end = static_cast<tick_t>(args.value(QStringLiteral("end")).toDouble());
	if (*start < 0) { return QStringLiteral("'start' must not be negative"); }
	if (*end <= *start)
	{
		return QStringLiteral("'end' must be past 'start': an empty punch region captures "
			"nothing, so it is refused rather than armed");
	}
	if (*end > MaxSongLength) { return QStringLiteral("'end' is past the end of the timeline"); }
	return QString();
}

//! The inverse that puts a punch state back. punch_set cannot express "there was
//! no region" (an empty range is refused), so the inverse of the FIRST punch
//! call is punch_clear and every later one is punch_set with the old values.
PunchInverse punchInverseFor(const QJsonObject& before)
{
	if (before.value(QStringLiteral("punch_end")).toDouble() <= 0)
	{
		return PunchInverse{QStringLiteral("transport.punch_clear"), QJsonObject{}};
	}
	QJsonObject args;
	args.insert(QStringLiteral("start"), before.value(QStringLiteral("punch_begin")));
	args.insert(QStringLiteral("end"), before.value(QStringLiteral("punch_end")));
	args.insert(QStringLiteral("enabled"), before.value(QStringLiteral("punch_enabled")));
	return PunchInverse{QStringLiteral("transport.punch_set"), args};
}

//! The A16 result: the after-state plus the transaction the registry records.
QJsonObject withPunchTransaction(const QJsonObject& before, QJsonObject after)
{
	const PunchInverse inverse = punchInverseFor(before);
	after.insert(QStringLiteral("__transaction"),
		transactionPayload(before, inverse.op, inverse.args, true, ClausePunchRegion));
	return after;
}

// ---------------------------------------------------------------------------
// transport.punch_set
// ---------------------------------------------------------------------------
ControlResult punchSet(const QJsonObject& args)
{
	Song* song = Engine::getSong();
	Timeline& timeline = songTransport(song);

	tick_t start = 0;
	tick_t end = 0;
	const QString problem = parsePunchRange(args, &start, &end);
	if (!problem.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, problem);
	}
	// Arming is the point of the command; `enabled: false` is how a caller sets
	// a region up without arming it yet.
	const bool enabled = !args.contains(QStringLiteral("enabled"))
		|| args.value(QStringLiteral("enabled")).toBool();

	const QJsonObject before = punchStateJson(timeline, song);
	// SPEC A16: the checkpoint is taken BEFORE the write, so the refusal above
	// has already written nothing and one undo restores the pre-call region.
	timeline.addJournalCheckPoint();
	timeline.setPunchRange(start, end);
	timeline.setPunchEnabled(enabled);

	return ControlResult::success(withPunchTransaction(before, punchStateJson(timeline, song)));
}

// ---------------------------------------------------------------------------
// transport.punch_clear
// ---------------------------------------------------------------------------
ControlResult punchClear(const QJsonObject& args)
{
	Q_UNUSED(args);
	Song* song = Engine::getSong();
	Timeline& timeline = songTransport(song);
	if (!timeline.shouldPersistPunch())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("there is no punch region to clear"));
	}

	const QJsonObject before = punchStateJson(timeline, song);
	timeline.addJournalCheckPoint();
	timeline.clearPunch();

	return ControlResult::success(withPunchTransaction(before, punchStateJson(timeline, song)));
}

// ---------------------------------------------------------------------------
// registration
// ---------------------------------------------------------------------------
void registerTransportPunchSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("transport.punch_set");
	cmd.group = QStringLiteral("transport");
	cmd.verb = QStringLiteral("punch_set");
	cmd.description = QStringLiteral("Set the transport's punch region - the tick range "
		"[start, end) that recording captures inside - and arm it (pass \"enabled\": false to "
		"set the range without arming it). An empty range is refused. The region is project "
		"state: it is written with the timeline (`punch0pos`/`punch1pos`/`punchstate` on the "
		"<timeline> element, and only when it is set, so a project that never punches saves the "
		"bytes it always has) and one control.undo takes it back off through the Timeline's own "
		"checkpoint. Reversible through the ProjectJournal. The AUDIO-SIDE GATE IS NOT WIRED in "
		"0.3.0: the region and the predicate are real, and no capture path in this build "
		"consults them yet (docs/KNOWN-LIMITATIONS.md).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("start"), integerProperty()},
		{QStringLiteral("end"), integerProperty()},
		{QStringLiteral("enabled"), booleanProperty()},
	}, {QStringLiteral("start"), QStringLiteral("end")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("punch_begin"), integerProperty()},
		{QStringLiteral("punch_end"), integerProperty()},
		{QStringLiteral("punch_enabled"), booleanProperty()},
		{QStringLiteral("punch_armed"), booleanProperty()},
		{QStringLiteral("position_ticks"), integerProperty()},
		{QStringLiteral("punch_active"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return punchSet(args); };
	registry.registerCommand(cmd);
}

void registerTransportPunchClear(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("transport.punch_clear");
	cmd.group = QStringLiteral("transport");
	cmd.verb = QStringLiteral("punch_clear");
	cmd.description = QStringLiteral("Disarm the punch region and forget it, leaving the "
		"timeline in the state a project that never punched has (which is also what gets "
		"written to the project file: the punch attributes are omitted). Refused, typed, when "
		"there is no region to clear. Reversible through the ProjectJournal (Timeline "
		"checkpoint: the recorded inverse is transport.punch_set with the region that was "
		"cleared).");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("punch_begin"), integerProperty()},
		{QStringLiteral("punch_end"), integerProperty()},
		{QStringLiteral("punch_enabled"), booleanProperty()},
		{QStringLiteral("punch_armed"), booleanProperty()},
		{QStringLiteral("position_ticks"), integerProperty()},
		{QStringLiteral("punch_active"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return punchClear(args); };
	registry.registerCommand(cmd);
}

void registerTransportPunchGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("transport.punch_get_state");
	cmd.group = QStringLiteral("transport");
	cmd.verb = QStringLiteral("punch_get_state");
	cmd.description = QStringLiteral("The transport's punch region: its range, whether it is "
		"armed, and `punch_active` - Timeline::punchCapturesAt() at the current play position, "
		"i.e. whether a capture starting here would be inside the region. Seek inside and "
		"outside the range to see the gate answer both ways. Writes nothing.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("punch_begin"), integerProperty()},
		{QStringLiteral("punch_end"), integerProperty()},
		{QStringLiteral("punch_enabled"), booleanProperty()},
		{QStringLiteral("punch_armed"), booleanProperty()},
		{QStringLiteral("position_ticks"), integerProperty()},
		{QStringLiteral("punch_active"), booleanProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) {
		Song* song = Engine::getSong();
		return ControlResult::success(punchStateJson(songTransport(song), song));
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerTransportPunchCommands(ControlRegistry& registry)
{
	registerTransportPunchSet(registry);
	registerTransportPunchClear(registry);
	registerTransportPunchGetState(registry);
}

} // namespace lmms
