/*
 * ShellCommandMenus.cpp - what a track, a clip and a mixer channel offer: registered commands
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

#include "ShellCommandMenus.h"

#include <algorithm>

#include <QJsonArray>
#include <QMenu>

#include "CommandForm.h"
#include "AutomationClip.h"
#include "CommandLists.h"
#include "ControlAutomationSupport.h"
#include "ControlRegistry.h"
#include "ClipEdits.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "ShellLayout.h"
#include "Song.h"

namespace lmms::gui::ShellCommandMenus
{

namespace
{

QString tr(const char* text)
{
	return QObject::tr(text);
}

//! An entry running @a id with @a fixed - through its form when @a ask, with @a defaults.
QAction* add(QMenu* menu, const char* label, const char* id, const QJsonObject& fixed, bool ask,
	const QJsonObject& defaults = {}, bool showResult = false)
{
	const QString title = tr(label).remove(QStringLiteral("..."));
	return addCommandAction(menu, tr(label), QLatin1String(id), [id, fixed, ask, defaults, showResult, title] {
		return CommandCall{QLatin1String(id), fixed, defaults, title, ask, showResult};
	});
}

//! A submenu of @a id's values for @a key (enum or On/Off), each run at once with @a fixed.
void choices(QMenu* menu, const char* label, const char* id, const char* key, const QJsonObject& fixed)
{
	addCommandChoices(menu, tr(label), QLatin1String(id), QLatin1String(key),
		[id, fixed] { return CommandCall{QLatin1String(id), fixed, {}, QString(), false}; });
}

//! An entry opening the list surface @a make builds.
void list(QMenu* menu, const char* label, std::function<ListSpec()> make)
{
	QAction* action = menu->addAction(tr(label));
	// The list command it reads is what the entry declares (SPEC A11's agent-surface sweep).
	action->setProperty("controlCommand", make().listId);
	QObject::connect(action, &QAction::triggered, menu, [menu, make = std::move(make)] {
		QWidget* host = menu;
		while (qobject_cast<QMenu*>(host) != nullptr) { host = host->parentWidget(); }
		showCommandList(host, make());
	});
}

QJsonObject with(QJsonObject base, const char* key, const QJsonValue& value)
{
	base.insert(QLatin1String(key), value);
	return base;
}

//! The clip after @a clip on its track, by start position; nullptr when it is the last.
Clip* nextClip(Clip* clip)
{
	Clip* best = nullptr;
	for (Clip* other : clip->getTrack()->getClips())
	{
		if (other == clip || other->startPosition() <= clip->startPosition()) { continue; }
		if (best == nullptr || other->startPosition() < best->startPosition()) { best = other; }
	}
	return best;
}

//! L06: a lane (or a trim lane) for each of the track's automatable parameters, and for the last touched one.
void automationMenu(QMenu* menu, const QString& trackId)
{
	QMenu* automation = menu->addMenu(tr("Automation"));
	QMenu* lanes = automation->addMenu(tr("Lane for"));
	QMenu* trims = automation->addMenu(tr("Trim Lane for"));
	const QJsonArray tracks = ControlRegistry::instance()->invoke(QStringLiteral("automation.get_state"),
		{{QStringLiteral("track"), trackId}}).result.value(QStringLiteral("tracks")).toArray();
	constexpr int MaxListed = 64;
	int listed = 0;
	for (const QJsonValue& value : tracks.isEmpty() ? QJsonArray() : tracks.first().toObject().value(QStringLiteral("parameters")).toArray())
	{
		if (++listed > MaxListed) { break; }
		const QJsonObject parameter = value.toObject();
		const QJsonObject args{{QStringLiteral("track"), trackId}, {QStringLiteral("parameter"), parameter.value(QStringLiteral("id"))}};
		const QString name = parameter.value(QStringLiteral("name")).toString();
		addCommandAction(lanes, name, QStringLiteral("automation.lane_add"), [args] { return CommandCall{QStringLiteral("automation.lane_add"), args, {}, QString(), false}; });
		addCommandAction(trims, name, QStringLiteral("automation.add_trim"), [args] { return CommandCall{QStringLiteral("automation.add_trim"), args, {}, QString(), false}; });
	}
	lanes->setEnabled(listed > 0);
	trims->setEnabled(listed > 0);
	QAction* last = automation->addAction(tr("Lane for Last Touched Control"));
	last->setProperty("controlCommand", QStringLiteral("automation.last_touched"));
	QObject::connect(last, &QAction::triggered, automation, [automation] {
		const ControlResult touched = ControlRegistry::instance()->invoke(QStringLiteral("automation.last_touched"));
		const QJsonObject args{{QStringLiteral("track"), touched.result.value(QStringLiteral("track"))},
			{QStringLiteral("parameter"), touched.result.value(QStringLiteral("parameter"))}};
		runCommand(automation->parentWidget(), touched.ok ? CommandCall{QStringLiteral("automation.lane_add"), args, {}, QString(), false}
			: CommandCall{QStringLiteral("automation.last_touched"), {}, {}, QString(), false});
	});
}

//! L06: shapes, transform, thinning and a point's curve on an automation clip's parameter.
void automationClipMenus(QMenu* menu, Clip* clip)
{
	auto* automation = dynamic_cast<AutomationClip*>(clip);
	control::ParameterAddress address;
	if (automation == nullptr || automation->firstObject() == nullptr
		|| !control::addressableParameterForModel(automation->firstObject(), &address)) { return; }
	const QJsonObject self{{QStringLiteral("track"), address.target}, {QStringLiteral("parameter"), address.parameter.id()}};
	const QJsonObject span{{QStringLiteral("start"), static_cast<int>(clip->startPosition())}, {QStringLiteral("end"), static_cast<int>(clip->endPosition())}};
	QJsonObject shapeDefaults = span;
	shapeDefaults.insert(QStringLiteral("shape"), QStringLiteral("sine"));
	add(menu, "Draw Shape...", "automation.draw_shape", self, true, shapeDefaults);
	add(menu, "Transform...", "automation.transform", self, true, span);
	add(menu, "Thin Points...", "automation.thin", self, true, {{QStringLiteral("tolerance"), 0.01}});
	add(menu, "Point Curve...", "automation.set_curve", self, true, {{QStringLiteral("curve"), QStringLiteral("bend")}});
}

void recordMenu(QMenu* menu, const QJsonObject& self, bool instrument)
{
	QMenu* record = menu->addMenu(tr("Record"));
	add(record, "Arm", "track.set_arm", with(self, "armed", true), false);
	add(record, "Arm With Input Channel...", "track.set_arm", with(self, "armed", true), true);
	add(record, "Disarm", "track.set_arm", with(self, "armed", false), false);
	record->addSeparator();
	add(record, "Audio Input...", "record.input_set", {}, true);
	QMenu* retro = menu->addMenu(tr("Retrospective Capture"));
	choices(retro, "Keep Listening", "record.retro_capture_arm", "armed", {});
	if (instrument) { add(retro, "Last MIDI to a New Clip", "midi.retro_capture_to_clip", self, false, {}, true); }
	add(retro, "Save Last Audio as a Take...", "record.retro_capture_to_take", {}, true, {}, true);
}

void sampleClipMenus(QMenu* menu, Clip* clip, const QJsonObject& self)
{
	const auto* sample = static_cast<SampleClip*>(clip);
	const QString id = control::clipIdOf(clip);
	QMenu* warp = menu->addMenu(tr("Tempo and Warp"));
	choices(warp, "Tempo", "warp.set", "mode", self);
	choices(warp, "Stretch Algorithm", "warp.stretch", "mode", self);
	add(warp, "Add Warp Marker...", "warp.add", self, true);
	list(warp, "Warp Markers...", [id] { return CommandLists::warpMarkers(id); });
	const QJsonObject file{{QStringLiteral("path"), sample->sampleFile()}};
	add(warp, "Detect Tempo and Key", "detect.analyze", file, false, {}, true);
	add(warp, "Detect and Apply to Project...", "detect.apply", file, true);
	QMenu* process = menu->addMenu(tr("Process"));
	add(process, "Normalize...", "sample.normalize", self, true);
	add(process, "Amplify...", "sample.amplify", self, true);
	add(process, "Reverse", "sample.reverse", self, false);
	choices(process, "Fade", "sample.fade", "direction", self);
	add(process, "Gain Envelope...", "clip.gain_envelope_set", self, true);
	add(process, "Clear Gain Envelope", "clip.gain_envelope_set", with(self, "points", QJsonArray()), false);
	Clip* next = nextClip(clip);
	QAction* fade = add(menu, "Crossfade Into Next Clip...", "clip.crossfade",
		{{QStringLiteral("out"), id}, {QStringLiteral("in"), next != nullptr ? control::clipIdOf(next) : QString()}}, true);
	fade->setEnabled(fade->isEnabled() && next != nullptr);
}

void midiClipMenus(QMenu* menu, const QJsonObject& self)
{
	QMenu* harmony = menu->addMenu(tr("Scale and Chords"));
	add(harmony, "Fit Notes to Scale...", "scale.snap_notes", self, true);
	add(harmony, "Detect Chords", "chord.detect", self, false, {}, true);
	add(harmony, "Detected Chords to Chord Track", "chord.detect_to_track", self, false);
	add(harmony, "Write Chord Track Into Clip...", "chord.track_write", self, true);
	add(harmony, "Generate Progression...", "chord.progression_generate", self, true);
	QMenu* groove = menu->addMenu(tr("Groove"));
	add(groove, "Apply Groove...", "groove.apply", self, true);
	add(groove, "Extract Groove...", "groove.extract", self, true);
	list(groove, "Grooves...", [] { return CommandLists::grooves(); });
	add(groove, "Random Seed...", "note.random_seed_set", {}, true);
}

} // namespace

void populateTrack(QMenu* menu, Track* track)
{
	if (track == nullptr) { return; }
	const QString id = control::trackIdOf(track);
	const QJsonObject self{{QStringLiteral("track"), id}};
	recordMenu(menu, self, dynamic_cast<InstrumentTrack*>(track) != nullptr);
	automationMenu(menu, id);
	QMenu* takes = menu->addMenu(tr("Takes and Comping"));
	list(takes, "Take Lanes...", [id] { return CommandLists::takeLanes(id); });
	add(takes, "Add Take Lane...", "comp.lane_add", self, true);
	add(takes, "Comp a Range...", "comp.select", self, true);
	QMenu* render = menu->addMenu(tr("Render"));
	add(render, "Bounce to New Track...", "bounce.land", self, true);
	add(render, "Bounce in Place...", "bounce.in_place", self, true);
	add(render, "Freeze", "freeze.track", self, false);
	if (dynamic_cast<SampleTrack*>(track) != nullptr)
	{
		add(menu, "Insert Test Signal...", "sample.generate", self, true,
			{{QStringLiteral("position"), 0}, {QStringLiteral("length"), 192}, {QStringLiteral("kind"), QStringLiteral("tone")}});
	}
}

void populateClip(QMenu* menu, Clip* clip)
{
	if (clip == nullptr) { return; }
	const QJsonObject self{{QStringLiteral("clip"), control::clipIdOf(clip)}};
	const QJsonObject at{{QStringLiteral("position"), static_cast<int>(clip->startPosition())}};
	add(menu, "Move to Position...", "clip.move", self, true, at);
	add(menu, "Duplicate", "clip.repeat", self, false);
	add(menu, "Repeat...", "clip.repeat", self, true, {{QStringLiteral("count"), 4}});
	QMenu* scale = menu->addMenu(tr("Time Scale"));
	add(scale, "Double", "clip.time_scale", with(self, "factor", 2.0), false);
	add(scale, "Halve", "clip.time_scale", with(self, "factor", 0.5), false);
	add(scale, "Time Scale...", "clip.time_scale", self, true, {{QStringLiteral("factor"), 1.0}});
	if (dynamic_cast<SampleClip*>(clip) != nullptr)
	{
		add(menu, "Consolidate", "clip.consolidate", {{QStringLiteral("track"), control::trackIdOf(clip->getTrack())},
			{QStringLiteral("start"), static_cast<int>(clip->startPosition())}, {QStringLiteral("end"), static_cast<int>(clip->endPosition())}}, false);
	}
	add(menu, "Move With Edit Group...", "vca.edit_move", self, true, at);
	const int transpose = clip->clipEdits().transpose;
	QMenu* pitch = menu->addMenu(tr("Transpose"));
	for (const int step : {12, 1, -1, -12})
	{
		addCommandAction(pitch, QStringLiteral("%1%2").arg(step > 0 ? QStringLiteral("+") : QString()).arg(step), QStringLiteral("clip.set_transpose"),
			[self, transpose, step] { return CommandCall{QStringLiteral("clip.set_transpose"), with(self, "semitones", std::clamp(transpose + step, -48, 48)), {}, QString(), false}; });
	}
	add(pitch, "Reset", "clip.set_transpose", with(self, "semitones", 0), false);
	add(pitch, "Transpose...", "clip.set_transpose", self, true, {{QStringLiteral("semitones"), transpose}});
	QMenu* links = menu->addMenu(tr("Linked Clips"));
	add(links, "Link With Clips...", "clip.link_create", self, true);
	add(links, "Make Unique", "clip.link_remove", self, false);
	add(links, "Update Linked Copies", "clip.link_sync", self, false);
	add(links, "Link State", "clip.link_get_state", self, false, {}, true);
	if (dynamic_cast<SampleClip*>(clip) != nullptr) { sampleClipMenus(menu, clip, self); }
	if (dynamic_cast<MidiClip*>(clip) != nullptr) { midiClipMenus(menu, self); }
	automationClipMenus(menu, clip);
}

void populateChannel(QMenu* menu, const QString& channel)
{
	if (channel.isEmpty()) { return; }
	const QJsonObject self{{QStringLiteral("channel"), channel}};
	add(menu, "Send To...", "mixer.send_to", self, true, {{QStringLiteral("pre_fader"), false}});
	add(menu, "Pre-Fader Send To...", "mixer.send_to", with(self, "pre_fader", true), true);
	add(menu, "Sidechain To...", "mixer.sidechain_to", self, true);
	QMenu* rack = menu->addMenu(tr("Rack"));
	list(rack, "Zones...", [channel] { return CommandLists::rackZones(channel); });
	add(rack, "Add Zone...", "rack.zone_add", self, true);
	add(rack, "Wire the Chain...", "patcher.set_wiring", {{QStringLiteral("target"), channel}}, true);
	add(rack, "Chain Wiring", "patcher.get_state", {{QStringLiteral("target"), channel}}, false, {}, true);
	choices(menu, "Loudness Meter", "meter.arm", "enabled", {});
}

void populateProject(QMenu* menu)
{
	list(menu, "Revisions...", [] { return CommandLists::revisions(); });
	list(menu, "Project Files...", [] { return CommandLists::missingFiles(Engine::getSong()->projectFileName()); });
	add(menu, "Collect Media...", "project.collect_media", {}, true);
	list(menu, "Plug-ins in Project...", [] { return CommandLists::pluginsInProject(); });
	list(menu, "Groups...", [] { return CommandLists::vcaGroups(); });
	list(menu, "Grooves...", [] { return CommandLists::grooves(); });
	menu->addSeparator();
	add(menu, "Auto-Master...", "mastering.run", {}, true);
	add(menu, "Mastering Status", "mastering.get_state", {}, false, {}, true);
	choices(menu, "Loudness Meter", "meter.arm", "enabled", {});
	add(menu, "Loudness Reading", "meter.get_state", {}, false, {}, true);
	add(menu, "Disarm All Tracks", "record.disarm_all", {}, false);
	menu->addSeparator();
	list(menu, "Safe Start...", [] { return CommandLists::safeStart(); });
	populateCustomise(menu->addMenu(tr("Customise")));
}

void populateCustomise(QMenu* menu)
{
	list(menu, "Keyboard Shortcuts...", [] { return CommandLists::shortcuts(); });
	list(menu, "Macros...", [] { return CommandLists::macros(); });
	list(menu, "MIDI Mappings...", [] { return CommandLists::controllerMappings(); });
	list(menu, "Mapping Templates...", [] { return CommandLists::mappingTemplates(); });
	QMenu* layouts = menu->addMenu(tr("Window Layouts"));
	add(layouts, "Save Layout...", "window.layout_save", {}, true);
	const QJsonArray names = ControlRegistry::instance()->invoke(QStringLiteral("window.layout_list")).result
		.value(QStringLiteral("layouts")).toArray();
	QMenu* apply = layouts->addMenu(tr("Apply"));
	QMenu* remove = layouts->addMenu(tr("Remove"));
	for (const QJsonValue& name : names)
	{
		const QJsonObject named{{QStringLiteral("name"), name}};
		addCommandAction(apply, name.toString(), QStringLiteral("window.layout_apply"),
			[named] { return CommandCall{QStringLiteral("window.layout_apply"), named, {}, QString(), false}; });
		addCommandAction(remove, name.toString(), QStringLiteral("window.layout_remove"),
			[named] { return CommandCall{QStringLiteral("window.layout_remove"), named, {}, QString(), false}; });
	}
	apply->setEnabled(!names.isEmpty());
	remove->setEnabled(!names.isEmpty());
	QMenu* surface = menu->addMenu(tr("Control Surface (Mackie Control)"));
	add(surface, "Connect...", "surface.mcu_connect", {}, true);
	add(surface, "Ports", "surface.mcu_ports", {}, false, {}, true);
	add(surface, "State", "surface.mcu_state", {}, false, {}, true);
	add(surface, "Disconnect", "surface.mcu_disconnect", {}, false);
	QMenu* pop = menu->addMenu(tr("Pop Out Panel"));
	for (const ShellPanelSpec& spec : ShellLayout::panels())
	{
		const QJsonObject panel{{QStringLiteral("panel"), spec.id}};
		addCommandAction(pop, spec.title, QStringLiteral("window.detach"),
			[panel] { return CommandCall{QStringLiteral("window.detach"), panel, {}, QString(), false}; });
	}
}

} // namespace lmms::gui::ShellCommandMenus
