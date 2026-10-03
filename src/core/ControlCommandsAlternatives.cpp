/*
 * ControlCommandsAlternatives.cpp - named alternatives of one project, saved beside it and switched between
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
/*! 0.5.0 L12 (WF-032): project alternatives - other takes on one song (a remix, a radio edit) kept with
 *  it and switched between, without managing loose files by hand. project.alternative_save keeps the
 *  session as it is now under a name, in <project>-alternatives/<name>.mmp beside the main project file;
 *  <alternatives> in the project lists them (and the main file), and every file of the family carries
 *  the list, which a switch carries forward. project.alternative_switch saves the session where it
 *  belongs (unless asked not to) and opens the other one - a project.open, so it is not undoable
 *  (switch back instead). Saving and removing an entry are one undo step each (the list; a written file
 *  stays on disk). */

#include <algorithm>
#include <vector>

#include <QDir>
#include <QDomElement>
#include <QFileInfo>
#include <QJsonArray>

#include "ControlRegistry.h"
#include "ProjectSectionRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ProjectSection.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxAlternatives = 64;

struct Alternatives
{
	QString main;    //!< the main project file
	QString active;  //!< the alternative the session is, empty for the main file
	QStringList names;
	bool operator==(const Alternatives&) const = default;
};

class AlternativesSection : public ProjectSection
{
public:
	Alternatives state;

	QString elementName() const override { return QStringLiteral("alternatives"); }
	bool shouldPersist() const override { return !state.names.isEmpty(); }
	void save(QDomDocument& doc, QDomElement& parent) const override
	{
		QDomElement list = doc.createElement(elementName());
		list.setAttribute(QStringLiteral("main"), state.main);
		if (!state.active.isEmpty()) { list.setAttribute(QStringLiteral("active"), state.active); }
		for (const QString& name : state.names)
		{
			QDomElement element = doc.createElement(QStringLiteral("alternative"));
			element.setAttribute(QStringLiteral("name"), name);
			list.appendChild(element);
		}
		parent.appendChild(list);
	}
	void load(const QDomElement& element) override
	{
		state = Alternatives{element.attribute(QStringLiteral("main")), element.attribute(QStringLiteral("active")), {}};
		for (QDomElement child = element.firstChildElement(QStringLiteral("alternative")); !child.isNull() && state.names.size() < MaxAlternatives;
			child = child.nextSiblingElement(QStringLiteral("alternative")))
		{
			state.names << child.attribute(QStringLiteral("name"));
		}
	}
	void clear() override { state = Alternatives{}; }
};

AlternativesSection& section()
{
	static AlternativesSection* alternatives = [] {
		auto* made = new AlternativesSection();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *alternatives;
}

//! The main project file: the one the list names, else the session's own.
QString mainFile()
{
	return section().state.main.isEmpty() ? Engine::getSong()->projectFileName() : section().state.main;
}

QString fileOf(const QString& name)
{
	const QFileInfo main(mainFile());
	return main.dir().filePath(main.completeBaseName() + QStringLiteral("-alternatives/") + name + QStringLiteral(".mmp"));
}

bool plainName(const QString& name)
{
	return !name.isEmpty() && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\')) && !name.startsWith(QLatin1Char('.'))
		&& name != QLatin1String("main");
}

void record(const Alternatives& before)
{
	const Alternatives after = section().state;
	addUndoStep([before]() { section().state = before; }, [after]() { section().state = after; });
	Engine::getSong()->setModified();
}

ControlResult saveAlternative(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (!plainName(name)) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("an alternative needs a plain 'name' (not main)")); }
	if (mainFile().isEmpty()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("save the project first: alternatives are kept beside it")); }
	const Alternatives before = section().state;
	if (!before.names.contains(name) && before.names.size() >= MaxAlternatives)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the project keeps at most %1 alternatives").arg(MaxAlternatives));
	}
	section().state.main = mainFile();
	if (!section().state.names.contains(name)) { section().state.names << name; }
	const QString file = fileOf(name);
	QDir().mkpath(QFileInfo(file).absolutePath());
	if (!Engine::getSong()->saveProjectFile(file))
	{
		section().state = before;
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the alternative could not be written to %1").arg(file));
	}
	record(before);
	return ControlResult::success({{QStringLiteral("name"), name}, {QStringLiteral("file"), file}});
}

