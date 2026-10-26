/*
 * TrackOperationsWidget.cpp - implementation of TrackOperationsWidget class
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "TrackOperationsWidget.h"

#include <algorithm>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>

#include <vector>

#include "AutomatableButton.h"
#include "AutomationClip.h"
#include "AutomationTrackView.h"
#include "ColorChooser.h"
#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "embed.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InstrumentTrackView.h"
#include "KeyboardShortcuts.h"
#include "lmms_math.h"
#include "MainWindow.h"
#include "SampleTrackView.h"
#include "ShellCommandMenus.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "SubWindow.h"
#include "TakeLanesPanel.h"
#include "Track.h"
#include "TrackFolderMenu.h"
#include "TrackRenderMenu.h"
#include "ChainPresetMenu.h"
#include "TrackContainerView.h"
#include "TrackGrip.h"
#include "TrackView.h"

namespace lmms::gui
{

/*! \brief Create a new trackOperationsWidget
 *
 * The trackOperationsWidget is the grip and the mute button of a track.
 *
 * \param parent the trackView to contain this widget
 */
TrackOperationsWidget::TrackOperationsWidget( TrackView * parent ) :
	QWidget( parent ),             /*!< The parent widget */
	m_trackView( parent )          /*!< The parent track view */
{
	setToolTip(tr("Press <%1> while clicking on move-grip "
				"to begin a new drag'n'drop action." ).arg(UI_CTRL_KEY) );

	auto toMenu = new QMenu(this);
	connect( toMenu, SIGNAL(aboutToShow()), this, SLOT(updateMenu()));


	setObjectName( "automationEnabled" );

	auto layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->setAlignment(Qt::AlignTop);

	m_trackGrip = new TrackGrip(m_trackView, this);
	layout->addWidget(m_trackGrip);

	// This widget holds the gear icon and the mute and solo
	// buttons in a layout.
	auto operationsWidget = new QWidget(this);
	auto operationsLayout = new QHBoxLayout(operationsWidget);
	operationsLayout->setContentsMargins(2, 6, 0, 6);
	operationsLayout->setSpacing(2);

	m_trackOps = new QPushButton(operationsWidget);
	m_trackOps->setFocusPolicy( Qt::NoFocus );
	m_trackOps->setMenu( toMenu );
	m_trackOps->setToolTip(tr("Actions"));
	m_trackOps->setCursor(Qt::PointingHandCursor);

	m_muteBtn = new AutomatableButton(operationsWidget, tr("Mute"));
	m_muteBtn->setCheckable(true);
	m_muteBtn->setToolTip(tr("Mute"));
	m_muteBtn->setObjectName("btn-mute");
	m_soloBtn = new AutomatableButton(operationsWidget, tr("Solo"));
	m_soloBtn->setCheckable(true);
	m_soloBtn->setToolTip(tr("Solo"));
	m_soloBtn->setObjectName("btn-solo");

	operationsLayout->addWidget(m_trackOps);
	operationsLayout->addWidget(m_muteBtn);
	operationsLayout->addWidget(m_soloBtn);

	// 0.5.0 L03 (WF-074): a record-arm button on the tracks that record (track.set_arm).
	const Track::Type type = m_trackView->getTrack()->type();
	if (type == Track::Type::Sample || type == Track::Type::Instrument)
	{
		m_armBtn = new QToolButton(operationsWidget);
		m_armBtn->setObjectName(QStringLiteral("btn-arm"));
		m_armBtn->setText(QStringLiteral("\u25CF"));
		m_armBtn->setCheckable(true);
		m_armBtn->setToolTip(tr("Arm for recording"));
		m_armBtn->setAccessibleName(tr("Arm the track for recording"));
		m_armBtn->setProperty("controlCommand", QStringLiteral("track.set_arm"));
		connect(m_armBtn, &QToolButton::clicked, this, [this](bool armed) {
			const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("track.set_arm"),
				{{QStringLiteral("track"), control::trackIdOf(m_trackView->getTrack())}, {QStringLiteral("armed"), armed}});
			if (!result.ok) { m_armBtn->setToolTip(result.errorMessage); }
			m_armBtn->setChecked(control::trackRecordArmed(m_trackView->getTrack()));
		});
		// The arm state lives in the engine's recorder, which signals nothing: a script, an agent or the Record
		// menu arming the track shows here on the next GUI tick, not only when this header happens to repaint.
		// A hidden header is skipped: a closed track view (its track deleted, the view not yet) is hidden too.
		if (getGUI() != nullptr && getGUI()->mainWindow() != nullptr)
		{
			connect(getGUI()->mainWindow(), &MainWindow::periodicUpdate, m_armBtn, [this]() {
				if (m_armBtn->isVisible()) { m_armBtn->setChecked(control::trackRecordArmed(m_trackView->getTrack())); }
			});
		}
		operationsLayout->addWidget(m_armBtn);
	}

	// 0.5.0 Q050-022: fold/unfold this track's child automation lanes. Shown only
	// when the track owns lanes (automation.lane_add placed an AutomationTrack
	// following it); paintEvent keeps that up to date because a lane can be added
	// at any time. The click goes through the registered command, the same one the
	// agent surface uses.
	m_lanesBtn = new QToolButton(operationsWidget);
	m_lanesBtn->setObjectName(QStringLiteral("btn-automation-lanes"));
	m_lanesBtn->setText(QStringLiteral("\u25BE"));
	m_lanesBtn->setAutoRaise(true);
	m_lanesBtn->setToolTip(tr("Fold this track's automation lanes"));
	m_lanesBtn->setAccessibleName(tr("Fold or unfold this track's automation lanes"));
	m_lanesBtn->setProperty("controlCommand", QStringLiteral("automation.lanes_set_collapsed"));
	m_lanesBtn->setVisible(false);
	connect(m_lanesBtn, &QToolButton::clicked, this, [this]() {
		Track* track = m_trackView->getTrack();
		const ControlResult result = ControlRegistry::instance()->invoke(
			QStringLiteral("automation.lanes_set_collapsed"),
			{{QStringLiteral("track"), control::trackIdOf(track)},
				{QStringLiteral("collapsed"), !track->automationLanesCollapsed()}});
		if (!result.ok) { m_lanesBtn->setToolTip(result.errorMessage); }
		// No realignTracks() here: the command changes the track's fold flag, whose
		// signal the container listens to (Q050-022), so the agent, the menu and this
		// button all refresh the rows through the same path.
		m_trackView->update();
	});
	operationsLayout->addWidget(m_lanesBtn);

	// 0.5.0 Q050-141: in the touch-first layout every operation is a 44 px target, a touch gap
	// apart, and the grip is wide enough to grab as well.
	if (tokens::accessibleMode())
	{
		const int target = tokens::targetSize();
		operationsLayout->setContentsMargins(0, 0, 0, 0);
		operationsLayout->setSpacing(tokens::targetSpacing(2));
		m_trackGrip->setFixedWidth(std::max(m_trackGrip->width(), target));
		const QString metrics = QStringLiteral("min-width: %1px; max-width: %1px; min-height: %1px; max-height: %1px; padding: 0;")
			.arg(target).arg(target).arg(target).arg(target);
		for (QWidget* control : {static_cast<QWidget*>(m_trackOps), static_cast<QWidget*>(m_muteBtn),
				static_cast<QWidget*>(m_soloBtn), static_cast<QWidget*>(m_armBtn),
				static_cast<QWidget*>(m_lanesBtn)})
		{
			if (control == nullptr) { continue; }
			// The track header's inherited 14 px button rule would otherwise shrink these targets.
			control->setStyleSheet(metrics);
			control->setFixedSize(target, target);
		}
	}

	layout->addWidget(operationsWidget, 0, Qt::AlignTop | Qt::AlignLeading);

	// The removal is deferred to the container's next event-loop turn. The row is held in a QPointer, and it is
	// removed only if it is still one of the container's rows then: a row closed in the meantime (its track
	// deleted in the same turn) is skipped, not deleted a second time (m0 blocker, trackview-uaf).
	TrackContainerView * const container = m_trackView->trackContainerView();
	connect( this, &TrackOperationsWidget::trackRemovalScheduled, this, [container]( TrackView * view )
	{
		QPointer<TrackView> scheduled( view );
		QMetaObject::invokeMethod( container, [container, scheduled]
		{
			if( scheduled && container->trackViews().contains( scheduled.data() ) )
			{
				container->deleteTrackView( scheduled.data() );
			}
		}, Qt::QueuedConnection );
	}, Qt::DirectConnection );

	connect( m_trackView->getTrack()->getMutedModel(), SIGNAL(dataChanged()),
			this, SLOT(update()));

	connect(m_trackView->getTrack(), SIGNAL(colorChanged()), this, SLOT(update()));
}



