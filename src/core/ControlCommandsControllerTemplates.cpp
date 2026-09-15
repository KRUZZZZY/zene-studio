/*
 * ControlCommandsControllerTemplates.cpp - the mapping-template half of the
 *                     controller.* group (feature row 19, board task #651):
 *                     save, list, apply and delete a saved mapping template.
 *
 * Split from ControlCommandsController.cpp for the file-length ratchet: the
 * group's read/surface verbs and these four file verbs together measured 706
 * lines against the 500-line whole-tree limit, and a fork-NEW file over the
 * limit FAILS the gate (tests/file-length-gate.sh). The split is on the same
 * seam the comp.* and vca.* groups used - one translation unit per verb family
 * - and it is also the honest seam: this file touches NO controller or model,
 * only the template store, while ControlCommandsController.cpp is the half that
 * has to walk the project's live connections.
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

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "AutomatableModel.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControllerSurface.h"
// MaxSongLength (Song.h) bounds the integer properties below - the same import
// ControlCommandsMidi.cpp carries for the same reason.
#include "Song.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! A template name must be a bare file name: it becomes one entry in the user
//! preset tree, so a separator or a ".." would let a client write or delete
//! outside the template directory.
bool isUsableTemplateName(const QString& name, ControlResult* error)
{
	if (name.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("a template needs a non-empty 'name'"));
		return false;
	}
	if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
		|| name.contains(QStringLiteral("..")) || name.startsWith(QLatin1Char('.')))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("'%1' is not a usable template name: a template is one file in %2, "
				"so a name with a path separator, a \"..\" or a leading dot is refused")
				.arg(name, ControllerSurface::templateDirectory()));
		return false;
	}
	return true;
}

//! The bindings of \a loaded that this project cannot answer for, by name.
//! Asked through ControllerSurface::resolveTarget() - the same resolver
//! applyTemplate() uses - so "what it skipped" and "what it can bind" cannot
//! disagree.
QStringList unresolvedTargets(const ControllerTemplate& loaded)
{
	ControllerSurface& surface = ControllerSurface::instance();
	QStringList missing;
	for (const ControllerTemplateBinding& binding : loaded.bindings)
	{
		if (surface.resolveTarget(binding.targetName) == nullptr)
		{
			missing.append(binding.targetName);
		}
	}
	return missing;
}

void registerControllerTemplateSave(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("controller.template_save");
	cmd.group = QStringLiteral("controller");
	cmd.verb = QStringLiteral("template_save");
	cmd.description = QStringLiteral("Save the project's current MIDI bindings as a named "
		"mapping template - one JSON file in the user preset tree, listed by "
		"controller.template_list and re-applied by controller.template_apply. Each binding "
		"records the MIDI channel, the controller number, the model's fullDisplayName() and the "
		"binding's soft-takeover and feedback flags, so applying a template restores the whole "
		"surface and not just the addresses. A template is not a project: it survives closing "
		"the project and is what a user with two controllers swaps between. An existing "
		"template of the same name is overwritten. 'name' must be one file name - a path "
		"separator, a \"..\" or a leading dot is refused.");
	cmd.argsSchema = objectSchema({{QStringLiteral("name"), stringProperty()}});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("bindings"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("directory"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const QString name = args.value(QStringLiteral("name")).toString();
		ControlResult error;
		if (!isUsableTemplateName(name, &error)) { return error; }

		ControllerSurface& surface = ControllerSurface::instance();
		const int before = surface.listTemplates().size();
		const bool wasPresent = surface.listTemplates().contains(name);

		if (!surface.saveTemplate(name))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("could not write the template file for '%1' under %2")
					.arg(name, ControllerSurface::templateDirectory()));
		}

		QJsonObject result;
		result.insert(QStringLiteral("name"), name);
		result.insert(QStringLiteral("path"), surface.templatePathFor(name));
		result.insert(QStringLiteral("bindings"), surface.loadTemplate(name).bindings.size());
		result.insert(QStringLiteral("directory"), ControllerSurface::templateDirectory());
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(
				QJsonObject{{QStringLiteral("name"), name},
					{QStringLiteral("existed"), wasPresent},
					{QStringLiteral("template_count"), before}},
				QStringLiteral("controller.template_delete"),
				QJsonObject{{QStringLiteral("name"), name}},
				false,
				QStringLiteral("a template is a file outside the project "
					"(ControllerSurface::templateDirectory()); the inverse is "
					"controller.template_delete, which removes it - no ProjectJournal "
					"checkpoint can hold a file revision")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerControllerTemplateList(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("controller.template_list");
	cmd.group = QStringLiteral("controller");
	cmd.verb = QStringLiteral("template_list");
	cmd.description = QStringLiteral("Read-only: the mapping templates saved on this machine, "
		"by name, sorted, with the directory they live in and - per template - how many "
		"bindings it holds, how many of them resolve in the CURRENT project, and the names that "
		"do not. A template whose targets do not resolve still lists: it is a saved mapping set, "
		"not a claim about the project that is open.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("count"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("directory"), stringProperty()},
		{QStringLiteral("templates"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
	});
	// A13: reads the user preset tree, no display or device - swept headlessly.
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) {
		ControllerSurface& surface = ControllerSurface::instance();
		const QStringList names = surface.listTemplates();

		QJsonArray templates;
		for (const QString& name : names)
		{
			const ControllerTemplate loaded = surface.loadTemplate(name);
			const QStringList missing = unresolvedTargets(loaded);
			QJsonObject entry;
			entry.insert(QStringLiteral("name"), name);
			entry.insert(QStringLiteral("bindings"), loaded.bindings.size());
			entry.insert(QStringLiteral("resolvable"),
				loaded.bindings.size() - missing.size());
			entry.insert(QStringLiteral("missing"), QJsonArray::fromStringList(missing));
			templates.append(entry);
		}

		QJsonObject result;
		result.insert(QStringLiteral("count"), templates.size());
		result.insert(QStringLiteral("directory"), ControllerSurface::templateDirectory());
		result.insert(QStringLiteral("templates"), templates);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerControllerTemplateApply(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("controller.template_apply");
	cmd.group = QStringLiteral("controller");
	cmd.verb = QStringLiteral("template_apply");
	cmd.description = QStringLiteral("Apply a saved mapping template to this project: for every "
		"binding in it whose target model resolves, create the MIDI controller and the "
		"connection that drives that model, and restore the binding's soft-takeover and feedback "
		"flags. 'bound' counts the controls the call bound, 'skipped' counts the bindings whose "
		"target is not in this project and 'missing' names them - a template saved against "
		"another song applies the part that resolves and REPORTS the rest, rather than failing as "
		"a whole. A binding whose target is already driven by a MIDI controller is re-bound, "
		"which is what makes applying a template a way to move a surface onto a different set of "
		"addresses. This creates project state (the model's <connection> element), so the project "
		"is marked modified. controller.surface_state reports the resulting surface.");
	cmd.argsSchema = objectSchema({{QStringLiteral("name"), stringProperty()}});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("bound"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("skipped"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("missing"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const QString name = args.value(QStringLiteral("name")).toString();
		ControlResult error;
		if (!isUsableTemplateName(name, &error)) { return error; }

		ControllerSurface& surface = ControllerSurface::instance();
		if (!surface.listTemplates().contains(name))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("no template named '%1' under %2: controller.template_list "
					"reports the names that exist")
					.arg(name, ControllerSurface::templateDirectory()));
		}

		// The surface does the binding; the command only reports it.
		// ControllerSurface::applyTemplate resolves each target by
		// fullDisplayName() and skips what it cannot resolve, which is the one
		// implementation controller.template_apply, a future menu and the tests
		// all go through.
		const int created = surface.applyTemplate(name);
		const QStringList missing = unresolvedTargets(surface.loadTemplate(name));

		QJsonObject result;
		result.insert(QStringLiteral("name"), name);
		result.insert(QStringLiteral("bound"), created);
		result.insert(QStringLiteral("skipped"), missing.size());
		result.insert(QStringLiteral("missing"), QJsonArray::fromStringList(missing));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(
				QJsonObject{{QStringLiteral("name"), name},
					{QStringLiteral("bound"), created}},
				QStringLiteral("controller.template_apply"),
				QJsonObject{{QStringLiteral("name"), name}},
				false,
				QStringLiteral("the created connections are serialized through their models' "
					"own <connection> elements, which no ProjectJournal checkpoint owns: there "
					"is no inverse command the engine can replay, so the recorded state is the "
					"binding list controller.surface_state reports")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerControllerTemplateDelete(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("controller.template_delete");
	cmd.group = QStringLiteral("controller");
	cmd.verb = QStringLiteral("template_delete");
	cmd.description = QStringLiteral("Remove one saved mapping template from this machine. "
		"'removed' is false when no template of that name existed, which is reported rather than "
		"treated as an error (the file is the state, and the state asked for - no such file - "
		"already holds). No project state is touched: the project's own bindings are unaffected.");
	cmd.argsSchema = objectSchema({{QStringLiteral("name"), stringProperty()}});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("removed"), booleanProperty()},
		{QStringLiteral("count"), integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const QString name = args.value(QStringLiteral("name")).toString();
		ControlResult error;
		if (!isUsableTemplateName(name, &error)) { return error; }

		ControllerSurface& surface = ControllerSurface::instance();
		const bool removed = surface.removeTemplate(name);

		QJsonObject result;
		result.insert(QStringLiteral("name"), name);
		result.insert(QStringLiteral("removed"), removed);
		result.insert(QStringLiteral("count"), surface.listTemplates().size());
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(
				QJsonObject{{QStringLiteral("name"), name}},
				QStringLiteral("controller.template_save"),
				QJsonObject{{QStringLiteral("name"), name}},
				false,
				QStringLiteral("a template is a file outside the project; the deletion is "
					"bounded to that one name and no ProjectJournal checkpoint can hold a file "
					"revision, so re-creating it means re-saving the bindings from a project "
					"that still has them")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace


void registerControllerTemplateCommands(ControlRegistry& registry)
{
	registerControllerTemplateSave(registry);
	registerControllerTemplateList(registry);
	registerControllerTemplateApply(registry);
	registerControllerTemplateDelete(registry);
}


} // namespace lmms
