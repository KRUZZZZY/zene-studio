/*
 * RnnoiseDenoiserControls.cpp - controls for the RNNoise denoiser effect
 *
 * Copyright (c) 2026 AI-KOS Team
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

#include "RnnoiseDenoiserControls.h"
#include "RnnoiseDenoiserEffect.h"

namespace lmms
{

RnnoiseDenoiserControls::RnnoiseDenoiserControls(RnnoiseDenoiserEffect* effect) :
	EffectControls(effect),
	m_effect(effect)
{
}


void RnnoiseDenoiserControls::loadSettings(const QDomElement& parent)
{
	// The built-in wet/dry model from Effect is handled automatically.
	// Future: load additional parameters here.
}


void RnnoiseDenoiserControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	// The built-in wet/dry model from Effect is handled automatically.
	// Future: save additional parameters here.
}

} // namespace lmms