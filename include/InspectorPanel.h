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

#include <functional>

#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include "lmms_export.h"

class QGridLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
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
//!
//! 0.5.0 theme 13: the panel keeps its context as the selection moves. Each section (channel, track, clip) is kept as
//! it is while it shows the same object - selecting another clip on the same track leaves the track section, its focus
//! and its scroll position alone - and a section that must change is replaced where it stands, with the keyboard focus
//! put back on the control of the same name and the scroll position kept. The selected notes' fields say "Multiple
//! values" when the notes differ, volumes read in dB, velocities in %, times in the time format (MusicalTime.h), and
//! both a track's and a channel's name are edited in place (track.rename, mixer.rename_channel).
class LMMS_EXPORT InspectorPanel : public QWidget
{
	Q_OBJECT

public:
	explicit InspectorPanel(QWidget* parent = nullptr);

	//! window.select's `inspector`: {track, clip, channel, name, muted, soloed}. Theme 13 adds `sections`: the keys of
	//! the sections shown, in order ("track", "clip", "channel" or "empty").
	QJsonObject describe() const;
	void rebuild();

protected:
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	//! One section of the panel and what it was made for: kept while the selection still names the same object.
	struct Section
	{
		QString key;
		QPointer<QWidget> widget;
	};
	void dropContent();
	void scheduleRebuild();
	//! Theme 13 (InspectorSections.cpp): @a slot shows @a key's section - the one shown when its key matches, else
	//! @a make()'s, put where the old one stood (or at @a index), keeping the focus by name and the scroll position.
	void placeSection(Section& slot, const QString& key, const std::function<QWidget*()>& make, int index);
	void retireSection(Section& slot);
	//! The track's section, then its clip's when one is selected (rebuild()'s third case).
	void placeTrackAndClip();
	//! The channel section made again on the next turn (its routing lists changed), keeping focus and scroll.
	void refreshChannelSection();
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
	//! The channel's name field shows a rename made elsewhere (the strip, a script, an undo) unless it is being typed in.
	void syncChannelName();

	QPointer<Track> m_track;
	QPointer<Clip> m_clip;
	QVBoxLayout* m_column;
	QWidget* m_content = nullptr;
	QVBoxLayout* m_sections = nullptr;  //!< m_content's column: the sections, in order
	QScrollArea* m_scroll = nullptr;
	Section m_empty;
	Section m_channelSection;
	Section m_trackSection;
	Section m_clipSection;
	QLineEdit* m_name = nullptr;
	QPointer<QLineEdit> m_channelName;  //!< theme 13: the channel's name field, kept in step with renames made elsewhere
	QToolButton* m_mute = nullptr;
	QToolButton* m_solo = nullptr;
	QTimer m_sync;
	QList<QPointer<class ReadoutField>> m_liveFields;  //!< refreshed by syncToggles
	//! The redraw a change queues for the next event-loop turn; rebuild() cancels it.
	QTimer m_pending;
};

} // namespace lmms::gui

#endif // LMMS_GUI_INSPECTOR_PANEL_H