/*!
 * \brief Repaint the trackOperationsWidget
 *
 * Only things that's done for now is to paint the background
 * with the brush of the window from the palette.
 */
void TrackOperationsWidget::paintEvent(QPaintEvent*)
{
	// The arm state can change elsewhere (a script, the Record menu): shown as it is on each repaint.
	if (m_armBtn != nullptr) { m_armBtn->setChecked(control::trackRecordArmed(m_trackView->getTrack())); }
	// 0.5.0 Q050-022: the lanes fold button appears only once the track owns a
	// lane, and it points the way the lanes will move (down = shown, right = folded).
	if (m_lanesBtn != nullptr)
	{
		Track* track = m_trackView->getTrack();
		const bool hasLanes = track->hasAutomationLaneChildren();
		m_lanesBtn->setVisible(hasLanes);
		if (hasLanes)
		{
			const bool folded = track->automationLanesCollapsed();
			m_lanesBtn->setText(folded ? QStringLiteral("\u25B8") : QStringLiteral("\u25BE"));
			m_lanesBtn->setToolTip(folded ? tr("Unfold this track's automation lanes")
				: tr("Fold this track's automation lanes"));
		}
	}
	QPainter p( this );

	p.fillRect(rect(), palette().brush(QPalette::Window));
}


/*! \brief Show a message box warning the user that this track is about to be closed */
bool TrackOperationsWidget::confirmRemoval()
{
	bool needConfirm = ConfigManager::inst()->value("ui", "trackdeletionwarning", "1").toInt();
	if (!needConfirm){ return true; }
	
	QString messageRemoveTrack = tr("After removing a track, it can not "
					"be recovered. Are you sure you want to remove track \"%1\"?")
					.arg(m_trackView->getTrack()->name());
	QString messageTitleRemoveTrack = tr("Confirm removal");
	QString askAgainText = tr("Don't ask again");
	auto askAgainCheckBox = new QCheckBox(askAgainText, nullptr);
	auto onCheckedStateChanged = [](auto state){
		// Invert button state, if it's checked we *shouldn't* ask again
		ConfigManager::inst()->setValue("ui", "trackdeletionwarning", state != Qt::Unchecked ? "0" : "1");
	};

#if (QT_VERSION >= QT_VERSION_CHECK(6, 7, 0))
	connect(askAgainCheckBox, &QCheckBox::checkStateChanged, onCheckedStateChanged);
#else
	connect(askAgainCheckBox, &QCheckBox::stateChanged, onCheckedStateChanged);
#endif

	QMessageBox mb;
	mb.setText(messageRemoveTrack);
	mb.setWindowTitle(messageTitleRemoveTrack);
	mb.setIcon(QMessageBox::Warning);
	mb.addButton(QMessageBox::Cancel);
	mb.addButton(QMessageBox::Ok);
	mb.setCheckBox(askAgainCheckBox);
	mb.setDefaultButton(QMessageBox::Cancel);

	return mb.exec() == QMessageBox::Ok;
}

