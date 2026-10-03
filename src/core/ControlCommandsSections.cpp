/*
 * ControlCommandsSections.cpp - section.add / set / remove / list / select
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
/*! 0.5.0 L04 (ED-047): the song's sections - add, change, remove and list them, and select one as the
 *  arranger's range (so a verse can be copied, bounced or looped as one). Each edit is one undo step
 *  that puts the previous list back. */

#include <QColor>
#include <QJsonArray>

#include "ArrangementSections.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

QString sectionId(int id) { return QStringLiteral("sec-%1").arg(id); }

QJsonObject sectionJson(const ArrangementSection& section)
{
	return {{QStringLiteral("section"), sectionId(section.id)}, {QStringLiteral("start"), static_cast<int>(section.start)},
		{QStringLiteral("end"), static_cast<int>(section.end)}, {QStringLiteral("name"), section.name}, {QStringLiteral("color"), section.color}};
}

QJsonArray listJson()
{
	QJsonArray out;
	for (const ArrangementSection& section : ArrangementSections::instance().sections()) { out.append(sectionJson(section)); }
	return out;
}

int sectionIndex(const QJsonObject& args)
{
	const QString id = args.value(QStringLiteral("section")).toString();
	bool ok = false;
	const int index = id.startsWith(QStringLiteral("sec-")) ? id.mid(4).toInt(&ok) : -1;
	return ok ? index : -1;  // "sec-garbage" is no section, not sec-0
}

//! Run @a edit and record the list before and after it as one undo step.
ControlResult recorded(const std::function<ControlResult()>& edit)
{
	const QVector<ArrangementSection> before = ArrangementSections::instance().sections();
	const QJsonArray beforeJson = listJson();
	ControlResult result = edit();
	if (!result.ok) { return result; }
	const QVector<ArrangementSection> after = ArrangementSections::instance().sections();
	result.result.insert(QStringLiteral("sections"), listJson());
	if (listJson() == beforeJson) { return result; }  // nothing changed: no undo step (review W3-batchB #4)
	addUndoStep([before] { ArrangementSections::instance().restore(before); }, [after] { ArrangementSections::instance().restore(after); });
	Engine::getSong()->setModified();
	return result;
}

QString colourOf(const QJsonObject& args, bool* ok)
{
	const QString color = args.value(QStringLiteral("color")).toString();
	*ok = color.isEmpty() || QColor::isValidColorName(color);
	return color.isEmpty() ? color : QColor(color).name();
}

ControlResult addSection(const QJsonObject& args)
{
	bool ok = false;
	const QString color = colourOf(args, &ok);
	if (!ok) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'color' is #rrggbb")); }
	return recorded([&] {
		const int id = ArrangementSections::instance().add(args.value(QStringLiteral("start")).toInt(), args.value(QStringLiteral("end")).toInt(),
			args.value(QStringLiteral("name")).toString(), color);
		if (id < 0) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a section runs from 'start' to a later 'end' (at most %1)").arg(ArrangementSections::MaxSections)); }
		return ControlResult::success(sectionJson(*ArrangementSections::instance().find(id)));
	});
}

ControlResult setSection(const QJsonObject& args)
{
	const ArrangementSection* found = ArrangementSections::instance().find(sectionIndex(args));
	if (found == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no section '%1'").arg(args.value(QStringLiteral("section")).toString())); }
	ArrangementSection changed = *found;
	changed.start = args.value(QStringLiteral("start")).toInt(static_cast<int>(changed.start));
	changed.end = args.value(QStringLiteral("end")).toInt(static_cast<int>(changed.end));
	changed.name = args.value(QStringLiteral("name")).toString(changed.name);
	bool ok = true;
	if (args.contains(QStringLiteral("color"))) { changed.color = colourOf(args, &ok); }
	if (!ok || changed.end <= changed.start) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a section runs from 'start' to a later 'end', its colour #rrggbb")); }
	return recorded([changed] {
		ArrangementSections::instance().replace(changed.id, changed);
		return ControlResult::success(sectionJson(*ArrangementSections::instance().find(changed.id)));
	});
}

ControlResult removeSection(const QJsonObject& args)
{
	const int id = sectionIndex(args);
	if (ArrangementSections::instance().find(id) == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no such section")); }
	return recorded([id] {
		ArrangementSections::instance().remove(id);
		return ControlResult::success({{QStringLiteral("removed"), sectionId(id)}});
	});
}

ControlResult selectSection(const QJsonObject& args)
{
	const ArrangementSection* found = ArrangementSections::instance().find(sectionIndex(args));
	if (found == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no such section")); }
	QJsonObject range{{QStringLiteral("start"), static_cast<double>(found->start)}, {QStringLiteral("end"), static_cast<double>(found->end)}};
	if (args.contains(QStringLiteral("tracks"))) { range.insert(QStringLiteral("tracks"), args.value(QStringLiteral("tracks"))); }
	return ControlRegistry::instance()->invoke(QStringLiteral("range.set"), range);
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("section.") + QLatin1String(verb), description, objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
}

} // namespace

void registerSectionCommands(ControlRegistry& registry)
{
	ArrangementSections::instance();  // registers its project section before a project loads
	const QJsonObject span{{QStringLiteral("start"), tickProperty()}, {QStringLiteral("end"), tickProperty()}, {QStringLiteral("name"), stringProperty()},
		{QStringLiteral("color"), stringProperty()}};
	add(registry, "list", QStringLiteral("The song's sections in time order: id (sec-<n>), start, end, name, colour."), {}, {}, false,
		[](const QJsonObject&) { return ControlResult::success({{QStringLiteral("sections"), listJson()}}); });
	add(registry, "add", QStringLiteral("Add a section from 'start' to 'end' (ticks) - a verse, a chorus - with a 'name' and an optional "
		"'color'. Drawn as a lane over the arranger, saved with the project. One undo step."), span,
		{QStringLiteral("start"), QStringLiteral("end")}, true, addSection);
	QJsonObject change = span;
	change.insert(QStringLiteral("section"), stringProperty());
	add(registry, "set", QStringLiteral("Move, resize, rename or recolour section 'section' (only the fields given change). One undo step."),
		change, {QStringLiteral("section")}, true, setSection);
	add(registry, "remove", QStringLiteral("Remove section 'section'. One undo step."), {{QStringLiteral("section"), stringProperty()}},
		{QStringLiteral("section")}, true, removeSection);
	add(registry, "select", QStringLiteral("Select section 'section' as the arranger's range (over 'tracks', or every track), so it can "
		"be copied, bounced, looped or deleted as one. Through range.set."),
		{{QStringLiteral("section"), stringProperty()}, {QStringLiteral("tracks"), arrayProperty()}}, {QStringLiteral("section")}, false, selectSection);
}

} // namespace lmms
