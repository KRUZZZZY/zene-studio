/*
 * PeakController.h - peak-controller class
 *
 * Copyright (c) 2008-2009 Paul Giblock <drfaygo/at/gmail.com>
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

#ifndef LMMS_PEAK_CONTROLLER_H
#define LMMS_PEAK_CONTROLLER_H

#include <atomic>
#include <vector>
#include "Controller.h"
#include "ControllerDialog.h"

class QWidget;
//! The white-box regression that owns the coefficient-publication seam below.
class PeakControllerNullEffectTest;

namespace lmms
{


class PeakControllerEffect;

namespace gui { class PeakControllerDialog; }

using PeakControllerEffectVector = std::vector<PeakControllerEffect*>;

class LMMS_EXPORT PeakController : public Controller
{
	Q_OBJECT
public:
	PeakController( Model * _parent,
		PeakControllerEffect *_peak_effect = nullptr );


	~PeakController() override;

	void saveSettings( QDomDocument & _doc, QDomElement & _this ) override;
	void loadSettings( const QDomElement & _this ) override;
	QString nodeName() const override;

	static void initGetControllerBySetting();
	static PeakController * getControllerBySetting( const QDomElement & _this );

	static PeakControllerEffectVector s_effects;

#ifdef LMMS_TESTING
	/*! Test seam (cluster new-p20-95): \a probe runs on the thread that renders a
	 *  period, inside updateValueBuffer(), immediately after that period's attack
	 *  and decay coefficients have been computed from the models - the window in
	 *  which the GUI thread's attack/decay change lands (the models' dataChanged
	 *  slot, updateCoeffs(), asks for the coefficients again from the GUI thread).
	 *  A regression injects exactly that change there and reads what the following
	 *  period publishes. It is null in the product. */
	static void setCoeffPublishProbeForTest( void (*probe)(PeakController*) );
#endif


public slots:
	gui::ControllerDialog * createDialog( QWidget * _parent ) override;
	void handleDestroyedEffect();
	void updateCoeffs();

protected:
	// The internal per-controller get-value function
	void updateValueBuffer() override;

	PeakControllerEffect * m_peakEffect;

	friend class gui::PeakControllerDialog;
	//! White-box access for the coefficient-publication regression (cluster new-p20-95).
	friend class ::PeakControllerNullEffectTest;

private:
	float m_currentSample;
	//backward compatibility for <= 0.4.15
	static int m_getCount;
	static int m_loadCount;
	static bool m_buggedFile;
	
	float m_attackCoeff;
	float m_decayCoeff;
	//! Set by updateCoeffs() on whichever thread owns the attack/decay models and
	//! consumed by updateValueBuffer() on the render thread: an atomic handoff, claimed
	//! before the coefficients are read so a change that lands while they are computed
	//! is not lost (cluster new-p20-95).
	std::atomic<bool> m_coeffNeedsUpdate{ true };
} ;

namespace gui
{

class PeakControllerDialog : public ControllerDialog
{
	Q_OBJECT
public:
	PeakControllerDialog( Controller * _controller, QWidget * _parent );
	~PeakControllerDialog() override = default;

protected:
	void contextMenuEvent( QContextMenuEvent * _me ) override;
	void paintEvent( QPaintEvent * _pe ) override;
	void modelChanged() override;

	PeakController * m_peakController;

};


} // namespace gui

} // namespace lmms

#endif // LMMS_PEAK_CONTROLLER_H
