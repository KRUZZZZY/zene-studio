/*
 * DevicePanel.h - the shell's Device panel: the selected track's devices as cards
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

#ifndef LMMS_GUI_DEVICE_PANEL_H
#define LMMS_GUI_DEVICE_PANEL_H

#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

#include "lmms_export.h"

class QContextMenuEvent;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QToolButton;

namespace lmms
{
class AutomatableModel;
class Effect;
class EffectChain;
class InstrumentTrack;
class MixerChannel;
class Track;
}

namespace lmms::gui
{

class SubWindow;

//! One device as a card: a header (power, name, the device's own editor, remove) over the
//! device's parameters, drawn from its own models with the flat controls - a knob per number,
//! a toggle per switch, a menu per choice. The parameters are the list plugin.param_get
//! reports (controlEffectParameters / controlInstrumentParameters), so what a user sees on the
//! card and what an agent reads over the socket are the same list in the same order. Every
//! header button dispatches the registered command (plugin.bypass, plugin.unload,
//! plugin.editor_open), as every shell action does (SPEC A11).
class LMMS_EXPORT DeviceCard : public QFrame
{
	Q_OBJECT

public:
	struct Spec
	{
		QString target;   //!< trk-<n> or ch-<n>: the chain's owner
		QString plugin;   //!< fx-<n>, or inst for a track's instrument
		QString title;
		QString subtitle;
		bool enabled = true;
		Effect* effect = nullptr;               //!< the effect a card draws, if it draws one
		InstrumentTrack* instrumentTrack = nullptr; //!< the track whose instrument it draws
	};

	//! Parameters drawn on one card; the rest are in the device's own editor, one click away.
	static constexpr int MaxParameters = 16;
	//! The width of one parameter cell (a knob and its caption).
	static constexpr int CellWidth = 76;

	DeviceCard(const Spec& spec, const QList<AutomatableModel*>& parameters, QWidget* parent = nullptr);
	~DeviceCard() override;

	const Spec& spec() const { return m_spec; }
	int shownParameters() const { return m_shown; }
	int totalParameters() const { return m_total; }
	//! Close the editor window this card opened, while its device still exists.
	void closeEditor();
	//! 0.5.0 L10 (MX-095): light the header's dot while the device processes audio, and bring the
	//! header's power, fold and delta switches back to the device's state (an undo, a socket command
	//! or the card's own menu changes it without the card).
	void refreshActivity();

protected:
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	QWidget* buildHeader();
	QWidget* buildParameters(const QList<AutomatableModel*>& parameters);
	void openEditor();
	void openEffectDialog();
	//! Engine audit E18: the device's pin connector - which track channels feed which plug-in
	//! ports - in its own window (the view existed, nothing opened it).
	void openPins();
	void syncHeader();

	Spec m_spec;
	int m_shown = 0;
	int m_total = 0;
	QPointer<SubWindow> m_editor;
	QPointer<SubWindow> m_pins;
	QLabel* m_activity = nullptr;
	QToolButton* m_power = nullptr;
	QToolButton* m_fold = nullptr;
	QToolButton* m_delta = nullptr;
	QWidget* m_body = nullptr;  //!< the parameters, hidden while the device is folded (MX-113)
};

//! A modulator, placed right after the device it modulates: its shape, rate (drag or type -
//! modulator.rate_set) and the parameters it drives with their depths.
class LMMS_EXPORT ModulatorCard : public QFrame
{
	Q_OBJECT

public:
	explicit ModulatorCard(const QJsonObject& modulator, QWidget* parent = nullptr);
	QString modulatorId() const { return m_id; }

private:
	QString m_id;
};

//! The Device panel: the selected track's instrument, its device chain, then the mixer
//! channel it plays into (when that is not the master) with that channel's inserts and the
//! modulators driving them - or, for a selected mixer strip, that channel alone. It follows
//! ShellSelection. A chain edited from anywhere (rack, socket, undo) redraws it: the
//! chain's own signals drop the cards BEFORE a device is deleted - a card never outlives the
//! models it draws - and a light poll while the panel is visible catches what has no signal
//! (a modulator added over the socket).
class LMMS_EXPORT DevicePanel : public QWidget
{
	Q_OBJECT

public:
	explicit DevicePanel(QWidget* parent = nullptr);

	//! What the panel shows (window.get_state's `devices`): {track, channel, cards: [{target,
	//! plugin, name, enabled, parameters, shown}], modulators: [id...]}.
	QJsonObject describe() const;
	void rebuild();

protected:
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	void dropCards();
	void scheduleRebuild();
	void watch(Track* track, MixerChannel* channel);
	void watchChain(EffectChain* chain);
	void addInstrumentCard(InstrumentTrack* track);
	//! @a chainIndex is the chain's index in its channel's rack (0: the channel's own inserts), as a
	//! modulator's route names it.
	void addChain(EffectChain* chain, const QString& target, const QJsonArray& modulators, int chainIndex = 0);
	void addModulators(const QJsonArray& modulators, const QString& target, int chainIndex, int effectIndex);
	void addSection(const QString& text);
	QWidget* buildAddButton();
	QString signature() const;
	void poll();

	QPointer<Track> m_track;
	QList<QMetaObject::Connection> m_watch;
	QScrollArea* m_scroll;
	QWidget* m_row;
	QHBoxLayout* m_cards;
	QLabel* m_empty;
	QWidget* m_add;
	QTimer m_poll;
	//! The redraw a change queues for the next event-loop turn; rebuild() cancels it.
	QTimer m_pending;
	QString m_signature;
	QString m_target;   //!< where Add device puts a device: the track, or the selected channel
	QString m_channel;  //!< the mixer channel whose inserts are drawn, if any
	QStringList m_modulators;
};

} // namespace lmms::gui

#endif // LMMS_GUI_DEVICE_PANEL_H
