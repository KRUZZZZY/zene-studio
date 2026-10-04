/*
 * ControlCommandsTheme.cpp - theme.*: the interface's colours, edited
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

/*! 0.5.0 W3 L13 (WF-117 theme editing, beside WF-118's light palette): the design tokens' colour roles,
 *  read and changed one at a time, reset, exported to a file and imported from one. The user's colours
 *  are overrides on the chosen palette (window.appearance's 'palette'), kept in the config file
 *  (ui/theme, UiConfigStore: one undo step per write) and applied from the next start, like the palette.
 *
 *  0.5.0 UX decision (theme 20), the research's industry default for its open points: the contrast check is a
 *  confirmation, not a hard block. A change (theme.set_token, theme.import) that would put a declared text/surface
 *  pair below its WCAG minimum - 4.5:1 for text, 3:1 for large text and components - that met it before is REFUSED,
 *  naming each pair, its ratio and both colours, unless 'allow_low_contrast' is true: then it is kept (it is the
 *  user's theme; an artistic or semantic palette is a legitimate choice) and the reply still names them. A pair that
 *  already failed does not block an unrelated change. Clip and track colours are not theme roles and are not checked.
 *  Every reply says when the colours take effect (Appearance.h applyTiming) and whether a restart is pending, and a
 *  change made at the window is confirmed there in a floating message. */

#include <algorithm>

#include <QJsonArray>

#include "Appearance.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "DesignTokens.h"
#include "JsonFile.h"
#include "TextFloat.h"
#include "UiConfigStore.h"
#include "WcagContrast.h"
#include "embed.h"

