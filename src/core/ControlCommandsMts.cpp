/*
 * ControlCommandsMts.cpp - the mts.* command group (board card #712)
 *
 * THE PREFIX: `clock.*` is the MIDI clock's and a grep for `mts` returned
 * zero hits before this file - `mts.*` is free and names MTS-ESP host-wide
 * dynamic tuning. The engine half is include/SessionTuning.h (+ its three
 * TUs): ONE session-wide 128-entry table every instrument reads AT RENDER
 * when active - the missing piece; upstream microtuning (Interval/Scale/
 * Keymap, the per-instrument Microtuner) shipped already and is untouched.
 * This group is the ONLY way to reach the table: no interface surface for it
 * exists in this release (docs/KNOWN-LIMITATIONS.md).
 *
 * THE SEVEN IDS and their A16 classes (one R() row each, in
 * src/core/ControlReversibilityTableMts.cpp):
 *   mts.get_state    not_mutating - the table and the MTS-ESP status.
 *   mts.load_scale   snapshot     - parse an .scl, build + ACTIVATE;
 *                                   inverse = mts.set_tuning, the before-state.
 *   mts.load_keymap  snapshot     - parse a .kbm, rebuild content (active flag
 *                                   preserved); same inverse.
 *   mts.set_tuning   snapshot     - the whole-table writer AND the inverse
 *                                   every mutating verb records; its args are
 *                                   the stated cap: 128 finite frequencies +
 *                                   active + source <= 256 characters.
 *   mts.set_note     snapshot     - one entry (MTS-ESP per-note tuning).
 *   mts.reset        snapshot     - inactive 12-TET: the pre-#712 render path.
 *   mts.master_set   true_inverse - arm/disarm MTS-ESP publication, an action
 *                                   checkpoint the way clock's is.
 *
 * A16: every mutating verb writes the BEFORE-state into __transaction BEFORE
 * SessionTuning flips a buffer (a refusal returns before the engine call, so
 * it writes nothing), records ONE engine undo step (control::addUndoStep -
 * the stack Ctrl+Z unwinds) whose closure replays that state, and names
 * mts.set_tuning as the inverse command. control.undo therefore puts the
 * table - and, through the retune walk, the SOUND - back; proved by
 * invoke -> undo -> read-back in tests/src/core/SessionTuningTest.cpp.
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
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301 USA.
 *
 */

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Keymap.h"
#include "Scale.h"
#include "SessionTuning.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! Clock's shape: the state plus the private __transaction payload the
//! registry records and strips from the wire.
QJsonObject withTransaction(const QJsonObject& state, const QJsonObject& transaction)
{
	QJsonObject out = state;
	out.insert(QStringLiteral("__transaction"), transaction);
	return out;
}

//! mts.get_state's payload - the bounded state (the snapshot itself) plus the
//! observability around it: what the table was built from, and the MTS-ESP
//! publication status. SessionTuning::stateJson() is EXACTLY the three keys
//! mts.set_tuning's schema accepts, so it doubles as `before` and as the
//! inverse's args without a translation anywhere.
QJsonObject fullState()
{
	SessionTuning* tuning = SessionTuning::instance();
	QJsonObject state = tuning->stateJson();
	state.insert(QStringLiteral("scale"),
		tuning->scale() != nullptr ? tuning->scale()->getDescription() : QString());
	state.insert(QStringLiteral("keymap"),
		tuning->keymap() != nullptr ? tuning->keymap()->getDescription() : QString());
	state.insert(QStringLiteral("table_size"), SessionTuning::TableSize);
	const QJsonObject mts = tuning->mtsEspJson();
	state.insert(QStringLiteral("mts_library"), mts.value(QStringLiteral("library")));
	state.insert(QStringLiteral("mts_master"), mts.value(QStringLiteral("master_enabled")));
	state.insert(QStringLiteral("mts_clients"), mts.value(QStringLiteral("clients")));
	return state;
}

//! The mechanism sentence of a table mutation's snapshot row: names the cap,
//! both halves of the reversal, and the read-back.
QString tableMechanism(const QString& what)
{
	return QStringLiteral("snapshot: the table BEFORE %1 - 128 finite frequencies, the "
		"active flag and the source string (the stated cap: 128 reals + <= 256 chars), "
		"nothing else - is recorded into __transaction BEFORE any buffer flips; "
		"control.undo runs the recorded engine step that replays exactly that state "
		"(the same stack Ctrl+Z unwinds), the transaction names mts.set_tuning with the "
		"same args as the inverse command for callers that replay records, and "
		"mts.get_state reads the table back").arg(what);
}

/*! The tail shared by every mutating table verb: capture the before-state,
 *  record ONE engine undo step whose closures flip the table to `before`
 *  (undo) and to `after` (redo, so a redo is faithful), and return the state
 *  plus the transaction. \a before MUST be the state captured before the write
 *  - every caller takes it before calling the engine.
 */
