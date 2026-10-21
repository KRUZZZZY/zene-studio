/* eqfader.h - defination of EqFader class.
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

#ifndef EQFADER_H
#define EQFADER_H
#include <atomic>

#include <QList>
#include <QWidget>

#include "EffectControls.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "TextFloat.h"

namespace lmms::gui
{


class EqFader : public Fader
{

public:
	Q_OBJECT
public:
	// cluster new-p9-122: the peak fields are the plugin controls' atomics -
	// the audio thread publishes maxima with relaxed stores while this widget
	// runs on the GUI timer, so the raw-float handoff raced and the old
	// read-then-clear could drop a peak written in between.
	EqFader( FloatModel * model, const QString & name, QWidget * parent,  std::atomic<float>* lPeak, std::atomic<float>* rPeak ) :
		Fader(model, name, parent, false)
	{
		setMinimumSize( 23, 116 );
		setMaximumSize( 23, 116 );
		resize( 23, 116 );
		m_lPeak = lPeak;
		m_rPeak = rPeak;
		connect( getGUI()->mainWindow(), SIGNAL( periodicUpdate() ), this, SLOT( updateVuMeters() ) );
		m_model = model;
		setPeak_L( 0 );
		setPeak_R( 0 );
	}



	~EqFader() override = default;


private slots:

	void updateVuMeters()
	{
		const float opl = getPeak_L();
		const float opr = getPeak_R();
		const float fallOff = 1.07f;
		// Take the peak and clear the field in one atomic step: the old
		// plain read followed by a plain clear raced with the audio thread
		// and could republish or lose a peak written in between.
		const float l = m_lPeak->exchange(0.0f, std::memory_order_relaxed);
		if( l > opl )
		{
			setPeak_L( l );
		}
		else
		{
			setPeak_L( opl/fallOff );
		}

		const float r = m_rPeak->exchange(0.0f, std::memory_order_relaxed);
		if( r > opr )
		{
			setPeak_R( r );
		}
		else
		{
			setPeak_R( opr/fallOff );
		}
		update();
	}




private:
	std::atomic<float>* m_lPeak;
	std::atomic<float>* m_rPeak;
	FloatModel* m_model;

};


} // namespace lmms::gui

#endif // EQFADER_H
