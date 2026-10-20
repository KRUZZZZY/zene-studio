/*
 * VersionedSaveDialog.h - declaration of class VersionedSaveDialog, a file save
 * dialog that provides buttons to increment or decrement a version which is
 * appended to the file name. (e.g. "MyProject-01.mmpz")
 *
 * Copyright (c) 2014 Lukas W <lukaswhl/at/gmail.com>
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

#ifndef LMMS_GUI_VERSIONED_SAVE_DIALOG_H
#define LMMS_GUI_VERSIONED_SAVE_DIALOG_H

#include <functional>

#include "FileDialog.h"
#include "Song.h"

class QLineEdit;

namespace lmms::gui
{

class LedCheckBox;

class SaveOptionsWidget : public QWidget {
public:
	SaveOptionsWidget(Song::SaveOptions &saveOptions);

private:
	LedCheckBox *m_discardMIDIConnectionsCheckbox;
	LedCheckBox *m_saveAsProjectBundleCheckbox;
	LedCheckBox *m_saveAsV2ContainerCheckbox;
};

class VersionedSaveDialog : public FileDialog
{
	Q_OBJECT
public:
	explicit VersionedSaveDialog( QWidget *parent = 0,
								  QWidget *saveOptionsWidget = nullptr,
								  const QString &caption = QString(),
								  const QString &directory = QString(),
								  const QString &filter = QString() );

	// Returns true if file name was changed, returns false if it wasn't
	static bool changeFileNameVersion( QString &fileName, bool increment );
	static bool fileExistsQuery( QString FileName, QString WindowTitle );

	/*! TEST SEAM, C++ only (no command, Lua binding or setting reaches it). The hook
	 *  answers WHICH VERSION NUMBER a file name carries: it is called with the digits
	 *  the name ends in and fills in the number, answering false for a name that carries
	 *  no number. The product parses the digits itself while no hook is installed, so
	 *  the shipped behaviour is exactly what it was.
	 *
	 *  A test installs one to BOUND a loop that would otherwise never end: a name whose
	 *  version cannot be advanced stays the same, so a caller that looks for the next
	 *  free name keeps finding the same file. The hook lets the test say what the next
	 *  name is without touching a file - no timing, no race on the project. */
	using VersionNumberHook = std::function<bool(const QString& digits, qulonglong& version)>;
	static void setVersionNumberHook( VersionNumberHook hook );

public slots:
	void incrementVersion();
	void decrementVersion();
};


} // namespace lmms::gui

#endif // LMMS_GUI_VERSIONED_SAVE_DIALOG_H
