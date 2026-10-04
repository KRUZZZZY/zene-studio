/*
 * TrackView.cpp - implementation of TrackView class
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


#include "TrackView.h"

#include "TrackFolder.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QTimer>


#include "AudioEngine.h"
#include "AutomatableButton.h"
#include "ConfigManager.h"
#include "DataFile.h"
#include "DeprecationHelper.h"
#include "DpiHelper.h"
#include "Engine.h"
#include "FadeButton.h"
#include "StringPairDrag.h"
#include "Track.h"
#include "TrackGrip.h"
#include "TrackContainerView.h"
#include "TrackSpacers.h"
#include "ClipView.h"


namespace lmms::gui
{

namespace
{

//! WF-107: a spacer's band - darker than a row, a divider line at its middle, its label at the left.
void paintSpacer(QPainter& p, const QRect& band, const QString& label)
{
	p.fillRect(band, QColor(20, 22, 25));
	p.setPen(QColor(70, 76, 84));
	const int middle = band.center().y();
	if (label.isEmpty())
	{
		p.drawLine(band.left() + 8, middle, band.right() - 8, middle);
		return;
	}
	const QRect text = p.fontMetrics().boundingRect(label);
	p.drawText(band.left() + 10, middle + text.height() / 2 - p.fontMetrics().descent(), label);
	p.drawLine(band.left() + 18 + text.width(), middle, band.right() - 8, middle);
}

} // namespace

/*! \brief Create a new track View.
 *
 *  The track View is handles the actual display of the track, including
 *  displaying its various widgets and the track segments.
 *
 *  \param track The track to display.
 *  \param tcv The track Container View for us to be displayed in.
 *  \todo Is my description of these properties correct?
 */
TrackView::TrackView( Track * track, TrackContainerView * tcv ) :
	QWidget( tcv->contentWidget() ),   /*!< The Track Container View's content widget. */
	ModelView( nullptr, this ),            /*!< The model view of this track */
	m_track( track ),                  /*!< The track we're displaying */
	m_trackContainerView( tcv ),       /*!< The track Container View we're displayed in */
	m_trackOperationsWidget( this ),    /*!< Our trackOperationsWidget */
	m_trackSettingsWidget( this ),      /*!< Our trackSettingsWidget */
	m_trackContentWidget( this ),       /*!< Our trackContentWidget */
	m_action( Action::None )                /*!< The action we're currently performing */
{
	setAutoFillBackground( true );
	QPalette pal;
	pal.setColor( backgroundRole(), QColor( 32, 36, 40 ) );
	setPalette( pal );

	m_trackSettingsWidget.setAutoFillBackground( true );

	auto layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing( 0 );
	layout->addWidget( &m_trackOperationsWidget );
	layout->addWidget( &m_trackSettingsWidget );
	layout->addWidget( &m_trackContentWidget, 1 );
	setFixedHeight(scaledPixels(m_track->getHeight()));

	resizeEvent( nullptr );

	setAcceptDrops( true );
	setAttribute( Qt::WA_DeleteOnClose, true );


	connect( m_track, SIGNAL(destroyedTrack()), this, SLOT(close()));
	connect( m_track,
		SIGNAL(clipAdded(lmms::Clip*)),
			this, SLOT(createClipView(lmms::Clip*)),
			Qt::QueuedConnection );

	connect( &m_track->m_mutedModel, SIGNAL(dataChanged()),
			&m_trackContentWidget, SLOT(update()));

	connect(&m_track->m_mutedModel, SIGNAL(dataChanged()),
			this, SLOT(muteChanged()));

	connect( &m_track->m_soloModel, SIGNAL(dataChanged()),
			m_track, SLOT(toggleSolo()), Qt::DirectConnection );
	
	auto trackGrip = m_trackOperationsWidget.getTrackGrip();
	connect(trackGrip, &TrackGrip::grabbed, this, &TrackView::onTrackGripGrabbed);
	connect(trackGrip, &TrackGrip::released, this, &TrackView::onTrackGripReleased);

	// create views for already existing clips
	for (const auto& clip : m_track->m_clips)
	{
		createClipView(clip);
	}

	m_trackContainerView->addTrackView( this );
	// WF-107: the subclasses set their own height after this constructor; the spacer goes on after them.
	QTimer::singleShot(0, this, &TrackView::applySpacer);
}








