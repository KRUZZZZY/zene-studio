/*
 * ControlCommandsMixerView.cpp - window.mixer_view: the mixer view's sends-on-faders mode and its folded strips
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

/*! MX-049 / MX-066 (0.5.0 L09): window.mixer_view sets and answers the mixer view's modes
 *  (MixerOverlays.h) - which return the faders show sends to, and which channel strips a collapsed
 *  folder hides (folding itself is track.folder_set_collapsed, the arranger's own flag) - and, from
 *  L08, the view options (MixerViewOptions.h: sections, strip width, filter, meter mode and scale).
 *  0.5.0 theme 6: the answer names the return the faders show and what every fader drives, so an agent
 *  sees what the banner and the strips show. 0.5.0 theme 16: the view (mix / routing) and the density
 *  (compact / expanded), and per channel what its strip says - its type, format, routes by name, hardware
 *  output and modes (MixerStripInfo.h). Interface state: nothing is journalled.
 *  0.5.0 L08 (MX-137): window.phase_scope opens the stereo phase view (PhaseScopeView.h) on a channel. */

#include <QJsonArray>
#include <QTimer>

#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InputChannelMeters.h"
#include "InputReadings.h"
#include "Mixer.h"
#include "MixerOverlays.h"
#include "MixerStripInfo.h"
#include "MixerView.h"
#include "MixerViewOptions.h"
#include "MixerZones.h"
#include "PhaseScopeTap.h"
#include "PhaseScopeView.h"

namespace lmms
{

using namespace control;

namespace
{

//! Theme 6: what every channel's fader drives - "volume", "send" (to the return shown) or "no_send".
QJsonArray faderRoles()
{
	static const QStringList names{QStringLiteral("volume"), QStringLiteral("send"), QStringLiteral("no_send")};
	QJsonArray faders;
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		faders.append(QJsonObject{{QStringLiteral("channel"), channelIdOf(Engine::mixer()->mixerChannel(i))},
			{QStringLiteral("drives"), names.at(static_cast<int>(gui::mixeroverlays::faderRole(i)))}});
	}
	return faders;
}

//! Theme 16: what every channel's strip says - type, format, inputs, outputs, hardware output, modes - by name.
QJsonArray stripInfos()
{
	const gui::mixerstripinfo::Context context = gui::mixerstripinfo::buildContext();
	QJsonArray strips;
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		QJsonObject strip = gui::mixerstripinfo::toJson(gui::mixerstripinfo::describe(*Engine::mixer(), i, context));
		strip.insert(QStringLiteral("channel"), channelIdOf(Engine::mixer()->mixerChannel(i)));
		strip.insert(QStringLiteral("name"), Engine::mixer()->mixerChannel(i)->m_name);
		strips.append(strip);
	}
	return strips;
}

//! The return 'sends_on_faders' names, as an index into @a index (left alone for the channel faders); false,
//! with @a error, when it names no channel.
bool sendsTargetOf(const QJsonObject& args, int* index, ControlResult* error)
{
	const QString id = args.value(QStringLiteral("sends_on_faders")).toString();
	if (id.isEmpty()) { return true; }
	MixerChannel* channel = resolveMixerChannel(id, error);
	if (channel == nullptr) { return false; }
	*index = channel->index();
	return true;
}

//! MX-005: the channels 'selected_channels' names (ch-<n> ids) as indices into @a indices (true when it is not
//! given); false, with @a error, on any that is not a channel, the master (it always shows) or not a string.
bool selectionOf(const QJsonObject& args, QList<int>* indices, ControlResult* error)
{
	if (!args.contains(QStringLiteral("selected_channels"))) { return true; }
	const QJsonValue value = args.value(QStringLiteral("selected_channels"));
	if (!value.isArray())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'selected_channels' is an array of ch-<n> ids"));
		return false;
	}
	for (const QJsonValue& id : value.toArray())
	{
		MixerChannel* channel = resolveMixerChannel(id.toString(), error);
		if (channel == nullptr) { return false; }
		if (channel->index() == 0)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("the master always shows; it is not selected"));
			return false;
		}
		indices->append(channel->index());
	}
	return true;
}

