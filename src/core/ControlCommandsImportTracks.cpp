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
 *  every imported track.
 *  0.5.0 L12 (WF-042): project.copy_tracks / paste_tracks copy tracks between two OPEN projects - two
 *  instances, each with its own song - through the system clipboard (MIME type
 *  application/x-zene-tracks), detached the same way an import detaches them. */

#include <memory>
#include <vector>

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QMimeData>

#include "ControlEdit.h"
#include "ControlProjectAssets.h"
#include "ControlProjectAssetsShared.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "DataFile.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackFolder.h"

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

//! Drop the stable ids a copied subtree carries and point its mixer channel at the master. A track's
//! 'folder' is another track's id too, so it goes as well unless @a keepFolders (a copy, decided at
//! paste time): kept, a track from another project would join whichever folder here has that id.
void detach(QDomElement element, bool keepFolders = false)
{
	static const QStringList withIds{QStringLiteral("track"), QStringLiteral("effect"), QStringLiteral("midiclip"),
		QStringLiteral("sampleclip"), QStringLiteral("automationclip"), QStringLiteral("patternclip")};
	if (withIds.contains(element.tagName())) { element.removeAttribute(QStringLiteral("id")); }
	if (!keepFolders && element.tagName() == QLatin1String("track")) { element.removeAttribute(QStringLiteral("folder")); }
	if (element.hasAttribute(QStringLiteral("mixch"))) { element.setAttribute(QStringLiteral("mixch"), 0); }
	for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) { detach(child, keepFolders); }
}

//! A pasted track stays in its folder only when it was copied from this project and the folder is still here.
void settleFolder(QDomElement track, bool sameProject)
{
	bool ok = false;
	const int folder = track.attribute(QStringLiteral("folder")).toInt(&ok);
	if (!sameProject || !ok || dynamic_cast<TrackFolder*>(Engine::getSong()->findTrackById(folder)) == nullptr)
	{
		track.removeAttribute(QStringLiteral("folder"));
	}
}

//! Point a copied subtree's media references that resolve against the source project's folder ('local:'
//! and legacy relative paths) at their files, so they do not resolve against this project's folder instead.
void rebaseMedia(QDomElement element, const QString& sourceDir)
{
	for (const projectassets::AssetElement& spec : projectassets::kAssetElements)
	{
		if (!projectassets::elementMatches(spec, element.tagName(), QString())) { continue; }
		const QString attribute = QString::fromLatin1(spec.attribute);
		const projectassets::Resolution where = projectassets::resolveAssetPath(sourceDir, element.attribute(attribute));
		if (where.resolved && where.via != QLatin1String("absolute")) { element.setAttribute(attribute, controlProjectAssetStoredPath(where.path)); }
	}
	for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) { rebaseMedia(child, sourceDir); }
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

//! The documents of the chosen tracks ('tracks' indices; all when left out), media rebased from @a sourceDir.
bool chosen(const QJsonObject& args, const QList<QDomElement>& tracks, const QString& sourceDir, QStringList* out, ControlResult* error)
{
	std::vector<int> indices;
	for (const QJsonValue& value : args.value(QStringLiteral("tracks")).toArray()) { indices.push_back(value.toInt(-1)); }
	if (!args.contains(QStringLiteral("tracks"))) { for (int i = 0; i < tracks.size(); ++i) { indices.push_back(i); } }
	if (indices.empty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'tracks' names no track (leave it out to import every one)"));
		return false;
	}
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
		rebaseMedia(copy, sourceDir);
		doc.appendChild(copy);
		out->append(doc.toString());
	}
	return true;
}

//! The reply of an import or paste: the new tracks' ids under @a key, and how many of the @a asked
//! documents made no track (a track type this build cannot create - a newer file's).
QJsonObject madeResult(const QString& key, const std::vector<int>& made, int asked)
{
	QJsonArray ids;
	for (const int id : made) { ids.append(trackId(id)); }
	QJsonObject result{{key, ids}, {QStringLiteral("count"), ids.size()}};
	if (ids.size() < asked) { result.insert(QStringLiteral("skipped"), asked - static_cast<int>(ids.size())); }
	return result;
}

//! The tracks of @a documents, made in the song; @a made receives their stable ids.
void build(const QStringList& documents, std::vector<int>* made)
{
	made->clear();
	for (const QString& text : documents)
	{
		QDomDocument doc;
		doc.setContent(text);
		if (Track* track = Track::create(doc.documentElement(), Engine::getSong())) { made->push_back(track->id()); }
	}
}

