/*
 * delaycontrols.h - declaration of DelayControl class.
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

#ifndef DELAYCONTROLS_H
#define DELAYCONTROLS_H

#include <atomic>

#include "EffectControls.h"
#include "DelayControlsDialog.h"

namespace lmms
{


class DelayEffect;


class DelayControls : public EffectControls
{
	Q_OBJECT
public:
	DelayControls( DelayEffect* effect );
	~DelayControls() override = default;
	void saveSettings( QDomDocument& doc, QDomElement& parent ) override;
	void loadSettings( const QDomElement& _this ) override;
	inline QString nodeName() const override
	{
		return "Delay";
	}
	int controlCount() override{
		return 5;
	}
	gui::EffectControlDialog* createView() override
	{
		return new gui::DelayControlsDialog( this );
	}

	// cluster new-p9-122 (new-p16-57): peak meters written on the audio thread
	// (DelayEffect::processImpl) and read+cleared by the shared EqFader on the
	// GUI timer - atomics, lock-free, no data race on the handoff.
	std::atomic<float> m_outPeakL;
	std::atomic<float> m_outPeakR;


private slots:
	void changeSampleRate();

private:
	DelayEffect* m_effect;
	TempoSyncKnobModel m_delayTimeModel;
	FloatModel m_feedbackModel;
	TempoSyncKnobModel m_lfoTimeModel;
	TempoSyncKnobModel m_lfoAmountModel;
	FloatModel m_outGainModel;

	friend class gui::DelayControlsDialog;
	friend class DelayEffect;
};


} // namespace lmms

// The peak meters cross the audio/GUI thread boundary (cluster new-p9-122);
// a non-lock-free atomic would put a lock or syscall on the render thread.
static_assert(std::atomic<float>::is_always_lock_free,
	"DelayControls peak meters are written on the audio thread and must be lock-free");

#endif // DELAYCONTROLS_H
