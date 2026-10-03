/*
 * ControlCommandsSubprojects.cpp - other projects placed in this one as rendered audio, kept linked to their files
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
/*! 0.5.0 L12 (WF-045): subprojects. project.subproject_add renders another project FILE (the CLI render
 *  child, as render.render does) and places the audio on a new track of this project, remembering the
 *  link in <subprojects>; project.subprojects lists the links (stale when the file changed after its
 *  render); project.subproject_refresh renders it again into a new file and points the track's clips at
 *  it. Editing a subproject is opening its file (project.open). A project that would contain itself -
 *  directly or through its subprojects' links, followed MaxDepth levels - is refused. The nested project
 *  is never loaded into this one: no second Song exists. Each edit is one undo step. */

#include <algorithm>
#include <memory>
#include <vector>

#include <QDateTime>
#include <QDir>
#include <QDomElement>
#include <QFileInfo>
#include <QJsonArray>

#include "ControlBounceSupport.h"
#include "ControlEdit.h"
#include "ControlExportPresetSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "DataFile.h"
#include "Engine.h"
#include "ProjectSection.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxDepth = 8;

struct Subproject
{
	int track = 0;
	QString path;
	QString audio;
	qint64 renderedMs = 0;
};

//! The links, saved as <subprojects> only when there is one (an absent element loads as none).
class SubprojectSection : public ProjectSection
{
public:
	std::vector<Subproject> entries;

	QString elementName() const override { return QStringLiteral("subprojects"); }
	bool shouldPersist() const override { return !entries.empty(); }
	void save(QDomDocument& doc, QDomElement& parent) const override
	{
		QDomElement list = doc.createElement(elementName());
		for (const Subproject& entry : entries)
		{
			QDomElement element = doc.createElement(QStringLiteral("subproject"));
			element.setAttribute(QStringLiteral("track"), entry.track);
			element.setAttribute(QStringLiteral("path"), entry.path);
			element.setAttribute(QStringLiteral("audio"), entry.audio);
			element.setAttribute(QStringLiteral("rendered"), static_cast<qlonglong>(entry.renderedMs));
			list.appendChild(element);
		}
		parent.appendChild(list);
	}
	void load(const QDomElement& element) override
	{
		entries.clear();
		for (QDomElement child = element.firstChildElement(QStringLiteral("subproject")); !child.isNull();
			child = child.nextSiblingElement(QStringLiteral("subproject")))
		{
			entries.push_back(Subproject{child.attribute(QStringLiteral("track")).toInt(), child.attribute(QStringLiteral("path")),
				child.attribute(QStringLiteral("audio")), child.attribute(QStringLiteral("rendered")).toLongLong()});
		}
	}
	void clear() override { entries.clear(); }
};

SubprojectSection& section()
{
	static SubprojectSection* links = [] {
		auto* made = new SubprojectSection();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *links;
}

bool samePath(const QString& a, const QString& b)
{
	const QString first = QFileInfo(a).canonicalFilePath();
	return !first.isEmpty() && first == QFileInfo(b).canonicalFilePath();
}

//! Whether the project at @a from links to @a target through its subprojects, within @a depth levels.
bool reaches(const QString& from, const QString& target, int depth)
{
	if (depth <= 0) { return false; }
	DataFile file(from);
	const QDomElement links = file.content().firstChildElement(QStringLiteral("subprojects"));
	for (QDomElement link = links.firstChildElement(QStringLiteral("subproject")); !link.isNull();
		link = link.nextSiblingElement(QStringLiteral("subproject")))
	{
		const QString path = link.attribute(QStringLiteral("path"));
		if (!QFileInfo(path).isFile()) { continue; }  // a dead link reaches nothing (and is never opened)
		if (samePath(path, target) || reaches(path, target, depth - 1)) { return true; }
	}
	return false;
}

//! A new file for a render of @a source: beside this project (<project>-subprojects/), else in the temp folder.
QString renderTarget(const QString& source)
{
	const QString project = Engine::getSong()->projectFileName();
	const QDir dir(project.isEmpty() ? QDir::temp().filePath(QStringLiteral("zene-subprojects"))
		: QFileInfo(project).dir().filePath(QFileInfo(project).completeBaseName() + QStringLiteral("-subprojects")));
	QDir().mkpath(dir.absolutePath());
	return dir.filePath(QStringLiteral("%1-%2.wav").arg(QFileInfo(source).completeBaseName()).arg(QDateTime::currentMSecsSinceEpoch()));
}

bool render(const QString& source, QString* audio, ControlResult* error)
{
	*audio = renderTarget(source);
	int exitCode = 0;
	if (controlRenderProjectFile(source, *audio, &exitCode)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 could not be rendered (exit %2)").arg(source).arg(exitCode));
	return false;
}

//! The project file @a args names, refused when it is missing or would contain this project.
bool sourceOf(const QJsonObject& args, QString* path, ControlResult* error)
{
	*path = QFileInfo(args.value(QStringLiteral("path")).toString()).absoluteFilePath();
	const QString suffix = QFileInfo(*path).suffix().toLower();
	if (!QFileInfo(*path).isFile() || (suffix != QLatin1String("mmp") && suffix != QLatin1String("mmpz")))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("'path' must be an existing .mmp or .mmpz project file"));
		return false;
	}
	const QString current = Engine::getSong()->projectFileName();
	if (!current.isEmpty() && (samePath(*path, current) || reaches(*path, current, MaxDepth)))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 contains this project: it cannot be its subproject").arg(*path));
		return false;
	}
	return true;
}

