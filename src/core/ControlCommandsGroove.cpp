/*
 * ControlCommandsGroove.cpp - the groove.* command group (SPEC A11-A16), read
 *                              half: groove.list and groove.extract.
 *
 * THE ENGINE HALF. `GrooveTemplate` (include/GrooveTemplate.h) is a named
 * cycle of timing and velocity offsets: `extractGroove` reads the feel OUT of
 * a note list, `applyGroove` writes it back with a strength, and
 * `NoteTransform::quantizeNotes` is the grid quantise with a strength and a
 * humanise amount. The pool those templates live in is `GroovePool`
 * (include/GroovePool.h), owned by the Song and persisted as ONE
 * <groove-pool> element (docs/GROOVE-POOL.md). All of that arithmetic is
 * testable with no Engine at all, which is what
 * tests/src/core/ControlGrooveCommandsTest.cpp does with it.
 *
 * WHAT THIS FILE ADDS IS THE SURFACE. Without it a groove can only be captured
 * by editing the project file by hand, which is the gap AGENT-TOOLING.md
 * section 1 makes a defect: a feature that cannot be driven through the socket
 * is not in the release.
 *
 * The shared helpers are in ControlGrooveSupport.cpp (wire form, schemas,
 * argument readers) and the mutating verbs in ControlCommandsGrooveEdit.cpp -
 * the same split the automation, warp, rack and comp groups use.
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

#include "ControlEdit.h" // ClipRef, resolveMidiClip, clipId, trackIdOf
#include "ControlGrooveSupport.h"
#include "ControlRegistry.h"
#include "MidiClip.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace control
{

namespace
{

//! The largest tick value the schema subset can carry - the same bound the
//! warp group states, because both are sizes on the same wire (an int).
constexpr int kMaxSchemaInteger = 2147483647;

const QString kGroup = QStringLiteral("groove");

} // namespace


namespace
{

/*! The geometry a capture was asked for: the name, the slot width, and the
 *  cycle length (defaulted to four slots - one beat of sixteenth notes at this
 *  engine's own 192-tick bar - which is the shortest cycle that is a feel
 *  rather than a single shift). */
struct ExtractGeometry
{
	QString name;
	tick_t grid = 0;
	tick_t length = 0;
};

//! Reads (and proves) the geometry of one groove.extract call.
bool readExtractGeometry(const QJsonObject& args, ExtractGeometry* out, ControlResult* error)
{
	out->name = GrooveTemplate::normalisedName(args.value(QStringLiteral("name")).toString());
	if (!readTicks(args, QStringLiteral("grid"), GrooveTemplate::MinStepTicks,
		GrooveTemplate::MaxStepTicks, &out->grid, error))
	{
		return false;
	}
	out->length = 4 * out->grid;
	if (args.contains(QStringLiteral("length"))
		&& !readTicks(args, QStringLiteral("length"), out->grid,
			static_cast<tick_t>(GrooveTemplate::MaxSteps) * out->grid, &out->length, error))
	{
		return false;
	}
	if (GrooveTemplate::isWritable(out->name, out->length, out->grid)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'%1' over %2 ticks in slots of %3 is not a groove this engine "
			"holds: the name is 1..%4 characters, the slot 1..%5 ticks, and the length a "
			"whole 1..%6 slots").arg(out->name).arg(out->length).arg(out->grid)
			.arg(GrooveTemplate::MaxNameLength).arg(GrooveTemplate::MaxStepTicks)
			.arg(GrooveTemplate::MaxSteps));
	return false;
}

//! Whether the pool may take \a name: always when it is already there (a capture
//! over a name REPLACES it), otherwise only below the pool's own cap.
bool poolHasRoomFor(const GroovePool& pool, const QString& name, ControlResult* error)
{
	if (pool.find(name) != nullptr || pool.size() < GroovePool::MaxTemplates) { return true; }
	*error = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("the pool already holds its maximum of %1 grooves and no groove "
			"called '%2' to replace; remove one first")
			.arg(GroovePool::MaxTemplates).arg(name));
	return false;
}

ControlResult grooveList(const QJsonObject& args)
{
	GroovePool* pool = projectGroovePool();
	if (pool == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the song is not up yet, so there is no groove pool to read"));
	}
	QJsonObject out = groovePoolState(*pool);
	const QString name = args.value(QStringLiteral("name")).toString();
	if (!name.isEmpty())
	{
		const GrooveTemplate* groove = pool->find(GrooveTemplate::normalisedName(name));
		if (groove == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("no groove called '%1' (the pool holds %2)")
					.arg(name).arg(pool->size()));
		}
		out.insert(QStringLiteral("groove"), grooveJson(*groove, true));
	}
	return ControlResult::success(out);
}