//! One ch-<n> id a caller named, as the channel's canonical id; false, with @a error, unless it is a string that
//! names a channel other than the master (the master is its own dock; it is not pinned).
bool pinIdOf(const QJsonValue& value, QString* id, ControlResult* error)
{
	if (!value.isString())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("pins are ch-<n> ids (strings)"));
		return false;
	}
	MixerChannel* channel = resolveMixerChannel(value.toString(), error);
	if (channel == nullptr) { return false; }
	if (channel->index() == 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("the master is its own dock; it is not pinned"));
		return false;
	}
	*id = channelIdOf(channel);
	return true;
}

//! 'pinned_channels' replaces the list with the channels it names, in the order given (each once); false, with
//! @a error, when it is not an array or an entry is refused. @a pinned changes only on success.
bool replacePins(const QJsonValue& value, QStringList* pinned, ControlResult* error)
{
	if (!value.isArray())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'pinned_channels' is an array of ch-<n> ids"));
		return false;
	}
	QStringList replaced;
	for (const QJsonValue& entry : value.toArray())
	{
		QString id;
		if (!pinIdOf(entry, &id, error)) { return false; }
		if (!replaced.contains(id)) { replaced.append(id); }
	}
	*pinned = replaced;
	return true;
}

//! 'pin' appends one channel (when it is not pinned yet) and 'unpin' removes it; false, with @a error, when it
//! names no pinnable channel.
bool pinOne(const QJsonValue& value, bool pin, QStringList* pinned, ControlResult* error)
{
	QString id;
	if (!pinIdOf(value, &id, error)) { return false; }
	if (pin && !pinned->contains(id)) { pinned->append(id); }
	if (!pin) { pinned->removeAll(id); }
	return true;
}

//! How many of the pin keys ('pin', 'unpin', 'pinned_channels') a call gives. They are not view options.
int pinKeysIn(const QJsonObject& args)
{
	return static_cast<int>(args.contains(QStringLiteral("pin"))) + static_cast<int>(args.contains(QStringLiteral("unpin")))
		+ static_cast<int>(args.contains(QStringLiteral("pinned_channels")));
}

//! Pins start from the layout's list; 'pinned_channels' replaces it, then 'pin' is appended and 'unpin' removed.
//! False, with @a error, on any refused name; with no pin key given, @a pinned is the layout's list.
bool pinsOf(const QJsonObject& args, QStringList* pinned, ControlResult* error)
{
	*pinned = gui::mixerzones::MixerZoneState::instance()->layout().pinned;
	if (args.contains(QStringLiteral("pinned_channels"))
		&& !replacePins(args.value(QStringLiteral("pinned_channels")), pinned, error)) { return false; }
	if (args.contains(QStringLiteral("pin"))
		&& !pinOne(args.value(QStringLiteral("pin")), true, pinned, error)) { return false; }
	if (args.contains(QStringLiteral("unpin"))
		&& !pinOne(args.value(QStringLiteral("unpin")), false, pinned, error)) { return false; }
	return true;
}

//! The mixer on screen follows a mode change now, not at its next half-second sync (queued: the call may
//! come from a menu on one of its strips, which a sync can rebuild).
void syncMixerViewSoon()
{
	if (gui::getGUI() == nullptr || gui::getGUI()->mixerView() == nullptr) { return; }
	QTimer::singleShot(0, gui::getGUI()->mixerView(), &gui::MixerView::syncWithMixer);
}

//! MX-021: set the follow preference through the same path the GUI toggle uses.
void applyFollowSelection(const QJsonObject& args)
{
	if (!args.contains(QStringLiteral("follow_selection"))) { return; }
	gui::mixerviewoptions::setFollowSelection(args.value(QStringLiteral("follow_selection")).toBool());
	syncMixerViewSoon();
}

