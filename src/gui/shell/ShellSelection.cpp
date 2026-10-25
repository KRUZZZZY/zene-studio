/*
 * ShellSelection.cpp - the track and clip the shell's Inspector and Device panel follow
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

#include "ShellSelection.h"
#include "Song.h"
#include "ConfigManager.h"

#include <QApplication>
#include <QEvent>
#include <QWidget>

#include "ClipView.h"
#include "ControlMixerSupport.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "RemoteControls.h"
#include "TrackView.h"

namespace lmms::gui
{

namespace
{

//! 0.5.0 L10 (MX-106): connected remote-control knobs follow the selection - a track's instrument, or
//! the first device of the selected channel.
void followRemoteControls(const ShellSelection* selection)
{
	RemoteControls* remote = RemoteControls::instance();
	if (remote == nullptr) { return; }
	if (Track* track = selection->track())
	{
		remote->followSelection(control::trackIdOf(track), QStringLiteral("inst"));
		return;
	}
	ControlResult ignored;
	MixerChannel* channel = control::resolveMixerChannel(selection->channel(), &ignored);
	if (channel != nullptr && !channel->m_fxChain.effects().empty())
	{
		remote->followSelection(selection->channel(), control::effectIdOf(channel->m_fxChain.effects().front()));
		return;
	}
	// Nothing here to follow - a channel with no devices, or no selection at all. The knobs have
	// to stop driving whatever the last selection named, or a turn writes to a device that is no
	// longer on screen.
	remote->followSelection(QString(), QString());
}

} // namespace

Track* ShellSelection::followedTrack(const QString& channelId)
{
	if (ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("followchannel")).toInt() == 0) { return nullptr; }
	ControlResult ignored;
	const MixerChannel* channel = control::resolveMixerChannel(channelId, &ignored);
	if (channel == nullptr || channel->index() == 0) { return nullptr; }
	for (Track* track : Engine::getSong()->tracks())
	{
		if (IntModel* model = track->mixerChannelModel(); model != nullptr && model->value() == channel->index()) { return track; }
	}
	return nullptr;
}

ShellSelection* ShellSelection::instance()
{
	static ShellSelection* selection = [] {
		auto* made = new ShellSelection;
		QObject::connect(made, &ShellSelection::changed, made, [made] { followRemoteControls(made); });
		return made;
	}();
	return selection;
}

void ShellSelection::watchSelectedObjects()
{
	QObject::disconnect(m_trackDestroyedConnection);
	QObject::disconnect(m_clipDestroyedConnection);
	m_trackDestroyedConnection = {};
	m_clipDestroyedConnection = {};
	const auto clear = [this] { clearDestroyedSelection(); };
	if (m_track != nullptr) { m_trackDestroyedConnection = connect(m_track, &Track::destroyedTrack, this, clear); }
	if (m_clip != nullptr)
	{
		m_clipDestroyedConnection = connect(m_clip, &QObject::destroyed, this, [this] {
			m_clipDestroyedConnection = {};
			m_clip = nullptr;
			emit changed();
		});
	}
}

void ShellSelection::clearDestroyedSelection()
{
	QObject::disconnect(m_trackDestroyedConnection);
	QObject::disconnect(m_clipDestroyedConnection);
	m_trackDestroyedConnection = {};
	m_clipDestroyedConnection = {};
	m_track = nullptr;
	m_clip = nullptr;
	m_channel.clear();
	emit changed();
}

void ShellSelection::setTrack(Track* track)
{
	const bool clipStays = m_clip != nullptr && m_clip->getTrack() == track;
	if (m_track == track && (clipStays || m_clip == nullptr) && m_channel.isEmpty()) { return; }
	m_track = track;
	m_channel.clear();
	if (!clipStays) { m_clip = nullptr; }
	watchSelectedObjects();
	emit changed();
}

void ShellSelection::setClip(Clip* clip)
{
	if (m_clip == clip && (clip == nullptr || m_track == clip->getTrack()) && m_channel.isEmpty()) { return; }
	m_clip = clip;
	m_channel.clear();
	if (clip != nullptr) { m_track = clip->getTrack(); }
	watchSelectedObjects();
	emit changed();
}

void ShellSelection::setChannel(const QString& id)
{
	// 0.5.0 L08 (MX-021): with following on (config ui/followchannel), the channel's track is selected instead.
	if (Track* owner = followedTrack(id)) { setTrack(owner); return; }
	if (m_channel == id && m_track == nullptr) { return; }
	m_track = nullptr;
	m_clip = nullptr;
	m_channel = id;
	watchSelectedObjects();
	emit changed();
}

void ShellSelection::trackClicks()
{
	static bool installed = false;
	if (installed || qApp == nullptr) { return; }
	qApp->installEventFilter(this);
	installed = true;
}

bool ShellSelection::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() != QEvent::MouseButtonPress || !watched->isWidgetType()) { return false; }
	for (auto* widget = static_cast<QWidget*>(watched); widget != nullptr; widget = widget->parentWidget())
	{
		if (auto* clipView = dynamic_cast<ClipView*>(widget))
		{
			setClip(clipView->getClip());
			break;
		}
		if (auto* trackView = dynamic_cast<TrackView*>(widget))
		{
			setTrack(trackView->getTrack());
			break;
		}
		if (auto* strip = dynamic_cast<MixerChannelView*>(widget))
		{
			setChannel(control::channelIdOf(Engine::mixer()->mixerChannel(strip->channelIndex())));
			break;
		}
	}
	return false;
}

} // namespace lmms::gui
