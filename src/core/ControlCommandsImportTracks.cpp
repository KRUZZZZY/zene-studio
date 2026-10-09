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
 *  between two projects, so a track brings its channel by NAME (0.5.0 L09, Q050-027): it plays into the
 *  channel of that name here, or into a new one made with the source channel's hardware routing; a track
 *  whose channel is the master or unnamed plays into the master. The ids a track, its clips and
 *  its devices carried are dropped, so the imported ones get ids of their own. One undo step removes
 *  every imported track and the channels made for them.
 *  0.5.0 L12 (WF-042): project.copy_tracks / paste_tracks copy tracks between two OPEN projects - two
 *  instances, each with its own song - through the system clipboard (MIME type
 *  application/x-zene-tracks), detached the same way an import detaches them.
 *  0.5.0 F3 (WF-042): what these share - detaching, rebasing media, building tracks as one step - is
 *  exposed in ControlProjectTransplant.h for the reference project's copies (ControlCommandsReference.cpp). */

#include <memory>
#include <vector>

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QMimeData>

#include "ControlEdit.h"
#include "ControlProjectTransplant.h"
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

namespace control::transplant
{

QList<QDomElement> songTracks(const QDomElement& content)
{
	QList<QDomElement> out;
	const QDomElement container = content.firstChildElement(QStringLiteral("trackcontainer"));
	for (QDomElement track = container.firstChildElement(QStringLiteral("track")); !track.isNull();
		track = track.nextSiblingElement(QStringLiteral("track")))
	{
		out.append(track);
	}
	return out;
}

//! Drop the stable ids a copied subtree carries and point its mixer channel at the master. A track's
//! 'folder' is another track's id too, so it goes as well unless @a keepFolders (a copy, decided at
//! paste time): kept, a track from another project would join whichever folder here has that id.
void detach(QDomElement element, bool keepFolders)
{
	static const QStringList withIds{QStringLiteral("track"), QStringLiteral("effect"), QStringLiteral("midiclip"),
		QStringLiteral("sampleclip"), QStringLiteral("automationclip"), QStringLiteral("patternclip"),
		// A note id is as project-scoped as a track's: keeping one would let an id-based note
		// operation here address a note of the source project's track.
		QStringLiteral("note")};
	if (withIds.contains(element.tagName())) { element.removeAttribute(QStringLiteral("id")); }
	if (!keepFolders && element.tagName() == QLatin1String("track")) { element.removeAttribute(QStringLiteral("folder")); }
	if (element.hasAttribute(QStringLiteral("mixch"))) { element.setAttribute(QStringLiteral("mixch"), 0); }
	for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) { detach(child, keepFolders); }
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

} // namespace control::transplant

namespace
{

using transplant::detach;
using transplant::rebaseMedia;

//! The song's <track> elements of the project @a file holds, or an empty list (with @a error).
QList<QDomElement> songTracksOf(DataFile& file, ControlResult* error)
{
	const QList<QDomElement> out = transplant::songTracks(file.content());
	if (out.isEmpty()) { *error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the project has no song tracks")); }
	return out;
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

//! The documents of the chosen tracks ('tracks' indices; all when left out), media rebased from @a sourceDir, and the
//! channels they play into (described from @a sourceMixer) in @a plan.
bool chosen(const QJsonObject& args, const QList<QDomElement>& tracks, const QString& sourceDir, const QDomElement& sourceMixer,
	transplant::TrackPlan* plan, ControlResult* error)
{
	std::vector<int> indices;
	for (const QJsonValue& value : args.value(QStringLiteral("tracks")).toArray()) { indices.push_back(value.toInt(-1)); }
	if (!args.contains(QStringLiteral("tracks"))) { for (int i = 0; i < tracks.size(); ++i) { indices.push_back(i); } }
	if (indices.empty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'tracks' names no track (leave it out to import every one)"));
		return false;
	}
	transplant::ChannelNeeds needs(sourceMixer, plan);
	for (const int index : indices)
	{
		if (index < 0 || index >= tracks.size())
		{
			*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the project has no track %1").arg(index));
			return false;
		}
		QDomDocument doc;
		QDomElement copy = doc.importNode(tracks[index], true).toElement();
		plan->need.push_back(needs.noteFor(copy, QStringLiteral("mixch")));  // before detach points it at the master
		detach(copy);
		rebaseMedia(copy, sourceDir);
		doc.appendChild(copy);
		plan->documents.append(doc.toString());
	}
	return true;
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
	ControlResult imported = transplant::importTrackElements(tracks, args, QFileInfo(path).absolutePath(), QStringLiteral("imported"),
		file.content().firstChildElement(QStringLiteral("mixer")));
	if (!imported.ok) { return imported; }
	imported.result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("path"), path}}, QStringLiteral("control.undo"), {},
		true, QStringLiteral("action checkpoint: the recorded undo step removes the imported tracks and the mixer channels made for them; its redo imports them again")));
	return imported;
}