QJsonObject mixerViewState()
{
	QJsonArray folded;
	for (const int index : gui::mixeroverlays::foldedChannels())
	{
		if (index < Engine::mixer()->numChannels()) { folded.append(channelIdOf(Engine::mixer()->mixerChannel(index))); }
	}
	const int sends = gui::mixeroverlays::sendsOnFaders();
	QJsonObject state = gui::mixerviewoptions::toJson(gui::mixerviewoptions::options());
	state.insert(QStringLiteral("sends_on_faders"), sends >= 0 ? channelIdOf(Engine::mixer()->mixerChannel(sends)) : QString());
	state.insert(QStringLiteral("sends_on_faders_name"), sends >= 0 ? Engine::mixer()->mixerChannel(sends)->m_name : QString());
	state.insert(QStringLiteral("faders"), faderRoles());
	state.insert(QStringLiteral("strips"), stripInfos());
	state.insert(QStringLiteral("folded"), folded);
	state.insert(QStringLiteral("selected_channels"), gui::mixerviewoptions::selectionJson());  // MX-005
	state.insert(QStringLiteral("follow_selection"), gui::mixerviewoptions::followSelection());  // MX-021
	QJsonArray visible;
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		if (gui::mixerviewoptions::shows(i)) { visible.append(channelIdOf(Engine::mixer()->mixerChannel(i))); }
	}
	state.insert(QStringLiteral("visible_channels"), visible);
	// Q050-227.2: the pins whose channel still exists, in mixer order (a pin of a removed channel is left out).
	const QStringList pins = gui::mixerzones::MixerZoneState::instance()->layout().pinned;
	QJsonArray pinned;
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		const QString id = channelIdOf(Engine::mixer()->mixerChannel(i));
		if (pins.contains(id)) { pinned.append(id); }
	}
	state.insert(QStringLiteral("pinned_channels"), pinned);
	return state;
}

ControlResult mixerView(const QJsonObject& args)
{
	// Interface state needs no ready engine, but its answer names mixer channels and folders.
	if (Engine::mixer() == nullptr || Engine::getSong() == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy, QStringLiteral("the engine is not up yet; poll control.ping"));
	}
	const bool sendsGiven = args.contains(QStringLiteral("sends_on_faders"));
	int index = -1;
	QList<int> selection;
	ControlResult refused;
	QStringList pinned;
	if (!sendsTargetOf(args, &index, &refused) || !selectionOf(args, &selection, &refused)
		|| !pinsOf(args, &pinned, &refused)) { return refused; }
	gui::mixerviewoptions::Options next = gui::mixerviewoptions::options();
	QString error;
	if (!gui::mixerviewoptions::merge(args, &next, &error)) { return ControlResult::failure(ControlErrorKind::InvalidArgs, error); }
	// Everything is checked before anything is set: a refused call changes no mode.
	const int pinKeys = pinKeysIn(args);
	if (args.contains(QStringLiteral("selected_channels"))) { gui::mixerviewoptions::setSelectedChannels(selection); }
	applyFollowSelection(args);
	if (pinKeys > 0) { gui::mixerzones::MixerZoneState::instance()->setPinned(pinned); }  // Q050-227.2
	if (sendsGiven)
	{
		gui::mixeroverlays::setSendsOnFaders(index);
		syncMixerViewSoon();
	}
	if (args.size() - pinKeys > (sendsGiven ? 1 : 0))
	{
		gui::mixerviewoptions::setOptions(next);
		syncMixerViewSoon();  // theme 16: the strips and the mode bar follow an agent's change at once
	}
	return ControlResult::success(mixerViewState());
}

//! MX-137: open the phase view on 'channel' (the open one's channel, else the master, when left out).
ControlResult phaseScope(const QJsonObject& args)
{
	if (Engine::mixer() == nullptr || Engine::getSong() == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy, QStringLiteral("the engine is not up yet; poll control.ping"));
	}
	const gui::PhaseScopeWindow* open = gui::openPhaseScope();
	int index = open != nullptr ? open->channel() : 0;
	if (args.contains(QStringLiteral("channel")))
	{
		ControlResult error;
		MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
		if (channel == nullptr) { return error; }
		index = channel->index();
	}
	if (controlMainWindow() == nullptr) { return noInterface(QStringLiteral("window.phase_scope")); }
	gui::PhaseScopeWindow* window = gui::showPhaseScope(index);
	const MixerChannel* shown = Engine::mixer()->mixerChannel(window->channel());
	return ControlResult::success({{QStringLiteral("shown"), true}, {QStringLiteral("channel"), channelIdOf(shown)},
		{QStringLiteral("channel_name"), shown->m_name}, {QStringLiteral("correlation"), static_cast<double>(shown->m_correlation.get())},
		{QStringLiteral("frames_seen"), static_cast<double>(PhaseScopeTap::instance().written())}});
}

