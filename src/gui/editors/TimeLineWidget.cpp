/*
 * TimeLineWidget.cpp - class timeLine, representing a time-line with position marker
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

#include "TimeLineWidget.h"

#include <algorithm>
#include <cstdlib>
#include <memory>

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolBar>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "DeprecationHelper.h"
#include "embed.h"
#include "KeyboardShortcuts.h"
#include "NStateButton.h"
#include "TextFloat.h"

namespace lmms::gui
{

namespace
{
	constexpr int MIN_BAR_LABEL_DISTANCE = 35;
	//! 0.5.0 L03 (Q050-147): how wide a punch handle is, and how tall its band. The band is a
	//! third of the ruler; the loop brace keeps the room above it.
	constexpr int PUNCH_HANDLE_WIDTH = 5;
	constexpr int PUNCH_MIN_BAND_HEIGHT = 4;
	constexpr int PUNCH_MAX_BAND_HEIGHT = 14;
	constexpr int PUNCH_BAND_GAP = 2;
}

TimeLineWidget::TimeLineWidget(const int xoff, const int yoff, const float ppb, Timeline& timeline,
		const TimePos& begin, QWidget* parent) :
	QWidget{parent},
	m_xOffset{xoff},
	m_ppb{ppb},
	m_timeline{&timeline},
	m_begin{begin}
{
	move( 0, yoff );

	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	setAccessibleName(tr("Time ruler"));
	setAccessibleDescription(tr("Press Menu or Shift+F10 for the loop actions at the play position"));

	connect( Engine::getSong(), SIGNAL(timeSignatureChanged(int,int)),
					this, SLOT(update()));
	connect(m_timeline, &Timeline::positionChanged, this, qOverload<>(&QWidget::update));
	connect(m_timeline, &Timeline::punchChanged, this, qOverload<>(&QWidget::update));
}




TimeLineWidget::~TimeLineWidget()
{
	delete m_hint;
}



void TimeLineWidget::setXOffset(const int x)
{
	m_xOffset = x;
}

void TimeLineWidget::addToolButtons( QToolBar * _tool_bar )
{
	auto autoScroll = new NStateButton(_tool_bar);
	autoScroll->setGeneralToolTip(tr("Auto scrolling"));
	autoScroll->addState(embed::getIconPixmap("autoscroll_stepped_on"), tr("Stepped auto scrolling"));
	autoScroll->addState(embed::getIconPixmap("autoscroll_continuous_on"), tr("Continuous auto scrolling"));
	autoScroll->addState(embed::getIconPixmap("autoscroll_off"), tr("Auto scrolling disabled"));
	autoScroll->changeState(static_cast<int>(m_autoScroll));
	connect( autoScroll, SIGNAL(changedState(int)), this,
					SLOT(toggleAutoScroll(int)));

	auto loopPoints = new NStateButton(_tool_bar);
	loopPoints->setGeneralToolTip( tr( "Loop points" ) );
	loopPoints->addState( embed::getIconPixmap( "loop_points_off" ) );
	loopPoints->addState( embed::getIconPixmap( "loop_points_on" ) );
	connect(loopPoints, &NStateButton::changedState, m_timeline, &Timeline::setLoopEnabled);
	connect(m_timeline, &Timeline::loopEnabledChanged, loopPoints, &NStateButton::changeState);
	connect(m_timeline, &Timeline::loopEnabledChanged, this, static_cast<void (QWidget::*)()>(&QWidget::update));
	loopPoints->changeState(static_cast<int>(m_timeline->loopEnabled()));

	auto behaviourAtStop = new NStateButton(_tool_bar);
	behaviourAtStop->addState( embed::getIconPixmap( "back_to_zero" ),
					tr( "After stopping go back to beginning" )
									);
	behaviourAtStop->addState( embed::getIconPixmap( "back_to_start" ),
					tr( "After stopping go back to "
						"position at which playing was "
						"started" ) );
	behaviourAtStop->addState( embed::getIconPixmap( "keep_stop_position" ),
					tr( "After stopping keep position" ) );
	connect(behaviourAtStop, &NStateButton::changedState, m_timeline,
		[timeline = m_timeline](int value) {
			timeline->setStopBehaviour(static_cast<Timeline::StopBehaviour>(value));
		}
	);
	connect(m_timeline, &Timeline::stopBehaviourChanged, behaviourAtStop,
		[button = behaviourAtStop](Timeline::StopBehaviour value) {
			button->changeState(static_cast<int>(value));
		}
	);
	behaviourAtStop->changeState(static_cast<int>(m_timeline->stopBehaviour()));

	_tool_bar->addWidget( autoScroll );
	_tool_bar->addWidget( loopPoints );
	_tool_bar->addWidget( behaviourAtStop );
}

void TimeLineWidget::toggleAutoScroll( int _n )
{
	m_autoScroll = static_cast<AutoScrollState>( _n );
}

void TimeLineWidget::paintEvent( QPaintEvent * )
{
	QPainter p( this );

	// Draw background
	p.fillRect( 0, 0, width(), height(), p.background() );

	// Clip so that we only draw everything starting from the offset
	p.setClipRect(m_xOffset, 0, width() - m_xOffset, height());

	// Variables for the loop rectangle
	int const loopRectMargin = getLoopRectangleVerticalPadding();
	// 0.5.0 L03 (Q050-147): the punch range has its own band under the loop brace, so the two
	// ranges never share a row and a handle cannot be mistaken for the other range's.
	const QRect punchBand = punchBandRect();
	int const punchRoom = punchBand.isEmpty() ? 0 : punchBand.height() + PUNCH_BAND_GAP;
	int const loopRectHeight = this->height() - 2 * loopRectMargin - punchRoom;
	int const loopStart = markerX(m_timeline->loopBegin());
	int const loopEndR = markerX(m_timeline->loopEnd());
	int const loopRectWidth = loopEndR - loopStart;

	bool const loopPointsActive = m_timeline->loopEnabled();

	// Draw the main loop rectangle (inner fill only)
	QRect outerRectangle( loopStart, loopRectMargin, loopRectWidth - 1, loopRectHeight - 1 );
	p.fillRect( outerRectangle, loopPointsActive ? getActiveLoopBrush() : getInactiveLoopBrush());

	// Draw the bar lines and numbers
	// Activate hinting on the font
	QFont font = p.font();
	font.setHintingPreference( QFont::PreferFullHinting );
	p.setFont(font);
	int const fontAscent = p.fontMetrics().ascent();
	int const fontHeight = p.fontMetrics().height();

	QColor const & barLineColor = getBarLineColor();
	QColor const & barNumberColor = getBarNumberColor();

	bar_t barNumber = m_begin.getBar();
	int const x = m_xOffset - ((static_cast<int>(m_begin * m_ppb) / TimePos::ticksPerBar()) % static_cast<int>(m_ppb));

	// Double the interval between bar numbers until they are far enough apart. 0.5.0 theme 9: far enough is also
	// the widest number on screen and a gap, so a long song's numbers (or the larger type of the interface zoom)
	// never run into each other.
	const bar_t lastBar = m_begin.getBar() + static_cast<bar_t>(width() / std::max(m_ppb, 1.f)) + 1;
	const int labelRoom = std::max(MIN_BAR_LABEL_DISTANCE, 5 + p.fontMetrics().horizontalAdvance(QString::number(lastBar)) + 5);
	int barLabelInterval = 1;
	while (barLabelInterval * m_ppb < labelRoom) { barLabelInterval *= 2; }

	for( int i = 0; x + i * m_ppb < width(); ++i )
	{
		++barNumber;
		if ((barNumber - 1) % barLabelInterval == 0)
		{
			const int cx = x + qRound( i * m_ppb );
			p.setPen( barLineColor );
			p.drawLine( cx, 5, cx, height() - 6 );

			const QString s = QString::number( barNumber );
			p.setPen( barNumberColor );
			p.drawText( cx + 5, ((height() - fontHeight) / 2) + fontAscent, s );
		}
	}

	// Draw the loop rectangle's outer outline
	p.setPen( loopPointsActive ? getActiveLoopColor() : getInactiveLoopColor() );
	p.setBrush( Qt::NoBrush );
	p.drawRect( outerRectangle );

	// Draw the loop rectangle's inner outline
	QRect innerRectangle = outerRectangle.adjusted( 1, 1, -1, -1 );
	p.setPen( loopPointsActive ? getActiveLoopInnerColor() : getInactiveLoopInnerColor() );
	p.setBrush( Qt::NoBrush );
	p.drawRect( innerRectangle );
	
	// 0.5.0 L03 (Q050-147): the punch band. It is drawn apart from the loop brace, filled when
	// armed and only outlined when disarmed (the bounds stay for another pass), with a handle
	// at each end. A short range keeps the handles inside the band.
	if (!punchBand.isEmpty())
	{
		const bool armed = m_timeline->punchArmed();
		QColor fill = armed ? QColor(198, 84, 62, 190) : QColor(198, 84, 62, 60);
		QColor edge = armed ? QColor(236, 132, 104) : QColor(198, 84, 62, 180);
		p.fillRect(punchBand, fill);
		p.setPen(edge);
		p.setBrush(Qt::NoBrush);
		p.drawRect(punchBand.adjusted(0, 0, -1, -1));

		const QRect beginHandle = punchBeginHandleRect();
		const QRect endHandle = punchEndHandleRect();
		p.fillRect(beginHandle, edge);
		p.fillRect(endHandle, edge);

		p.setPen(armed ? QColor(255, 240, 235) : QColor(236, 200, 190));
		QFont bandFont = p.font();
		bandFont.setPointSizeF(std::max(6.0, bandFont.pointSizeF() - 2.0));
		bandFont.setBold(armed);
		p.setFont(bandFont);
		const QRect textRect(punchBand.left() + 4, punchBand.top(), std::max(0, punchBand.width() - 8),
			punchBand.height());
		p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, armed ? tr("PUNCH") : tr("punch"));
		p.setFont(font);
	}

	// Draw loop handles if necessary
	const auto handleMode = ConfigManager::inst()->value("app", "loopmarkermode") == "handles";
	if (handleMode && underMouse() && QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
	{
		const auto handleWidth = std::min(m_loopHandleWidth, loopRectWidth / 2 - 1);
		const auto leftHandle = QRectF(loopStart - .5, loopRectMargin - .5, handleWidth, loopRectHeight);
		const auto rightHandle = QRectF(loopEndR - handleWidth - .5, loopRectMargin - .5, handleWidth, loopRectHeight);
		const auto color = loopPointsActive ? m_activeLoopHandleColor : m_inactiveLoopHandleColor;

		p.fillRect(leftHandle, color);
		p.fillRect(rightHandle, color);
	}

	const QPixmap& marker = !m_isRecording ? m_posMarkerPixmap : m_recordingPosMarkerPixmap;

	// Only draw the position marker if the position line is in view
	if (m_isPlayheadVisible && markerX(m_timeline->pos()) >= m_xOffset && markerX(m_timeline->pos()) < width() - marker.width() / 2)
	{
		// Let the position marker extrude to the left
		p.setClipping(false);
		p.setOpacity(0.6);
		p.drawPixmap(markerX(m_timeline->pos()) - (marker.width() / 2),
			height() - marker.height(), marker);
	}
}

bool TimeLineWidget::hasPunchRange() const
{
	return m_timeline->punchEnd() > m_timeline->punchBegin();
}

QRect TimeLineWidget::punchBandRect() const
{
	if (!hasPunchRange()) { return {}; }
	// A short ruler still leaves the loop brace a row above the band: the band never takes the
	// whole height, even when the widget has not been laid out to its usual size yet.
	const int roomForLoop = std::max(2, height() - 2 - 4);
	const int bandHeight = std::min(std::clamp(height() / 3, PUNCH_MIN_BAND_HEIGHT, PUNCH_MAX_BAND_HEIGHT), roomForLoop);
	return QRect(m_xOffset, height() - bandHeight, std::max(0, width() - m_xOffset), bandHeight);
}

QRect TimeLineWidget::punchBeginHandleRect() const
{
	const QRect band = punchBandRect();
	if (band.isEmpty()) { return {}; }
	return QRect(markerX(TimePos(m_timeline->punchBegin())) - PUNCH_HANDLE_WIDTH / 2, band.top(),
		PUNCH_HANDLE_WIDTH, band.height());
}

QRect TimeLineWidget::punchEndHandleRect() const
{
	const QRect band = punchBandRect();
	if (band.isEmpty()) { return {}; }
	return QRect(markerX(TimePos(m_timeline->punchEnd())) - PUNCH_HANDLE_WIDTH / 2, band.top(),
		PUNCH_HANDLE_WIDTH, band.height());
}

auto TimeLineWidget::getClickedTime(const int xPosition) const -> TimePos
{
	// How far into the timeline we clicked, measuring pixels from the leftmost part of the editor
	const auto pixelDelta = std::max(xPosition - m_xOffset, 0);
	return m_begin + static_cast<int>(pixelDelta * TimePos::ticksPerBar() / m_ppb);
}

auto TimeLineWidget::getLoopAction(QMouseEvent* event) const -> TimeLineWidget::Action
{
	const auto pos = position(event);

	const auto mode = ConfigManager::inst()->value("app", "loopmarkermode");
	const auto xPos = pos.x();
	const auto button = event->button();

	if (mode == "handles")
	{
		// Loop start and end pos, or closest edge of screen if loop extends off it
		const auto leftMost = std::max(markerX(m_timeline->loopBegin()), m_xOffset);
		const auto rightMost = std::min(markerX(m_timeline->loopEnd()), width());
		// Distance from click to handle, positive aimed towards center of loop
		const auto deltaLeft = xPos - leftMost;
		const auto deltaRight = rightMost - xPos;

		if (deltaLeft < 0 || deltaRight < 0) { return Action::NoAction; } // Clicked outside loop
		else if (deltaLeft <= m_loopHandleWidth && deltaLeft < deltaRight) { return Action::MoveLoopBegin; }
		else if (deltaRight <= m_loopHandleWidth) { return Action::MoveLoopEnd; }
		else { return Action::MoveLoop; }
	}
	else if (mode == "closest")
	{
		const TimePos loopMid = (m_timeline->loopBegin() + m_timeline->loopEnd()) / 2;
		return getClickedTime(xPos) < loopMid ? Action::MoveLoopBegin : Action::MoveLoopEnd;
	}
	else // Default to dual-button mode
	{
		if (button == Qt::LeftButton) { return Action::MoveLoopBegin; }
		else if (button == Qt::RightButton) { return Action::MoveLoopEnd; }
		return Action::NoAction;
	}
}

auto TimeLineWidget::getPunchAction(QMouseEvent* event) const -> TimeLineWidget::Action
{
	const QRect band = punchBandRect();
	if (band.isEmpty() || !band.contains(position(event))) { return Action::NoAction; }
	if (event->button() != Qt::LeftButton) { return Action::NoAction; }

	const int x = position(event).x();
	const int beginX = markerX(TimePos(m_timeline->punchBegin()));
	const int endX = markerX(TimePos(m_timeline->punchEnd()));
	// The handles are always visible, so the nearest one wins even when the range is short.
	if (std::abs(x - beginX) <= PUNCH_HANDLE_WIDTH) { return Action::MovePunchBegin; }
	if (std::abs(x - endX) <= PUNCH_HANDLE_WIDTH) { return Action::MovePunchEnd; }
	if (x > beginX && x < endX) { return Action::MovePunch; }
	return Action::NoAction;
}

auto TimeLineWidget::actionCursor(Action action) const -> QCursor
{
	switch (action) {
		case Action::MoveLoop: return Qt::SizeHorCursor;
		case Action::MoveLoopBegin: return m_cursorSelectLeft;
		case Action::MoveLoopEnd: return m_cursorSelectRight;
		case Action::MovePunchBegin: return m_cursorSelectLeft;
		case Action::MovePunchEnd: return m_cursorSelectRight;
		case Action::MovePunch: return Qt::SizeHorCursor;
		// Fall back to normal cursor if no action or action cursor not specified
		default: return Qt::ArrowCursor;
	}
}

void TimeLineWidget::mousePressEvent(QMouseEvent* event)
{
	const auto pos = position(event);

	if (pos.x() < m_xOffset) { return; }

	const auto shift = event->modifiers() & Qt::ShiftModifier;
	const auto ctrl = event->modifiers() & Qt::ControlModifier;

	if (shift) // loop marker manipulation
	{
		m_action = getLoopAction(event);
		setCursor(actionCursor(m_action));

		if (m_action == Action::MoveLoop)
		{
			m_dragStartPos = getClickedTime(pos.x());
			m_oldLoopPos = {m_timeline->loopBegin(), m_timeline->loopEnd()};
		}
	}
	else if (event->button() == Qt::LeftButton && ctrl) // selection
	{
		m_action = Action::SelectSongClip;
		m_initalXSelect = pos.x();
	}
	else if (event->button() == Qt::LeftButton) // 0.5.0 L03: the punch band's own handles first
	{
		m_action = getPunchAction(event);
		if (m_action == Action::MovePunchBegin || m_action == Action::MovePunchEnd || m_action == Action::MovePunch)
		{
			setCursor(actionCursor(m_action));
			m_punchDragStart = getClickedTime(pos.x()).getTicks();
			m_oldPunchBegin = m_timeline->punchBegin();
			m_oldPunchEnd = m_timeline->punchEnd();
			m_punchDragged = false;
		}
		else
		{
			m_action = Action::MovePositionMarker;
		}
	}

	if (m_action == Action::MoveLoopBegin || m_action == Action::MoveLoopEnd)
	{
		delete m_hint;
		m_hint = TextFloat::displayMessage(tr("Hint"),
			tr("Press <%1> to disable magnetic loop points.").arg(UI_CTRL_KEY),
			embed::getIconPixmap("hint"), 0);
	}

	setContextMenuPolicy(m_action == Action::NoAction ? Qt::DefaultContextMenu : Qt::PreventContextMenu);

	mouseMoveEvent(event);
}

void TimeLineWidget::mouseMoveEvent( QMouseEvent* event )
{
	parentWidget()->update(); // essential for widgets that this timeline had taken their mouse move event from.

	const auto pos = position(event);

	auto timeAtCursor = getClickedTime(pos.x());
	const auto control = event->modifiers() & Qt::ControlModifier;

	switch( m_action )
	{
		case Action::MovePositionMarker:
			m_timeline->setTicks(timeAtCursor.getTicks());
			if (!( Engine::getSong()->isPlaying()))
			{
				//Song::PlayMode::None is used when nothing is being played.
				Engine::getSong()->getTimeline(Song::PlayMode::None).setTicks(timeAtCursor.getTicks());
			}
			update();
			break;

		case Action::MoveLoopBegin:
		case Action::MoveLoopEnd:
		{
			const auto otherPoint = m_action == Action::MoveLoopBegin
				? m_timeline->loopEnd()
				: m_timeline->loopBegin();
			if (control)
			{
				// no ctrl-press-hint when having ctrl pressed
				delete m_hint;
				m_hint = nullptr;
			}
			else
			{
				timeAtCursor = timeAtCursor.quantize(m_snapSize);
			}
			// Catch begin == end
			if (timeAtCursor == otherPoint)
			{
				const int offset = control ? 1 : m_snapSize * TimePos::ticksPerBar();
				if (m_action == Action::MoveLoopBegin) { timeAtCursor -= offset; }
				else { timeAtCursor += offset; }
			}
			// Update m_action so we still move the correct point even if it is
			// dragged past the other.
			m_action = timeAtCursor < otherPoint ? Action::MoveLoopBegin : Action::MoveLoopEnd;
			m_timeline->setLoopPoints(timeAtCursor, otherPoint);
			update();
			break;
		}
		case Action::MoveLoop:
		{
			const TimePos dragDelta = timeAtCursor - m_dragStartPos;
			auto loopPos = m_oldLoopPos;
			for (auto& point : loopPos)
			{
				point += dragDelta;
				if (!control) { point = point.quantize(m_snapSize); }
			}
			m_timeline->setLoopPoints(loopPos[0], loopPos[1]);
			break;
		}
		case Action::MovePunchBegin:
		case Action::MovePunchEnd:
		{
			const auto otherPoint = m_action == Action::MovePunchBegin
				? TimePos(m_timeline->punchEnd())
				: TimePos(m_timeline->punchBegin());
			if (!control) { timeAtCursor = timeAtCursor.quantize(m_snapSize); }
			// Keep at least one tick: a half-open region of no ticks captures nothing.
			if (timeAtCursor == otherPoint)
			{
				const tick_t offset = control ? 1 : std::max<tick_t>(1, static_cast<tick_t>(m_snapSize * TimePos::ticksPerBar()));
				if (m_action == Action::MovePunchBegin) { timeAtCursor = TimePos(std::max<tick_t>(0, timeAtCursor.getTicks() - offset)); }
				else { timeAtCursor = TimePos(timeAtCursor.getTicks() + offset); }
			}
			m_action = timeAtCursor.getTicks() < otherPoint.getTicks() ? Action::MovePunchBegin : Action::MovePunchEnd;
			m_timeline->setPunchRange(timeAtCursor.getTicks(), otherPoint.getTicks());
			m_punchDragged = true;
			update();
			break;
		}
		case Action::MovePunch:
		{
			tick_t delta = timeAtCursor.getTicks() - m_punchDragStart;
			if (!control)
			{
				const tick_t snapped = TimePos(m_oldPunchBegin + delta).quantize(m_snapSize).getTicks();
				delta = snapped - m_oldPunchBegin;
			}
			tick_t begin = m_oldPunchBegin + delta;
			tick_t end = m_oldPunchEnd + delta;
			if (begin < 0) { end -= begin; begin = 0; }
			if (end > MaxSongLength) { begin -= end - MaxSongLength; end = MaxSongLength; }
			begin = std::clamp<tick_t>(begin, 0, MaxSongLength);
			end = std::clamp<tick_t>(end, begin + 1, MaxSongLength);
			m_timeline->setPunchRange(begin, end);
			m_punchDragged = true;
			update();
			break;
		}
		case Action::SelectSongClip:
			emit regionSelectedFromPixels(m_initalXSelect, pos.x());
			break;

		default:
			break;
	}

	if (event->buttons() == Qt::NoButton)
	{
		setCursor(QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier)
			? actionCursor(getLoopAction(event))
			: Qt::ArrowCursor
		);
	}
}

void TimeLineWidget::mouseReleaseEvent( QMouseEvent* event )
{
	delete m_hint;
	m_hint = nullptr;
	if (m_action == Action::MovePunchBegin || m_action == Action::MovePunchEnd || m_action == Action::MovePunch)
	{
		commitPunchDrag();
	}
	if ( m_action == Action::SelectSongClip ) { emit selectionFinished(); }
	m_action = Action::NoAction;
}

void TimeLineWidget::commitPunchDrag()
{
	if (!m_punchDragged) { return; }
	m_punchDragged = false;
	const tick_t begin = m_timeline->punchBegin();
	const tick_t end = m_timeline->punchEnd();
	if (end <= begin) { return; }
	// The drag previewed straight onto the timeline (so the band followed the pointer). Put the
	// range it started with back FIRST: transport.punch_set takes its checkpoint from what it
	// sees, and that checkpoint is what makes the whole drag one undo step.
	m_timeline->setPunchRange(m_oldPunchBegin, m_oldPunchEnd);
	invokePunchSet(begin, end, m_timeline->punchEnabled());
	update();
}

void TimeLineWidget::invokePunchSet(const tick_t begin, const tick_t end, const bool enabled)
{
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("transport.punch_set"),
		{{QStringLiteral("start"), static_cast<int>(begin)},
		 {QStringLiteral("end"), static_cast<int>(end)},
		 {QStringLiteral("enabled"), enabled}});
	if (!result.ok)
	{
		// A refused range (the model's own validation) leaves the timeline as it was.
		m_timeline->setPunchRange(m_oldPunchBegin, m_oldPunchEnd);
	}
}

QMenu* TimeLineWidget::buildLoopMenu(const TimePos& at, bool snap)
{
	auto* menu = new QMenu(this);
	menu->setAccessibleName(tr("Ruler actions"));
	menu->addAction(tr("Set loop begin here"), [this, at, snap] {
		// The mouse menu has always honoured Ctrl held at the moment the action fires, not at
		// the moment the menu opened; the keyboard path uses the same rule (Q050-322).
		auto begin = (snap && !QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier))
			? at.quantize(m_snapSize) : at;
		const auto end = m_timeline->loopEnd();
		if (begin == end) { m_timeline->setLoopEnd(end + m_snapSize * TimePos::ticksPerBar()); }
		m_timeline->setLoopBegin(begin);
		update();
	});
	menu->addAction(tr("Set loop end here"), [this, at, snap] {
		const auto begin = m_timeline->loopBegin();
		auto end = (snap && !QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier))
			? at.quantize(m_snapSize) : at;
		if (begin == end) { m_timeline->setLoopBegin(begin - m_snapSize * TimePos::ticksPerBar()); }
		m_timeline->setLoopEnd(end);
		update();
	});

	menu->addSeparator();

	// 0.5.0 L03 (Q050-147): the punch range's own edits. They go through the transport commands,
	// the same path the toolbar arm control and an agent use, so each is one undo step.
	menu->addAction(tr("Set punch in here"), [this, at, snap] {
		m_oldPunchBegin = m_timeline->punchBegin();
		m_oldPunchEnd = m_timeline->punchEnd();
		auto begin = at.getTicks();
		if (snap && !QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) { begin = TimePos(begin).quantize(m_snapSize).getTicks(); }
		const tick_t end = std::max<tick_t>(begin + 1, m_timeline->punchEnd());
		invokePunchSet(begin, end, true);
		update();
	});
	menu->addAction(tr("Set punch out here"), [this, at, snap] {
		m_oldPunchBegin = m_timeline->punchBegin();
		m_oldPunchEnd = m_timeline->punchEnd();
		auto end = at.getTicks();
		if (snap && !QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) { end = TimePos(end).quantize(m_snapSize).getTicks(); }
		const tick_t begin = std::min<tick_t>(std::max<tick_t>(0, end - 1), m_timeline->punchBegin());
		if (end <= begin) { return; }
		invokePunchSet(begin, end, true);
		update();
	});
	const auto armPunch = menu->addAction(tr("Arm the punch range"), [this] {
		const tick_t begin = m_timeline->punchBegin();
		const tick_t end = m_timeline->punchEnd();
		if (end <= begin) { return; }
		m_oldPunchBegin = begin;
		m_oldPunchEnd = end;
		invokePunchSet(begin, end, true);
		update();
	});
	armPunch->setEnabled(m_timeline->punchEnd() > m_timeline->punchBegin() && !m_timeline->punchEnabled());
	const auto clearPunch = menu->addAction(tr("Clear the punch range"), [this] {
		ControlRegistry::instance()->invoke(QStringLiteral("transport.punch_clear"), {});
		update();
	});
	clearPunch->setEnabled(m_timeline->shouldPersistPunch());

	menu->addSeparator();

	const auto loopMenu = menu->addMenu(tr("Loop edit mode (hold shift)"));
	const auto loopMode = ConfigManager::inst()->value("app", "loopmarkermode", "dual");
	const auto addLoopModeAction = [loopMenu, &loopMode](QString text, QString mode) {
		const auto action = loopMenu->addAction(text, [mode] {
			ConfigManager::inst()->setValue("app", "loopmarkermode", mode);
		});
		action->setCheckable(true);
		if (loopMode == mode) { action->setChecked(true); }
	};
	addLoopModeAction(tr("Dual-button"), "dual");
	addLoopModeAction(tr("Grab closest"), "closest");
	addLoopModeAction(tr("Handles"), "handles");
	return menu;
}

void TimeLineWidget::contextMenuEvent(QContextMenuEvent* event)
{
	if (event->x() < m_xOffset) { return; }
	std::unique_ptr<QMenu> menu(buildLoopMenu(getClickedTime(event->x()), true));
	menu->exec(event->globalPos());
}

bool TimeLineWidget::isMenuKey(int key, Qt::KeyboardModifiers modifiers)
{
	return key == Qt::Key_Menu || (key == Qt::Key_F10 && modifiers.testFlag(Qt::ShiftModifier));
}

void TimeLineWidget::openRulerMenu()
{
	QMenu* menu = buildLoopMenu(m_timeline->pos(), true);
	menu->setAttribute(Qt::WA_DeleteOnClose);
	menu->popup(mapToGlobal(QPoint(std::max(m_xOffset, markerX(m_timeline->pos())), height())));
}

void TimeLineWidget::keyPressEvent(QKeyEvent* event)
{
	if (isMenuKey(event->key(), event->modifiers()))
	{
		openRulerMenu();
		event->accept();
		return;
	}
	QWidget::keyPressEvent(event);
}


TimeLineWidget::AutoScrollState TimeLineWidget::defaultAutoScrollState()
{
	QString autoScrollState = ConfigManager::inst()->value("ui", "autoscroll");
	if (autoScrollState == AutoScrollSteppedString) { return AutoScrollState::Stepped; }
	else if (autoScrollState == AutoScrollContinuousString) { return AutoScrollState::Continuous; }
	else if (autoScrollState == AutoScrollDisabledString) { return AutoScrollState::Disabled; }
	else { return AutoScrollState::Stepped; }
}


} // namespace lmms::gui