void TrackView::updateFolderIndent()
{
	constexpr int IndentPerLevel = 10;
	int depth = 0;
	QString path;
	for (const TrackFolder* folder = getTrack()->parentFolder(); folder != nullptr; folder = folder->parentFolder())
	{
		++depth;
		path = path.isEmpty() ? folder->name() : folder->name() + QStringLiteral(" / ") + path;
	}
	m_trackSettingsWidget.setContentsMargins(depth * IndentPerLevel, 0, 0, 0);
	m_trackSettingsWidget.setAccessibleDescription(depth == 0 ? QString() : tr("In folder %1").arg(path));
}


/*! \brief Resize this track View.
 *
 *  \param re the Resize Event to handle.
 */
void TrackView::resizeEvent( QResizeEvent * re )
{
	if( ConfigManager::inst()->value( "ui",
					  "compacttrackbuttons" ).toInt() )
	{
		m_trackOperationsWidget.setFixedSize( TRACK_OP_WIDTH_COMPACT, laneHeight() - 1 );
		m_trackSettingsWidget.setFixedSize( DEFAULT_SETTINGS_WIDGET_WIDTH_COMPACT, laneHeight() - 1 );
	}
	else
	{
		m_trackOperationsWidget.setFixedSize( TRACK_OP_WIDTH, laneHeight() - 1 );
		m_trackSettingsWidget.setFixedSize( DEFAULT_SETTINGS_WIDGET_WIDTH, laneHeight() - 1 );
	}

	m_trackContentWidget.setFixedHeight( laneHeight() );
}




int TrackView::spacerPixels() const
{
	return scaledPixels(TrackSpacers::instance().spacer(m_track->id()).height);
}




void TrackView::applySpacer()
{
	const int spacer = spacerPixels();
	// No spacer now and none before: the row is left exactly as it always was.
	if (layout() == nullptr || (spacer == 0 && layout()->contentsMargins().bottom() == 0)) { return; }
	layout()->setContentsMargins(0, 0, 0, spacer);
	setFixedHeight(scaledPixels(m_track->getHeight()) + spacer);
}




/*! \brief Update this track View and all its content objects.
 *
 */
void TrackView::update()
{
	m_trackContentWidget.update();
	if( !m_trackContainerView->fixedClips() )
	{
		m_trackContentWidget.changePosition();
	}
	QWidget::update();
}




/*! \brief Create a menu for assigning/creating channels for this track.
 *
 */
QMenu * TrackView::createMixerMenu(QString title, QString newMixerLabel)
{
	Q_UNUSED(title)
	Q_UNUSED(newMixerLabel)
	return nullptr;
}




/*! \brief Close this track View.
 *
 */
bool TrackView::close()
{
	m_trackContainerView->removeTrackView( this );
	return QWidget::close();
}




/*! \brief Register that the model of this track View has changed.
 *
 */
void TrackView::modelChanged()
{
	m_track = castModel<Track>();
	Q_ASSERT( m_track != nullptr );
	connect( m_track, SIGNAL(destroyedTrack()), this, SLOT(close()));
	m_trackOperationsWidget.m_muteBtn->setModel( &m_track->m_mutedModel );
	m_trackOperationsWidget.m_soloBtn->setModel( &m_track->m_soloModel );
	ModelView::modelChanged();
	setFixedHeight(scaledPixels(m_track->getHeight()));
	applySpacer();  // WF-107: a no-op for a row without a spacer
}




/*! \brief Start a drag event on this track View.
 *
 *  \param dee the DragEnterEvent to start.
 */
void TrackView::dragEnterEvent( QDragEnterEvent * dee )
{
	StringPairDrag::processDragEnterEvent( dee, "track_" +
					QString::number( static_cast<int>(m_track->type()) ) );
}