void registerPhaseScope(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("window.phase_scope");
	cmd.group = QStringLiteral("window");
	cmd.verb = QStringLiteral("phase_scope");
	cmd.mutating = false;
	cmd.requiresEngine = false;
	cmd.description = QStringLiteral("MX-137: open the Phase Scope (an open one is raised and moved) on mixer channel 'channel' "
		"(a ch-<n> id; left out, the open one's channel, else the master): a goniometer of the channel's newest frames - "
		"mono stands up the middle, out of phase lies flat, the left channel alone leans left - beside its phase "
		"correlation (+1 mono, 0 wide, -1 out of phase). The answer names the channel ('channel', 'channel_name'), its "
		"'correlation' now and 'frames_seen', the frames the view's tap has been fed since it was pointed at the "
		"channel (it grows while the engine runs and the channel is not muted). Closing the window stops the tap. A display.");
	cmd.argsSchema = objectSchema({{QStringLiteral("channel"), stringProperty()}});
	cmd.resultSchema = objectSchema({{QStringLiteral("shown"), booleanProperty()}, {QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("channel_name"), stringProperty()}, {QStringLiteral("correlation"), numberProperty()},
		{QStringLiteral("frames_seen"), numberProperty()}});
	cmd.handler = phaseScope;
	registry.registerCommand(cmd);
}

//! Q050-340: what the Input Meters window shows, meter by meter.
QJsonArray metersJson(const std::vector<gui::InputChannelMeters::Meter>& meters)
{
	QJsonArray list;
	for (const auto& meter : meters)
	{
		QJsonObject entry{{QStringLiteral("label"), meter.label}, {QStringLiteral("input_channel"), meter.inputChannel},
			{QStringLiteral("level_db"), inputreadings::peakDb(meter.level)}};
		if (meter.route >= 0) { entry.insert(QStringLiteral("route"), meter.route); }
		list.append(entry);
	}
	return list;
}

ControlResult inputMeters(const QJsonObject&)
{
	if (controlMainWindow() == nullptr) { return noInterface(QStringLiteral("window.input_meters")); }
	const gui::InputChannelMeters* window = gui::showInputMeters();
	return ControlResult::success({{QStringLiteral("shown"), true}, {QStringLiteral("channels"), metersJson(window->channels())},
		{QStringLiteral("armed"), metersJson(window->armed())}});
}

void registerInputMeters(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("window.input_meters");
	cmd.group = QStringLiteral("window");
	cmd.verb = QStringLiteral("input_meters");
	cmd.mutating = false;
	cmd.requiresEngine = false;
	cmd.description = QStringLiteral("Q050-340: open the Input Meters window (an open one is raised): a labelled bar for every "
		"capture channel the input carries ('In 1', 'In 2', ...) and one for every record-armed track's selected input "
		"('<track> - In <n>'), -60..0 dB with the mixer's fall-off. The answer is what the window shows now: 'channels' "
		"and 'armed', each meter's 'label', 'input_channel' (0-based), 'level_db' (-120 for silence) and, for an armed "
		"track, its record 'route'. meter.inputs reads the same levels for an agent without taking the window's. A display.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("shown"), booleanProperty()}, {QStringLiteral("channels"), arrayProperty()},
		{QStringLiteral("armed"), arrayProperty()}});
	cmd.handler = inputMeters;
	registry.registerCommand(cmd);
}

/*! Q050-227.1: window.mixer_zones - where the mixer's regions are docked and how wide their separators
 *  leave them. The arrangement lives with the layout the project keeps (window.layout_project_save's
 *  snapshot, under `mixer_zones`), so this command answers it and one layout save carries it. The
 *  arguments: an optional 'dock' ("<region>" or "<region>=<left|right>"), an optional 'sizes'
 *  ({region: pixels}), 'reset', and 'live' - which a separator drag sends with the widths it just left,
 *  so the interface's own resize is this command's write too, and no relayout happens mid-gesture.
 *  Every argument is checked before anything is applied. */
QJsonObject zonesState()
{
	const gui::mixerzones::Layout& layout = gui::mixerzones::MixerZoneState::instance()->layout();
	QJsonObject docks;
	QJsonObject sizes;
	for (const QString& id : gui::mixerzones::regionIds())
	{
		if (const QString dock = layout.docks.value(id); !dock.isEmpty()) { docks.insert(id, dock); }
		if (const int size = layout.sizes.value(id, 0); size > 0) { sizes.insert(id, size); }
	}
	return {{QStringLiteral("zones"), docks}, {QStringLiteral("sizes"), sizes},
		{QStringLiteral("mixer_zones"), gui::mixerzones::toJson(layout)}};
}

