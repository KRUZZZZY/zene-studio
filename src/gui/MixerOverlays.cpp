/*
 * MixerOverlays.cpp - mixer view modes: folded folders' strips hidden, and faders that show one send
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

#include "MixerOverlays.h"

#include <QPointer>

#include "ControlMixerSupport.h"
#include "Engine.h"
#include "Fader.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "MixerViewOptions.h"
#include "Song.h"
#include "TrackFolder.h"

namespace lmms::gui::mixeroverlays
{

namespace
{

//! The return the faders show sends to, by its stable id (MixerChannel::id()), not its index: a channel
//! deleted or moved must not leave the faders on some other channel's sends. -1: the channel faders.
int s_sendsOnFadersId = -1;

//! Every channel under @a folder: its children's current ones and, for a routing folder, the ones they
//! had; a nested folder folds with it, so its own channel and everything under it too.
void subtreeChannels(const TrackFolder* folder, QSet<int>* out)
{
	for (Track* child : folder->children())
	{
		if (IntModel* channel = child->mixerChannelModel()) { out->insert(channel->value()); }
		const auto* nested = dynamic_cast<const TrackFolder*>(child);
		if (nested == nullptr) { continue; }
		if (nested->mixerChannel() >= 0) { out->insert(nested->mixerChannel()); }
		subtreeChannels(nested, out);
	}
	for (const TrackFolder::ChannelBinding& binding : folder->savedChildChannels()) { out->insert(binding.channel); }
}

//! The channels @a folder's collapse hides: everything under it but its own strip (taken out of this
//! folder's set alone, so another collapsed folder that holds it still hides it).
void childChannels(const TrackFolder* folder, QSet<int>* out)
{
	QSet<int> hidden;
	subtreeChannels(folder, &hidden);
	hidden.remove(folder->mixerChannel());
	out->unite(hidden);
}

//! The model a channel strip's fader shows with the faders on @a target's sends (-1: the channel faders),
//! or nullptr when it shows no fader.
FloatModel* faderModelFor(int index, int target)
{
	MixerChannel* channel = Engine::mixer()->mixerChannel(index);
	if (target < 0 || index == target || index == 0) { return &channel->m_volumeModel; }
	MixerRoute* route = control::findMixerRoute(static_cast<mix_ch_t>(index), static_cast<mix_ch_t>(target));
	return route != nullptr ? route->amount() : nullptr;
}

//! Put @a model on @a fader (of channel @a index). A send removed while its fader shows it must not
//! leave the fader on a freed model (the BUGS_FOUND 11.12 lesson): it goes back to the channel's own.
void showOnFader(Fader* fader, FloatModel* model, int index)
{
	fader->setModel(model);
	FloatModel* volume = &Engine::mixer()->mixerChannel(index)->m_volumeModel;
	if (model == volume) { return; }
	const QPointer<FloatModel> guarded(volume);  // the channel may be going too
	// A guard left from an earlier mode acts only while the fader still shows this model (its pointer
	// reads null once the model is going); a fader moved on to another send is left alone.
	QObject::connect(model, &QObject::destroyed, fader, [fader, guarded, model] {
		if (fader->model() != nullptr && fader->model() != model) { return; }
		if (guarded) { fader->setModel(guarded.data()); } else { fader->hide(); }
	});
}

} // namespace

int sendsOnFaders()
{
	if (s_sendsOnFadersId < 0) { return -1; }
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		if (Engine::mixer()->mixerChannel(i)->id() == s_sendsOnFadersId) { return i; }
	}
	return -1;
}

void setSendsOnFaders(int returnIndex)
{
	const bool valid = returnIndex >= 0 && returnIndex < Engine::mixer()->numChannels();
	s_sendsOnFadersId = valid ? Engine::mixer()->mixerChannel(returnIndex)->id() : -1;
}

QSet<int> foldedChannels()
{
	QSet<int> out;
	for (Track* track : Engine::getSong()->tracks())
	{
		const auto* folder = dynamic_cast<const TrackFolder*>(track);
		if (folder != nullptr && folder->isCollapsed()) { childChannels(folder, &out); }
	}
	out.remove(0);
	return out;
}

void apply(const QVector<MixerChannelView*>& views)
{
	const QSet<int> folded = foldedChannels();
	const int target = sendsOnFaders();
	if (target < 0) { s_sendsOnFadersId = -1; }  // the return is gone: back to the channel faders
	for (int i = 1; i < views.size() && i < Engine::mixer()->numChannels(); ++i)
	{
		MixerChannelView* view = views.at(i);
		view->setVisible(!folded.contains(i) && mixerviewoptions::shows(i));  // L08 (MX-005): the channel filter
		FloatModel* model = faderModelFor(i, target);
		if (model != nullptr && view->fader()->model() != model) { showOnFader(view->fader(), model, i); }
		view->fader()->setVisible(model != nullptr);
	}
}

} // namespace lmms::gui::mixeroverlays
