/*
 * MixerLatencyText.h - what a mixer strip's tooltip says about delay compensation
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

#ifndef LMMS_GUI_MIXER_LATENCY_TEXT_H
#define LMMS_GUI_MIXER_LATENCY_TEXT_H

#include <QCoreApplication>
#include <QString>

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "plugin delay compensation is readable, not settable, and has no interface":
 *  the tooltip a mixer strip carries - its name, then, when there is any, the latency its effects
 *  add (@a chainFrames) and the alignment point its input is delayed to (@a inputFrames), in
 *  frames and milliseconds at @a sampleRate. The master also gets the mix's total (@a totalFrames,
 *  negative for any other strip). What pdc.report reports, readable where the strip is.
 *  Q050-089.1: an enabled hardware insert adds its effective latency (@a insertFrames, pdc.report's
 *  hardware_insert_latency_frames) and where it comes from (@a insertSource, its
 *  hardware_insert_latency_source: "override", "measured", "unmeasured"; "off" and "bypassed" say nothing). */
inline QString mixerLatencyText(const QString& name, int chainFrames, int inputFrames, int totalFrames, int sampleRate,
	int insertFrames = 0, const QString& insertSource = QString())
{
	const auto ms = [sampleRate](int frames) { return sampleRate > 0 ? frames * 1000.0 / sampleRate : 0.0; };
	QString text = name;
	if (chainFrames > 0)
	{
		text += QCoreApplication::translate("MixerView", "\nEffects latency: %1 frames (%2 ms)")
			.arg(chainFrames).arg(ms(chainFrames), 0, 'f', 1);
	}
	if (insertSource == QLatin1String("override") || insertSource == QLatin1String("measured"))
	{
		const QString source = insertSource == QLatin1String("override")
			? QCoreApplication::translate("MixerView", "typed") : QCoreApplication::translate("MixerView", "measured");
		text += QCoreApplication::translate("MixerView", "\nHardware insert latency: %1 frames (%2 ms, %3)")
			.arg(insertFrames).arg(ms(insertFrames), 0, 'f', 1).arg(source);
	}
	else if (insertSource == QLatin1String("unmeasured"))
	{
		text += QCoreApplication::translate("MixerView", "\nHardware insert latency: not measured (ping it or type a figure)");
	}
	if (inputFrames > 0)
	{
		text += QCoreApplication::translate("MixerView", "\nInput aligned to %1 frames (%2 ms)")
			.arg(inputFrames).arg(ms(inputFrames), 0, 'f', 1);
	}
	if (totalFrames > 0)
	{
		text += QCoreApplication::translate("MixerView", "\nMix latency, compensated: %1 frames (%2 ms)")
			.arg(totalFrames).arg(ms(totalFrames), 0, 'f', 1);
	}
	return text;
}

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_LATENCY_TEXT_H