/*! \brief Clone this track
 *
 */
void TrackOperationsWidget::cloneTrack()
{
	TrackContainerView *tcView = m_trackView->trackContainerView();

	Track *newTrack = m_trackView->getTrack()->clone();
	TrackView *newTrackView = tcView->createTrackView( newTrack );

	int index = tcView->trackViews().indexOf( m_trackView );
	int i = tcView->trackViews().size();
	while ( i != index + 1 )
	{
		tcView->moveTrackView( newTrackView, i - 1 );
		i--;
	}

	if (m_soloBtn->model()->value())
	{
		// if this track was solo, make the new track the new solo
		newTrack->toggleSolo();
	}
}


/*! \brief Clear this track - clears all Clips from the track */
void TrackOperationsWidget::clearTrack()
{
	Track * t = m_trackView->getTrack();
	t->addJournalCheckPoint();
	t->lock();
	t->deleteClips();
	t->unlock();
}


/*! \brief Remove this track from the track list
 *
 */
void TrackOperationsWidget::removeTrack()
{
	if (confirmRemoval())
	{
		// The deletion itself is DEFERRED - the trackRemovalScheduled connection
		// above is a queued one - and ~Track destroys the track's clips BEFORE
		// the container forgets the pointer. So the inverse has to be recorded
		// HERE, while the track and its music are still alive: a checkpoint taken
		// inside deleteTrackView would restore a track with an empty clip list.
		// One implementation, shared with the control surface's track.remove
		// (control::journalTrackRemoval, task #664) - a delete the user can undo
		// and a delete an agent can undo are the same delete.
		//
		// 0.5.0 Q050-022: the track's child automation lanes go with it, as ONE
		// undo step (journalTrackRemovalWithLanes). Leaving a lane behind would
		// leave a row following a track that is gone.
		Track* track = m_trackView->getTrack();
		const std::vector<Track*> lanes = track->automationLaneChildren();
		if (lanes.empty()) { control::journalTrackRemoval(track); }
		else { control::journalTrackRemovalWithLanes(track); }

		TrackContainerView* container = m_trackView->trackContainerView();
		for (Track* lane : lanes)
		{
			for (TrackView* view : container->trackViews())
			{
				if (view->getTrack() == lane) { emit trackRemovalScheduled(view); break; }
			}
		}
		emit trackRemovalScheduled(m_trackView);
	}
}