/*! \brief Accept a drop event on this track View.
 *
 *  We only accept drop events that are of the same type as this track.
 *  If so, we decode the data from the drop event by just feeding it
 *  back into the engine as a state.
 *
 *  \param de the DropEvent to handle.
 */
void TrackView::dropEvent( QDropEvent * de )
{
	QString type = StringPairDrag::decodeKey( de );
	QString value = StringPairDrag::decodeValue( de );
	if( type == ( "track_" + QString::number( static_cast<int>(m_track->type()) ) ) )
	{
		// value contains our XML-data so simply create a
		// DataFile which does the rest for us...
		DataFile dataFile( value.toUtf8() );
		Engine::audioEngine()->requestChangeInModel();
		m_track->restoreState( dataFile.content().firstChild().toElement() );
		Engine::audioEngine()->doneChangeInModel();
		de->accept();
	}
}




/*! \brief Handle a mouse press event on this track View.
 *
 *  If this track container supports rubber band selection, let the
 *  widget handle that and don't bother with any other handling.
 *
 *  If the left mouse button is pressed, we handle two things.  If
 *  SHIFT is pressed, then we resize vertically.  Otherwise we start
 *  the process of moving this track to a new position.
 *
 *  Otherwise we let the widget handle the mouse event as normal.
 *
 *  \param me the MouseEvent to handle.
 */
void TrackView::mousePressEvent( QMouseEvent * me )
{
	const auto pos = position(me);

	// If previously dragged too small, restore on shift-leftclick
	if( laneHeight() < scaledPixels(DEFAULT_TRACK_HEIGHT) &&
		me->modifiers() & Qt::ShiftModifier &&
		me->button() == Qt::LeftButton )
	{
		setFixedHeight(scaledPixels(DEFAULT_TRACK_HEIGHT) + spacerPixels());
		m_track->setHeight( DEFAULT_TRACK_HEIGHT );
	}


	int widgetTotal = ConfigManager::inst()->value( "ui",
							"compacttrackbuttons" ).toInt()==1 ?
		DEFAULT_SETTINGS_WIDGET_WIDTH_COMPACT + TRACK_OP_WIDTH_COMPACT :
		DEFAULT_SETTINGS_WIDGET_WIDTH + TRACK_OP_WIDTH;
	if (m_trackContainerView->allowRubberband() == true  && pos.x() > widgetTotal)
	{
		QWidget::mousePressEvent( me );
	}
	else if( me->button() == Qt::LeftButton )
	{
		if( me->modifiers() & Qt::ShiftModifier )
		{
			m_action = Action::Resize;
			QCursor::setPos(mapToGlobal(QPoint(pos.x(), height())));
			QApplication::setOverrideCursor(Qt::SizeVerCursor);
		}

		me->accept();
	}
	else
	{
		QWidget::mousePressEvent( me );
	}
}




/*! \brief Handle a mouse move event on this track View.
 *
 *  If this track container supports rubber band selection, let the
 *  widget handle that and don't bother with any other handling.
 *
 *  Otherwise if we've started the move process (from mousePressEvent())
 *  then move ourselves into that position, reordering the track list
 *  with moveTrackViewUp() and moveTrackViewDown() to suit.  We make a
 *  note of this in the undo journal in case the user wants to undo this
 *  move.
 *
 *  Likewise if we've started a resize process, handle this too, making
 *  sure that we never go below the minimum track height.
 *
 *  \param me the MouseEvent to handle.
 */