/*! The whole extract, once the arguments have been read: capture the feel of
 *  one clip into a NAMED groove, replacing a groove of the same name.
 *
 *  The order is the SPEC A16 rule: every refusal happens BEFORE the before-state
 *  is captured and before anything is written, so a refused call leaves no undo
 *  step behind and no half-written pool.
 */
ControlResult grooveExtract(const QJsonObject& args)
{
	GroovePool* pool = projectGroovePool();
	if (pool == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the song is not up yet, so there is no groove pool to write"));
	}
	ControlResult error;
	ClipRef ref;
	MidiClip* clip = resolveGrooveClip(args, &ref, &error);
	if (clip == nullptr) { return error; }

	ExtractGeometry wanted;
	if (!readExtractGeometry(args, &wanted, &error)) { return error; }
	if (!poolHasRoomFor(*pool, wanted.name, &error)) { return error; }

	GrooveTemplate captured;
	int notesRead = 0;
	if (!extractGroove(clip->notes(), wanted.name, wanted.length, wanted.grid, &captured,
		&notesRead))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("no groove could be read from %1: it carries no note a slot can be "
				"measured from").arg(clipId(ref.ordinal)));
	}

	const QString before = pool->toXml();
	// The PREVIOUS template is captured BY VALUE before the write: the pointer
	// find() hands back is into the pool's own vector, and inserting a new
	// template can move it (the same "capture values, never a pointer" rule the
	// modulation layer's recorded steps follow).
	const GrooveTemplate* existing = pool->find(wanted.name);
	const bool hadPrevious = existing != nullptr;
	const GrooveTemplate previous = hadPrevious ? *existing : GrooveTemplate();
	bool replaced = false;
	if (!pool->set(captured, &replaced))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the pool refused '%1' (it is full or the geometry is out of "
				"range); nothing was written").arg(wanted.name));
	}
	recordGroovePoolRestore(before);

	// The recorded inverse is the REAL one: a groove that was replaced comes
	// back exactly through groove.set, and one that was created goes away
	// through groove.remove. Saying "remove" for both would be a descriptor a
	// reader could not act on.
	QJsonObject result = groovePoolState(*pool);
	result.insert(QStringLiteral("groove"), grooveJson(captured, true));
	result.insert(QStringLiteral("replaced"), replaced);
	result.insert(QStringLiteral("notes_read"), notesRead);
	result.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	result.insert(QStringLiteral("track"), trackIdOf(ref.track));
	result.insert(QStringLiteral("__transaction"), groovePoolInverse(before,
		hadPrevious ? QStringLiteral("groove.set") : QStringLiteral("groove.remove"),
		hadPrevious ? grooveWriteArgs(previous)
			: QJsonObject{{QStringLiteral("name"), wanted.name}}));
	return ControlResult::success(result);
}

} // namespace


} // namespace control

void registerGrooveCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("groove.list");
		cmd.group = kGroup;
		cmd.verb = QStringLiteral("list");
		cmd.description = QStringLiteral("The project's groove pool: every named groove with "
			"its cycle length, slot width and slot count, and - with 'name' - one groove's own "
			"steps (the tick and the velocity each slot applies, both absolute). "
			"Read-only. A groove is the timing and velocity feel of a note pattern, captured "
			"from a clip by groove.extract and written back by groove.apply.");
		cmd.argsSchema = objectSchema({{QStringLiteral("name"), stringProperty()}});
		cmd.resultSchema = grooveStateSchema({
			{QStringLiteral("groove"), grooveDetailSchema()},
		});
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject& args) { return grooveList(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("groove.extract");
		cmd.group = kGroup;
		cmd.verb = QStringLiteral("extract");
		cmd.description = QStringLiteral("Capture the feel of a MIDI clip's notes into a NAMED "
			"groove: each slot's step becomes the mean timing deviation of the notes that fell "
			"in it and their mean velocity. The name is the key - extracting over an existing "
			"name replaces that groove. One undoable step (a recorded action checkpoint: the "
			"pool is project state the Song's journal checkpoint does not carry).");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("name"), stringProperty()},
			{QStringLiteral("grid"), integerProperty(GrooveTemplate::MinStepTicks,
				static_cast<int>(GrooveTemplate::MaxStepTicks))},
			{QStringLiteral("length"), integerProperty(1, kMaxSchemaInteger)},
		}, {QStringLiteral("clip"), QStringLiteral("name"), QStringLiteral("grid")});
		cmd.resultSchema = grooveStateSchema({
			{QStringLiteral("groove"), grooveDetailSchema()},
			{QStringLiteral("replaced"), booleanProperty()},
			{QStringLiteral("notes_read"), integerProperty(0, kMaxSchemaInteger)},
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("track"), stringProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return grooveExtract(args); };
		registry.registerCommand(cmd);
	}

	registerGrooveEditCommands(registry);
	registerGroovePoolCommands(registry);
}

} // namespace lmms