void removeEntry(int track)
{
	auto& entries = section().entries;
	entries.erase(std::remove_if(entries.begin(), entries.end(), [track](const Subproject& s) { return s.track == track; }), entries.end());
}

ControlResult addSubproject(const QJsonObject& args)
{
	QString path;
	QString audio;
	ControlResult error;
	if (!sourceOf(args, &path, &error) || !render(path, &audio, &error)) { return error; }
	const auto at = static_cast<tick_t>(args.value(QStringLiteral("at")).toInt(0));
	const QString name = QFileInfo(path).completeBaseName();
	auto made = std::make_shared<Subproject>(Subproject{0, path, audio, QDateTime::currentMSecsSinceEpoch()});
	const auto place = [made, name, at]() {
		Track* track = landAudioTrack(name, made->audio, at, -1);
		made->track = track != nullptr ? track->id() : 0;
		section().entries.push_back(*made);
		return track;
	};
	addUndoStep([made]() {
		removeEntry(made->track);
		ControlResult ignored;
		if (Track* track = resolveTrack(trackId(made->track), &ignored)) { removeTrack(track); }
	}, [place]() { place(); });
	Track* track = place();
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("path"), path}, {QStringLiteral("audio"), audio}});
}

QJsonObject entryJson(const Subproject& entry)
{
	const QFileInfo source(entry.path);
	return {{QStringLiteral("track"), trackId(entry.track)}, {QStringLiteral("path"), entry.path}, {QStringLiteral("audio"), entry.audio},
		{QStringLiteral("exists"), source.isFile()}, {QStringLiteral("rendered_ms"), static_cast<double>(entry.renderedMs)},
		{QStringLiteral("stale"), source.isFile() && source.lastModified().toMSecsSinceEpoch() > entry.renderedMs}};
}

ControlResult listSubprojects(const QJsonObject&)
{
	QJsonArray out;
	for (const Subproject& entry : section().entries) { out.append(entryJson(entry)); }
	return ControlResult::success({{QStringLiteral("subprojects"), out}, {QStringLiteral("count"), out.size()}});
}

void pointClips(Track* track, const QString& audio)
{
	for (Clip* clip : track->getClips())
	{
		if (auto* sample = dynamic_cast<SampleClip*>(clip)) { sample->setSampleFile(audio); }
	}
}

ControlResult refreshSubproject(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	auto entry = std::find_if(section().entries.begin(), section().entries.end(), [track](const Subproject& s) { return s.track == track->id(); });
	if (entry == section().entries.end()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("that track is not a subproject")); }
	QString audio;
	if (!render(entry->path, &audio, &error)) { return error; }
	const Subproject before = *entry;
	Subproject after = before;
	after.audio = audio;
	after.renderedMs = QDateTime::currentMSecsSinceEpoch();
	const auto apply = [](const Subproject& state) {
		for (Subproject& each : section().entries) { if (each.track == state.track) { each = state; } }
		ControlResult ignored;
		if (Track* owner = resolveTrack(trackId(state.track), &ignored)) { pointClips(owner, state.audio); }
	};
	apply(after);
	addUndoStep([apply, before]() { apply(before); }, [apply, after]() { apply(after); });
	Engine::getSong()->setModified();
	return ControlResult::success(entryJson(after));
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("project");
	cmd.verb = QLatin1String(verb);
	cmd.id = cmd.group + QLatin1Char('.') + cmd.verb;
	cmd.description = description;
	cmd.argsSchema = objectSchema(args, QJsonArray::fromStringList(required));
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerSubprojectCommands(ControlRegistry& registry)
{
	section();  // registers the project section before a project loads
	add(registry, "subproject_add", QStringLiteral("Place another project ('path', a .mmp or .mmpz) in this one: it is rendered "
		"(the CLI render, waiting for it) and its audio put on a new track at 'at' (ticks, 0 when left out), linked to the file "
		"so it can be rendered again (project.subproject_refresh); edit it by opening its file. A project that contains this "
		"one, directly or through its own subprojects, is refused. One undo step removes the track and the link."),
		{{QStringLiteral("path"), stringProperty()}, {QStringLiteral("at"), tickProperty()}}, {QStringLiteral("path")}, true, addSubproject);
	add(registry, "subprojects", QStringLiteral("This project's subprojects: each track, its project file, the audio it plays, "
		"when that was rendered and whether the file changed since ('stale')."), {}, {}, false, listSubprojects);
	add(registry, "subproject_refresh", QStringLiteral("Render subproject 'track' again from its file into a new audio file and "
		"point the track's clips at it. One undo step points them back."),
		{{QStringLiteral("track"), stringProperty()}}, {QStringLiteral("track")}, true, refreshSubproject);
}

} // namespace lmms
