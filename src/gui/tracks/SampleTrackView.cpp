/*
 * SampleTrackView.cpp
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "SampleTrackView.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QJsonObject>
#include <QSpacerItem>
#include <QToolButton>
#include <QVBoxLayout>

#include "ArrangerFileDrop.h"
#include "LevelBar.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DeprecationHelper.h"
#include "MainWindow.h"
#include "embed.h"
#include "Engine.h"
#include "FadeButton.h"
#include "Mixer.h"
#include "MixerChannelLcdSpinBox.h"
#include "MixerView.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "SampleClip.h"
#include "SampleTrackWindow.h"
#include "SongEditor.h"
#include "StringPairDrag.h"
#include "TrackContainerView.h"
#include "DesignTokens.h"
#include "TrackLabelButton.h"


namespace lmms::gui
{


SampleTrackView::SampleTrackView( SampleTrack * _t, TrackContainerView* tcv ) :
	TrackView( _t, tcv )
{
	setFixedHeight(trackLaneHeight());

	m_tlb = new TrackLabelButton(this, getTrackSettingsWidget());
	m_tlb->setCheckable(true);
	connect(m_tlb, SIGNAL(clicked(bool)),
			this, SLOT(showEffects()));
	m_tlb->setIcon(embed::getIconPixmap("sample_track"));
	m_tlb->show();

	m_mixerChannelNumber = new MixerChannelLcdSpinBox(2, getTrackSettingsWidget(), tr("Mixer channel"), this);
	if (tokens::accessibleMode()) { m_mixerChannelNumber->setMinimumSize(tokens::targetSize(), tokens::targetSize()); }
	m_mixerChannelNumber->show();

	m_volumeKnob = new VolumeKnob(KnobType::Small17, tr("VOL"), getTrackSettingsWidget(), Knob::LabelRendering::LegacyFixedFontSize, tr("Track volume"));
	m_volumeKnob->setModel( &_t->m_volumeModel );
	m_volumeKnob->setHintText(tr("Volume:"), "%");
	m_volumeKnob->show();

	m_panningKnob = new Knob(KnobType::Small17, tr("PAN"), getTrackSettingsWidget(), Knob::LabelRendering::LegacyFixedFontSize, tr("Panning"));
	m_panningKnob->setModel( &_t->m_panningModel );
	m_panningKnob->setHintText( tr( "Panning:" ), "%" );
	m_panningKnob->show();

	m_activityIndicator = new FadeButton(getTrackSettingsWidget());
	m_activityIndicator->setFixedSize(tokens::accessibleMode() ? tokens::targetSize() : 8,
		tokens::accessibleMode() ? tokens::targetSize() : 28);
	m_activityIndicator->show();

	// R2.1: the input monitor, a registry action like every other (M3.2): the button runs
	// track.set_monitor and shows what the track reports back, so an agent and a person
	// change the same state through the same verb.
	m_monitorButton = new QToolButton(getTrackSettingsWidget());
	// 0.5.0: wide enough for its longest word (AUT) in the interface's typeface.
	m_monitorButton->setFixedSize(tokens::accessibleMode() ? tokens::targetSize() : 34,
		tokens::accessibleMode() ? tokens::targetSize() : 22);
	m_monitorButton->setProperty("controlCommand", QStringLiteral("track.set_monitor"));
	m_monitorButton->setAccessibleName(tr("Input monitoring"));
	connect(m_monitorButton, &QToolButton::clicked, this, &SampleTrackView::cycleMonitor);
	showMonitorMode();

	auto masterLayout = new QVBoxLayout(getTrackSettingsWidget());
	masterLayout->setContentsMargins(0, 1, 0, 0);
	auto layout = new QHBoxLayout();
	// 0.5.0 (docs/UI-SHELL.md §1): a gap between the header's controls, so the knobs' captions
	// no longer run together ("VOLPAN") and the row reads as separate controls.
	layout->setContentsMargins(0, 0, 4, 0);
	layout->setSpacing(tokens::targetSpacing(4));
	layout->addWidget(m_tlb);
	layout->addWidget(m_mixerChannelNumber);
	layout->addWidget(m_activityIndicator);
	layout->addWidget(m_monitorButton);
	layout->addWidget(m_volumeKnob);
	layout->addWidget(m_panningKnob);
	layout->addWidget(trackLevelBar(getTrack(), getTrackSettingsWidget()));  // 0.5.0 L08 (MX-011)
	masterLayout->addLayout(layout);
	masterLayout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

	connect(_t, SIGNAL(playingChanged()), this, SLOT(updateIndicator()));
	connect(getGUI()->mainWindow(), &MainWindow::periodicUpdate, this, &SampleTrackView::corruptStateUpdate);

	setModel( _t );

	m_window = new SampleTrackWindow(this);
	m_window->toggleVisibility(false);
}




void SampleTrackView::updateIndicator()
{
	if (model()->isPlaying()) { m_activityIndicator->activateOnce(); }
	else { m_activityIndicator->noteEnd(); }
}




SampleTrackView::~SampleTrackView()
{
	if(m_window != nullptr)
	{
		m_window->setSampleTrackView(nullptr);
		m_window->parentWidget()->hide();
	}
	m_window = nullptr;
}



//FIXME: This is identical to InstrumentTrackView::createMixerMenu
QMenu * SampleTrackView::createMixerMenu(QString title, QString newMixerLabel)
{
	int channelIndex = model()->mixerChannelModel()->value();

	MixerChannel *mixerChannel = Engine::mixer()->mixerChannel(channelIndex);

	// If title allows interpolation, pass channel index and name
	if (title.contains("%2"))
	{
		title = title.arg(channelIndex).arg(mixerChannel->m_name);
	}

	auto mixerMenu = new QMenu(title);

	mixerMenu->addAction(newMixerLabel, this, SLOT(createMixerLine()));
	mixerMenu->addSeparator();

	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		MixerChannel * currentChannel = Engine::mixer()->mixerChannel(i);

		if (currentChannel != mixerChannel)
		{
			const auto index = currentChannel->index();
			QString label = tr("%1: %2").arg(index).arg(currentChannel->m_name);
			mixerMenu->addAction(label, [this, index](){
				assignMixerLine(index);
			});
		}
	}

	return mixerMenu;
}




void SampleTrackView::showEffects()
{
	m_window->toggleVisibility(m_window->parentWidget()->isHidden());
}



void SampleTrackView::modelChanged()
{
	auto st = castModel<SampleTrack>();
	m_volumeKnob->setModel(&st->m_volumeModel);
	m_mixerChannelNumber->setModel(&st->m_mixerChannelModel);

	TrackView::modelChanged();
}




void SampleTrackView::dragEnterEvent(QDragEnterEvent *dee)
{
	if (arrangerdrop::carriesFile(dee->mimeData()))  // 0.5.0 L04 (Q050-031)
	{
		TrackView::dragEnterEvent(dee);
		return;
	}
	StringPairDrag::processDragEnterEvent(dee, QString("samplefile"));
}




void SampleTrackView::dropEvent(QDropEvent *de)
{
	if (arrangerdrop::carriesFile(de->mimeData()))  // 0.5.0 L04 (Q050-031)
	{
		TrackView::dropEvent(de);
		return;
	}
	QString type  = StringPairDrag::decodeKey(de);
	QString value = StringPairDrag::decodeValue(de);

	if (type == "samplefile")
	{
		int trackHeadWidth = ConfigManager::inst()->value("ui", "compacttrackbuttons").toInt()==1
				? trackSettingsWidth(true) + trackOperationsWidth(true)
				: trackSettingsWidth(false) + trackOperationsWidth(false);

		const int deX = position(de).x();
		int xPos = deX < trackHeadWidth
				? trackHeadWidth
				: deX;

		const float snapSize = getGUI()->songEditor()->m_editor->getSnapSize();
		TimePos clipPos = trackContainerView()->fixedClips()
				? TimePos(0)
				: TimePos(((xPos - trackHeadWidth) / trackContainerView()->pixelsPerBar()
							* TimePos::ticksPerBar()) + trackContainerView()->currentPosition()
						).quantize(snapSize, true);

		auto sClip = static_cast<SampleClip*>(getTrack()->createClip(clipPos));
		if (sClip) { sClip->setSampleFile(value); }
	}
}




/*! \brief Create and assign a new mixer Channel for this track */
void SampleTrackView::createMixerLine()
{
	int channelIndex = getGUI()->mixerView()->addNewChannel();
	auto channel = Engine::mixer()->mixerChannel(channelIndex);

	channel->m_name = getTrack()->name();
	channel->setColor(getTrack()->color());

	assignMixerLine(channelIndex);
}