ControlResult tableMutation(const QJsonObject& before, const QString& mechanism)
{
	const QJsonObject after = SessionTuning::instance()->stateJson();
	control::addUndoStep(
		[before]() {
			QString ignored;
			SessionTuning::instance()->setState(before, &ignored);
		},
		[after]() {
			QString ignored;
			SessionTuning::instance()->setState(after, &ignored);
		});
	const QJsonObject transaction = transactionPayload(before,
		QStringLiteral("mts.set_tuning"), before, true, mechanism);
	return ControlResult::success(withTransaction(
		SessionTuning::instance()->stateJson(), transaction));
}

ControlResult getState(const QJsonObject&)
{
	return ControlResult::success(fullState());
}

/*! mts.load_scale - parse an .scl, build the table from it with the current
 *  keymap, ACTIVATE it: every track - whatever its own Microtuner says - reads
 *  this table from the next period on. The file checks are here (typed kinds),
 *  the format parse in SessionTuning (the same line rules
 *  MicrotunerConfig's dialog applies), and NOTHING is written on any refusal.
 */
ControlResult loadScale(const QJsonObject& args)
{
	const QString path = args.value(QStringLiteral("path")).toString();
	if (path.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'path' is required and must not be empty (an absolute .scl path)"));
	}
	if (!QFileInfo::exists(path))
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no scale file at '%1' (a path argument must be absolute)").arg(path));
	}
	const QJsonObject before = SessionTuning::instance()->stateJson();
	QString error;
	if (!SessionTuning::instance()->loadScaleFile(path, &error))
	{
		return ControlResult::failure(ControlErrorKind::Refused, error);
	}
	return tableMutation(before, tableMechanism(QStringLiteral("an .scl was loaded")));
}

/*! mts.load_keymap - parse a .kbm and rebuild the table's content with it.
 *  Content only: the active flag is preserved (a keymap edit does not switch
 *  the session table on - only mts.load_scale / mts.set_tuning / mts.set_note
 *  activate, and mts.reset deactivates).
 */
ControlResult loadKeymap(const QJsonObject& args)
{
	const QString path = args.value(QStringLiteral("path")).toString();
	if (path.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'path' is required and must not be empty (an absolute .kbm path)"));
	}
	if (!QFileInfo::exists(path))
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no keymap file at '%1' (a path argument must be absolute)").arg(path));
	}
	const QJsonObject before = SessionTuning::instance()->stateJson();
	QString error;
	if (!SessionTuning::instance()->loadKeymapFile(path, &error))
	{
		return ControlResult::failure(ControlErrorKind::Refused, error);
	}
	return tableMutation(before, tableMechanism(QStringLiteral("a .kbm was loaded")));
}

/*! mts.set_tuning - the whole-table writer and the inverse every mutating
 *  verb records. Exactly one of the two shapes: `frequencies` (the whole
 *  128-entry snapshot - how a recorded inverse replays) or the pair
 *  `note` + `frequency` (one entry). Optional `active` / `source`; a
 *  whole-table write without them means active + "manual".
 */
ControlResult setTuning(const QJsonObject& args)
{
	const bool hasFrequencies = args.contains(QStringLiteral("frequencies"));
	const bool hasPair = args.contains(QStringLiteral("note"))
		&& args.contains(QStringLiteral("frequency"));
	if (hasFrequencies == hasPair)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("exactly one of 'frequencies' (the whole table) or the pair "
				"'note' + 'frequency' is required"));
	}

	const QJsonObject before = SessionTuning::instance()->stateJson();
	QString error;
	if (hasFrequencies)
	{
		QJsonObject state;
		state.insert(QStringLiteral("frequencies"), args.value(QStringLiteral("frequencies")));
		state.insert(QStringLiteral("active"),
			args.contains(QStringLiteral("active")) ? args.value(QStringLiteral("active")).toBool(true)
				: true);
		state.insert(QStringLiteral("source"),
			args.contains(QStringLiteral("source"))
				? args.value(QStringLiteral("source")).toString()
				: QStringLiteral("manual (mts.set_tuning)"));
		if (!SessionTuning::instance()->setState(state, &error))
		{
			return ControlResult::failure(ControlErrorKind::Refused, error);
		}
		return tableMutation(before, tableMechanism(QStringLiteral("the table was written")));
	}

	const int note = args.value(QStringLiteral("note")).toInt(-1);
	const double frequency = args.value(QStringLiteral("frequency")).toDouble(0.0);
	const QString source = args.value(QStringLiteral("source")).toString();
	if (!SessionTuning::instance()->setNoteFrequency(note, frequency, source, &error))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, error);
	}
	return tableMutation(before, tableMechanism(QStringLiteral("one note was written")));
}

