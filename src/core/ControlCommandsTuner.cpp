/*
 * ControlCommandsTuner.cpp - tuner.set / tuner.get_state: a pitch readout
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

/*! 0.5.0 W3 L14 (WF-147): a tuner. tuner.set chooses what it listens to - the engine's input (what a
 *  recording would take) or the master output - and the reference pitch; tuner.get_state reads the newest
 *  frames from the tap (TunerTap.h, filled on the audio thread without allocation or locks) and answers the
 *  pitch YIN finds in them (PitchTrack.h, on this thread): the frequency, the nearest note and how many
 *  cents off it is. Session state: nothing is recorded and nothing is saved. */

#include <array>
#include <cmath>
#include <vector>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "PitchTrack.h"
#include "TunerTap.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr std::size_t Window = 4096;   //!< frames read, halved before YIN
constexpr float Gate = 0.01f;          //!< below this RMS there is nothing to tune
constexpr double MinReference = 400.0;
constexpr double MaxReference = 480.0;
double g_reference = 440.0;

const QStringList& sourceNames()
{
	static const QStringList names{QStringLiteral("off"), QStringLiteral("input"), QStringLiteral("master")};
	return names;
}

QString noteName(int key)
{
	static const std::array<const char*, 12> names{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	return QStringLiteral("%1%2").arg(QLatin1String(names[static_cast<std::size_t>(((key % 12) + 12) % 12)])).arg(key / 12 - 1);
}

//! The reading of @a hz against the reference: the nearest key (MIDI numbering) and the cents off it.
QJsonObject reading(double hz)
{
	const double midi = 69.0 + 12.0 * std::log2(hz / g_reference);
	const int key = static_cast<int>(std::lround(midi));
	return {{QStringLiteral("hz"), hz}, {QStringLiteral("key"), key}, {QStringLiteral("note"), noteName(key)},
		{QStringLiteral("cents"), std::round((midi - key) * 1000.0) / 10.0}};
}

//! The pitch of @a frames at @a rate (halved first: YIN's cost falls fourfold and its lowest note an octave).
float pitchOf(const std::vector<float>& frames, int rate)
{
	std::vector<float> half(frames.size() / 2);
	for (std::size_t i = 0; i < half.size(); ++i) { half[i] = 0.5f * (frames[2 * i] + frames[2 * i + 1]); }
	return pitchtrack::pitchAt(half.data(), half.size(), 0, static_cast<int>(half.size()), rate / 2);
}

QJsonObject stateJson()
{
	return {{QStringLiteral("source"), sourceNames().value(static_cast<int>(TunerTap::instance().source()))},
		{QStringLiteral("reference_hz"), g_reference}};
}

ControlResult setTuner(const QJsonObject& args)
{
	if (args.contains(QStringLiteral("reference_hz"))) { g_reference = args.value(QStringLiteral("reference_hz")).toDouble(); }
	if (args.contains(QStringLiteral("source")))
	{
		TunerTap::instance().setSource(static_cast<TunerTap::Source>(sourceNames().indexOf(args.value(QStringLiteral("source")).toString())));
		TunerTap::instance().reset();
	}
	return ControlResult::success(stateJson());
}

ControlResult getState(const QJsonObject&)
{
	QJsonObject out = stateJson();
	std::vector<float> frames(Window);
	const bool ready = TunerTap::instance().source() != TunerTap::Source::Off && TunerTap::instance().latest(frames.data(), Window);
	double power = 0.0;
	for (const float v : frames) { power += static_cast<double>(v) * v; }
	const auto level = static_cast<float>(ready ? std::sqrt(power / Window) : 0.0);
	out.insert(QStringLiteral("level"), level);
	const float hz = ready && level >= Gate ? pitchOf(frames, static_cast<int>(Engine::audioEngine()->baseSampleRate())) : 0.f;
	out.insert(QStringLiteral("clear"), hz > 0.f);
	if (hz > 0.f)
	{
		const QJsonObject pitch = reading(hz);
		for (auto it = pitch.begin(); it != pitch.end(); ++it) { out.insert(it.key(), it.value()); }
	}
	return ControlResult::success(out);
}

} // namespace

void registerTunerCommands(ControlRegistry& registry)
{
	ControlCommand set;
	set.id = QStringLiteral("tuner.set");
	set.group = QStringLiteral("tuner");
	set.verb = QStringLiteral("set");
	set.description = QStringLiteral("What the tuner listens to - 'source': off (the default), input (the engine's input, "
		"what a recording takes) or master (the master output) - and its 'reference_hz' for A4 (400..480, 440 when never "
		"set). Session state: nothing is recorded or saved; while off the audio thread does one relaxed load.");
	set.argsSchema = objectSchema({{QStringLiteral("source"), enumProperty(sourceNames())}, {QStringLiteral("reference_hz"), numberProperty()}});
	set.resultSchema = objectProperty();
	set.handler = [](const QJsonObject& args) {
		const double reference = args.value(QStringLiteral("reference_hz")).toDouble(440.0);
		if (reference < MinReference || reference > MaxReference)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'reference_hz' is 400..480"));
		}
		return setTuner(args);
	};
	registry.registerCommand(set);

	ControlCommand get;
	get.id = QStringLiteral("tuner.get_state");
	get.group = QStringLiteral("tuner");
	get.verb = QStringLiteral("get_state");
	get.description = QStringLiteral("The tuner's reading of the newest audio from its source: 'level' (RMS), and when a "
		"pitch is clear, 'hz', the nearest 'note' (A4, C#3...) with its MIDI 'key' and how many 'cents' off it is (-50..50) "
		"against reference_hz. 'clear' is false when the source is off, silent or has no single pitch.");
	get.argsSchema = objectSchema({});
	get.resultSchema = objectProperty();
	get.handler = getState;
	registry.registerCommand(get);
}

} // namespace lmms
