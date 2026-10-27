/*
 * ConsoleStripSection.h - the mixer strip's optional Console section
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

#ifndef LMMS_GUI_CONSOLE_STRIP_SECTION_H
#define LMMS_GUI_CONSOLE_STRIP_SECTION_H

#include <memory>
#include <vector>

#include <QWidget>

#include "lmms_export.h"

class QJsonValue;
class QLabel;
class QToolButton;

namespace lmms
{
class FloatModel;
}

namespace lmms::gui
{

class Knob;

/*! Q050-088: the Console section of a mixer strip (window.mixer_view 'show_console'): small knobs for the
 *  channel's trim, EQ (low, mid, mid frequency, high) and its Console's drive and glue threshold, ratio and
 *  makeup, and switches for each side's polarity and the glue. Every edit runs mixer.console_strip on this
 *  channel - the command an agent takes, one undo step a gesture (a drag coalesces) - so the knobs' models
 *  are the widget's own views of the values (refresh() follows edits made elsewhere). Without a Console on
 *  the channel its five controls are disabled and a line says so; showing the section adds no processing. */
class LMMS_EXPORT ConsoleStripSection : public QWidget
{
	Q_OBJECT
public:
	ConsoleStripSection(const QString& channel, QWidget* parent);
	~ConsoleStripSection() override;

	//! Take the channel's current values (and whether it has a Console).
	void refresh();
	//! The control for a mixer.console_strip field ('trim_db', 'glue', ...); nullptr for no such field.
	QWidget* control(const QString& field) const;

private:
	struct Field;
	void addKnob(const char* field, const QString& label, float low, float high, float step, bool console);
	void addSwitch(const char* field, const QString& label, bool console);
	void send(const QString& field, const QJsonValue& value);

	QString m_channel;
	bool m_syncing = false;
	QLabel* m_noConsole = nullptr;
	std::vector<std::unique_ptr<Field>> m_fields;
};

} // namespace lmms::gui

#endif // LMMS_GUI_CONSOLE_STRIP_SECTION_H
