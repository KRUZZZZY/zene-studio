/*
 * ControlCommandsClipProcess.cpp - clip.process: one audio clip printed through a chain's devices
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
 *
 */
/*! 0.5.0 L10 (W3) MX-083, the offline half: effects on a single clip, printed. clip.process runs one
 *  audio clip's source through copies of the devices of a chain (a track's or a channel's, or one of
 *  its rack's chains) - cloned through the project's own chain serializer onto a scratch chain, so
 *  the chain itself is not touched - and puts the result in place of the clip's source, as one undo
 *  step (Clip checkpoint). The source is resampled to the engine's rate first, the rate the devices
 *  compute at; 'tail' seconds of silence let a reverb or a delay ring out. A live chain on the clip
 *  (processed while it plays) is not offered. */

#include <algorithm>
#include <cmath>
#include <vector>

#include <QDomDocument>

#include "AudioBus.h"
#include "AudioEngine.h"
#include "ControlDevicePlacement.h"
#include "ControlDeviceSupport.h"
#include "ControlDeviceTools.h"
#include "ControlEdit.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "EffectChain.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleWindow.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr double kMaxTailSeconds = 30.0;

//! @a frames from @a from Hz to @a to Hz (linear), plus @a tail frames of silence.
std::vector<SampleFrame> prepared(const SampleFrame* frames, std::size_t count, double from, double to, std::size_t tail)
{
	const double step = from / to;
	const auto length = static_cast<std::size_t>(static_cast<double>(count) / step);
	std::vector<SampleFrame> out(length + tail);
	for (std::size_t i = 0; i < length; ++i)
	{
		const double at = static_cast<double>(i) * step;
		const auto k = std::min(static_cast<std::size_t>(at), count - 1);
		const SampleFrame next = k + 1 < count ? frames[k + 1] : SampleFrame();
		const auto t = static_cast<float>(at - static_cast<double>(k));
		out[i] = frames[k] * (1.f - t) + next * t;
	}
	return out;
}

//! @a audio through @a chain, one engine period at a time (the last one padded), in place.
void render(EffectChain& chain, std::vector<SampleFrame>& audio)
{
	const auto period = static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod());
	std::vector<SampleFrame> block(period);
	SampleFrame* data[1] = {block.data()};
	for (std::size_t start = 0; start < audio.size(); start += period)
	{
		const std::size_t n = std::min(period, audio.size() - start);
		std::fill(block.begin(), block.end(), SampleFrame());
		std::copy(audio.begin() + static_cast<std::ptrdiff_t>(start), audio.begin() + static_cast<std::ptrdiff_t>(start + n), block.begin());
		AudioBus bus{data, 1, static_cast<f_cnt_t>(period)};
		chain.processAudioBuffer(bus);
		std::copy(block.begin(), block.begin() + static_cast<std::ptrdiff_t>(n), audio.begin() + static_cast<std::ptrdiff_t>(start));
	}
}

//! A copy of @a source's devices and settings on @a scratch, through the chain's own serializer.
void cloneChain(EffectChain& source, EffectChain& scratch)
{
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("chainclone"));
	doc.appendChild(root);
	source.saveState(doc, root);
	scratch.restoreState(root.firstChildElement(source.nodeName()));
}

void extendClipLengthForTail(SampleClip& clip, std::size_t tailFrames)
{
	if (tailFrames == 0) { return; }
	const TimePos processedLength = clip.sampleLength();
	if (processedLength.getTicks() > clip.length().getTicks()) { clip.changeLength(processedLength); }
}

std::size_t sourceFrameCountForPrint(const SampleClip& clip, bool appendTail)
{
	return appendTail ? static_cast<std::size_t>(clip.sampleWindow().sourceOut) : clip.sample().sampleSize();
}

ControlResult process(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	auto* clip = dynamic_cast<SampleClip*>(ref.clip);
	if (clip == nullptr || clip->sample().data() == nullptr || clip->sample().sampleSize() == 0 || clip->sample().sampleRate() <= 0)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'clip' must be an audio clip with audio"));
	}
	ControlTarget from;
	QJsonObject named{{QStringLiteral("target"), args.value(QStringLiteral("from"))}};
	if (args.contains(QStringLiteral("chain"))) { named.insert(QStringLiteral("chain"), args.value(QStringLiteral("chain"))); }
	if (!resolveChainTarget(named, &from, &error)) { return error; }
	if (from.chain->effects().empty()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1's chain has no devices").arg(from.id)); }
	const double rate = Engine::audioEngine()->outputSampleRate();
	const double tail = std::clamp(args.value(QStringLiteral("tail")).toDouble(0.0), 0.0, kMaxTailSeconds);
	const auto tailFrames = static_cast<std::size_t>(tail * rate);
	const SampleWindow window = clip->sampleWindow();
	const auto sourceFrames = sourceFrameCountForPrint(*clip, tailFrames > 0);
	std::vector<SampleFrame> audio = prepared(clip->sample().data(), sourceFrames, clip->sample().sampleRate(), rate,
		tailFrames);
	EffectChain scratch(nullptr);
	cloneChain(*from.chain, scratch);
	render(scratch, audio);
	// The window the clip plays, in frames of the new source (the rate may have changed).
	const double scale = rate / static_cast<double>(clip->sample().sampleRate());
	const auto frames = static_cast<f_cnt_t>(audio.size());
	const auto keptIn = static_cast<f_cnt_t>(std::llround(static_cast<double>(window.sourceIn) * scale));
	const auto keptOut = tailFrames > 0 ? frames
		: static_cast<f_cnt_t>(std::llround(static_cast<double>(window.sourceOut) * scale));
	const SampleWindow kept = SampleWindow::clamped(keptIn, keptOut, frames);
	clip->addJournalCheckPoint();
	clip->replaceSample(std::make_shared<SampleBuffer>(std::move(audio), static_cast<int>(rate)), kept);
	extendClipLengthForTail(*clip, tailFrames);
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("from"), from.id},
		{QStringLiteral("devices"), static_cast<int>(scratch.effects().size())}, {QStringLiteral("frames"), static_cast<double>(frames)}});
}

} // namespace

void registerClipProcessCommand(ControlRegistry& registry)
{
	addDeviceCommand(registry, "clip.process", QStringLiteral("Print an audio clip through the devices of a chain - 'from' (trk-<n> / "
		"ch-<n>; 'chain' n for a rack's parallel chain) - with their settings: the clip's source is replaced by the processed audio "
		"(resampled to the engine's rate), the chain itself is untouched; 'tail' seconds (0..30) of silence are processed too so "
		"a reverb or a delay can ring out. One undo step (Clip checkpoint)."),
		{{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("from"), stringProperty()}, {QStringLiteral("chain"), integerProperty(0, 64)},
			{QStringLiteral("tail"), numberProperty()}},
		{QStringLiteral("clip"), QStringLiteral("from")}, true, process);
}

} // namespace lmms