/*! mts.set_note - one note's frequency, the per-note form MTS-ESP calls
 *  dynamic tuning. The same bounded inverse as everything else: the whole
 *  table's before-state, not just the one entry (so the record stays one
 *  shape for every verb).
 */
ControlResult setNote(const QJsonObject& args)
{
	const int note = args.value(QStringLiteral("note")).toInt(-1);
	const double frequency = args.value(QStringLiteral("frequency")).toDouble(0.0);
	const QString source = args.value(QStringLiteral("source")).toString();
	const QJsonObject before = SessionTuning::instance()->stateJson();
	QString error;
	if (!SessionTuning::instance()->setNoteFrequency(note, frequency, source, &error))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, error);
	}
	return tableMutation(before, tableMechanism(QStringLiteral("one note was written")));
}

//! mts.reset - 12-TET content and inactive: with the table off, render takes
//! exactly the branches it took before this card existed.
ControlResult resetTable(const QJsonObject&)
{
	const QJsonObject before = SessionTuning::instance()->stateJson();
	SessionTuning::instance()->reset();
	return tableMutation(before,
		tableMechanism(QStringLiteral("the session table was reset to inactive 12-TET")));
}

/*! mts.master_set - arm or disarm the MTS-ESP publication. Arming with no
 *  installed MTS-ESP IPC library is REFUSED, typed, naming the library -
 *  the wrapper's calls would be inert no-ops and a refusal that writes
 *  nothing is the honest answer (thirdparty/mts-esp/README.md).
 */
ControlResult masterSet(const QJsonObject& args)
{
	const bool enabled = args.value(QStringLiteral("enabled")).toBool();
	SessionTuning* tuning = SessionTuning::instance();
	const bool wasEnabled = tuning->mtsMasterEnabled();

	QString error;
	if (!tuning->setMtsMaster(enabled, &error))
	{
		return ControlResult::failure(ControlErrorKind::Refused, error);
	}
	// The flag pair is bounded, so it is ONE recorded action step - the same
	// shape transport and clock use for a mode switch.
	control::addUndoStep(
		[wasEnabled]() {
			QString ignored;
			SessionTuning::instance()->setMtsMaster(wasEnabled, &ignored);
		},
		[enabled]() {
			QString ignored;
			SessionTuning::instance()->setMtsMaster(enabled, &ignored);
		});
	QJsonObject before;
	before.insert(QStringLiteral("enabled"), wasEnabled);
	const QJsonObject transaction = transactionPayload(before,
		QStringLiteral("mts.master_set"), before, true,
		QStringLiteral("action checkpoint: the recorded undo step calls "
			"SessionTuning::setMtsMaster with the flag the before-state holds, "
			"deregistering the master this call registered (or re-registering the one "
			"it took down), and the publication state is read back in mts.get_state"));
	return ControlResult::success(withTransaction(fullState(), transaction));
}

} // namespace

void registerMtsCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mts.get_state");
		cmd.group = QStringLiteral("mts");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("The session-wide tuning table: whether it is "
			"ACTIVE (every instrument reads it at render then - that is what makes "
			"host-wide tuning host-wide), its 128 frequencies in Hz (0 = unmapped key), "
			"the source, the Scale/Keymap it was built from, and the MTS-ESP status "
			"(mts_library is 'absent' unless upstream's libMTS.so is installed - "
			"docs/KNOWN-LIMITATIONS.md). Read-only.");
		cmd.mutating = false;
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("active"), booleanProperty()},
			{QStringLiteral("source"), stringProperty()},
			{QStringLiteral("scale"), stringProperty()},
			{QStringLiteral("keymap"), stringProperty()},
			{QStringLiteral("table_size"), integerProperty(SessionTuning::TableSize,
				SessionTuning::TableSize)},
			{QStringLiteral("frequencies"), arrayProperty()},
			{QStringLiteral("mts_library"), stringProperty()},
			{QStringLiteral("mts_master"), booleanProperty()},
			{QStringLiteral("mts_clients"), integerProperty(0, 4096)},
		});
		cmd.handler = [](const QJsonObject& args) { return getState(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mts.load_scale");
		cmd.group = QStringLiteral("mts");
		cmd.verb = QStringLiteral("load_scale");
		cmd.description = QStringLiteral("Parse a Scala .scl file (the same line rules "
			"the Microtuner dialog's loader applies) and ACTIVATE the session-wide "
			"tuning table from it: from the next audio period, EVERY track reads this "
			"table at render - sounding notes included - whatever its own Microtuner "
			"says. Typed refusals (nothing written): empty or missing path, a file that "
			"is not a scale, a keymap that leaves the base key unmapped. Reversible: one "
			"control.undo restores the previous table (and the previous sound).");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()}},
			{QStringLiteral("path")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("active"), booleanProperty()},
			{QStringLiteral("source"), stringProperty()},
			{QStringLiteral("frequencies"), arrayProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return loadScale(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mts.load_keymap");
		cmd.group = QStringLiteral("mts");
		cmd.verb = QStringLiteral("load_keymap");
		cmd.description = QStringLiteral("Parse a Scala .kbm file (the fields the "
			"Microtuner dialog reads) and rebuild the session table's CONTENT with it. "
			"Content only - it does not switch an inactive table on. Typed refusals "
			"write nothing; reversible through the same bounded snapshot as mts.load_scale.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()}},
			{QStringLiteral("path")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("active"), booleanProperty()},
			{QStringLiteral("source"), stringProperty()},
			{QStringLiteral("frequencies"), arrayProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return loadKeymap(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mts.set_tuning");
		cmd.group = QStringLiteral("mts");
		cmd.verb = QStringLiteral("set_tuning");
		cmd.description = QStringLiteral("Write the session-wide table. Exactly one of: "
			"'frequencies' - all 128 entries in Hz (0 = unmapped key; this is also the "
			"inverse command every mutating mts.* verb records, over the stated cap: 128 "
			"finite numbers + active + a source of <= 256 characters), or the pair "
			"'note' (0..127) + 'frequency' (finite, > 0 Hz) for one entry. Optional "
			"'active' (default true on a whole-table write) and 'source'. Table and note "
			"writes ACTIVATE the table; mts.reset deactivates. Reversible through the "
			"same bounded snapshot.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({
			{QStringLiteral("frequencies"), arrayProperty()},
			{QStringLiteral("note"), integerProperty(0, SessionTuning::TableSize - 1)},
			{QStringLiteral("frequency"), numberProperty()},
			{QStringLiteral("active"), booleanProperty()},
			{QStringLiteral("source"), stringProperty()},
		});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("active"), booleanProperty()},
			{QStringLiteral("source"), stringProperty()},
			{QStringLiteral("frequencies"), arrayProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return setTuning(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mts.set_note");
		cmd.group = QStringLiteral("mts");
		cmd.verb = QStringLiteral("set_note");
		cmd.description = QStringLiteral("Set ONE key's frequency in the session-wide "
			"table (MTS-ESP's per-note dynamic tuning) and activate the table. "
			"'note' is 0..127, 'frequency' is finite and > 0 Hz, 'source' is an "
			"optional label (<= 256 characters). Typed refusals write nothing; "
			"reversible through the bounded whole-table snapshot like every mutator.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({
			{QStringLiteral("note"), integerProperty(0, SessionTuning::TableSize - 1)},
			{QStringLiteral("frequency"), numberProperty()},
			{QStringLiteral("source"), stringProperty()},
		}, {QStringLiteral("note"), QStringLiteral("frequency")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("active"), booleanProperty()},
			{QStringLiteral("source"), stringProperty()},
			{QStringLiteral("frequencies"), arrayProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return setNote(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mts.reset");
		cmd.group = QStringLiteral("mts");
		cmd.verb = QStringLiteral("reset");
		cmd.description = QStringLiteral("Put the session table back to 12-TET content "
			"and INACTIVE it: render then takes exactly the branches it took before "
			"board card #712. Reversible: one control.undo re-activates the previous "
			"table.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("active"), booleanProperty()},
			{QStringLiteral("source"), stringProperty()},
			{QStringLiteral("frequencies"), arrayProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return resetTable(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mts.master_set");
		cmd.group = QStringLiteral("mts");
		cmd.verb = QStringLiteral("master_set");
		cmd.description = QStringLiteral("Arm or disarm publishing the session table "
			"through MTS-ESP, so MTS-ESP CLIENT plugins loaded anywhere in this host "
			"follow Zene's tuning (thirdparty/mts-esp/, upstream ODDSound/MTS-ESP, 0BSD). "
			"Arming needs upstream's IPC library (libMTS.so at /usr/local/lib on Linux) "
			"and is a TYPED REFUSAL naming that path when it is absent - inert no-ops "
			"are not a success. 'enabled' is required. Reversible: an action checkpoint "
			"restores the flag.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({{QStringLiteral("enabled"), booleanProperty()}},
			{QStringLiteral("enabled")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("active"), booleanProperty()},
			{QStringLiteral("mts_library"), stringProperty()},
			{QStringLiteral("mts_master"), booleanProperty()},
			{QStringLiteral("mts_clients"), integerProperty(0, 4096)},
			{QStringLiteral("frequencies"), arrayProperty()},
			{QStringLiteral("keymap"), stringProperty()},
			{QStringLiteral("scale"), stringProperty()},
			{QStringLiteral("source"), stringProperty()},
			{QStringLiteral("table_size"), numberProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return masterSet(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
