/*
 * delaycontrolsdialog.cpp - definition of DelayControlsDialog class.
 *
 * Copyright (c) 2014 David French <dave/dot/french3/at/googlemail/dot/com>
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

#include "AutomatableModel.h"
#include "DelayControlsDialog.h"
#include "DelayControls.h"
#include "DeprecationHelper.h"
#include "embed.h"
#include "TempoSyncKnob.h"
#include "../Eq/EqFader.h"
#include <QMouseEvent>
#include <QPainter>

namespace lmms::gui
{


DelayControlsDialog::DelayControlsDialog( DelayControls *controls ) :
	EffectControlDialog( controls )
{
	setAutoFillBackground( true );
	QPalette pal;
	pal.setBrush( backgroundRole(), PLUGIN_NAME::getIconPixmap( "artwork" ) );
	setPalette( pal );
	setFixedSize( 300, 208 );

	auto sampleDelayKnob = new TempoSyncKnob(KnobType::Bright26, tr("DELAY"), this, Knob::LabelRendering::LegacyFixedFontSize);
	sampleDelayKnob->move( 10,14 );
	sampleDelayKnob->setModel( &controls->m_delayTimeModel );
	sampleDelayKnob->setHintText(tr("Delay time:"), " s");

	auto feedbackKnob = new VolumeKnob(KnobType::Bright26, tr("FDBK"), this, Knob::LabelRendering::LegacyFixedFontSize);
	feedbackKnob->move( 11, 58 );
	feedbackKnob->setZeroDbfsPoint(1.f);
	feedbackKnob->setModel( &controls->m_feedbackModel);
	feedbackKnob->setHintText(tr("Feedback amount:"), "");

	auto lfoPeriodKnob = new TempoSyncKnob(KnobType::Bright26, tr("TIME"), this, Knob::LabelRendering::LegacyFixedFontSize);
	lfoPeriodKnob->move( 11, 119 );
	lfoPeriodKnob->setModel( &controls->m_lfoTimeModel );
	lfoPeriodKnob->setHintText(tr("LFO period:"), " s");

	auto lfoAmtKnob = new TempoSyncKnob(KnobType::Bright26, tr("AMNT"), this, Knob::LabelRendering::LegacyFixedFontSize);
	lfoAmtKnob->move( 11, 159 );
	lfoAmtKnob->setModel( &controls->m_lfoAmountModel );
	lfoAmtKnob->setHintText(tr("LFO amount:"), " s");

	auto outFader
		= new EqFader(&controls->m_outGainModel, tr("Out gain"), this, &controls->m_outPeakL, &controls->m_outPeakR);
	outFader->setMaximumHeight( 196 );
	outFader->move( 263, 45 );
	outFader->setDisplayConversion( false );
	outFader->setHintText(tr("Gain:"), " dBFS");

	auto pad = new XyPad(this, &controls->m_feedbackModel, &controls->m_delayTimeModel);
	pad->resize( 200, 200 );
	pad->move( 50, 5 );
}




XyPad::XyPad(QWidget *parent, FloatModel *xModel, FloatModel *yModel) :
	QWidget( parent ),
	m_xModel( xModel ),
	m_yModel( yModel ),
	m_acceptInput( false )
{
	if (m_xModel) { connect( m_xModel, SIGNAL( dataChanged() ) , this, SLOT( update() ) ); }
	if (m_yModel) { connect( m_yModel, SIGNAL( dataChanged() ) , this, SLOT( update() ) ); }
}




void XyPad::paintEvent(QPaintEvent *event)
{
	if (width() <= 0 || height() <= 0 || !m_xModel || !m_yModel) { return; }

	QPainter painter( this );
		//Draw Frequecy maker lines
	painter.setPen( QPen( QColor( 200, 200, 200, 200 ), 8, Qt::SolidLine, Qt::RoundCap, Qt::BevelJoin ) );
	painter.setRenderHint( QPainter::Antialiasing, true );

	const int xPos = xyPadCoordinates::pixelForValue(
		m_xModel->value(), m_xModel->minValue(), m_xModel->maxValue(), width());
	const int yPos = xyPadCoordinates::pixelForValue(
		m_yModel->value(), m_yModel->minValue(), m_yModel->maxValue(), height());

	painter.drawPoint( xPos, yPos );
}




void XyPad::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
	{
		m_acceptInput = true;
	}
}




void XyPad::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
	{
		m_acceptInput = false;
	}
}




void XyPad::mouseMoveEvent(QMouseEvent *event)
{
	if (width() <= 0 || height() <= 0 || !m_xModel || !m_yModel) { return; }

	const auto pos = position(event);

	if (m_acceptInput && (pos.x() >= 0) && (pos.x() < width())
		&& (pos.y() >= 0) && (pos.y() < height()))
	{
		//set xmodel
		m_xModel->setValue(xyPadCoordinates::valueForPixel(
			pos.x(), m_xModel->minValue(), m_xModel->maxValue(), width()));

		//set ymodel
		m_yModel->setValue(xyPadCoordinates::valueForPixel(
			pos.y(), m_yModel->minValue(), m_yModel->maxValue(), height()));
	}
}


} // namespace lmms::gui
