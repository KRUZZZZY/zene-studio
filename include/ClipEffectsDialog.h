/*
 * ClipEffectsDialog.h - an audio clip's own effects, in the clip's menu (Q050-111)
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
 */

#ifndef LMMS_GUI_CLIP_EFFECTS_DIALOG_H
#define LMMS_GUI_CLIP_EFFECTS_DIALOG_H

#include <functional>
#include <optional>

#include <QDialog>
#include <QJsonObject>
#include <QPointer>

#include "lmms_export.h"

class QColor;
class QMenu;
class QPainter;
class QRect;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace lmms
{
class Clip;
class SampleClip;
}

namespace lmms::gui
{

/*! Q050-111: the "Clip effects" window of one audio clip - its devices in processing order, each with its on/off
 *  state and its parameters, and the buttons that add, reorder, bypass and remove them. The window holds no state
 *  of its own and edits nothing directly: it reads the chain through dsp.get_state and every button runs the same
 *  registered command an agent sends with the clip as the target (plugin.load / plugin.move / plugin.bypass /
 *  plugin.unload / plugin.param_set, target clip-<n>), so each edit is one control.undo step either way. */
class LMMS_EXPORT ClipEffectsDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ClipEffectsDialog(SampleClip* clip, QWidget* parent = nullptr);

	//! The clip's window, opened (or raised, when it is already open) - one per clip.
	static ClipEffectsDialog* showFor(SampleClip* clip);

	//! Re-reads the clip's chain (dsp.get_state) into the list.
	void refresh();

	QTreeWidget* deviceList() const { return m_list; }

	//! Tests replace the parameter prompt (default: a number input between the parameter's min and max, seeded
	//! with its value; nullopt cancels). Passing an empty function restores the default.
	static void setValuePrompt(std::function<std::optional<double>(const QString& name, double value, double min,
		double max)> prompt);

public slots:
	void addEffect();
	void moveSelectedUp();
	void moveSelectedDown();
	void toggleSelectedBypass();
	void removeSelected();
	//! Prompts for a new value of the parameter @a item names (a parameter row; a device row does nothing).
	void editParameter(QTreeWidgetItem* item);

private:
	QPointer<SampleClip> m_clip;
	QTreeWidget* m_list = nullptr;
	QPushButton* m_moveUp = nullptr;
	QPushButton* m_moveDown = nullptr;
	QPushButton* m_bypass = nullptr;
	QPushButton* m_remove = nullptr;

	QString target() const;
	//! Runs @a command with the clip as its target; a refusal is shown (or logged unattended). True when it ran.
	bool run(const QString& command, QJsonObject args);
	//! The device row selected, or of the selected parameter row; nullptr when none is.
	QTreeWidgetItem* selectedDevice() const;
	//! Re-selects the device row whose fx id is @a pluginId after a refresh.
	void selectDevice(const QString& pluginId);
	void updateButtons();
};

/*! The audio clip's menu item: "Clip effects..." (with the device count once it has any) opening the clip's
 *  ClipEffectsDialog. Nothing for a MIDI or pattern clip. */
LMMS_EXPORT void addClipEffectsAction(QMenu* menu, Clip* clip);

//! The clip view's badge: "FX" (with the count past one) in the bottom-right corner of @a rect while @a clip
//! carries any effect of its own; nothing otherwise. Its text is also what clipEffectsBadgeText() returns.
LMMS_EXPORT void paintClipEffectsBadge(QPainter& p, const QRect& rect, const SampleClip& clip, const QColor& text,
	const QColor& shadow);
//! The badge's text for @a clip ("" without effects, "FX", or "FX 3").
LMMS_EXPORT QString clipEffectsBadgeText(const SampleClip& clip);

} // namespace lmms::gui

#endif // LMMS_GUI_CLIP_EFFECTS_DIALOG_H
