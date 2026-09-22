/*
 * ControlCommandsSampleEdit.cpp - the sample.* group's BAKERS (board card #706)
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

// The four BAKERS of the destructive waveform editor's first slice (board card
// #706): amplify, normalize, reverse and fade, each producing a transformed
// RESULT through the clip model - a new SampleBuffer swapped in with
// SampleClip::setSampleBuffer, never frames written in place, so a buffer a
// second clip shares is untouched (decision §2.1: no operator writes sample
// data in place, and no inverse machinery is built for one - the Clip's own
// ProjectJournal checkpoint IS the inverse, the same true-inverse set_gain
// already runs on). Socket-only in this release: no widget, menu, toolbar,
// dock or dialog reaches these ids; placement stays an open owner decision
// for the UI phase (docs/KNOWN-LIMITATIONS.md carries the same sentence).

#include <memory>
#include <vector>

#include <QJsonObject>
#include <QString>

#include "Clip.h"
#include "ClipEdits.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleOperators.h"
#include "SampleWindow.h"
#include "Track.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

const QString ClauseClipBake = QStringLiteral("ProjectJournal (Clip checkpoint: the checkpoint "
	"taken before SampleClip::setSampleBuffer captured the clip's serialized source (its src "
	"file or its data attribute), so Clip::restoreState re-loads the previous buffer - one "
	"control.undo, one step; the transaction's before-state is bounded metadata (id/frames/"
	"rate), never the samples (decision §2.1, include/ControlReversibility.h:437)");

const QString NoInverseCommand = QStringLiteral("UNIMPLEMENTED: no command re-applies a buffer "
	"by hand - the Clip checkpoint holds the previous source, which is the documented route");

struct BakeTarget
{
	ClipRef ref;
	SampleClip* clip = nullptr;
};

//! Resolve the clip, require it to BE a SampleClip, require it to have a
//! source, and require it to play its WHOLE source: this release bakes the
//! whole buffer, and SampleClip::setSampleWindow's authored window is a later
//! slice (decision §3.2 says the operator's input window; the windowed case is
//! refused rather than silently re-windowed). Everything is validated BEFORE
//! the caller checkpoints (SPEC A11: a refused call has written nothing).
bool resolveBakeTarget(const QJsonObject& args, BakeTarget* target, ControlResult* error)
{
	const QString id = args.value(QStringLiteral("clip")).toString();
	if (!resolveClip(id, &target->ref, error)) { return false; }
	auto* clip = dynamic_cast<SampleClip*>(target->ref.clip);
	if (clip == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is not a sample clip: the sample.* bakers replace a "
				"SampleClip's source with the transformed result, and this clip sits on "
				"a %2 track").arg(id, trackTypeNameOf(target->ref.track->type())));
		return false;
	}
	const auto frames = static_cast<f_cnt_t>(clip->sample().sampleSize());
	if (frames == 0)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 plays no source, so there is nothing to transform - load or "
				"generate audio into it first (sample.generate is the generator)").arg(id));
		return false;
	}
	if (!(clip->sampleWindow() == SampleWindow::full(frames)))
	{
		const SampleWindow window = clip->sampleWindow();
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 plays a trimmed source window [%2, %3), and this release's "
				"bakers transform the WHOLE source only - trim back to the full source "
				"first; a window-limited operator is a later slice").arg(id)
				.arg(window.sourceIn).arg(window.sourceOut));
		return false;
	}
	target->clip = clip;
	return true;
}

//! The bounded before-state: identity and shape only. The old SAMPLES live
//! exclusively in the journal checkpoint the caller has taken.
QJsonObject sourceState(const BakeTarget& target)
{
	QJsonObject before;
	before.insert(QStringLiteral("clip"), clipId(target.ref.id));
	before.insert(QStringLiteral("frames"),
		static_cast<double>(target.clip->sample().sampleSize()));
	before.insert(QStringLiteral("sample_rate"),
		static_cast<double>(target.clip->sample().sampleRate()));
	return before;
}

//! One checkpoint, one buffer swap, one transaction - the shared tail of every
//! baker, so the four handlers differ only in the transform they computed.
ControlResult commitBake(const BakeTarget& target, std::vector<SampleFrame>&& baked,
	QJsonObject result, const QJsonObject& before)
{
	const int rate = static_cast<int>(target.clip->sample().sampleRate());
	target.clip->addJournalCheckPoint();
	target.clip->setSampleBuffer(std::make_shared<SampleBuffer>(std::move(baked), rate));
	result.insert(QStringLiteral("frames"),
		static_cast<double>(target.clip->sample().sampleSize()));
	result.insert(QStringLiteral("sample_rate"), rate);
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(before, NoInverseCommand, QJsonObject(), true, ClauseClipBake));
	return ControlResult::success(result);
}

} // namespace

void registerSampleAmplify(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("sample.amplify");
	cmd.group = QStringLiteral("sample");
	cmd.verb = QStringLiteral("amplify");
	cmd.description = QStringLiteral("Bake a gain change into a sample clip's source: every "
		"frame multiplied by the requested dB, swapped in through SampleClip::setSampleBuffer "
		"as a transformed COPY - the previous buffer is never modified, so a second clip "
		"sharing it keeps its audio, and one control.undo re-loads the previous source. No "
		"limiter is applied (the float pipeline keeps values past 0 dBFS); sample.normalize "
		"is the peak-targeted bake and clip.set_gain the non-baking alternative. Socket-only "
		"in this release (docs/KNOWN-LIMITATIONS.md).");
	cmd.argsSchema = objectSchema(
		{
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("gain_db"), numberProperty()},
		},
		{QStringLiteral("clip"), QStringLiteral("gain_db")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("gain_db"), numberProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		BakeTarget target;
		ControlResult error;
		if (!resolveBakeTarget(args, &target, &error)) { return error; }
		const double db = args.value(QStringLiteral("gain_db")).toDouble(0.0);
		if (db < -60.0 || db > 24.0)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("gain_db %1 is outside -60..24 dB, the range both this bake and "
					"clip.set_gain declare").arg(db));
		}
		const auto& source = target.clip->sample();
		const float gain = gainDbToLinear(static_cast<float>(db));
		std::vector<SampleFrame> baked = sampleops::amplify(
			source.data(), static_cast<f_cnt_t>(source.sampleSize()), gain);
		QJsonObject result;
		result.insert(QStringLiteral("clip"), clipId(target.ref.id));
		result.insert(QStringLiteral("gain_db"), db);
		return commitBake(target, std::move(baked), std::move(result), sourceState(target));
	};
	registry.registerCommand(cmd);
}

void registerSampleNormalize(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("sample.normalize");
	cmd.group = QStringLiteral("sample");
	cmd.verb = QStringLiteral("normalize");
	cmd.description = QStringLiteral("Bake a level change that puts the sample clip's peak at "
		"target_dbFS (default 0): the peak across both channels is measured, one scale factor "
		"is applied to every frame, and the result replaces the source through "
		"SampleClip::setSampleBuffer as a transformed COPY. A silent source is REFUSED with "
		"the measured peak - there is no scale factor to apply - and one control.undo "
		"re-loads the previous source. Socket-only in this release "
		"(docs/KNOWN-LIMITATIONS.md).");
	cmd.argsSchema = objectSchema(
		{
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("target_db"), numberProperty()},
		},
		{QStringLiteral("clip")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("target_db"), numberProperty()},
		{QStringLiteral("applied_gain_db"), numberProperty()},
		{QStringLiteral("peak_before_db"), numberProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		BakeTarget target;
		ControlResult error;
		if (!resolveBakeTarget(args, &target, &error)) { return error; }
		const double targetDb = args.value(QStringLiteral("target_db")).toDouble(0.0);
		if (targetDb < -60.0 || targetDb > 0.0)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("target_db %1 is outside -60..0 dBFS: a peak target cannot sit "
					"above full scale").arg(targetDb));
		}
		const auto& source = target.clip->sample();
		const auto frames = static_cast<f_cnt_t>(source.sampleSize());
		const float peak = sampleops::peakOf(source.data(), frames);
		if (peak <= 0.0f)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("%1 is pure silence - the measured peak is 0, so there is no "
					"level to scale; generate or amplify audio first")
					.arg(clipId(target.ref.id)));
		}
		const float scale = gainDbToLinear(static_cast<float>(targetDb)) / peak;
		std::vector<SampleFrame> baked = sampleops::amplify(source.data(), frames, scale);
		QJsonObject result;
		result.insert(QStringLiteral("clip"), clipId(target.ref.id));
		result.insert(QStringLiteral("target_db"), targetDb);
		result.insert(QStringLiteral("applied_gain_db"),
			static_cast<double>(gainLinearToDb(scale)));
		result.insert(QStringLiteral("peak_before_db"),
			static_cast<double>(gainLinearToDb(peak)));
		return commitBake(target, std::move(baked), std::move(result), sourceState(target));
	};
	registry.registerCommand(cmd);
}

void registerSampleReverse(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("sample.reverse");
	cmd.group = QStringLiteral("sample");
	cmd.verb = QStringLiteral("reverse");
	cmd.description = QStringLiteral("Bake a reversal into a sample clip's source: the frame "
		"order is reversed, both channels together, and the result replaces the source "
		"through SampleClip::setSampleBuffer as a transformed COPY - the previous buffer is "
		"never modified and one control.undo re-loads it, so the command is its own "
		"documented inverse rather than a flag to toggle. Socket-only in this release "
		"(docs/KNOWN-LIMITATIONS.md).");
	cmd.argsSchema = objectSchema(
		{{QStringLiteral("clip"), stringProperty()}},
		{QStringLiteral("clip")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		BakeTarget target;
		ControlResult error;
		if (!resolveBakeTarget(args, &target, &error)) { return error; }
		const auto& source = target.clip->sample();
		std::vector<SampleFrame> baked = sampleops::reverse(
			source.data(), static_cast<f_cnt_t>(source.sampleSize()));
		QJsonObject result;
		result.insert(QStringLiteral("clip"), clipId(target.ref.id));
		return commitBake(target, std::move(baked), std::move(result), sourceState(target));
	};
	registry.registerCommand(cmd);
}

void registerSampleFade(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("sample.fade");
	cmd.group = QStringLiteral("sample");
	cmd.verb = QStringLiteral("fade");
	cmd.description = QStringLiteral("Bake a LINEAR fade into a sample clip's source: "
		"direction=in ramps 0 -> unity across the whole source, direction=out ramps unity -> "
		"0, both channels together. The result replaces the source through "
		"SampleClip::setSampleBuffer as a transformed COPY and one control.undo re-loads the "
		"previous source. This release has no range/selection model, so a fade always spans "
		"the whole clip source and has only the linear curve; clip.set_fade remains the "
		"non-baking window fade. Socket-only (docs/KNOWN-LIMITATIONS.md).");
	cmd.argsSchema = objectSchema(
		{
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("direction"), enumProperty({QStringLiteral("in"),
				QStringLiteral("out")})},
		},
		{QStringLiteral("clip"), QStringLiteral("direction")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("direction"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		BakeTarget target;
		ControlResult error;
		if (!resolveBakeTarget(args, &target, &error)) { return error; }
		const QString direction = args.value(QStringLiteral("direction")).toString();
		if (direction != QLatin1String("in") && direction != QLatin1String("out"))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'%1' is not a fade direction: direction is 'in' or 'out'")
					.arg(direction));
		}
		const auto& source = target.clip->sample();
		std::vector<SampleFrame> baked = sampleops::fade(source.data(),
			static_cast<f_cnt_t>(source.sampleSize()), direction == QLatin1String("in"));
		QJsonObject result;
		result.insert(QStringLiteral("clip"), clipId(target.ref.id));
		result.insert(QStringLiteral("direction"), direction);
		return commitBake(target, std::move(baked), std::move(result), sourceState(target));
	};
	registry.registerCommand(cmd);
}

void registerSampleEditCommands(ControlRegistry& registry)
{
	// The group's four bakers, registered together (the clip.edits umbrella's
	// shape): one declaration in ControlRegistryGroups.h, one call site in
	// ControlRegistryRegistrations.cpp, four translation-unit-local writers.
	registerSampleAmplify(registry);
	registerSampleNormalize(registry);
	registerSampleReverse(registry);
	registerSampleFade(registry);
}

} // namespace lmms
