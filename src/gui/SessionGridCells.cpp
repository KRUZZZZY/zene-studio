/*
 * SessionGridCells.cpp - what the clip launcher's headers, cells and scene buttons say
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

#include "SessionGridCells.h"

#include <cmath>

#include <QObject>
#include <QStringList>

#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms::gui::sessiongrid
{

namespace
{

const QString& stoppedMark()
{
	static const QString mark = QStringLiteral("▷ ");  // ▷
	return mark;
}

const QString& queuedMark()
{
	static const QString mark = QStringLiteral("▷… ");  // ▷…
	return mark;
}

const QString& playingMark()
{
	static const QString mark = QStringLiteral("▶ ");  // ▶
	return mark;
}

bool sceneIsInGrid(int scene, int scenes)
{
	return scenes > 0 && scene >= 0 && scene < scenes;
}

SceneState trackSceneState(int scene, int playingScene, const QString& phase)
{
	if (playingScene != scene) { return SceneState::Empty; }
	if (phase == QLatin1String("launching")) { return SceneState::Queued; }
	return phase == QLatin1String("playing") ? SceneState::Playing : SceneState::Empty;
}

//! @a value with its sign ("+12", "-6.0"); @a decimals after the point.
QString signedText(double value, int decimals)
{
	return (value > 0.0 ? QStringLiteral("+") : QString()) + QString::number(value, 'f', decimals);
}

//! The audio-cell playback settings off their defaults, read once for the badge and the words.
struct Playback
{
	bool audio = false;
	double gainDb = 0.0;
	int transpose = 0;
	int detune = 0;
	int loopStart = 0;
	int loopLength = 0;
	bool warp = false;
	double warpBpm = 0.0;

	explicit Playback(const QJsonObject& slot) :
		audio(slot.value(QStringLiteral("type")).toString() == QLatin1String("audio")),
		gainDb(slot.value(QStringLiteral("gain_db")).toDouble()),
		transpose(slot.value(QStringLiteral("transpose")).toInt()),
		detune(slot.value(QStringLiteral("detune")).toInt()),
		loopStart(slot.value(QStringLiteral("loop_start")).toInt()),
		loopLength(slot.value(QStringLiteral("loop_length")).toInt()),
		warp(slot.value(QStringLiteral("warp")).toBool()),
		warpBpm(slot.value(QStringLiteral("warp_bpm")).toDouble())
	{
	}
	bool gained() const { return std::fabs(gainDb) >= 0.05; }  // what one decimal shows
	bool looped() const { return loopStart > 0 || loopLength > 0; }
};

} // namespace

SceneState sceneState(int scene, int scenes, const std::vector<QString>& names,
	const std::vector<int>& playingScene, const std::vector<QString>& phases)
{
	if (!sceneIsInGrid(scene, scenes)) { return SceneState::Empty; }
	bool filled = false;
	bool playing = false;
	for (std::size_t track = 0; track < playingScene.size() && track < phases.size(); ++track)
	{
		const std::size_t cell = track * static_cast<std::size_t>(scenes) + static_cast<std::size_t>(scene);
		filled = filled || (cell < names.size() && !names[cell].isEmpty());
		const SceneState trackState = trackSceneState(scene, playingScene[track], phases[track]);
		if (trackState == SceneState::Queued) { return SceneState::Queued; }
		playing = playing || trackState == SceneState::Playing;
	}
	if (playing) { return SceneState::Playing; }
	return filled ? SceneState::Stopped : SceneState::Empty;
}

QString stateText(SceneState state)
{
	switch (state)
	{
	case SceneState::Empty: return QObject::tr("empty");
	case SceneState::Queued: return QObject::tr("queued");
	case SceneState::Playing: return QObject::tr("playing");
	case SceneState::Stopped: break;
	}
	return QObject::tr("stopped");
}

QString sceneMark(SceneState state)
{
	if (state == SceneState::Queued) { return queuedMark(); }
	return state == SceneState::Playing ? playingMark() : stoppedMark();
}

QString cellMark(const QString& phase, bool filled)
{
	if (!filled) { return QString(); }
	if (phase == QLatin1String("playing")) { return playingMark(); }
	if (phase == QLatin1String("launching")) { return queuedMark(); }
	return phase == QLatin1String("stopping") ? QStringLiteral("■ ") : stoppedMark();  // ■ stopping
}

QString recordingMark(const QString& state)
{
	if (state == QLatin1String("recording") || state == QLatin1String("finishing")) { return QStringLiteral("\u25CF Rec "); }
	return state == QLatin1String("armed") ? QStringLiteral("\u25CB Rec ") : QString();
}

QString recordingWords(const QString& state)
{
	if (state == QLatin1String("recording") || state == QLatin1String("finishing")) { return QObject::tr("recording"); }
	return state == QLatin1String("armed") ? QObject::tr("armed to record") : QString();
}

QString columnTitle(int track)
{
	const Song* song = Engine::getSong();
	const bool known = song != nullptr && track >= 0 && track < static_cast<int>(song->tracks().size());
	const QString name = known ? song->tracks()[static_cast<std::size_t>(track)]->name().trimmed() : QString();
	return name.isEmpty() ? QObject::tr("Track %1").arg(track + 1) : name;
}

QString launchReplaces(const QString& name, const QString& playing)
{
	if (name.isEmpty() || playing.isEmpty()) { return QString(); }
	return QObject::tr("Launching %1 replaces %2, which this track plays now").arg(name, playing);
}

QString playbackBadge(const QJsonObject& slot)
{
	const Playback p(slot);
	if (!p.audio) { return QString(); }
	QStringList parts;
	if (p.gained()) { parts.append(signedText(p.gainDb, 1) + QStringLiteral("dB")); }
	if (p.transpose != 0) { parts.append(signedText(p.transpose, 0) + QStringLiteral("st")); }
	if (p.detune != 0) { parts.append(signedText(p.detune, 0) + QStringLiteral("ct")); }
	if (p.looped()) { parts.append(QStringLiteral("\u27F2")); }  // ⟲ a loop region
	if (p.warp) { parts.append(QStringLiteral("W") + QString::number(p.warpBpm, 'g', 6)); }
	return parts.join(QLatin1Char(' '));
}

QString playbackWords(const QJsonObject& slot)
{
	const Playback p(slot);
	if (!p.audio) { return QString(); }
	QStringList parts;
	if (p.gained()) { parts.append(QObject::tr("gain %1 dB").arg(signedText(p.gainDb, 1))); }
	if (p.transpose != 0) { parts.append(QObject::tr("transposed %1 semitones").arg(signedText(p.transpose, 0))); }
	if (p.detune != 0) { parts.append(QObject::tr("detuned %1 cents").arg(signedText(p.detune, 0))); }
	if (p.looped())
	{
		parts.append(p.loopLength > 0 ? QObject::tr("looping %1 ticks from tick %2").arg(p.loopLength).arg(p.loopStart)
			: QObject::tr("looping from tick %1 to the end").arg(p.loopStart));
	}
	if (p.warp) { parts.append(QObject::tr("warped to the song tempo from %1 BPM").arg(QString::number(p.warpBpm, 'g', 6))); }
	return parts.join(QStringLiteral(", "));
}

} // namespace lmms::gui::sessiongrid
