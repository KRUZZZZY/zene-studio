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
 *  Interface state: nothing is journalled. */

#include <QJsonArray>

#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerOverlays.h"
#include "MixerViewOptions.h"

namespace lmms
{

using namespace control;

namespace
{

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
	state.insert(QStringLiteral("folded"), folded);
	return state;
}

ControlResult mixerView(const QJsonObject& args)
{
	// Interface state needs no ready engine, but its answer names mixer channels and folders.
	if (Engine::mixer() == nullptr || Engine::getSong() == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy, QStringLiteral("the engine is not up yet; poll control.ping"));
	}
	if (args.contains(QStringLiteral("sends_on_faders")))
	{
		const QString id = args.value(QStringLiteral("sends_on_faders")).toString();
		int index = -1;
		if (!id.isEmpty())
		{
			ControlResult error;
			MixerChannel* channel = resolveMixerChannel(id, &error);
			if (channel == nullptr) { return error; }
			index = channel->index();
		}
		gui::mixeroverlays::setSendsOnFaders(index);
	}
	gui::mixerviewoptions::Options next = gui::mixerviewoptions::options();
	QString error;
	if (!gui::mixerviewoptions::merge(args, &next, &error)) { return ControlResult::failure(ControlErrorKind::InvalidArgs, error); }
	if (args.size() > (args.contains(QStringLiteral("sends_on_faders")) ? 1 : 0)) { gui::mixerviewoptions::setOptions(next); }
	return ControlResult::success(mixerViewState());
}

} // namespace

void registerMixerViewCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("window.mixer_view");
	cmd.group = QStringLiteral("window");
	cmd.verb = QStringLiteral("mixer_view");
	cmd.mutating = false;
	cmd.requiresEngine = false;
	cmd.description = QStringLiteral("The mixer view's modes, set and answered: 'sends_on_faders' (a ch-<n> return, or "
		"empty for the channel faders) makes every channel's fader drive its send to that return - a channel with no "
		"send there shows no fader; 'folded' answers the channel strips a collapsed folder hides "
		"(track.folder_set_collapsed folds). The view options, kept in the config file (ui/mixerview): 'show_sends', "
		"'show_pan', 'show_number', 'show_meter_info', 'show_inserts', 'show_macros' (a strip's sections - the meter line is "
		"the gain reduction and the clip light, the inserts are listed by name, the macros are the rack's first four as knobs), 'strip_width' (narrow / normal / wide), 'filter' (all / instruments / audio / returns / used - "
		"the channels a track plays into or a send reaches), 'meter_mode' (peak - after the fader -, rms, pre_fader) and "
		"'meter_scale' (db42 - the default range -, db60, db24, linear). Interface state: nothing is journalled.");
	const QJsonObject fields{{QStringLiteral("show_sends"), booleanProperty()}, {QStringLiteral("show_pan"), booleanProperty()},
		{QStringLiteral("show_number"), booleanProperty()}, {QStringLiteral("show_meter_info"), booleanProperty()},
		{QStringLiteral("show_inserts"), booleanProperty()}, {QStringLiteral("show_macros"), booleanProperty()},
		{QStringLiteral("strip_width"), enumProperty(gui::mixerviewoptions::widthNames())},
		{QStringLiteral("filter"), enumProperty(gui::mixerviewoptions::filterNames())},
		{QStringLiteral("meter_mode"), enumProperty(gui::mixerviewoptions::meterModeNames())},
		{QStringLiteral("meter_scale"), enumProperty(gui::mixerviewoptions::meterScaleNames())}};
	QJsonObject args = fields;
	args.insert(QStringLiteral("sends_on_faders"), stringProperty());
	QJsonObject result = args;
	result.insert(QStringLiteral("folded"), arrayProperty());
	cmd.argsSchema = objectSchema(args);
	cmd.resultSchema = objectSchema(result);
	cmd.handler = mixerView;
	registry.registerCommand(cmd);
}

} // namespace lmms
