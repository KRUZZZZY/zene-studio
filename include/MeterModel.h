/*
 * MeterModel.h - model for meter specification
 *
 * Copyright (c) 2008-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_METER_MODEL_H
#define LMMS_METER_MODEL_H

#include "AutomatableModel.h"

namespace lmms
{


class LMMS_EXPORT MeterModel : public Model
{
	Q_OBJECT
	mapPropertyFromModel(int,getDenominator,setDenominator,m_denominatorModel);
public:
	MeterModel( Model * _parent );
	~MeterModel() override = default;

	// The numerator is a beat count and every caller divides by it. fittedValue() does not clamp
	// NaN, so a NaN numerator is read as one beat here instead of through an undefined float-to-int
	// conversion (which yields 0 or INT_MIN, never a valid count).
	int getNumerator() const
	{
		const float numerator = m_numeratorModel.AutomatableModel::value<float>();
		return numerator >= 1.0f ? static_cast<int>(numerator) : 1;
	}

public slots:
	void setNumerator( const int val )
	{
		m_numeratorModel.setValue( (float) val );
	}

public:

	void saveSettings( QDomDocument & _doc, QDomElement & _this,
						const QString & _name );
	void loadSettings( const QDomElement & _this, const QString & _name );

	void reset();

	// Must have the sub-models exposed to programmatically connect
	// to automation or controllers
	IntModel & numeratorModel()
	{
		return m_numeratorModel;
	}

	IntModel & denominatorModel()
	{
		return m_denominatorModel;
	}


private:
	IntModel m_numeratorModel;
	IntModel m_denominatorModel;

} ;


} // namespace lmms

#endif // LMMS_METER_MODEL_H