//! The 'dock' argument: "<region>=<dock>", or just "<region>" for the side it is not on. False, with
//! @a error, unless it names a side region (the channels never move, so they have another side).
bool applyDockArg(const QString& arg, const gui::mixerzones::Layout& before, QJsonObject* zones, QString* error)
{
	const int eq = arg.indexOf(QLatin1Char('='));
	const QString id = eq < 0 ? arg : arg.left(eq);
	if (!gui::mixerzones::sideRegionIds().contains(id))
	{
		*error = QStringLiteral("'dock' names a side region (%1)").arg(gui::mixerzones::sideRegionIds().join(QStringLiteral(" / ")));
		return false;
	}
	QString target = eq < 0 ? QString() : arg.mid(eq + 1);
	if (target.isEmpty()) { target = before.docks.value(id) == QStringLiteral("left") ? QStringLiteral("right") : QStringLiteral("left"); }
	zones->insert(id, target);
	return true;
}

//! Put @a layout into force. A live report (a separator drag) sets ONLY the widths, through the state's
//! sizes-only path, because the splitter already shows them: re-laying it under the mouse would fight the
//! gesture. Anything else applies the whole arrangement and re-lays the view.
void applyZones(gui::MixerView* view, const gui::mixerzones::Layout& layout, bool live)
{
	if (live) { gui::mixerzones::MixerZoneState::instance()->setSizes(layout.sizes); return; }
	gui::mixerzones::MixerZoneState::instance()->set(layout);
	view->relayoutZones();  // the state's changed() signal does this too; this also covers a test with no loop yet
}

//! 'live' is the separator drag's own call: the widths on screen, reported so they are saved like any
//! other. It may carry nothing else - a dock or a reset during a gesture would re-lay the splitter under
//! the mouse, and it would also mean the drag is asking for something it never showed.
QString liveRefusal(const QJsonObject& args)
{
	if (!args.value(QStringLiteral("live")).toBool(false)) { return QString(); }
	if (args.contains(QStringLiteral("dock")) || args.contains(QStringLiteral("reset")) || args.contains(QStringLiteral("zones")))
	{
		return QStringLiteral("'live' reports the widths a separator drag left; it takes only 'sizes'");
	}
	return QString();
}

ControlResult mixerZones(const QJsonObject& args)
{
	if (controlMainWindow() == nullptr) { return noInterface(QStringLiteral("window.mixer_zones")); }
	gui::MixerView* view = gui::getGUI() != nullptr ? gui::getGUI()->mixerView() : nullptr;
	if (view == nullptr) { return noInterface(QStringLiteral("window.mixer_zones")); }
	if (const QString misuse = liveRefusal(args); !misuse.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, misuse);
	}
	const gui::mixerzones::Layout before = gui::mixerzones::MixerZoneState::instance()->layout();
	gui::mixerzones::Layout next = args.value(QStringLiteral("reset")).toBool(false)
		? gui::mixerzones::defaultLayout() : before;
	// One region per call: "<region>" asks for that region's other side, "<region>=<dock>" names it.
	// The 'zones' object (the saved shape) may also be sent whole; the two merge. Every argument is
	// checked before anything is applied, so a refused call changes nothing.
	QJsonObject zonesArg = args.value(QStringLiteral("zones")).toObject();
	QString error;
	if (args.contains(QStringLiteral("dock"))
		&& !applyDockArg(args.value(QStringLiteral("dock")).toString(), before, &zonesArg, &error))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, error);
	}
	QJsonObject checked = args;
	if (!zonesArg.isEmpty()) { checked.insert(QStringLiteral("zones"), zonesArg); }
	if (!gui::mixerzones::parse(checked, &next, &error)) { return ControlResult::failure(ControlErrorKind::InvalidArgs, error); }
	applyZones(view, next, args.value(QStringLiteral("live")).toBool(false));
	return ControlResult::success(zonesState());
}

} // namespace

