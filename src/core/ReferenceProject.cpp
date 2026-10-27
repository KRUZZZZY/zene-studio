/*
 * ReferenceProject.cpp - a second project open read-only beside the session (WF-042)
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

#include "ReferenceProject.h"

#include <QFileInfo>
#include <QJsonArray>

#include "ControlEdit.h"
#include "ControlProjectTransplant.h"
#include "DataFile.h"
#include "Track.h"

namespace lmms
{

namespace
{

ReferenceProject::ChangeHook s_hook = nullptr;

void announce()
{
	if (s_hook != nullptr) { s_hook(); }
}

bool isClip(const QString& tag)
{
	static const QStringList tags{QStringLiteral("midiclip"), QStringLiteral("sampleclip"), QStringLiteral("automationclip"),
		QStringLiteral("patternclip")};
	return tags.contains(tag);
}

QJsonArray devicesJson(const QDomElement& owner)
{
	QJsonArray out;
	const QList<QDomElement> effects = ReferenceProject::effectsOf(owner);
	for (int i = 0; i < effects.size(); ++i)
	{
		out.append(QJsonObject{{QStringLiteral("index"), i}, {QStringLiteral("plugin"), effects[i].attribute(QStringLiteral("name"))}});
	}
	return out;
}

QJsonArray clipsJson(const QDomElement& track)
{
	QJsonArray out;
	const QList<QDomElement> clips = ReferenceProject::clipsOf(track);
	for (int i = 0; i < clips.size(); ++i)
	{
		out.append(QJsonObject{{QStringLiteral("index"), i}, {QStringLiteral("kind"), clips[i].tagName()},
			{QStringLiteral("name"), clips[i].attribute(QStringLiteral("name"))},
			{QStringLiteral("position"), clips[i].attribute(QStringLiteral("pos")).toInt()},
			{QStringLiteral("length"), clips[i].attribute(QStringLiteral("len")).toInt()}});
	}
	return out;
}

QJsonObject trackJson(const QDomElement& track, int index)
{
	const auto type = static_cast<Track::Type>(track.attribute(QStringLiteral("type")).toInt());
	QJsonObject out{{QStringLiteral("index"), index}, {QStringLiteral("name"), track.attribute(QStringLiteral("name"))},
		{QStringLiteral("type"), control::trackTypeNameOf(type)}, {QStringLiteral("clips"), clipsJson(track)},
		{QStringLiteral("devices"), devicesJson(track)}};
	const QString instrument = track.firstChildElement().firstChildElement(QStringLiteral("instrument")).attribute(QStringLiteral("name"));
	if (!instrument.isEmpty()) { out.insert(QStringLiteral("instrument"), instrument); }
	return out;
}

} // namespace

ReferenceProject::ReferenceProject() = default;
ReferenceProject::~ReferenceProject() = default;

ReferenceProject& ReferenceProject::instance()
{
	static ReferenceProject reference;
	return reference;
}

void ReferenceProject::setChangeHook(ChangeHook hook)
{
	s_hook = hook;
}

bool ReferenceProject::open(const QString& path, QString* why)
{
	if (!QFileInfo(path).isFile())
	{
		if (why != nullptr) { *why = QStringLiteral("no project file '%1'").arg(path); }
		return false;
	}
	auto file = std::make_unique<DataFile>(path);
	if (control::transplant::songTracks(file->content()).isEmpty())
	{
		if (why != nullptr) { *why = QStringLiteral("'%1' is not a project with song tracks").arg(path); }
		return false;
	}
	m_file = std::move(file);
	m_path = QFileInfo(path).absoluteFilePath();
	m_parsedAt = QFileInfo(path).lastModified();
	announce();
	return true;
}

void ReferenceProject::close()
{
	m_file.reset();
	m_path.clear();
	announce();
}

QString ReferenceProject::sourceDir() const
{
	return QFileInfo(m_path).absolutePath();
}

bool ReferenceProject::stale() const
{
	return isOpen() && QFileInfo(m_path).lastModified() != m_parsedAt;
}

QList<QDomElement> ReferenceProject::tracks() const
{
	return isOpen() ? control::transplant::songTracks(m_file->content()) : QList<QDomElement>();
}

QDomElement ReferenceProject::mixer() const
{
	return isOpen() ? m_file->content().firstChildElement(QStringLiteral("mixer")) : QDomElement();
}

QList<QDomElement> ReferenceProject::channels() const
{
	QList<QDomElement> out;
	if (!isOpen()) { return out; }
	const QDomElement mixer = m_file->content().firstChildElement(QStringLiteral("mixer"));
	for (QDomElement channel = mixer.firstChildElement(QStringLiteral("mixerchannel")); !channel.isNull();
		channel = channel.nextSiblingElement(QStringLiteral("mixerchannel")))
	{
		out.append(channel);
	}
	return out;
}

QList<QDomElement> ReferenceProject::clipsOf(const QDomElement& track)
{
	QList<QDomElement> out;
	for (QDomElement child = track.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		if (isClip(child.tagName())) { out.append(child); }
	}
	return out;
}

QList<QDomElement> ReferenceProject::effectsOf(const QDomElement& owner)
{
	// A channel holds its chain itself; a track inside its type's element (<instrumenttrack>, <sampletrack>).
	const QDomElement holder = owner.tagName() == QLatin1String("mixerchannel") ? owner : owner.firstChildElement();
	QList<QDomElement> out;
	const QDomElement chain = holder.firstChildElement(QStringLiteral("fxchain"));
	for (QDomElement effect = chain.firstChildElement(QStringLiteral("effect")); !effect.isNull();
		effect = effect.nextSiblingElement(QStringLiteral("effect")))
	{
		out.append(effect);
	}
	return out;
}

QJsonObject ReferenceProject::listing() const
{
	if (!isOpen()) { return {{QStringLiteral("open"), false}}; }
	QJsonArray trackList;
	const QList<QDomElement> songTracks = tracks();
	for (int i = 0; i < songTracks.size(); ++i) { trackList.append(trackJson(songTracks[i], i)); }
	QJsonArray channelList;
	const QList<QDomElement> mixerChannels = channels();
	for (int i = 0; i < mixerChannels.size(); ++i)
	{
		channelList.append(QJsonObject{{QStringLiteral("index"), i},
			{QStringLiteral("name"), mixerChannels[i].attribute(QStringLiteral("name"))}, {QStringLiteral("devices"), devicesJson(mixerChannels[i])}});
	}
	return {{QStringLiteral("open"), true}, {QStringLiteral("path"), m_path}, {QStringLiteral("stale"), stale()},
		{QStringLiteral("tracks"), trackList}, {QStringLiteral("channels"), channelList}};
}

} // namespace lmms