//! build() as ONE undo step - its undo removes the tracks, its redo makes them again - answering their ids.
//! The step holds ids, never pointers: track.remove and its undo free a track and build an equal one.
std::vector<int> buildAsOneStep(const QStringList& documents)
{
	auto made = std::make_shared<std::vector<int>>();
	addUndoStep([made]() {
		for (auto it = made->rbegin(); it != made->rend(); ++it) { if (Track* live = trackWithId(*it)) { removeTrack(live); } }
		made->clear();
	}, [made, documents]() { build(documents, made.get()); });
	build(documents, made.get());
	return *made;
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
	if (!chosen(args, tracks, QFileInfo(path).absolutePath(), &documents, &error)) { return error; }
	const std::vector<int> made = buildAsOneStep(documents);
	Engine::getSong()->setModified();
	QJsonObject result = madeResult(QStringLiteral("imported"), made, documents.size());
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("path"), path}}, QStringLiteral("control.undo"), {},
		true, QStringLiteral("action checkpoint: the recorded undo step removes the imported tracks; its redo imports them again")));
	return ControlResult::success(result);
}

const QString TracksMime = QStringLiteral("application/x-zene-tracks");
//! The largest clipboard document copy_tracks writes (a track carries its clips' notes).
constexpr int MaxClipboardChars = 8 * 1024 * 1024;

QClipboard* clipboard()
{
	auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
	return app != nullptr ? QGuiApplication::clipboard() : nullptr;
}

ControlResult copyTracks(const QJsonObject& args)
{
	QClipboard* board = clipboard();
	if (board == nullptr) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("this instance has no clipboard")); }
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("zene-tracks"));
	doc.appendChild(root);
	ControlResult error;
	for (const QJsonValue& id : args.value(QStringLiteral("tracks")).toArray())
	{
		Track* track = resolveTrack(id.toString(), &error);
		if (track == nullptr) { return error; }
		track->saveState(doc, root);
	}
	const QString project = Engine::getSong()->projectFileName();
	root.setAttribute(QStringLiteral("project"), project);  // the paste keeps folders only in the same project
	for (QDomElement track = root.firstChildElement(); !track.isNull(); track = track.nextSiblingElement())
	{
		detach(track, true);
		if (!project.isEmpty()) { rebaseMedia(track, QFileInfo(project).absolutePath()); }
	}
	const QString text = doc.toString();
	if (root.firstChildElement().isNull() || text.size() > MaxClipboardChars)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("name the 'tracks' to copy (and at most %1 characters of them)")
			.arg(MaxClipboardChars));
	}
	auto* data = new QMimeData;
	data->setData(TracksMime, text.toUtf8());
	data->setText(QStringLiteral("%1 Zene Studio track(s)").arg(args.value(QStringLiteral("tracks")).toArray().size()));
	board->setMimeData(data);
	return ControlResult::success({{QStringLiteral("copied"), args.value(QStringLiteral("tracks")).toArray().size()}});
}

ControlResult pasteTracks(const QJsonObject&)
{
	QClipboard* board = clipboard();
	const QMimeData* data = board != nullptr ? board->mimeData() : nullptr;
	if (data == nullptr || !data->hasFormat(TracksMime))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the clipboard holds no tracks (project.copy_tracks puts them there)"));
	}
	QDomDocument doc;
	if (!doc.setContent(QString::fromUtf8(data->data(TracksMime))) || doc.documentElement().tagName() != QLatin1String("zene-tracks"))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("the clipboard's track data is not a project.copy_tracks document"));
	}
	QStringList documents;
	const bool sameProject = doc.documentElement().attribute(QStringLiteral("project")) == Engine::getSong()->projectFileName();
	for (QDomElement track = doc.documentElement().firstChildElement(QStringLiteral("track")); !track.isNull();
		track = track.nextSiblingElement(QStringLiteral("track")))
	{
		settleFolder(track, sameProject);
		QDomDocument one;
		one.appendChild(one.importNode(track, true));
		documents.append(one.toString());
	}
	if (documents.isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the clipboard's track data holds no tracks")); }
	const std::vector<int> made = buildAsOneStep(documents);
	Engine::getSong()->setModified();
	return ControlResult::success(madeResult(QStringLiteral("pasted"), made, documents.size()));
}

} // namespace

void registerImportTracksCommand(ControlRegistry& registry)
{
	ControlCommand copy;
	copy.id = QStringLiteral("project.copy_tracks");
	copy.group = QStringLiteral("project");
	copy.verb = QStringLiteral("copy_tracks");
	copy.description = QStringLiteral("Copy 'tracks' (trk-<n> ids) - device, effects, clips and automation - to the system "
		"clipboard, for project.paste_tracks in another open project (another instance) or in this one. Writes the clipboard, "
		"not the project.");
	copy.argsSchema = objectSchema({{QStringLiteral("tracks"), arrayProperty()}}, {QStringLiteral("tracks")});
	copy.resultSchema = objectProperty();
	copy.handler = copyTracks;
	registry.registerCommand(copy);
	ControlCommand paste;
	paste.id = QStringLiteral("project.paste_tracks");
	paste.group = QStringLiteral("project");
	paste.verb = QStringLiteral("paste_tracks");
	paste.description = QStringLiteral("Paste the tracks project.copy_tracks put on the clipboard into this project, each playing "
		"into the master (mixer channels do not carry between projects) with ids of its own. One undo step removes them.");
	paste.argsSchema = objectSchema();
	paste.resultSchema = objectProperty();
	paste.mutating = true;
	paste.handler = pasteTracks;
	registry.registerCommand(paste);

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