void registerMixerViewCommands(ControlRegistry& registry)
{
	registerPhaseScope(registry);  // MX-137
	registerInputMeters(registry);  // Q050-340
	ControlCommand cmd;
	cmd.id = QStringLiteral("window.mixer_view");
	cmd.group = QStringLiteral("window");
	cmd.verb = QStringLiteral("mixer_view");
	cmd.mutating = false;
	cmd.requiresEngine = false;
	cmd.description = QStringLiteral("The mixer view's modes, set and answered: 'sends_on_faders' (a ch-<n> return, or "
		"empty for the channel faders) makes every channel's fader drive its send to that return - a banner across the "
		"mixer names it, every fader is labelled with what it drives, and a channel with no send there keeps a dimmed, "
		"still 'No send' fader in place; the answer adds 'sends_on_faders_name' (the return's name) and 'faders' (per "
		"channel, what its fader drives: volume, send or no_send - the return's own fader and the master's stay volume); "
		"'folded' answers the channel strips a collapsed folder hides "
		"(track.folder_set_collapsed folds). MX-005: 'selected_channels' (ch-<n> ids, never the master) are the channels the "
		"'selected' filter shows - interface state, never saved, forgotten when the mixer is cleared for another project; a "
		"Ctrl+click on a strip adds or removes its channel, and the mode bar's 'Pick...' chooses them; the answer lists them in "
		"mixer order. MX-021: 'follow_selection' makes the 'selected' filter show the focused arranger track's (or clip "
		"owner's) mixer channel instead, keeping the manual set for when it is turned off; it is saved in the config file "
		"(ui/followchannel, off when absent), and the answer adds it with 'visible_channels' (the channels the filter shows "
		"now, master included). The view options, kept in the config file (ui/mixerview): 'show_sends', "
		"'show_pan', 'show_number', 'show_meter_info', 'show_inserts', 'show_macros' (a strip's sections - the meter line is "
		"the gain reduction and the clip light, the inserts are listed by name, the macros are the rack's first four as knobs), 'show_console' (Q050-088: the Console section - "
		"knobs and switches for the channel's trim, polarity, EQ and its Console's drive and glue compressor, each running "
		"mixer.console_strip; off by default, hidden in the routing view and the compact density), 'strip_width' (narrow / normal / wide), 'filter' (all / instruments / audio / returns / used - "
		"the channels a track plays into or a send reaches -, selected), 'meter_mode' (peak - after the fader -, rms, pre_fader) and "
		"'meter_scale' (db42 - the default range -, db60, db24, linear); MX-128, the meters' ballistics: 'meter_falloff' (fast - "
		"the default, about 116 dB/s -, medium - about 26 dB/s -, slow - about 12 dB/s, a peak programme meter's fall) and "
		"'peak_hold' (how long the peak line holds: off, 1.5s - the default -, 3s, 6s). Theme 16: 'view' (mix - levels, pan and processing, "
		"the default -, routing - each strip lists where its signal comes from and goes by name, the sends row always "
		"shown, inserts and macros left to the mix view) and 'density' (expanded - the default, every section the options "
		"show -, compact - no inserts, no macros, in the mix view no sends row, routing on one line); a mode bar across "
		"the mixer shows and sets both, the filter and the modes. The answer adds 'strips': per channel its 'type' (master, "
		"return, group, track, channel), 'format' (stereo, LCR, Quad, 5.1, 7.1), 'inputs' and 'outputs' (by name; each "
		"output with amount, pre_fader, enabled), 'hardware' (its hardware output's label, empty for none) and 'states' "
		"(afl, pfl, solo_safe, surround_pan, cue, hardware_insert). Interface state: nothing is journalled. "
		"Q050-227.2: 'pin' / 'unpin' (a ch-<n> id) and 'pinned_channels' (the whole list) pin channel strips beside the "
		"scrolling channels, so they stay on screen while the channels scroll; the strip's menu Pin Strip / Unpin Strip runs "
		"this command; the answer's 'pinned_channels' lists them in mixer order. The pins are interface state kept with the "
		"project's window layout (window.layout_project_save, inside 'mixer_zones'), restored when the project reopens; "
		"layouts without them open with none pinned.");
	const QJsonObject fields{{QStringLiteral("show_sends"), booleanProperty()}, {QStringLiteral("show_pan"), booleanProperty()},
		{QStringLiteral("show_number"), booleanProperty()}, {QStringLiteral("show_meter_info"), booleanProperty()},
		{QStringLiteral("show_inserts"), booleanProperty()}, {QStringLiteral("show_macros"), booleanProperty()},
		{QStringLiteral("show_console"), booleanProperty()},
		{QStringLiteral("strip_width"), enumProperty(gui::mixerviewoptions::widthNames())},
		{QStringLiteral("filter"), enumProperty(gui::mixerviewoptions::filterNames())},
		{QStringLiteral("meter_mode"), enumProperty(gui::mixerviewoptions::meterModeNames())},
		{QStringLiteral("meter_scale"), enumProperty(gui::mixerviewoptions::meterScaleNames())},
		{QStringLiteral("meter_falloff"), enumProperty(gui::mixerviewoptions::meterFallOffNames())},
		{QStringLiteral("peak_hold"), enumProperty(gui::mixerviewoptions::peakHoldNames())},
		{QStringLiteral("view"), enumProperty(gui::mixerviewoptions::viewNames())},
		{QStringLiteral("density"), enumProperty(gui::mixerviewoptions::densityNames())}};
	QJsonObject args = fields;
	args.insert(QStringLiteral("sends_on_faders"), stringProperty());
	args.insert(QStringLiteral("selected_channels"), arrayProperty());  // MX-005
	args.insert(QStringLiteral("follow_selection"), booleanProperty());  // MX-021
	args.insert(QStringLiteral("pin"), stringProperty());  // Q050-227.2
	args.insert(QStringLiteral("unpin"), stringProperty());
	args.insert(QStringLiteral("pinned_channels"), arrayProperty());
	QJsonObject result = args;
	result.remove(QStringLiteral("pin"));
	result.remove(QStringLiteral("unpin"));
	result.insert(QStringLiteral("folded"), arrayProperty());
	result.insert(QStringLiteral("sends_on_faders_name"), stringProperty());
	result.insert(QStringLiteral("faders"), arrayProperty());
	result.insert(QStringLiteral("strips"), arrayProperty());
	result.insert(QStringLiteral("visible_channels"), arrayProperty());
	cmd.argsSchema = objectSchema(args);
	cmd.resultSchema = objectSchema(result);
	cmd.handler = mixerView;
	registry.registerCommand(cmd);

	// Q050-227.1: the mixer's docked regions and their separators, saved with the project's layout.
	ControlCommand zones;
	zones.id = QStringLiteral("window.mixer_zones");
	zones.group = QStringLiteral("window");
	zones.verb = QStringLiteral("mixer_zones");
	zones.mutating = false;
	zones.requiresEngine = false;
	zones.description = QStringLiteral("Q050-227.1: where the mixer's regions are docked and how wide their separators leave "
		"them, set and answered. The regions are 'master' (the master strip), 'channels' (the scrolling channel area), 'vca' "
		"(the VCA strips) and 'rack' (the effect rack). 'channels' is always 'middle'; 'master', 'vca' and 'rack' dock 'left' "
		"or 'right' of it. 'dock' takes one side region, either alone (put it on the other side) or as '<region>=<left|right>', "
		"and 'zones' takes the whole arrangement as an object ({region: dock}). 'sizes' sets a region's width in pixels (%1..%2); "
		"dragging a separator in the interface sends the same 'sizes' back through this very command, with 'live': true, so the "
		"interface's own resize and an agent's are one path ('live' takes only 'sizes' and never re-lays the splitter, which is "
		"what makes it safe mid-gesture). 'reset' puts the default arrangement back: master "
		"left, channels middle, VCA and rack right. The answer is what the mixer shows now: 'zones' and 'sizes' (the portable "
		"arrangement), and 'mixer_zones', the one object window.layout_project_save keeps inside the project's window layout, "
		"so saving that layout and reopening the project brings the arrangement and the widths back. Projects and layouts "
		"saved before this feature carry no 'mixer_zones' and open with the default arrangement. 'mixer_zones' also carries "
			"'pinned' (the pinned channel strips, ch-<n> ids in pin order; window.mixer_view pins them). Interface state, kept with the "
		"layout: this command writes no project data of its own and records no undo step (the layout save is the step).")
		.arg(gui::mixerzones::MinZoneSize).arg(gui::mixerzones::MaxZoneSize);
	const QJsonObject zoneFields{{QStringLiteral("dock"), stringProperty()},
		{QStringLiteral("zones"), objectProperty()}, {QStringLiteral("sizes"), objectProperty()},
		{QStringLiteral("reset"), booleanProperty()}, {QStringLiteral("live"), booleanProperty()}};
	zones.argsSchema = objectSchema(zoneFields);
	zones.resultSchema = objectSchema({{QStringLiteral("zones"), objectProperty()},
		{QStringLiteral("sizes"), objectProperty()}, {QStringLiteral("mixer_zones"), objectProperty()}});
	zones.handler = mixerZones;
	registry.registerCommand(zones);
}

} // namespace lmms
