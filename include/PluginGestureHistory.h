/*
 * PluginGestureHistory.h - one plug-in editor gesture as one project-history step (VST3, CLAP)
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

#ifndef LMMS_PLUGIN_GESTURE_HISTORY_H
#define LMMS_PLUGIN_GESTURE_HISTORY_H

#include <cmath>

#include "AutomatableModel.h"
#include "PluginParamGesture.h"

namespace lmms
{

/**
 * What a parameter model remembers between polls: the gesture under way and its one journal checkpoint. Each poll
 * replays the plug-in's reports in order, so every gesture that finished since the last poll gets its own step, whose
 * undo value is the last gesture's final value. A gesture's first change takes the checkpoint, while the model still
 * holds the value from before it; its later ones write the model without one. A change outside a gesture takes its
 * own checkpoint, as it always did. GUI thread.
 */
class PluginGestureHistory
{
public:
	//! @a edits as PluginParamGesture::read() found them; a value within @a tolerance of the model is no change.
	void pull(FloatModel& model, const PluginParamGesture::Edits& edits, float tolerance)
	{
		for (std::uint32_t i = 0; i < edits.count; ++i)
		{
			// What the plug-in changed since its last mark goes where the mark's value says, before the mark counts.
			write(model, static_cast<float>(edits.reports[i].value), tolerance);
			m_open = edits.reports[i].mark == PluginParamGesture::Mark::Begin;
			m_checkpointed = false;
		}
		if (edits.lost)
		{
			m_open = edits.open;
			m_checkpointed = false;
		}
		if (edits.settled) { write(model, static_cast<float>(edits.value), tolerance); }
	}

private:
	void write(FloatModel& model, float value, float tolerance)
	{
		const float before = model.value();
		if (std::abs(before - value) <= tolerance) { return; }
		// An undo or a knob during the gesture moved the model off the value written here: the plug-in's next
		// value is a new edit, and the history needs a step for it.
		const bool covered = m_open && m_checkpointed && before == m_written;
		model.setValue(value, false, !covered);
		if (model.value() != before)
		{
			m_checkpointed = true;
			m_written = model.value();
		}
	}

	bool m_open = false;
	bool m_checkpointed = false;
	float m_written = 0.f;
};

} // namespace lmms

#endif // LMMS_PLUGIN_GESTURE_HISTORY_H
