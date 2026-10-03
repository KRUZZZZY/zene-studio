/*
 * ControlCommandsSettingsPresets.cpp - settings.search / export / import / preset_*: settings found, kept and backed up
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

/*! 0.5.0 W3 L13 (WF-122): the preferences as a whole rather than one key at a time.
 *  - settings.search: the known settings (the Settings dialog's, the appearance and the interface
 *    customisations), found by any words of their name, page or description, with their values;
 *  - settings.export / settings.import: a backup of them in a file, and putting one back;
 *  - settings.preset_save / _apply / _list / _remove: named sets of them kept in the config file.
 *  Only the catalogue's keys travel - never a folder path (the paths class), never a device's name - so a preset or
 *  a backup moves between machines without carrying anything private. Every write is ONE undo step that
 *  puts each key's previous value back (or deletes a key that did not exist), as settings.set does. */

#include <algorithm>
#include <vector>

#include <QJsonArray>
#include <QJsonDocument>

#include "Appearance.h"
#include "ConfigManager.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "JsonFile.h"

namespace lmms
{

using namespace control;

namespace
{

const QString FileFormat = QStringLiteral("zene-settings");
const QString PresetsSection = QStringLiteral("ui");
const QString PresetsKey = QStringLiteral("settings.presets");
constexpr int MaxPresets = 32;

struct Setting
{
	const char* key;   //!< "<class>/<attribute>", settings.get's form
	const char* page;
	const char* title;
};

//! The settings that travel: the Settings dialog's preferences, the appearance and the interface's own
//! customisations (each a JSON object kept under ui/). Audio and MIDI device names and folder paths do not.
const std::vector<Setting>& catalogue()
{
	static const std::vector<Setting> settings{
		{"app/language", "General", "Language of the interface"},
		{"app/openlastproject", "General", "Open the last project at start"},
		{"app/nommpz", "General", "Save projects uncompressed (.mmp)"},
		{"app/disablebackup", "General", "Do not keep a backup when saving"},
		{"app/sololegacybehavior", "General", "Solo: the legacy behaviour"},
		{"app/loopmarkermode", "General", "How the loop markers are dragged"},
		{"ui/trackdeletionwarning", "General", "Warn before deleting a track"},
		{"ui/mixerchanneldeletionwarning", "General", "Warn before deleting a mixer channel"},
		{"tooltips/disabled", "General", "Hide tooltips"},
		{"ui/displaywaveform", "Display", "Draw the waveform of the output"},
		{"ui/printnotelabels", "Display", "Show note names on notes"},
		{"ui/showfaderticks", "Display", "Show tick marks on faders"},
		{"ui/compacttrackbuttons", "Display", "Compact track buttons"},
		{"ui/oneinstrumenttrackwindow", "Display", "Only one instrument window open at a time"},
		{"ui/sidebaronright", "Display", "Side bar on the right"},
		{"ui/letpreviewsfinish", "Display", "Let previews in the browser play to the end"},
		{"ui/smoothscroll", "Display", "Smooth scrolling in the editors"},
		{"ui/animateafp", "Display", "Animate the sample player"},
		{"ui/autoscroll", "Display", "Follow the play position in the editors"},
		{"ui/detachbehavior", "Display", "Where a detached window opens"},
		{"ui/vstalwaysontop", "Plug-ins", "Plug-in windows always on top"},
		{"ui/vstembedmethod", "Plug-ins", "How plug-in editors are embedded"},
		{"ui/enableautosave", "Saving", "Save a recovery copy automatically"},
		{"ui/enablerunningautosave", "Saving", "Also while playing"},
		{"ui/saveinterval", "Saving", "Minutes between recovery copies"},
		{"ui/disableautoquit", "Audio", "Keep instruments running when silent"},
		{"audioengine/framesperaudiobuffer", "Audio", "Buffer size (frames per period)"},
		{"audioengine/samplerate", "Audio", "Sample rate"},
		{"audioengine/sanitizemix", "Audio", "Clean the mix of invalid samples"},
		{"midi/autoquantize", "MIDI", "Quantise recorded notes automatically"},
		{"ui/scale", "Appearance", "Interface scale"},
		{"ui/fontscale", "Appearance", "Text size"},
		{"ui/palette", "Appearance", "Colour palette: dark, high contrast or light"},
		{"ui/density", "Appearance", "Layout density: standard, comfortable or touch"},
		{"ui/theme", "Customise", "Theme colours"},
		{"ui/header", "Customise", "Header buttons"},
		{"ui/shortcuts", "Customise", "Keyboard shortcuts"},
		{"ui/macros", "Customise", "Macros"},
		{"ui/shell.layouts", "Customise", "Saved window layouts"},
	};
	return settings;
}

bool known(const QString& key)
{
	const auto& all = catalogue();
	return std::any_of(all.begin(), all.end(), [&key](const Setting& s) { return key == QLatin1String(s.key); });
}

//! One key's state, to put it back: its value and whether it was there at all.
struct KeyState
{
	QString key;
	QString value;
	bool present = false;
};

const QString Unset = QStringLiteral("\u0001unset\u0001");

KeyState stateOf(const QString& key)
{
	const QString cls = key.section(QLatin1Char('/'), 0, 0);
	const QString value = ConfigManager::inst()->value(cls, key.section(QLatin1Char('/'), 1), Unset);
	return {key, value == Unset ? QString() : value, value != Unset};
}

void restore(const std::vector<KeyState>& states)
{
	for (const KeyState& state : states)
	{
		const QString cls = state.key.section(QLatin1Char('/'), 0, 0);
		const QString attribute = state.key.section(QLatin1Char('/'), 1);
		if (state.present) { ConfigManager::inst()->setValue(cls, attribute, state.value); }
		else { ConfigManager::inst()->deleteValue(cls, attribute); }
	}
	ConfigManager::inst()->saveConfigFile();
	appearance::syncScaleFile();  // the scale is also kept beside the config, for main()
}

//! The catalogue's keys that are set, {key: value}.
QJsonObject currentValues()
{
	QJsonObject out;
	for (const Setting& setting : catalogue())
	{
		const KeyState state = stateOf(QLatin1String(setting.key));
		if (state.present) { out.insert(state.key, state.value); }
	}
	return out;
}

//! Write @a values (catalogue keys with string values, as an export writes them; @a rejected collects the rest) as ONE
//! undo step. A number or boolean is not read as a string: QJsonValue::toString would make it an empty setting.
int applyValues(const QJsonObject& values, QStringList* rejected)
{
	std::vector<KeyState> before;
	std::vector<KeyState> after;
	for (auto it = values.begin(); it != values.end(); ++it)
	{
		if (!known(it.key()) || !it.value().isString()) { rejected->append(it.key()); continue; }
		before.push_back(stateOf(it.key()));
		after.push_back({it.key(), it.value().toString(), true});
	}
	if (after.empty()) { return 0; }
	restore(after);
	addUndoStep([before] { restore(before); }, [after] { restore(after); });
	return static_cast<int>(after.size());
}

ControlResult appliedReply(int applied, const QStringList& rejected)
{
	return ControlResult::success({{QStringLiteral("applied"), applied}, {QStringLiteral("ignored"), QJsonArray::fromStringList(rejected)},
		{QStringLiteral("applies"), QStringLiteral("now; settings read at start take effect at the next start")}});
}

bool matchesAll(const Setting& setting, const QStringList& words)
{
	const QString haystack = QStringLiteral("%1 %2 %3").arg(QLatin1String(setting.key), QLatin1String(setting.page), QLatin1String(setting.title));
	return std::all_of(words.begin(), words.end(), [&haystack](const QString& w) { return haystack.contains(w, Qt::CaseInsensitive); });
}

ControlResult search(const QJsonObject& args)
{
	const QStringList words = args.value(QStringLiteral("text")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
	QJsonArray found;
	for (const Setting& setting : catalogue())
	{
		if (!matchesAll(setting, words)) { continue; }
		const KeyState state = stateOf(QLatin1String(setting.key));
		found.append(QJsonObject{{QStringLiteral("key"), state.key}, {QStringLiteral("page"), QLatin1String(setting.page)},
			{QStringLiteral("title"), QLatin1String(setting.title)}, {QStringLiteral("value"), state.value}, {QStringLiteral("present"), state.present}});
	}
	return ControlResult::success({{QStringLiteral("settings"), found}, {QStringLiteral("count"), found.size()}});
}

ControlResult exportSettings(const QJsonObject& args)
{
	const QJsonObject values = currentValues();
	QString error;
	if (!jsonfile::write(args.value(QStringLiteral("file")).toString(), {{QStringLiteral("format"), FileFormat}, {QStringLiteral("settings"), values}}, &error))
	{
		return ControlResult::failure(ControlErrorKind::Refused, error);
	}
	return ControlResult::success({{QStringLiteral("file"), args.value(QStringLiteral("file"))}, {QStringLiteral("settings"), values.size()}});
}

ControlResult importSettings(const QJsonObject& args)
{
	QString error;
	const QJsonObject doc = jsonfile::read(args.value(QStringLiteral("file")).toString(), &error);
	// A backup whose 'settings' is not an object would "restore" nothing and still answer success.
	if (doc.value(QStringLiteral("format")).toString() != FileFormat || !doc.value(QStringLiteral("settings")).isObject())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, error.isEmpty() ? QStringLiteral("the file is not a settings backup (settings.export writes one)") : error);
	}
	QStringList rejected;
	const int applied = applyValues(doc.value(QStringLiteral("settings")).toObject(), &rejected);
	return appliedReply(applied, rejected);
}

QJsonObject presets()
{
	return QJsonDocument::fromJson(ConfigManager::inst()->value(PresetsSection, PresetsKey).toUtf8()).object();
}

//! Store @a next as the presets, ONE undo step.
void storePresets(const QJsonObject& next)
{
	const KeyState before = stateOf(PresetsSection + QLatin1Char('/') + PresetsKey);
	const KeyState after{before.key, QString::fromUtf8(QJsonDocument(next).toJson(QJsonDocument::Compact)), true};
	restore({after});
	addUndoStep([before] { restore({before}); }, [after] { restore({after}); });
}

ControlResult presetSave(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	QJsonObject all = presets();
	if (name.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a preset needs a name")); }
	if (!all.contains(name) && all.size() >= MaxPresets)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("at most %1 presets are kept; remove one first").arg(MaxPresets));
	}
	all.insert(name, currentValues());
	storePresets(all);
	return ControlResult::success({{QStringLiteral("name"), name}, {QStringLiteral("settings"), all.value(name).toObject().size()},
		{QStringLiteral("count"), all.size()}});
}

