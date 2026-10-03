/*
 * MacroKnobs.h - a rack's macro knobs, for the Device panel and the mixer strip
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
#ifndef LMMS_GUI_MACRO_KNOBS_H
#define LMMS_GUI_MACRO_KNOBS_H

#include <memory>
#include <vector>

#include <QWidget>

#include "lmms_export.h"

class QHBoxLayout;

namespace lmms
{
class FloatModel;
}

namespace lmms::gui
{

class Knob;

/*! 0.5.0 L10 (MX-104 / MX-090): a rack's macros as knobs - in the Device panel beside the channel's
 *  devices and, small, on the mixer strip. A knob turned runs rack.macro_set (which coalesces a drag
 *  into one undo step), so a knob and a script take one path; refresh() follows the rack - a macro
 *  added, removed, renamed or set elsewhere. Each knob's model is the widget's own, a view of the
 *  macro's value (the macro itself is the rack's scalar, not a model). */
class LMMS_EXPORT MacroKnobs : public QWidget
{
	Q_OBJECT
public:
	//! @a maxKnobs bounds how many macros it shows (the strip shows fewer than the panel).
	MacroKnobs(const QString& channel, int maxKnobs, bool small, QWidget* parent);
	~MacroKnobs() override;

	//! Rebuild when the macros changed, else take their values.
	void refresh();
	int knobCount() const { return static_cast<int>(m_models.size()); }

private:
	void rebuild();

	QString m_channel;
	int m_maxKnobs;
	bool m_small;
	QString m_signature;
	bool m_syncing = false;
	QHBoxLayout* m_row;
	std::vector<std::unique_ptr<FloatModel>> m_models;
	std::vector<Knob*> m_knobs;
};

} // namespace lmms::gui

#endif // LMMS_GUI_MACRO_KNOBS_H
