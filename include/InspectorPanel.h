/*
 * InspectorPanel.h - the shell's Inspector: the selected track's and clip's properties
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

#ifndef LMMS_GUI_INSPECTOR_PANEL_H
#define LMMS_GUI_INSPECTOR_PANEL_H

#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include "lmms_export.h"

class QGridLayout;
class QLabel;
class QLineEdit;
class QToolButton;
class QVBoxLayout;

namespace lmms
{
class Clip;
class MidiClip;
class MixerChannel;
class Track;
}

namespace lmms::gui
{

//! The Inspector: what ShellSelection names, as editable properties. The track section has
//! its name, mute and solo, volume and pan, and the mixer channel it plays into; the clip
//! section its name, position, length and - by kind - its notes (opened in the note editor)
//! or its gain; a selected mixer channel shows its name, mute, solo, volume and pan. Each edit
//! is the registered command (track.rename, track.set_mute, track.set_solo, clip.set_gain,
//! window.note_editor_set) or the model's own journalled control, so the Inspector adds no
//! second way to change anything.
class LMMS_EXPORT InspectorPanel : public QWidget
{
	Q_OBJECT

public:
	explicit InspectorPanel(QWidget* parent = nullptr);

	//! window.select's `inspector`: {track, clip, channel, name, muted, soloed}.
	QJsonObject describe() const;
	void rebuild();

protected:
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	void dropContent();
	void scheduleRebuild();
	QWidget* trackSection(Track* track);
	QWidget* trackControls(Track* track, QWidget* parent);
	QWidget* clipSection(Clip* clip);
	QWidget* channelSection(MixerChannel* channel, const QString& id);
	void addRow(QGridLayout* grid, const QString& caption, QWidget* value);
	//! ED-015: the clip's Start / Length / Offset as fields that run clip.move / clip.resize /
	//! clip.slip (InspectorClipFields.cpp).
	void addClipFields(QGridLayout* grid, Clip* clip, const QString& id, QWidget* parent);
	//! ED-016: the selected notes' Key / Start / Length / Velocity as fields that run note.*.
	QWidget* notesSection(MidiClip* clip, const QString& id, QWidget* parent);
	void syncToggles();

	QPointer<Track> m_track;
	QPointer<Clip> m_clip;
	QVBoxLayout* m_column;
	QWidget* m_content = nullptr;
	QLineEdit* m_name = nullptr;
	QToolButton* m_mute = nullptr;
	QToolButton* m_solo = nullptr;
	QTimer m_sync;
	QList<QPointer<class ReadoutField>> m_liveFields;  //!< refreshed by syncToggles
	//! The redraw a change queues for the next event-loop turn; rebuild() cancels it.
	QTimer m_pending;
};

} // namespace lmms::gui

#endif // LMMS_GUI_INSPECTOR_PANEL_H