void TrackView::mouseMoveEvent( QMouseEvent * me )
{
	const auto pos = position(me);

	int widgetTotal = ConfigManager::inst()->value( "ui",
							"compacttrackbuttons" ).toInt()==1 ?
		DEFAULT_SETTINGS_WIDGET_WIDTH_COMPACT + TRACK_OP_WIDTH_COMPACT :
		DEFAULT_SETTINGS_WIDGET_WIDTH + TRACK_OP_WIDTH;
	if (m_trackContainerView->allowRubberband() == true && pos.x() > widgetTotal)
	{
		QWidget::mouseMoveEvent( me );
	}
	else if( m_action == Action::Move )
	{
		// look which track-widget the mouse-cursor is over
		const int yPos =
			m_trackContainerView->contentWidget()->mapFromGlobal(globalPosition(me)).y();
		const TrackView * trackAtY = m_trackContainerView->trackViewAt( yPos );

		// debug code
		//	qDebug( "y position %d", yPos );

		// a track-widget not equal to ourself?
		if( trackAtY != nullptr && trackAtY != this )
		{
			// then move us up/down there!
			if (pos.y() < 0)
			{
				m_trackContainerView->moveTrackViewUp( this );
			}
			else
			{
				m_trackContainerView->moveTrackViewDown( this );
			}
		}
	}
	else if( m_action == Action::Resize )
	{
		resizeToHeight(pos.y());
	}

	if( laneHeight() < scaledPixels(DEFAULT_TRACK_HEIGHT) )
	{
		setToolTip(m_track->m_name);
	}
}



/*! \brief Handle a mouse release event on this track View.
 *
 *  \param me the MouseEvent to handle.
 */
void TrackView::mouseReleaseEvent( QMouseEvent * me )
{
	m_action = Action::None;
	while( QApplication::overrideCursor() != nullptr )
	{
		QApplication::restoreOverrideCursor();
	}
	m_trackOperationsWidget.update();

	QWidget::mouseReleaseEvent( me );
}

void TrackView::wheelEvent(QWheelEvent* we)
{
	// Note: we add the values because one of them will be 0. If the alt modifier
	// is pressed x is non-zero and otherwise y.
	const int deltaY = we->angleDelta().x() + we->angleDelta().y();
	int const direction = deltaY < 0 ? -1 : 1;

	auto const modKeys = we->modifiers();
	int stepSize = modKeys == (Qt::ControlModifier | Qt::AltModifier) ? 1 : modKeys == (Qt::ShiftModifier | Qt::AltModifier) ? 5 : 0;

	if (stepSize != 0)
	{
		resizeToHeight(height() + stepSize * direction);
		we->accept();
		return;
	}
	we->ignore();
}




/*! \brief Repaint this track View.
 *
 *  \param pe the PaintEvent to start.
 */
void TrackView::paintEvent( QPaintEvent * pe )
{
	QStyleOption opt;
	opt.initFrom( this );
	QPainter p( this );
	style()->drawPrimitive( QStyle::PE_Widget, &opt, &p, this );
	const int spacer = spacerPixels();
	if (spacer > 0) { paintSpacer(p, QRect(0, height() - spacer, width(), spacer), TrackSpacers::instance().spacer(m_track->id()).label); }
}




/*! \brief Create a Clip View in this track View.
 *
 *  \param clip the Clip to create the view for.
 *  \todo is this a good description for what this method does?
 */
void TrackView::createClipView( Clip * clip )
{
	ClipView * tv = clip->createView( this );
	if( clip->getSelectViewOnCreate() == true )
	{
		tv->setSelected( true );
	}
	clip->selectViewOnCreate( false );
}




void TrackView::muteChanged()
{
	FadeButton * indicator = getActivityIndicator();
	if (indicator) { setIndicatorMute(indicator, m_track->m_mutedModel.value()); }
}


void TrackView::onTrackGripGrabbed()
{
	m_action = Action::Move;
}

void TrackView::onTrackGripReleased()
{
	m_action = Action::None;
}



void TrackView::setIndicatorMute(FadeButton* indicator, bool muted)
{
	indicator->setMuted(muted);
}


void TrackView::resizeToHeight(int h)
{
	// @a h is the whole row; the track keeps the part above its spacer (WF-107), set before the realign
	// that lays the row out from it.
	const int spacer = spacerPixels();
	setFixedHeight(qMax<int>(h - spacer, scaledPixels(MINIMAL_TRACK_HEIGHT)) + spacer);
	m_track->setHeight(unscaledPixels(height() - spacer));
	m_trackContainerView->realignTracks();
}


} // namespace lmms::gui