void TrackOperationsWidget::selectTrackColor()
{
	const auto newColor = ColorChooser{this}
		.withPalette(ColorChooser::Palette::Track)
		->getColor(m_trackView->getTrack()->color().value_or(Qt::white));

	if (!newColor.isValid()) { return; }

	const auto track = m_trackView->getTrack();
	track->addJournalCheckPoint();
	track->setColor(newColor);
	Engine::getSong()->setModified();
}

void TrackOperationsWidget::resetTrackColor()
{
	auto track = m_trackView->getTrack();
	track->addJournalCheckPoint();
	track->setColor(std::nullopt);
	Engine::getSong()->setModified();
}

void TrackOperationsWidget::randomizeTrackColor()
{
	QColor buffer = ColorChooser::getPalette(ColorChooser::Palette::Track)[fastRand(48)];
	auto track = m_trackView->getTrack();
	track->addJournalCheckPoint();
	track->setColor(buffer);
	Engine::getSong()->setModified();
}

void TrackOperationsWidget::resetClipColors()
{
	auto track = m_trackView->getTrack();
	track->addJournalCheckPoint();
	for (auto clip : track->getClips())
	{
		clip->setColor(std::nullopt);
	}
	Engine::getSong()->setModified();
}


/*! \brief Update the trackOperationsWidget context menu
 *
 *  For all track types, we have the Clone and Remove options.
 *  For instrument-tracks we also offer the MIDI-control-menu
 *  For automation tracks, extra options: turn on/off recording
 *  on all Clips (same should be added for sample tracks when
 *  sampletrack recording is implemented)
 */
