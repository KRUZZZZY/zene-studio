/*
 * ControlCommandsSample.cpp - the sample.* group's GENERATOR (board card #706)
 *
 * Copyright (2026) Zene Studio contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

// The destructive waveform editor's first slice, board card #706 (design:
// design/specs/DECISION-706-DESTRUCTION-MODEL.md, decided 2026-09-21). This
// TU carries the GENERATOR half: tone / white / pink / silence produced into
// a NEW SampleClip through the clip model the #611 wave left in the tree -
// Track::createClip hands the clip over, SampleClip::setSampleBuffer hands it
// its source - and never overwriting a source a clip already plays (decision
// §3.1). The four BAKERS live in ControlCommandsSampleEdit.cpp, one group
// across two translation units exactly as clip.* / warp.* split theirs.
//
// SCOPE, because it is a real limit rather than an accident: these ids are
// socket-only in this release. No widget, menu, toolbar, dock or dialog
// reaches them; placement (mixer-slot vs clip editor vs window) stays an open
// owner decision for the UI phase (docs/KNOWN-LIMITATIONS.md carries the
// same sentence).

#include <cmath>
#include <memory>
#include <vector>

#include <QJsonObject>
#include <QString>

#include "AudioEngine.h"
#include "Clip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleOperators.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

const QString ClauseTrackJournalled = QStringLiteral("ProjectJournal (Track checkpoint: "
	"Track::restoreState re-loads the track's serialized clips, so ONE control.undo (or one "
	"Ctrl+Z) removes the generated clip and the buffer inside it together; the recorded "
	"clip.delete names the manual equivalent, and applies defaults to the journal)");

//! The allocation guard this verb carries: a generated buffer may not exceed
//! this many BYTES whatever the length argument asks for. It is not a musical
//! limit - it keeps one typo'd length from requesting gigabytes - and the
//! typed refusal says exactly that.
constexpr qint64 MaxGeneratedBytes = 512ll * 1024 * 1024;

struct GenerateSpec
{
	sampleops::Kind kind;
	double frequency = 440.0;
	float amplitude = 0.8f;
	tick_t length = 0;
	f_cnt_t frames = 0;
	int sampleRate = 0;
};

QString kindName(sampleops::Kind kind)
{
	switch (kind)
	{
	case sampleops::Kind::Tone: return QStringLiteral("tone");
	case sampleops::Kind::White: return QStringLiteral("white");
	case sampleops::Kind::Pink: return QStringLiteral("pink");
	case sampleops::Kind::Silence: break;
	}
	return QStringLiteral("silence");
}

//! Parse and validate every generate argument BEFORE anything is checkpointed
//! or created, so a refused call has written nothing (SPEC A11: the engine's
//! refusal IS the typed error). Split three ways for the CCN ratchet (Gate 4):
//! each piece stays under the target rather than one parser over it.
bool parseKind(const QJsonObject& args, sampleops::Kind* kind, ControlResult* error)
{
	const QString name = args.value(QStringLiteral("kind")).toString();
	if (name == QLatin1String("tone")) { *kind = sampleops::Kind::Tone; return true; }
	if (name == QLatin1String("white")) { *kind = sampleops::Kind::White; return true; }
	if (name == QLatin1String("pink")) { *kind = sampleops::Kind::Pink; return true; }
	if (name == QLatin1String("silence")) { *kind = sampleops::Kind::Silence; return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'%1' is not a generator: kind is tone, white, pink or silence").arg(name));
	return false;
}

bool parseLevel(const QJsonObject& args, GenerateSpec* spec, ControlResult* error)
{
	spec->length = static_cast<tick_t>(args.value(QStringLiteral("length")).toDouble(0.0));
	const bool lengthOk = spec->length >= 1 && spec->length <= MaxSongLength;
	if (!lengthOk)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("length %1 ticks is outside 1..%2").arg(spec->length).arg(MaxSongLength));
		return false;
	}
	spec->amplitude = static_cast<float>(args.value(QStringLiteral("amplitude")).toDouble(0.8));
	const bool amplitudeOk = spec->amplitude >= 0.0f && spec->amplitude <= 1.0f;
	if (!amplitudeOk)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("amplitude %1 is outside the 0..1 linear range this verb takes")
				.arg(static_cast<double>(spec->amplitude)));
		return false;
	}
	return true;
}

bool sizeFrames(const QJsonObject& args, GenerateSpec* spec, ControlResult* error)
{
	spec->frequency = args.value(QStringLiteral("frequency")).toDouble(440.0);
	spec->sampleRate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	const bool tone = spec->kind == sampleops::Kind::Tone;
	const bool frequencyOk = !tone
		|| (spec->frequency > 0.0 && spec->frequency < spec->sampleRate * 0.5);
	if (!frequencyOk)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("tone frequency %1 Hz is not above 0 and below Nyquist (%2 Hz) at the "
				"engine's %3 Hz output rate").arg(spec->frequency).arg(spec->sampleRate * 0.5)
				.arg(spec->sampleRate));
		return false;
	}
	spec->frames = static_cast<f_cnt_t>(std::llround(
		static_cast<double>(spec->length) * static_cast<double>(Engine::framesPerTick())));
	const qint64 bytes = static_cast<qint64>(spec->frames) * 2 * static_cast<qint64>(sizeof(SampleFrame));
	const bool withinCap = bytes <= MaxGeneratedBytes;
	if (!withinCap)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 ticks at the engine's rate would be %2 bytes of samples, over this "
				"verb's 512 MiB generation cap - an allocation guard against a typo'd length, "
				"not a musical limit; ask for a shorter take").arg(spec->length).arg(bytes));
		return false;
	}
	return true;
}

bool parseGenerate(const QJsonObject& args, GenerateSpec* spec, ControlResult* error)
{
	if (!parseKind(args, &spec->kind, error)) { return false; }
	if (!parseLevel(args, spec, error)) { return false; }
	return sizeFrames(args, spec, error);
}

} // namespace

void registerSampleCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("sample.generate");
	cmd.group = QStringLiteral("sample");
	cmd.verb = QStringLiteral("generate");
	cmd.description = QStringLiteral("Generate a buffer (kind: tone, white noise, pink noise or "
		"silence) into a NEW audio clip on a sample track, routed through the non-destructive "
		"clip model: Track::createClip makes the clip and SampleClip::setSampleBuffer hands it "
		"the source, so an existing clip's audio is never overwritten (decision "
		"design/specs/DECISION-706-DESTRUCTION-MODEL.md). One journal checkpoint before the "
		"clip exists: one control.undo removes clip and buffer together. length is in ticks, "
		"position the clip's start; frequency (Hz) and amplitude (0..1) reach the tone and "
		"noise kinds - silence ignores them. Socket-only in this release: no interface reaches "
		"this verb (docs/KNOWN-LIMITATIONS.md).");
	cmd.argsSchema = objectSchema(
		{
			{QStringLiteral("track"), stringProperty()},
			{QStringLiteral("position"), integerProperty(0, MaxSongLength)},
			{QStringLiteral("length"), integerProperty(1, MaxSongLength)},
			{QStringLiteral("kind"), enumProperty({QStringLiteral("tone"), QStringLiteral("white"),
				QStringLiteral("pink"), QStringLiteral("silence")})},
			{QStringLiteral("frequency"), numberProperty()},
			{QStringLiteral("amplitude"), numberProperty()},
		},
		{QStringLiteral("track"), QStringLiteral("position"), QStringLiteral("length"),
			QStringLiteral("kind")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("position"), integerProperty()},
		{QStringLiteral("length"), integerProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("kind"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
		if (track == nullptr) { return error; }
		auto* sampleTrack = dynamic_cast<SampleTrack*>(track);
		if (sampleTrack == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("%1 is not a sample track: a generator hands its buffer to a new "
					"SampleClip, and only a sample track holds one - this is a %2 track")
					.arg(track->name(), trackTypeNameOf(track->type())));
		}
		GenerateSpec spec;
		if (!parseGenerate(args, &spec, &error)) { return error; }

		const tick_t position = static_cast<tick_t>(args.value(QStringLiteral("position")).toDouble(0.0));
		const QString trackArg = args.value(QStringLiteral("track")).toString();
		QJsonObject before;
		before.insert(QStringLiteral("track"), trackArg);
		before.insert(QStringLiteral("clip_count"), static_cast<double>(track->numOfClips()));

		// SPEC A16 / decision §3.2: the checkpoint is taken before the clip
		// exists, so the whole step - clip plus its buffer - is one undo.
		sampleTrack->addJournalCheckPoint();
		auto* clip = sampleTrack->createClip(TimePos(position));
		clip->changeLength(TimePos(spec.length));
		clip->setAutoResize(false); // the length was asked for, so it is explicit
		auto* generated = static_cast<SampleClip*>(clip);
		generated->setSampleBuffer(std::make_shared<SampleBuffer>(
			sampleops::generate(spec.kind, spec.frequency, spec.amplitude, spec.frames, spec.sampleRate),
			spec.sampleRate));

		QJsonObject result;
		const QString clipArg = clipId(generated->id());
		result.insert(QStringLiteral("clip"), clipArg);
		result.insert(QStringLiteral("track"), trackArg);
		result.insert(QStringLiteral("position"), static_cast<double>(position));
		result.insert(QStringLiteral("length"), static_cast<double>(spec.length));
		result.insert(QStringLiteral("frames"), static_cast<double>(spec.frames));
		result.insert(QStringLiteral("sample_rate"), spec.sampleRate);
		result.insert(QStringLiteral("kind"), kindName(spec.kind));
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), clipArg);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(before, QStringLiteral("clip.delete"), inverseArgs, true,
				ClauseTrackJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace lmms
