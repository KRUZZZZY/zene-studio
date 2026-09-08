/*
 * RnnoiseDenoiserControls.h - controls for the RNNoise denoiser effect
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

#ifndef LMMS_RNNOISE_DENOISER_CONTROLS_H
#define LMMS_RNNOISE_DENOISER_CONTROLS_H

#include "EffectControls.h"
#include "RnnoiseDenoiserControlDialog.h"

namespace lmms
{

class RnnoiseDenoiserEffect;

class RnnoiseDenoiserControls : public EffectControls
{
	Q_OBJECT
public:
	RnnoiseDenoiserControls(RnnoiseDenoiserEffect* effect);
	~RnnoiseDenoiserControls() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;

	inline QString nodeName() const override
	{
		return "RnnoiseDenoiserControls";
	}

	gui::EffectControlDialog* createView() override
	{
		return new gui::RnnoiseDenoiserControlDialog(this);
	}

	int controlCount() override { return 0; }

private:
	RnnoiseDenoiserEffect* m_effect;

	friend class gui::RnnoiseDenoiserControlDialog;
	friend class RnnoiseDenoiserEffect;
};

} // namespace lmms

#endif // LMMS_RNNOISE_DENOISER_CONTROLS_H