/*
 * DelayControlsTest.cpp - cluster new-p11-82: the Delay plugin's delay-time
 * parameter is labelled by its displayed name, and the value is in seconds
 *
 * Copyright (c) 2026 Zene Studio contributors
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

/*! The device card and plugin.param_get read each effect parameter by its model
 *  display name (ControlDeviceSupport.cpp: controlParameterJson inserts
 *  model->displayName()). The Delay plugin's delay-time model is labelled
 *  "Delay samples" while its value is in seconds - DelayEffect::processImpl
 *  multiplies it by the sample rate and the dialog shows it with " s" units.
 *  Read or automate the parameter by its displayed name and the unit is wrong.
 *  The gate: the model the engine range 0.01..5000 identifies must display a
 *  seconds label. */

#include <QtTest>

#include "AutomatableModel.h"
#include "ControlDeviceSupport.h"
#include "TempoSyncKnobModel.h"
#include "DelayControls.h"
#include "DelayEffect.h"
#include "EffectChain.h"
#include "Engine.h"

using namespace lmms;

class DelayControlsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init( true );
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void delayTimeParameterIsNamedInSeconds()
	{
		EffectChain chain{ nullptr };
		auto* effect = new DelayEffect( &chain, nullptr );

		// The same enumeration the device card and plugin.param_get use.
		const QList<AutomatableModel*> models = controlEffectParameters( effect );
		// The delay-time model is the tempo-sync knob whose scale is 5000 (the
		// LFO knobs use 20000 and 2000; the delay range itself is 0.01..5 s).
		const AutomatableModel* delayTime = nullptr;
		for ( const AutomatableModel* model : models )
		{
			const auto* knob = dynamic_cast<const TempoSyncKnobModel*>( model );
			if( knob != nullptr && knob->scale() == 5000.f )
			{
				delayTime = model;
				break;
			}
		}
		QVERIFY( delayTime != nullptr );
		// The value is seconds: DelayEffect::processImpl multiplies it by the
		// sample rate. The displayed name must say so, not "samples".
		QCOMPARE( delayTime->displayName(), QStringLiteral( "Delay time (s)" ) );

		delete effect;
	}
};

QTEST_MAIN( DelayControlsTest )

#include "DelayControlsTest.moc"
