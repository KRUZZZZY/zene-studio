/*
 * VcaStripView.h - M3.7: a VCA group's own strip in the mixer, and the channel's VCA menu
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

#ifndef LMMS_GUI_VCA_STRIP_VIEW_H
#define LMMS_GUI_VCA_STRIP_VIEW_H

#include <functional>

#include <QWidget>

#include "LmmsTypes.h"
#include "lmms_export.h"

class QLabel;
class QMenu;

namespace lmms
{
class VcaGroup;
}

namespace lmms::gui
{

class AutomatableButton;
class Fader;

/*! One VCA group as a mixer strip: its name, its member count, a fader on the group's own
 *  model (the member channels' faders are never written - VcaGroup.h), and its mute and
 *  solo. Every control is bound to the group's models, so the strip, the file, the undo
 *  stack and the vca.* commands all move the same state. */
class LMMS_EXPORT VcaStripView : public QWidget
{
	Q_OBJECT
public:
	VcaStripView(VcaGroup* group, QWidget* parent = nullptr);

	int groupId() const { return m_groupId; }
	Fader* fader() const { return m_fader; }
	AutomatableButton* muteButton() const { return m_mute; }
	AutomatableButton* soloButton() const { return m_solo; }
	QString caption() const;

private:
	int m_groupId;
	QLabel* m_name = nullptr;
	Fader* m_fader = nullptr;
	AutomatableButton* m_mute = nullptr;
	AutomatableButton* m_solo = nullptr;
};

/*! The mixer channel context menu's "VCA group" submenu for channel @a channelIndex: one
 *  checkable entry per group (checked = a member; toggling assigns or unassigns), then
 *  "New VCA group" (creates one named after the channel and assigns it). Every entry
 *  dispatches the vca.* command, so a menu edit is one undoable, recorded step exactly as
 *  the socket's; @a changed runs after each, so the caller can refresh its strips. */
LMMS_EXPORT void populateVcaMenu(QMenu* menu, mix_ch_t channelIndex, std::function<void()> changed = {});

} // namespace lmms::gui

#endif // LMMS_GUI_VCA_STRIP_VIEW_H