ControlResult listAlternatives(const QJsonObject&)
{
	QJsonArray out;
	for (const QString& name : section().state.names)
	{
		out.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("file"), fileOf(name)},
			{QStringLiteral("exists"), QFileInfo::exists(fileOf(name))}, {QStringLiteral("active"), name == section().state.active}});
	}
	return ControlResult::success({{QStringLiteral("alternatives"), out}, {QStringLiteral("main"), mainFile()},
		{QStringLiteral("active"), section().state.active.isEmpty() ? QStringLiteral("main") : section().state.active}});
}

//! The file alternative @a name lives in ('main' is the main file), or empty with @a error set.
QString targetOf(const QString& name, ControlResult* error)
{
	const bool toMain = name == QLatin1String("main");
	if (!toMain && !section().state.names.contains(name))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no alternative '%1'").arg(name));
		return QString();
	}
	const QString target = toMain ? mainFile() : fileOf(name);
	if (QFileInfo::exists(target)) { return target; }
	*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("%1 is missing").arg(target));
	return QString();
}

//! The session saved to its own file before it is left, unless the caller says not to.
bool savedFirst(const QJsonObject& args)
{
	Song* song = Engine::getSong();
	return !args.value(QStringLiteral("save_current")).toBool(true) || song->projectFileName().isEmpty() || song->guiSaveProject();
}

ControlResult switchAlternative(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	const Alternatives carried = section().state;
	ControlResult error;
	const QString target = targetOf(name, &error);
	if (target.isEmpty()) { return error; }
	if (!savedFirst(args))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the session could not be saved first; pass save_current false to leave it"));
	}
	ControlResult opened = ControlRegistry::instance()->invoke(QStringLiteral("project.open"), {{QStringLiteral("path"), target}});
	if (!opened.ok) { return opened; }
	const bool toMain = name == QLatin1String("main");
	section().state = carried;
	section().state.main = carried.main.isEmpty() ? target : carried.main;
	section().state.active = toMain ? QString() : name;
	opened.result.insert(QStringLiteral("alternative"), toMain ? QStringLiteral("main") : name);
	return opened;
}

ControlResult removeAlternative(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	const Alternatives before = section().state;
	if (!before.names.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no alternative '%1'").arg(name)); }
	section().state.names.removeAll(name);
	if (section().state.active == name) { section().state.active.clear(); }
	record(before);
	return ControlResult::success({{QStringLiteral("removed"), name}, {QStringLiteral("file_kept"), fileOf(name)}});
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

void projectsections::registerAlternatives()
{
	section();
}

void registerAlternativeCommands(ControlRegistry& registry)
{
	section();  // registers the project section before a project loads
	const QJsonObject named{{QStringLiteral("name"), stringProperty()}};
	const QStringList name{QStringLiteral("name")};
	add(registry, "alternative_save", QStringLiteral("Keep the session as it is now as alternative 'name' of this project: written "
		"to <project>-alternatives/<name>.mmp beside the main file and listed in the project (the project must have been "
		"saved). Saving under an existing name replaces that alternative's file. One undo step takes it off the list (the "
		"file stays)."), named, name, true, saveAlternative);
	add(registry, "alternatives", QStringLiteral("The project's alternatives (name, file, whether it exists) and which one the "
		"session is ('main' for the main file)."), {}, {}, false, listAlternatives);
	add(registry, "alternative_switch", QStringLiteral("Switch to alternative 'name' ('main' for the main file): the session is "
		"saved to its own file first (unless 'save_current' is false), then the other is opened - as project.open, so this "
		"is not undoable; switch back instead. The list of alternatives is carried across."),
		{{QStringLiteral("name"), stringProperty()}, {QStringLiteral("save_current"), booleanProperty()}}, name, true, switchAlternative);
	add(registry, "alternative_remove", QStringLiteral("Take alternative 'name' off the list (its file stays on disk). One undo step."),
		named, name, true, removeAlternative);
}

} // namespace lmms
