/*
 * DevicePicker.h - the searchable device picker: name, type, format, vendor and state per row, in words
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

#ifndef LMMS_GUI_DEVICE_PICKER_H
#define LMMS_GUI_DEVICE_PICKER_H

#include <QDialog>
#include <functional>

#include "lmms_export.h"

class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace lmms
{
struct ControlDeviceEntry;
}

namespace lmms::gui
{

//! 0.5.0 theme 12: the words a device row shows, shared by the picker and the instrument browser.
namespace devicelabels
{
//! "Built-in", "LADSPA", "LV2", "VST3", "CLAP" for plugin.list's format.
LMMS_EXPORT QString formatLabel(const QString& format);
//! "Instrument", "Effect", "Analysis tool", "Other" for plugin.list's kind.
LMMS_EXPORT QString kindLabel(const QString& kind);
//! Who made @a entry, as its own format reports it (a VST3 or CLAP factory's vendor since the plugin-hosting
//! lane); "Not reported" when the format carries no maker.
LMMS_EXPORT QString vendorOf(const ControlDeviceEntry& entry);
} // namespace devicelabels

/*! 0.5.0 theme 12: the device picker of the Device panel and the device card's Replace / Insert: every device of
 *  one kind the build catalogues (plugin.list's dev-<n>, so what it returns is what plugin.load takes), one row each
 *  with its name, type, format, vendor and state in words - "Favourite", "Cannot be loaded in this build" - never by
 *  colour alone. The search field comes first and has the focus: typing filters the rows live (every word must match
 *  the name, vendor, format or type), Up / Down / Page Up / Page Down move through them while typing, Enter picks the
 *  highlighted row, Escape cancels. Hidden devices (the plug-in organiser) are left out; favourites come first. */
class LMMS_EXPORT DevicePicker : public QDialog
{
	Q_OBJECT

public:
	enum class Kind
	{
		Effect,
		Instrument
	};

	DevicePicker(Kind kind, const QString& title, QWidget* parent = nullptr);

	//! The dev-<n> of the highlighted row, or empty when no loadable row is highlighted.
	QString selectedDevice() const;
	//! Filter the rows as typing @a query does (for scripts of the dialog and the tests).
	void setQuery(const QString& query);
	//! The rows the filter shows now.
	int shownRows() const;
	//! Row @a row of the shown ones, its columns joined by " | " - what a screen reader reads for it.
	QString shownRowText(int row) const;

	//! Opens the picker modally; the chosen dev-<n>, or empty when cancelled.
	static QString pick(QWidget* parent, Kind kind, const QString& title);
	//! For the tests: answer pick() with @a chooser instead of opening the dialog; an empty one restores the dialog.
	static void setChooser(std::function<QString(Kind, const QString&)> chooser);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void fill(Kind kind);
	void applyFilter();
	void moveCurrent(int key);
	void accept() override;

	QLineEdit* m_search = nullptr;
	QTreeWidget* m_rows = nullptr;
	QLabel* m_count = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_DEVICE_PICKER_H