namespace lmms
{

using namespace control;

namespace
{

const QString StoreKey = QStringLiteral("theme");
const QString FileFormat = QStringLiteral("zene-theme");

//! The palette the next start draws with, given @a overrides (the high-contrast one in accessible mode).
gui::tokens::Palette nextPalette(const QJsonObject& overrides)
{
	return gui::tokens::withOverrides(gui::tokens::paletteNamed(appearance::effectivePalette(appearance::options())), overrides);
}

//! The declared pairs @a palette leaves below their minimum.
QJsonArray contrastFailures(const gui::tokens::Palette& palette)
{
	QJsonArray out;
	for (const gui::tokens::ContrastPair& pair : gui::tokens::contrastPairs(palette))
	{
		const double ratio = a11y::contrastRatio(pair.foreground, pair.background);
		if (ratio < pair.minimum)
		{
			// The two colours too, so the failure can be acted on without working the palette out by hand.
			out.append(QJsonObject{{QStringLiteral("pair"), QLatin1String(pair.name)}, {QStringLiteral("ratio"), ratio},
				{QStringLiteral("minimum"), pair.minimum}, {QStringLiteral("foreground"), pair.foreground.name()},
				{QStringLiteral("background"), pair.background.name()}});
		}
	}
	return out;
}

//! Whether the interface draws @a next already, or a restart is pending before it does.
bool differsFromDrawn(const gui::tokens::Palette& next)
{
	const gui::tokens::Palette& drawn = gui::tokens::palette();
	const QStringList names = gui::tokens::tokenNames();
	return std::any_of(names.begin(), names.end(), [&](const QString& name) {
		return gui::tokens::roleColor(drawn, name) != gui::tokens::roleColor(next, name);
	});
}

QJsonObject themeState(const QJsonObject& overrides)
{
	const gui::tokens::Palette palette = nextPalette(overrides);
	QJsonArray colors;
	for (const QString& name : gui::tokens::tokenNames())
	{
		colors.append(QJsonObject{{QStringLiteral("name"), name},
			{QStringLiteral("color"), gui::tokens::roleColor(palette, name).name()},
			{QStringLiteral("overridden"), overrides.contains(name)}});
	}
	QJsonObject state{{QStringLiteral("palette"), appearance::effectivePalette(appearance::options())}, {QStringLiteral("overrides"), overrides},
		{QStringLiteral("colors"), colors}, {QStringLiteral("contrast_failures"), contrastFailures(palette)},
		{QStringLiteral("restart_pending"), differsFromDrawn(palette)}};
	const QJsonObject timing = appearance::applyTiming();
	for (auto it = timing.begin(); it != timing.end(); ++it) { state.insert(it.key(), it.value()); }
	return state;
}

//! The failures of @a after whose pair did not fail in @a before: what this change breaks.
QJsonArray newFailures(const QJsonArray& before, const QJsonArray& after)
{
	QStringList failing;
	for (const QJsonValue& entry : before) { failing.append(entry.toObject().value(QStringLiteral("pair")).toString()); }
	QJsonArray out;
	for (const QJsonValue& entry : after)
	{
		if (!failing.contains(entry.toObject().value(QStringLiteral("pair")).toString())) { out.append(entry); }
	}
	return out;
}

//! "'text on panel' 1.24:1 (#20242a on #17191d, needs 4.5:1)" for each of @a failures.
QString describeFailures(const QJsonArray& failures)
{
	QStringList parts;
	for (const QJsonValue& value : failures)
	{
		const QJsonObject f = value.toObject();
		parts.append(QStringLiteral("'%1' %2:1 (%3 on %4, needs %5:1)").arg(f.value(QStringLiteral("pair")).toString())
			.arg(f.value(QStringLiteral("ratio")).toDouble(), 0, 'f', 2).arg(f.value(QStringLiteral("foreground")).toString(),
			f.value(QStringLiteral("background")).toString()).arg(f.value(QStringLiteral("minimum")).toDouble(), 0, 'g', 2));
	}
	return parts.join(QStringLiteral("; "));
}

/*! Store @a overrides as ONE undo step whose inverse puts the previous ones back (theme.import with them) - unless the
 *  change would break a contrast pair and @a args do not allow it: then nothing is written and the refusal names the
 *  pairs. */
ControlResult storeOverrides(const QJsonObject& overrides, const QJsonObject& args)
{
	const QJsonObject previous = gui::tokens::themeOverrides();
	const QJsonArray broken = newFailures(contrastFailures(nextPalette(previous)), contrastFailures(nextPalette(overrides)));
	if (!broken.isEmpty() && !args.value(QStringLiteral("allow_low_contrast")).toBool())
	{
		ControlResult refused = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("this change puts text or a "
			"control below WCAG AA contrast: %1. Choose another colour, or pass allow_low_contrast true ('Allow low contrast') "
			"to keep it anyway").arg(describeFailures(broken)));
		refused.result = {{QStringLiteral("new_contrast_failures"), broken}};
		return refused;
	}
	QJsonObject state = themeState(overrides);
	state.insert(QStringLiteral("new_contrast_failures"), broken);
	ControlResult stored = gui::uistore::write(StoreKey, {{QStringLiteral("colors"), overrides}}, state,
		{QStringLiteral("theme.import"), {{QStringLiteral("colors"), previous}, {QStringLiteral("replace"), true},
			{QStringLiteral("allow_low_contrast"), true}}}, {});  // putting the previous theme back is never refused
	if (stored.ok) { appearance::noticeAppliesNextStart(); }
	return stored;
}

ControlResult getTheme(const QJsonObject&)
{
	return ControlResult::success(themeState(gui::tokens::themeOverrides()));
}

ControlResult setToken(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	const QString color = args.value(QStringLiteral("color")).toString().trimmed();
	QJsonObject overrides = gui::tokens::themeOverrides();
	if (color.isEmpty()) { overrides.remove(name); }
	else if (!QColor(color).isValid())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' is not a colour (#rrggbb)").arg(color));
	}
	else { overrides.insert(name, QColor(color).name()); }
	return storeOverrides(overrides, args);
}

ControlResult resetTheme(const QJsonObject&)
{
	// Nothing overridden: nothing to put back, so no undo step (review W3-agent-custom #2).
	if (gui::tokens::themeOverrides().isEmpty()) { return ControlResult::success({{QStringLiteral("overrides"), QJsonObject{}}, {QStringLiteral("changed"), false}}); }
	return storeOverrides({}, {{QStringLiteral("allow_low_contrast"), true}});  // the shipped palettes meet every pair
}

ControlResult exportTheme(const QJsonObject& args)
{
	const QJsonObject state = themeState(gui::tokens::themeOverrides());
	QJsonObject colors;
	for (const QJsonValue& entry : state.value(QStringLiteral("colors")).toArray())
	{
		colors.insert(entry.toObject().value(QStringLiteral("name")).toString(), entry.toObject().value(QStringLiteral("color")));
	}
	const QString path = args.value(QStringLiteral("file")).toString();
	QString error;
	if (!jsonfile::write(path, {{QStringLiteral("format"), FileFormat}, {QStringLiteral("palette"), state.value(QStringLiteral("palette"))},
		{QStringLiteral("colors"), colors}}, &error))
	{
		return ControlResult::failure(ControlErrorKind::Refused, error);
	}
	return ControlResult::success({{QStringLiteral("file"), path}, {QStringLiteral("colors"), colors.size()}});
}

