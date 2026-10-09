/*
 * ControlCommandsClipRecord.cpp - arm and disarm audio clips for recording.
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

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "SampleClip.h"
#include "Song.h"

namespace lmms
{

using namespace control;

/*! clip.set_record arms one audio clip for the next record pass. The flag is
 *  not serialized, so the command records its before-state as an action step. */
void registerClipSetRecord(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.set_record");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("set_record");
	cmd.description = QStringLiteral("Arm (record: true) or disarm an audio clip to receive the "
		"input the next time the song plays with recording (the song editor's Record while "
		"playing): the take starts where the clip starts, is latency-compensated, and replaces the "
		"clip's audio when the transport stops. A MIDI clip is refused - notes are recorded in the "
		"piano roll. Reversible through a recorded action step (the flag is not serialized).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("record"), booleanProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("record")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("record"), booleanProperty()},
		{QStringLiteral("record_before"), booleanProperty()},
		{QStringLiteral("song_recording"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ClipRef ref;
		if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
		auto* clip = dynamic_cast<SampleClip*>(ref.clip);
		if (clip == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("%1 is a %2 clip: only an audio clip records the input; notes are "
					"recorded in the piano roll").arg(clipId(ref.id), ref.clip->nodeName()));
		}
		const bool before = clip->isRecord();
		const bool wanted = args.value(QStringLiteral("record")).toBool();
		const QString id = clipId(ref.id);
		const auto apply = [id](bool on) {
			ClipRef again;
			ControlResult ignored;
			if (!resolveClip(id, &again, &ignored)) { return; }
			if (auto* audio = dynamic_cast<SampleClip*>(again.clip)) { audio->setRecord(on); }
		};
		addUndoStep([apply, before] { apply(before); }, [apply, wanted] { apply(wanted); });
		clip->setRecord(wanted);

		QJsonObject result;
		result.insert(QStringLiteral("clip"), id);
		result.insert(QStringLiteral("record"), clip->isRecord());
		result.insert(QStringLiteral("record_before"), before);
		result.insert(QStringLiteral("song_recording"), Engine::getSong()->isRecording());
		result.insert(QStringLiteral("__transaction"), transactionPayload(
			QJsonObject{{QStringLiteral("record"), before}}, QStringLiteral("clip.set_record"),
			QJsonObject{{QStringLiteral("clip"), id}, {QStringLiteral("record"), before}}, true,
			QStringLiteral("action checkpoint: the recorded undo step restores the clip's "
				"record flag from its before-state")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace lmms
