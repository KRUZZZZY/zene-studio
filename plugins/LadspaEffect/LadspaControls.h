/*
 * LadspaControls.h - model for LADSPA plugin controls
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

#ifndef LADSPA_CONTROLS_H
#define LADSPA_CONTROLS_H

#include "EffectControls.h"
#include "LadspaBase.h"
#include "LadspaMatrixControlDialog.h"

namespace lmms
{


class LadspaControl;
using control_list_t = QVector<LadspaControl*>;
namespace gui {
class LadspaControlDialog;
}
class LadspaEffect;


class LadspaControls : public EffectControls
{
	Q_OBJECT
public:
	LadspaControls( LadspaEffect * _eff );
	~LadspaControls() override;

	inline int controlCount() override
	{
		return m_controlCount;
	}

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;
	inline QString nodeName() const override
	{
		return "ladspacontrols";
	}

	gui::EffectControlDialog* createView() override
	{
		return new gui::LadspaMatrixControlDialog( this );
	}

#ifdef LMMS_TESTING
	//! Test seam: reports a port the global stereo link walks over. Installed with
	//! setLinkPortVisitor() so a test can drive the link over a plugin with more
	//! control ports than a test can afford to build.
	using LinkPortVisitor = void( * )( int _port, bool _linked, void * _user );

	//! Test seam: the controls model of a plugin with the given processor and control
	//! totals, without a LADSPA effect and without building the port controls.
	LadspaControls( ch_cnt_t _processors, int _controlCount );

	//! Test seam: sets the global stereo-link state, the way the control dialog does.
	void setGlobalLinkState( bool _state );

	//! Test seam: while a visitor is installed, the global stereo link reports every
	//! port it walks to it instead of updating the port control of that port.
	static void setLinkPortVisitor( LinkPortVisitor _visitor, void * _user );
#endif // LMMS_TESTING


protected slots:
	void updateLinkStatesFromGlobal();
	void linkPort( int _port, bool _state );


private:
	//! Applies the global link state to one port of the first processor.
	void applyLinkToPort( int _port, bool _linked );

	LadspaEffect * m_effect;
	ch_cnt_t m_processors;
	control_count_t m_controlCount;
	bool m_noLink;
	BoolModel m_stereoLinkModel;
	//! control vector for each processor
	QVector<control_list_t> m_controls;


	friend class gui::LadspaControlDialog;
	friend class gui::LadspaMatrixControlDialog;
	friend class LadspaEffect;


signals:
	void effectModelChanged( lmms::LadspaControls * );

} ;


} // namespace lmms

#endif