void TrackOperationsWidget::updateMenu()
{
	QMenu * toMenu = m_trackOps->menu();
	toMenu->clear();
	toMenu->addAction( embed::getIconPixmap( "edit_copy", 16, 16 ),
						tr( "Clone this track" ),
						this, SLOT(cloneTrack()));
	toMenu->addAction( embed::getIconPixmap( "cancel", 16, 16 ),
						tr( "Remove this track" ),
						this, SLOT(removeTrack()));

	// 0.5.0 Q050-022: fold/unfold the track's child automation lanes. Offered only
	// when the track owns one, and driven through the registered command.
	if (m_trackView->getTrack()->hasAutomationLaneChildren())
	{
		const bool folded = m_trackView->getTrack()->automationLanesCollapsed();
		toMenu->addAction(folded ? tr("Unfold automation lanes") : tr("Fold automation lanes"), this, [this]() {
			Track* track = m_trackView->getTrack();
			ControlRegistry::instance()->invoke(QStringLiteral("automation.lanes_set_collapsed"),
				{{QStringLiteral("track"), control::trackIdOf(track)},
					{QStringLiteral("collapsed"), !track->automationLanesCollapsed()}});
			// The command's fold flag reaches the container through the track's
			// signal (Q050-022): no manual realignTracks() here either.
			m_trackView->update();
		});
	}

	if( ! m_trackView->trackContainerView()->fixedClips() )
	{
		toMenu->addAction( tr( "Clear this track" ), this, SLOT(clearTrack()));
	}
	if (QMenu *mixerMenu = m_trackView->createMixerMenu(tr("Channel %1: %2"), tr("Assign to new Mixer Channel")))
	{
		toMenu->addMenu(mixerMenu);
	}

	if (auto trackView = dynamic_cast<InstrumentTrackView*>(m_trackView))
	{
		toMenu->addSeparator();
		toMenu->addMenu(trackView->midiMenu());
	}
	// M3 item 6: a sample track's take lanes, audition and consolidate - a registry-first
	// panel (TakeLanesPanel) in a sub-window of its own.
	if (dynamic_cast<SampleTrackView*>(m_trackView) != nullptr)
	{
		QAction* lanes = toMenu->addAction(tr("Take lanes..."), this, [this] {
			auto* panel = new TakeLanesPanel(control::trackIdOf(m_trackView->getTrack()));
			panel->setAttribute(Qt::WA_DeleteOnClose);
			getGUI()->mainWindow()->addWindowedWidget(panel)->show();
		});
		lanes->setProperty("controlCommand", QStringLiteral("comp.get_state"));
	}
	if( dynamic_cast<AutomationTrackView *>( m_trackView ) )
	{
		toMenu->addAction( tr( "Turn all recording on" ), this, SLOT(recordingOn()));
		toMenu->addAction( tr( "Turn all recording off" ), this, SLOT(recordingOff()));
	}
	// 0.5.0 W1: recording, takes and rendering - the same menu as the Inspector's (registry commands).
	ShellCommandMenus::populateTrack(toMenu->addMenu(tr("More")), m_trackView->getTrack());

	// Folder membership, and on a folder its collapse/pin/routing, through the track.* commands.
	addTrackFolderMenu(toMenu, m_trackView->getTrack());
	// Freeze / Unfreeze and Bounce in place (freeze.* / bounce.in_place).
	addTrackRenderActions(toMenu, m_trackView->getTrack());
	// Effect chain presets (chain.save / chain.apply / chain.remove).
	addChainPresetMenu(toMenu, m_trackView->getTrack());

	toMenu->addSeparator();

	QMenu* colorMenu = toMenu->addMenu(tr("Track color"));
	colorMenu->setIcon(embed::getIconPixmap("colorize"));
	colorMenu->addAction(tr("Change"), this, SLOT(selectTrackColor()));
	colorMenu->addAction(tr("Reset"), this, SLOT(resetTrackColor()));
	colorMenu->addAction(tr("Pick random"), this, SLOT(randomizeTrackColor()));
	colorMenu->addSeparator();
	colorMenu->addAction(tr("Reset clip colors"), this, SLOT(resetClipColors()));
}


void TrackOperationsWidget::toggleRecording( bool on )
{
	auto atv = dynamic_cast<AutomationTrackView*>(m_trackView);
	if( atv )
	{
		for( Clip * clip : atv->getTrack()->getClips() )
		{
			auto ap = dynamic_cast<AutomationClip*>(clip);
			if( ap ) { ap->setRecording( on ); }
		}
		atv->update();
	}
}


void TrackOperationsWidget::recordingOn()
{
	toggleRecording( true );
}


void TrackOperationsWidget::recordingOff()
{
	toggleRecording( false );
}


} // namespace lmms::gui
