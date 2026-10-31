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

#include "AutomatableModel.h"
#include "PluginParamGesture.h"

namespace lmms
{

/**
 * What a parameter model remembers between polls: the one journal checkpoint of the gesture under way. The
 * first change a gesture makes takes the checkpoint, while the model still holds the value from before it; the
 * later ones write the model without one. A change outside a gesture takes its own checkpoint, as it always did.
 * GUI thread.
 */
class PluginGestureHistory
{
public:
	//! @a value is what the plug-in reports now, @a differs whether that is a change worth writing to @a model.
	void pull(FloatModel& model, float value, bool differs, PluginParamGesture::Marks marks)
	{
		const bool sameGesture = marks.begins == m_marks.begins && m_marks.begins != m_marks.ends;
		if (marks.begins != m_marks.begins) { m_checkpointed = false; }
		m_marks = marks;
		if (!differs) { return; }

		const float before = model.value();
		// An undo or a knob during the gesture moved the model off the value written here: the plug-in's next
		// value is a new edit, and the history needs a step for it.
		const bool covered = sameGesture && m_checkpointed && before == m_written;
		model.setValue(value, false, !covered);
		if (model.value() != before)
		{
			m_checkpointed = true;
			m_written = model.value();
		}
	}

private:
	PluginParamGesture::Marks m_marks;
	bool m_checkpointed = false;
	float m_written = 0.f;
};

} // namespace lmms

#endif // LMMS_PLUGIN_GESTURE_HISTORY_H
