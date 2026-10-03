/*
 * InsertRecorder.h - an insert that records what passes it
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

#ifndef LMMS_INSERT_RECORDER_H
#define LMMS_INSERT_RECORDER_H

#include "AudioPlugin.h"
#include "ParameterControls.h"
#include "TakeRecorder.h"

namespace lmms
{

class InsertRecorderEffect;

//! The recorder has no knobs: its state is the last take, saved as 'last-take' only once there is one.
class InsertRecorderControls : public ParameterControls
{
public:
	explicit InsertRecorderControls(InsertRecorderEffect* effect);

	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;

private:
	InsertRecorderEffect* m_effect;
};

/*! 0.5.0 L10 (MX-156): an insert that records the signal passing it - wherever it sits in a chain -
 *  to a stereo 24-bit WAV take, through a TakeRecorder (a pre-allocated ring on the audio thread, a
 *  disk-writer thread for the file). The sound passes unchanged. plugin.record starts and stops a
 *  take; plugin.recorder_land puts the last one on a new audio track. While recording it never
 *  sleeps on silence, so a take keeps its pauses. */
class InsertRecorderEffect : public DefaultEffect
{
public:
	InsertRecorderEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~InsertRecorderEffect() override;

	EffectControls* controls() override { return &m_controls; }
	TakeRecorder& recorder() { return m_recorder; }

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;

	InsertRecorderControls m_controls;
	TakeRecorder m_recorder;
};

} // namespace lmms

#endif // LMMS_INSERT_RECORDER_H
