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

#include <QObject>

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

} // namespace

SceneState sceneState(int scene, int scenes, const std::vector<QString>& names,
	const std::vector<int>& playingScene, const std::vector<QString>& phases)
{
	bool filled = false;
	bool playing = false;
	for (std::size_t track = 0; track < playingScene.size() && track < phases.size(); ++track)
	{
		const std::size_t cell = track * static_cast<std::size_t>(scenes) + static_cast<std::size_t>(scene);
		filled = filled || (cell < names.size() && !names[cell].isEmpty());
		if (playingScene[track] != scene) { continue; }
		if (phases[track] == QLatin1String("launching")) { return SceneState::Queued; }
		playing = playing || phases[track] == QLatin1String("playing");
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

} // namespace lmms::gui::sessiongrid
