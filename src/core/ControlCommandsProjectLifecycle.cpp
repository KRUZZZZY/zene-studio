/*
 * ControlCommandsProjectLifecycle.cpp - project.new / save_as_template / import / export_midi
 *                                      and transport.set_metronome
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
 */

/*! M3.2 (registry-first actions): the File menu's and the main toolbar's actions each
 *  resolve to ONE registered command, which the action's slot dispatches - "one action,
 *  one implementation" (SPEC A11/A15). These are the verbs those actions had and the
 *  registry did not: a new project, the default template, an import, a MIDI export and
 *  the metronome. The GUI keeps its dialogs (a file picker, an overwrite question); the
 *  command is what runs once the dialog has answered.
 */

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ImportFilter.h"
#include "Metronome.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

ControlResult refused(const QString& message)
{
	return ControlResult::failure(ControlErrorKind::Refused, message);
}

QString requireAbsolute(const QJsonObject& args, const QString& key, ControlResult* error)
{
	const QString path = args.value(key).toString();
	if (path.isEmpty() || !QFileInfo(path).isAbsolute())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' must be an absolute path").arg(key));
		return QString();
	}
	return path;
}

QJsonObject songState(const Song& song)
{
	return QJsonObject{{QStringLiteral("file"), song.projectFileName()},
		{QStringLiteral("track_count"), static_cast<int>(song.tracks().size())}};
}

void registerProjectNew(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.new");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("new");
	cmd.description = QStringLiteral("Replace the session with a new project: the user's default "
		"template (or the factory one), or `template` - an absolute path to an .mpt - when given. "
		"File > New and the toolbar's new-project buttons dispatch this. The previous session is "
		"gone (unsaved edits included), exactly as project.open replaces it. `file` is empty "
		"afterwards: a new project has no file until it is saved.");
	cmd.argsSchema = objectSchema({{QStringLiteral("template"), stringProperty()}});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("track_count"), integerProperty()},
		{QStringLiteral("template"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		Song* song = Engine::getSong();
		QString templ;
		if (args.contains(QStringLiteral("template")))
		{
			ControlResult error;
			templ = requireAbsolute(args, QStringLiteral("template"), &error);
			if (templ.isEmpty()) { return error; }
			if (!QFileInfo::exists(templ))
			{
				return ControlResult::failure(ControlErrorKind::NotFound,
					QStringLiteral("there is no template at '%1'").arg(templ));
			}
			song->createNewProjectFromTemplate(templ);
		}
		else
		{
			song->createNewProject();
		}
		QJsonObject result = songState(*song);
		result.insert(QStringLiteral("template"), templ);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerProjectSaveAsTemplate(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.save_as_template");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("save_as_template");
	cmd.description = QStringLiteral("Write the session as the user's DEFAULT TEMPLATE "
		"(<user template dir>/default.mpt) - what every later project.new starts from. File > "
		"Save as default template dispatches this after asking before an overwrite. The session "
		"itself is unchanged; an existing default template is REPLACED (`replaced` says so).");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("replaced"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject&) {
		const QString path = ConfigManager::inst()->userTemplateDir() + QStringLiteral("default.mpt");
		QDir().mkpath(QFileInfo(path).absolutePath());
		const bool replaced = QFileInfo::exists(path);
		// saveProjectFile() writes the document and leaves the session's own
		// file name where it was: a template is an artefact, not the project's
		// new home (guiSaveProjectAs is the call that moves the name).
		Song* song = Engine::getSong();
		if (!song->saveProjectFile(path))
		{
			return refused(QStringLiteral("the template could not be written to '%1': %2")
				.arg(path, song->saveRefusal()));
		}
		return ControlResult::success(QJsonObject{{QStringLiteral("path"), path},
			{QStringLiteral("replaced"), replaced}});
	};
	registry.registerCommand(cmd);
}

void registerProjectImport(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.import");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("import");
	cmd.description = QStringLiteral("Import a foreign file INTO the session through the import "
		"filters this build carries (a Standard MIDI File's notes, a Hydrogen song): the tracks "
		"it yields are added beside the existing ones. File > Import dispatches this. Refused, "
		"typed, when no filter takes the file - with no dialog, unlike the menu's own failure box. "
		"Not undoable: the filters write with the project journal paused.");
	cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()}}, {QStringLiteral("path")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("tracks_added"), integerProperty()},
		{QStringLiteral("track_count"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		const QString path = requireAbsolute(args, QStringLiteral("path"), &error);
		if (path.isEmpty()) { return error; }
		if (!QFileInfo::exists(path))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("there is no file at '%1'").arg(path));
		}
		Song* song = Engine::getSong();
		const int before = static_cast<int>(song->tracks().size());
		if (!ImportFilter::tryImportFile(path, song))
		{
			return refused(QStringLiteral("no import filter in this build takes '%1' (MIDI "
				"sequences and Hydrogen songs are the formats it reads)").arg(path));
		}
		song->setLoadOnLaunch(false);
		const int after = static_cast<int>(song->tracks().size());
		return ControlResult::success(QJsonObject{{QStringLiteral("path"), path},
			{QStringLiteral("tracks_added"), after - before},
			{QStringLiteral("track_count"), after}});
	};
	registry.registerCommand(cmd);
}

void registerProjectExportMidi(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.export_midi");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("export_midi");
	cmd.description = QStringLiteral("Write the session's notes as a Standard MIDI File (the "
		"midiexport filter; interchange.smf_export writes the tempo map only). File > Export MIDI "
		"dispatches this after its file dialog. A missing `.mid` suffix is appended. The session "
		"is not modified.");
	cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()}}, {QStringLiteral("path")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("bytes"), integerProperty()},
	});
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		QString path = requireAbsolute(args, QStringLiteral("path"), &error);
		if (path.isEmpty()) { return error; }
		if (!path.endsWith(QStringLiteral(".mid"))) { path += QStringLiteral(".mid"); }
		Engine::getSong()->exportProjectMidi(path);
		const QFileInfo written(path);
		if (!written.exists())
		{
			return refused(QStringLiteral("no MIDI file was written to '%1' (the midiexport "
				"filter is not loadable in this process)").arg(path));
		}
		return ControlResult::success(QJsonObject{{QStringLiteral("path"), path},
			{QStringLiteral("bytes"), static_cast<qint64>(written.size())}});
	};
	registry.registerCommand(cmd);
}

void registerTransportSetMetronome(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("transport.set_metronome");
	cmd.group = QStringLiteral("transport");
	cmd.verb = QStringLiteral("set_metronome");
	cmd.description = QStringLiteral("Switch the metronome click on or off - the main toolbar's "
		"metronome toggle dispatches this. `previous` is the state before. Monitoring state, not "
		"project state: the project file does not carry it and nothing is journalled.");
	cmd.argsSchema = objectSchema({{QStringLiteral("enabled"), booleanProperty()}}, {QStringLiteral("enabled")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("previous"), booleanProperty()},
	});
	cmd.handler = [](const QJsonObject& args) {
		Metronome& metronome = Engine::getSong()->metronome();
		const bool previous = metronome.active();
		metronome.setActive(args.value(QStringLiteral("enabled")).toBool());
		return ControlResult::success(QJsonObject{{QStringLiteral("enabled"), metronome.active()},
			{QStringLiteral("previous"), previous}});
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerProjectLifecycleCommands(ControlRegistry& registry)
{
	registerProjectNew(registry);
	registerProjectSaveAsTemplate(registry);
	registerProjectImport(registry);
	registerProjectExportMidi(registry);
	registerTransportSetMetronome(registry);
}

} // namespace lmms
