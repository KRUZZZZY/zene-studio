/*
 * MixerModeBar.h - the bar across the mixer that shows and sets its view, density, filter and modes
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
#ifndef LMMS_GUI_MIXER_MODE_BAR_H
#define LMMS_GUI_MIXER_MODE_BAR_H

#include <QHash>
#include <QJsonValue>
#include <QWidget>

#include "MixerStripInfo.h"
#include "lmms_export.h"

class QButtonGroup;
class QComboBox;
class QHBoxLayout;
class QMenu;
class QPushButton;
class QToolButton;

namespace lmms::gui
{

/*! 0.5.0 theme 16 (the mixer reworked around clear modes). One bar across the top of the mixer, always
 *  shown, that says - in words - how the mixer is set and sets it:
 *  - View: Mix | Routing, and Density: Compact | Expanded (independent of each other), and Show: the
 *    channel filter - all through window.mixer_view; and (MX-005) Pick..., the channels its Selected filter shows;
 *  - the modes, each a button naming its state that opens a menu to change it: what the faders drive
 *    (window.mixer_view's sends_on_faders - the banner under the bar says it loudly while they drive
 *    sends), the solo mode (mixer.solo_mode), the listen bus (mixer.listen_mode) and the master's
 *    hardware output and speaker format (the wave 2 Hardware I/O and Surround menus, MixerHardwareMenu.h,
 *    not a copy of them);
 *  - Routing Matrix... (MixerRoutingMatrix.h).
 *  A mode that changes what you hear (sends on the faders, a channel listening) is drawn in the accent
 *  colour as well as named. MixerView::syncWithMixer refreshes it with the strips; a change made here asks
 *  the view to follow at once (changed()). */
class LMMS_EXPORT MixerModeBar : public QWidget
{
	Q_OBJECT
public:
	explicit MixerModeBar(QWidget* parent);

	void refresh(const mixerstripinfo::Context& context);

	//! "mix" / "routing", "compact" / "expanded".
	QPushButton* viewButton(const QString& view) const { return m_viewButtons.value(view); }
	QPushButton* densityButton(const QString& density) const { return m_densityButtons.value(density); }
	QComboBox* filterBox() const { return m_filter; }
	QToolButton* fadersButton() const { return m_faders; }
	QToolButton* soloButton() const { return m_solo; }
	QToolButton* listenButton() const { return m_listen; }
	QToolButton* masterButton() const { return m_master; }

signals:
	void changed();

private:
	//! A labelled row of exclusive buttons, one per value of window.mixer_view's @a key.
	QHash<QString, QPushButton*> addSegment(QHBoxLayout* layout, const QString& label, const char* key,
		const QStringList& values, const QStringList& captions);
	//! A button naming a mode's state (@a hint says what it is) whose menu @a fill builds each time it opens.
	QToolButton* addChip(QHBoxLayout* layout, const QString& objectName, const QString& hint, void (MixerModeBar::*fill)(QMenu*));
	void setOption(const QString& key, const QJsonValue& value);
	void fillFaders(QMenu* menu);
	void fillSolo(QMenu* menu);
	void fillListen(QMenu* menu);
	void fillMaster(QMenu* menu);
	void pickSelected();  //!< MX-005
	void refreshChips(const mixerstripinfo::Context& context);

	QHash<QString, QPushButton*> m_viewButtons;
	QHash<QString, QPushButton*> m_densityButtons;
	QComboBox* m_filter = nullptr;
	QToolButton* m_faders = nullptr;
	QToolButton* m_solo = nullptr;
	QToolButton* m_listen = nullptr;
	QToolButton* m_master = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_MODE_BAR_H