ControlResult presetApply(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	if (!presets().contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no settings preset '%1'").arg(name)); }
	QStringList rejected;
	return appliedReply(applyValues(presets().value(name).toObject(), &rejected), rejected);
}

ControlResult presetList(const QJsonObject&)
{
	QJsonArray out;
	const QJsonObject all = presets();
	for (auto it = all.begin(); it != all.end(); ++it)
	{
		out.append(QJsonObject{{QStringLiteral("name"), it.key()}, {QStringLiteral("settings"), it.value().toObject().size()}});
	}
	return ControlResult::success({{QStringLiteral("presets"), out}, {QStringLiteral("max_presets"), MaxPresets}});
}

ControlResult presetRemove(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	QJsonObject all = presets();
	if (!all.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no settings preset '%1'").arg(name)); }
	all.remove(name);
	storePresets(all);
	return ControlResult::success({{QStringLiteral("name"), name}, {QStringLiteral("count"), all.size()}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args, bool mutating,
	ControlResult (*handler)(const QJsonObject&))
{
	// requiresEngine false: ConfigManager only, as settings.get / settings.set.
	control::defineCommandWithSchema(registry, QStringLiteral("settings.") + QLatin1String(verb), description, args, mutating, handler, false);
}

} // namespace

void registerSettingsPresetCommands(ControlRegistry& registry)
{
	const QJsonObject file = objectSchema({{QStringLiteral("file"), stringProperty()}}, {QStringLiteral("file")});
	const QJsonObject named = objectSchema({{QStringLiteral("name"), stringProperty()}}, {QStringLiteral("name")});
	add(registry, "search", QStringLiteral("Find settings by any words of their name, page or key ('text'; every setting when "
		"empty): key, page, title, value and whether it is set. The keys are settings.get / settings.set's."),
		objectSchema({{QStringLiteral("text"), stringProperty()}}), false, search);
	add(registry, "export", QStringLiteral("Back the settings up to 'file' (JSON): the preferences, the appearance and the "
		"interface's customisations - never folder paths or device names. Writes the file only."), file, false, exportSettings);
	add(registry, "import", QStringLiteral("Put a backup from 'file' (settings.export's format) back; keys that are not known "
		"settings are ignored and listed. One undo step."), file, true, importSettings);
	add(registry, "preset_save", QStringLiteral("Keep the current settings as preset 'name' (replacing one of that name; at "
		"most 32), in the config file. One undo step."), named, true, presetSave);
	add(registry, "preset_apply", QStringLiteral("Switch to settings preset 'name'. Settings read at start take effect at the "
		"next start. One undo step."), named, true, presetApply);
	add(registry, "preset_list", QStringLiteral("The settings presets: each one's name and how many settings it holds."),
		objectSchema({}), false, presetList);
	add(registry, "preset_remove", QStringLiteral("Forget settings preset 'name'. One undo step."), named, true, presetRemove);
}

} // namespace lmms