/*! \brief Assign a specific mixer Channel for this track */
void SampleTrackView::assignMixerLine(int channelIndex)
{
	model()->mixerChannelModel()->setValue(channelIndex);

	getGUI()->mixerView()->setCurrentMixerChannel(channelIndex);
}

void SampleTrackView::corruptStateUpdate()
{
	// A closed view waits for its deferred deletion after its track has died, and the periodic tick can land
	// in that window: the model is gone then, so there is no state to read (m0 blocker, trackview-uaf).
	if (model() == nullptr) { return; }
	model()->notifyPlayingChanged();
	// The monitor mode can change under the view (an agent, an undo): the periodic tick
	// that already polls the bus state re-reads it too.
	showMonitorMode();
	if (model()->audioBusHandle()->isCorrupted())
	{
		m_activityIndicator->setState(FadeButton::State::Corrupted);
		m_activityIndicator->setToolTip(tr("Corrupted audio detected: muting affected channels"));
	}
	else
	{
		m_activityIndicator->setState(FadeButton::State::Normal);
		m_activityIndicator->setToolTip(QString{});
	}
}


void SampleTrackView::cycleMonitor()
{
	const Track* track = getTrack();
	if (track == nullptr) { return; }
	const MonitorMode next = track->monitorMode() == MonitorMode::Off ? MonitorMode::Auto
		: track->monitorMode() == MonitorMode::Auto ? MonitorMode::In : MonitorMode::Off;
	ControlRegistry::instance()->invoke(QStringLiteral("track.set_monitor"), QJsonObject{
		{QStringLiteral("track"), control::trackIdOf(track)},
		{QStringLiteral("mode"), monitorModeName(next)}});
	showMonitorMode();
}


void SampleTrackView::showMonitorMode()
{
	// getTrack(), not model(): this runs from the constructor, before the view's model is
	// attached (model() is null there - measured, a SIGSEGV on every project load).
	const Track* track = getTrack();
	if (track == nullptr) { return; }
	const MonitorMode mode = track->monitorMode();
	m_monitorButton->setText(mode == MonitorMode::In ? tr("IN") : mode == MonitorMode::Auto ? tr("AUT") : tr("OFF"));
	m_monitorButton->setCheckable(true);
	m_monitorButton->setChecked(mode != MonitorMode::Off);
	m_monitorButton->setToolTip(tr("Input monitoring: %1 (click to cycle off, auto, in)")
		.arg(monitorModeName(mode)));
}




} // namespace lmms::gui