//! The colours an import brings: the 'colors' argument, or the file's.
bool importedColors(const QJsonObject& args, QJsonObject* out, ControlResult* error)
{
	if (args.contains(QStringLiteral("colors")) && args.contains(QStringLiteral("file")))
	{
		// One source: with both, one of them would be ignored without a word.
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("give 'file' or 'colors', not both"));
		return false;
	}
	if (args.contains(QStringLiteral("colors"))) { *out = args.value(QStringLiteral("colors")).toObject(); return true; }
	QString reason;
	const QJsonObject doc = jsonfile::read(args.value(QStringLiteral("file")).toString(), &reason);
	if (doc.value(QStringLiteral("format")).toString() != FileFormat)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, reason.isEmpty()
			? QStringLiteral("the file is not a theme (theme.export writes one)") : reason);
		return false;
	}
	*out = doc.value(QStringLiteral("colors")).toObject();
	return true;
}

ControlResult importTheme(const QJsonObject& args)
{
	QJsonObject incoming;
	ControlResult error;
	if (!importedColors(args, &incoming, &error)) { return error; }
	QStringList rejected;
	gui::tokens::withOverrides(gui::tokens::Palette(), incoming, &rejected);
	if (!rejected.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("not colour roles or not colours: %1")
			.arg(rejected.join(QStringLiteral(", "))));
	}
	QJsonObject overrides = args.value(QStringLiteral("replace")).toBool() ? QJsonObject() : gui::tokens::themeOverrides();
	for (auto it = incoming.begin(); it != incoming.end(); ++it) { overrides.insert(it.key(), QColor(it.value().toString()).name()); }
	return storeOverrides(overrides, args);
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args, bool mutating,
	ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd = guiCommand("theme", verb);
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerThemeCommands(ControlRegistry& registry)
{
	const QJsonObject file = objectSchema({{QStringLiteral("file"), stringProperty()}}, {QStringLiteral("file")});
	const QString contrast = QStringLiteral(" A change that would put a text/surface pair below WCAG AA contrast (4.5:1 "
		"for text, 3:1 for large text and controls) that met it before is refused, naming the pairs - unless "
		"'allow_low_contrast' is true; the reply lists them either way ('new_contrast_failures').");
	add(registry, "get", QStringLiteral("The interface's colours as the next start will draw them: the palette, the user's "
		"overrides, every colour role {name, color, overridden}, the declared text/surface pairs that fall below their "
		"WCAG contrast, when the colours take effect ('applies' next_start, 'restart_required', 'applies_note') and "
		"whether the running interface still draws other colours ('restart_pending')."), objectSchema({}), false, getTheme);
	add(registry, "set_token", QStringLiteral("Give colour role 'name' (theme.get lists them: panel, text, accent, ...) the "
		"colour 'color' (#rrggbb), or with 'color' empty put the palette's own back. Applies from the next start; a "
		"preference in the config file, one undo step.") + contrast,
		objectSchema({{QStringLiteral("name"), enumProperty(gui::tokens::tokenNames())}, {QStringLiteral("color"), stringProperty()},
			{QStringLiteral("allow_low_contrast"), booleanProperty()}}, {QStringLiteral("name"), QStringLiteral("color")}), true, setToken);
	add(registry, "reset", QStringLiteral("Drop every colour override: the chosen palette as shipped, from the next start. "
		"One undo step."), objectSchema({}), true, resetTheme);
	add(registry, "export", QStringLiteral("Write the theme - every colour role as it will be drawn - to 'file' (JSON), to keep "
		"or share. Writes the file only."), file, false, exportTheme);
	add(registry, "import", QStringLiteral("Take a theme's colours from 'file' (theme.export's format) - or from 'colors', "
		"{role: #rrggbb} - on top of the current overrides, or instead of them with 'replace'. Every name must be a colour "
		"role. Applies from the next start; one undo step.") + contrast,
		objectSchema({{QStringLiteral("file"), stringProperty()}, {QStringLiteral("colors"), objectProperty()},
			{QStringLiteral("replace"), booleanProperty()}, {QStringLiteral("allow_low_contrast"), booleanProperty()}}), true, importTheme);
}

void appearance::noticeAppliesNextStart()
{
	// No one at a window to tell (an agent's headless instance, a test, the CLI): the reply says it.
	if (ControlRegistry::instance()->isHeadless() || controlMainWindow() == nullptr) { return; }
	gui::TextFloat::displayMessage(QObject::tr("Appearance saved"), QObject::tr("The change applies the next time Zene "
		"Studio starts. Until then the interface keeps its current look."), embed::getIconPixmap("hint"), 5000);
}

} // namespace lmms
