/*
 * SessionAnnouncements.cpp - spoken launcher cell and scene state changes (0.5.0 Q050-320)
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "SessionAnnouncements.h"

#include <algorithm>

#include <QCoreApplication>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QTimer>

#include "AccessibleWidgets.h"
#include "ControlRegistry.h"
#include "SessionGridCells.h"

namespace lmms::gui
{

namespace
{

//! The scene's label: its own name, else "Scene N" (the launcher's own wording).
QString sceneLabel(const QJsonArray& scenes, int scene)
{
	const QString name = scenes.at(scene).toObject().value(QStringLiteral("name")).toString().trimmed();
	return name.isEmpty() ? QObject::tr("Scene %1").arg(scene + 1) : name;
}

} // namespace

SessionAnnouncer::SessionAnnouncer(QObject* parent) :
	QObject(parent)
{
	setObjectName(QStringLiteral("zeneSessionAnnouncer"));
}

void SessionAnnouncer::start()
{
	if (m_timer != nullptr) { return; }
	m_timer = new QTimer(this);
	m_timer->setInterval(150);
	connect(m_timer, &QTimer::timeout, this, [this] {
		const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("session.get_state"));
		if (state.ok) { observe(state.result); }
	});
	m_timer->start();
}

void SessionAnnouncer::reset()
{
	m_seeded = false;
	m_playingScene.clear();
	m_names.clear();
	m_lastMessage.clear();
}

namespace
{

QVector<QString> namesOf(const QJsonObject& state, int tracks, int scenes)
{
	QVector<QString> names(tracks * scenes);
	for (const QJsonValue& value : state.value(QStringLiteral("slots")).toArray())
	{
		const QJsonObject slot = value.toObject();
		const int track = slot.value(QStringLiteral("track")).toInt(-1);
		const int scene = slot.value(QStringLiteral("scene")).toInt(-1);
		if (track < 0 || track >= tracks || scene < 0 || scene >= scenes) { continue; }
		const QString name = slot.value(QStringLiteral("name")).toString().trimmed();
		names[track * scenes + scene] = name.isEmpty() ? QObject::tr("clip") : name;
	}
	return names;
}

QVector<int> playingOf(const QJsonObject& state, int tracks, int scenes)
{
	QVector<int> playing(tracks);
	playing.fill(-1);
	for (const QJsonValue& value : state.value(QStringLiteral("columns")).toArray())
	{
		const QJsonObject column = value.toObject();
		const int track = column.value(QStringLiteral("track")).toInt(-1);
		const int scene = column.value(QStringLiteral("scene")).toInt(-1);
		if (track < 0 || track >= tracks || scene < 0 || scene >= scenes) { continue; }
		if (column.value(QStringLiteral("phase")).toString() == QLatin1String("playing")) { playing[track] = scene; }
	}
	return playing;
}

void say(QString* last, const QString& message)
{
	*last = message;
	a11y::announceStatus(QCoreApplication::instance(), message);
}

void announceStarted(QString* last, const QVector<int>& started, const QVector<int>& now, const QJsonArray& sceneStates,
	const QVector<QString>& names, int scenes)
{
	QMap<int, int> startedInScene;
	for (const int track : started) { ++startedInScene[now[track]]; }
	QSet<int> sceneAlreadySaid;
	for (auto it = startedInScene.cbegin(); it != startedInScene.cend(); ++it)
	{
		if (it.value() < 2) { continue; }
		sceneAlreadySaid.insert(it.key());
		say(last, QObject::tr("%1 playing").arg(sceneLabel(sceneStates, it.key())));
	}
	for (const int track : started)
	{
		if (sceneAlreadySaid.contains(now[track])) { continue; }
		say(last, QObject::tr("%1, scene %2: %3 playing").arg(sessiongrid::columnTitle(track)).arg(now[track] + 1)
			.arg(names[track * scenes + now[track]]));
	}
}

void announceStopped(QString* last, const QVector<int>& stopped, const QVector<int>& was, int tracks,
	const QJsonArray& sceneStates, const QVector<QString>& names, int scenes)
{
	QMap<int, int> stoppedInScene;
	for (const int track : stopped) { ++stoppedInScene[was[track]]; }
	QSet<int> sceneAlreadySaid;
	for (auto it = stoppedInScene.cbegin(); it != stoppedInScene.cend(); ++it)
	{
		int oldCount = 0;
		for (int track = 0; track < tracks; ++track) { oldCount += was[track] == it.key() ? 1 : 0; }
		if (oldCount <= 1 || it.value() != oldCount) { continue; }
		sceneAlreadySaid.insert(it.key());
		say(last, QObject::tr("%1 stopped").arg(sceneLabel(sceneStates, it.key())));
	}
	for (const int track : stopped)
	{
		const int scene = was[track];
		if (sceneAlreadySaid.contains(scene)) { continue; }
		say(last, QObject::tr("%1, scene %2: %3 stopped").arg(sessiongrid::columnTitle(track)).arg(scene + 1)
			.arg(names[track * scenes + scene]));
	}
}

} // namespace

void SessionAnnouncer::observe(const QJsonObject& state)
{
	const QJsonObject grid = state.value(QStringLiteral("grid")).toObject();
	const int tracks = grid.value(QStringLiteral("tracks")).toInt();
	const int scenes = grid.value(QStringLiteral("scenes")).toInt();
	if (tracks != m_tracks || scenes != m_scenes)
	{
		m_tracks = tracks;
		m_scenes = scenes;
		m_playingScene.fill(-1, tracks);
		m_names = QVector<QString>(tracks * scenes);
		m_seeded = false;
	}

	m_names = namesOf(state, tracks, scenes);
	const QVector<int> now = playingOf(state, tracks, scenes);
	if (!m_seeded)
	{
		m_playingScene = now;
		m_seeded = true;
		return;
	}

	QVector<int> started;
	QVector<int> stopped;
	for (int track = 0; track < tracks; ++track)
	{
		if (now[track] >= 0 && now[track] != m_playingScene[track]) { started << track; }
		if (now[track] < 0 && m_playingScene[track] >= 0) { stopped << track; }
	}
	const QJsonArray sceneStates = state.value(QStringLiteral("scenes")).toArray();
	announceStarted(&m_lastMessage, started, now, sceneStates, m_names, scenes);
	announceStopped(&m_lastMessage, stopped, m_playingScene, tracks, sceneStates, m_names, scenes);
	m_playingScene = now;
}

} // namespace lmms::gui