//! The attribute a copied track keeps its source channel's number in (detach() zeroes its 'mixch').
const QString SourceChannel = QStringLiteral("source-mixch");
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
	QDomElement mixer = doc.createElement(QStringLiteral("zene-mixer"));  // the channels the tracks play into, for the paste
	for (QDomElement track = root.firstChildElement(); !track.isNull(); track = track.nextSiblingElement())
	{
		transplant::noteLiveChannel(track, mixer, SourceChannel);  // before detach() points the track at the master
		detach(track, true);
		if (!project.isEmpty()) { rebaseMedia(track, QFileInfo(project).absolutePath()); }
	}
	const bool hasTracks = !root.firstChildElement().isNull();
	root.appendChild(mixer);
	const QString text = doc.toString();
	if (!hasTracks || text.size() > MaxClipboardChars)
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
	transplant::TrackPlan plan;
	transplant::ChannelNeeds needs(doc.documentElement().firstChildElement(QStringLiteral("zene-mixer")), &plan);
	// Two unsaved projects both name no file, so an empty == empty match would call every
	// pair of unsaved projects the same one - and the pasted track would keep a folder id that
	// here belongs to an unrelated folder. Only a named source can be recognised as this project.
	const QString source = doc.documentElement().attribute(QStringLiteral("project"));
	const bool sameProject = !source.isEmpty() && source == Engine::getSong()->projectFileName();
	for (QDomElement track = doc.documentElement().firstChildElement(QStringLiteral("track")); !track.isNull();
		track = track.nextSiblingElement(QStringLiteral("track")))
	{
		settleFolder(track, sameProject);
		QDomDocument one;
		QDomElement copy = one.importNode(track, true).toElement();
		plan.need.push_back(needs.noteFor(copy, SourceChannel));
		one.appendChild(copy);
		plan.documents.append(one.toString());
	}
	if (plan.documents.isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the clipboard's track data holds no tracks")); }
	const transplant::Built made = transplant::buildAsOneStep(plan);
	Engine::getSong()->setModified();
	return ControlResult::success(transplant::madeResult(QStringLiteral("pasted"), made, plan.documents.size()));
}

} // namespace

ControlResult control::transplant::importTrackElements(const QList<QDomElement>& tracks, const QJsonObject& args, const QString& sourceDir,
	const QString& key, const QDomElement& sourceMixer)
{
	TrackPlan plan;
	ControlResult error;
	if (!chosen(args, tracks, sourceDir, sourceMixer, &plan, &error)) { return error; }
	const Built made = buildAsOneStep(plan);
	Engine::getSong()->setModified();
	return ControlResult::success(madeResult(key, made, plan.documents.size()));
}

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
	paste.description = QStringLiteral("Paste the tracks project.copy_tracks put on the clipboard into this project, each keeping "
		"its mixer channel: the channel of the same name in this project, or a new one made with the source channel's name, colour, "
		"volume, pan and hardware routing (direct out, cue sends, insert send and return); 'created_channels' lists the new ones. "
		"A track on the master stays on it. Tracks get ids of their own. One undo step removes the tracks and the channels made for them.");
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
		"project - device, effects, clips and automation - each keeping its mixer channel: the channel of the "
		"same name in this project, or a new one made with the source channel's name, colour, volume, pan and hardware routing "
		"(direct out, cue sends, insert send and return); 'created_channels' lists the new ones. A track on the master stays on it. "
		"One undo step removes the tracks and the channels made for them.");
	cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()}, {QStringLiteral("tracks"), arrayProperty()},
		{QStringLiteral("list"), booleanProperty()}}, {QStringLiteral("path")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = importTracks;
	registry.registerCommand(cmd);
}

} // namespace lmms
