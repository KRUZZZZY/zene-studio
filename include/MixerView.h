/*
 * MixerView.h - effect-mixer-view for LMMS
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_GUI_MIXER_VIEW_H
#define LMMS_GUI_MIXER_VIEW_H

#include <QWidget>

#include "MixerChannelView.h"
#include "ModelView.h"
#include "SerializingObject.h"

class QDomDocument;  // IWYU pragma: keep
class QDomElement;  // IWYU pragma: keep
class QHBoxLayout;
class QStackedLayout;
class QScrollArea;

namespace lmms
{
	class Mixer;
}

class QAction;
class QMenu;

namespace lmms::gui
{

class MixerModeBar;
class MixerSendsBanner;

class LMMS_EXPORT MixerView
	: public QWidget
	, public ModelView
	, public SerializingObjectHook
{
	Q_OBJECT
public:
	MixerView(Mixer* mixer);
	void keyPressEvent(QKeyEvent* e) override;

	void saveSettings(QDomDocument& doc, QDomElement& domElement) override;
	void loadSettings(const QDomElement& domElement) override;

	inline MixerChannelView* currentMixerChannel()
	{
		return m_currentMixerChannel;
	}

	//! Q050-100: the strip holding keyboard focus (the focused control's strip), else the selected strip.
	MixerChannelView* focusedChannelView() const;
	//! Q050-100: the mixer's Speak Meter action (Ctrl+Shift+M). Public for tests and for a menu.
	QAction* speakMeterAction() const { return m_speakMeterAction; }
	//! Q050-100: fill @a menu with the peak-watch choices (off and -18..0 dBFS), the current one checked.
	void populatePeakWatchMenu(QMenu* menu) const;

	inline MixerChannelView* channelView(int index)
	{
		return m_mixerChannelViews[index];
	}

	//! The number of strips in the view (the mixer's channels at the last sync).
	inline int channelCount() const { return m_mixerChannelViews.size(); }


	void setCurrentMixerChannel(MixerChannelView* channel);
	void setCurrentMixerChannel(int channel);

	void clear();


	// display the send button and knob correctly
	void updateMixerChannel(int index);

	// notify the view that a mixer channel was deleted
	void deleteChannel(int index);

	// delete all unused channels
	void deleteUnusedChannels();

	// move the channel to the left or right
	void moveChannelLeft(int index);
	void moveChannelRight(int index);

	void renameChannel(int index);

	// make sure the display syncs up with the mixer.
	// useful for loading projects
	void refreshDisplay();

	//! M3.7: rebuild the VCA strips from Mixer::vcaGroups() (after a load, and
	//! after a VCA menu edit). A mix with no groups shows no VCA area at all.
	void refreshVcaStrips();

	/*! Brings the view in line with the model when something other than this
	 *  view changed it - mixer.add_channel, vca.*, a script, an undo: the view used
	 *  to add a channel view only for its own "+" button, so a channel created over
	 *  the socket was invisible until a reload. Channel count differs -> the whole
	 *  display is rebuilt; VCA structure differs -> the VCA strips are. Run every
	 *  500 ms on the GUI thread; the check is a count and a short signature. */
	void syncWithMixer();

	/*! The mixer deleted a channel. When this view did not delete it itself, the
	 *  display is rebuilt NOW rather than at the next 500 ms sync: until then a strip
	 *  outlives its channel, and painting it walks freed models - measured as a
	 *  SIGSEGV in MixerChannelView::paintEvent after a folder released its routing
	 *  channel with the mixer on screen (BUGS_FOUND 11.27). */
	void engineDeletedChannel();

	//! 0.5.0 theme 6: the bar naming the return whose sends the faders show (hidden on the channel faders).
	MixerSendsBanner* sendsBanner() const { return m_sendsBanner; }
	//! 0.5.0 theme 16: the bar naming and setting the view, density, filter and modes (MixerModeBar.h).
	MixerModeBar* modeBar() const { return m_modeBar; }

public slots:
	int addNewChannel();

private slots:
	void updateFaders();
	// TODO This should be improved. Currently the solo and mute models are connected via
	// the MixerChannelView's constructor with the MixerView. It would already be an improvement
	// if the MixerView connected itself to each new MixerChannel that it creates/handles.
	void toggledSolo();
	void toggledMute();

private:
	Mixer* getMixer() const;
	void updateAllMixerChannels();
	void connectToSoloAndMute(int channelIndex);
	void disconnectFromSoloAndMute(int channelIndex);

private:
	QVector<MixerChannelView*> m_mixerChannelViews;

	MixerChannelView* m_currentMixerChannel;
	//! The selected channel's stable id, taken when it was selected. The view only knows an index, and a
	//! model change (a removal, a reorder) moves the index; this id is what the selection follows.
	int m_selectedChannelId = -1;
	//! deleteChannel() is between its mixer call and removing the view itself.
	bool m_deletingOwnChannel = false;

	QScrollArea* channelArea;
	QHBoxLayout* chLayout;
	QWidget* m_channelAreaWidget;
	QStackedLayout* m_racksLayout;
	QWidget* m_racksWidget;
	//! M3.7: the VCA groups' strips, to the right of the channels.
	QWidget* m_vcaArea = nullptr;
	QHBoxLayout* m_vcaLayout = nullptr;
	//! What the VCA strips were last built from (id, name, member count per group).
	QString m_vcaSignature;
	//! The channels' ids in order, last time the strips were built: a reorder keeps the count but
	//! moves the channels, and every strip binds its controls to the channel at its own number.
	QString m_channelOrderSignature;
	int m_viewOptionsGeneration = -1;  // 0.5.0 L08: MixerViewOptions::generation() last applied
	MixerSendsBanner* m_sendsBanner = nullptr;
	MixerModeBar* m_modeBar = nullptr;
	QAction* m_speakMeterAction = nullptr;  //!< Q050-100
	Mixer* m_mixer;

	void updateMaxChannelSelector();

	friend class MixerChannelView;
} ;


} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_VIEW_H
