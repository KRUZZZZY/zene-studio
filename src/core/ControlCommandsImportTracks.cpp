/*
 * ControlCommandsImportTracks.cpp - project.import_tracks: tracks from another project into this one
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
/*! 0.5.0 L12 (WF-036): import tracks from another project. project.import_tracks reads another
 *  project file (.mmp / .mmpz) and lists its song tracks, or copies the chosen ones - each with its
 *  device, effects, clips and automation - into this project. Mixer channel numbers mean nothing
 *  between two projects, so an imported track plays into the master; the ids a track, its clips and
 *  its devices carried are dropped, so the imported ones get ids of their own. One undo step removes
 *  every imported track. */

#include <memory>
#include <vector>

#include <QFileInfo>
#include <QJsonArray>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "DataFile.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

//! The song's <track> elements of the project at @a path, or an empty list (with @a error).
QList<QDomElement> songTracksOf(DataFile& file, ControlResult* error)
{
	QList<QDomElement> out;
	const QDomElement container = file.content().firstChildElement(QStringLiteral("trackcontainer"));
	for (QDomElement track = container.firstChildElement(QStringLiteral("track")); !track.isNull();
		track = track.nextSiblingElement(QStringLiteral("track")))
	{
		out.append(track);
	}
	if (out.isEmpty()) { *error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the project has no song tracks")); }
	return out;
}

//! Drop the stable ids a copied subtree carries and point its mixer channel at the master.
void detach(QDomElement element)
{
	static const QStringList withIds{QStringLiteral("track"), QStringLiteral("effect"), QStringLiteral("midiclip"),
		QStringLiteral("sampleclip"), QStringLiteral("automationclip"), QStringLiteral("patternclip")};
	if (withIds.contains(element.tagName())) { element.removeAttribute(QStringLiteral("id")); }
	if (element.hasAttribute(QStringLiteral("mixch"))) { element.setAttribute(QStringLiteral("mixch"), 0); }
	for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) { detach(child); }
}

QJsonArray listing(const QList<QDomElement>& tracks)
{
	QJsonArray out;
	for (int i = 0; i < tracks.size(); ++i)
	{
		out.append(QJsonObject{{QStringLiteral("index"), i}, {QStringLiteral("name"), tracks[i].attribute(QStringLiteral("name"))},
			{QStringLiteral("type"), tracks[i].attribute(QStringLiteral("type")).toInt()}});
	}
	return out;
}

//! The documents of the chosen tracks ('tracks' indices; all when left out).
bool chosen(const QJsonObject& args, const QList<QDomElement>& tracks, QStringList* out, ControlResult* error)
{
	std::vector<int> indices;
	for (const QJsonValue& value : args.value(QStringLiteral("tracks")).toArray()) { indices.push_back(value.toInt(-1)); }
	if (indices.empty()) { for (int i = 0; i < tracks.size(); ++i) { indices.push_back(i); } }
	for (const int index : indices)
	{
		if (index < 0 || index >= tracks.size())
		{
			*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the project has no track %1").arg(index));
			return false;
		}
		QDomDocument doc;
		QDomElement copy = doc.importNode(tracks[index], true).toElement();
		detach(copy);
		doc.appendChild(copy);
		out->append(doc.toString());
	}
	return true;
}

void build(const QStringList& documents, std::vector<Track*>* made)
{
	made->clear();
	for (const QString& text : documents)
	{
		QDomDocument doc;
		doc.setContent(text);
		if (Track* track = Track::create(doc.documentElement(), Engine::getSong())) { made->push_back(track); }
	}
}

ControlResult importTracks(const QJsonObject& args)
{
	const QString path = args.value(QStringLiteral("path")).toString();
	if (!QFileInfo(path).isFile()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no project file '%1'").arg(path)); }
	DataFile file(path);
	ControlResult error;
	const QList<QDomElement> tracks = songTracksOf(file, &error);
	if (tracks.isEmpty()) { return error; }
	if (args.value(QStringLiteral("list")).toBool()) { return ControlResult::success({{QStringLiteral("tracks"), listing(tracks)}}); }
	QStringList documents;
	if (!chosen(args, tracks, &documents, &error)) { return error; }
	auto made = std::make_shared<std::vector<Track*>>();
	addUndoStep([made]() { for (auto it = made->rbegin(); it != made->rend(); ++it) { removeTrack(*it); } made->clear(); },
		[made, documents]() { build(documents, made.get()); });
	build(documents, made.get());
	Engine::getSong()->setModified();
	QJsonArray ids;
	for (const Track* track : *made) { ids.append(trackIdOf(track)); }
	QJsonObject result{{QStringLiteral("imported"), ids}, {QStringLiteral("count"), ids.size()}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("path"), path}}, QStringLiteral("control.undo"), {},
		true, QStringLiteral("action checkpoint: the recorded undo step removes the imported tracks; its redo imports them again")));
	return ControlResult::success(result);
}

} // namespace

void registerImportTracksCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.import_tracks");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("import_tracks");
	cmd.description = QStringLiteral("Tracks from another project ('path', a .mmp or .mmpz): with 'list' it answers that project's "
		"song tracks (index, name, type); otherwise it copies the ones in 'tracks' (indices; every one when left out) into this "
		"project - device, effects, clips and automation - each playing into the master (mixer channels do not carry between "
		"projects). One undo step removes them.");
	cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()}, {QStringLiteral("tracks"), arrayProperty()},
		{QStringLiteral("list"), booleanProperty()}}, {QStringLiteral("path")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = importTracks;
	registry.registerCommand(cmd);
}

} // namespace lmms
