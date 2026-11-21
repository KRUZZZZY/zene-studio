/*
 * MixerChannelView.h
 *
 * Copyright (c) 2024 saker
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

#ifndef LMMS_GUI_MIXER_CHANNEL_VIEW_H
#define LMMS_GUI_MIXER_CHANNEL_VIEW_H

#include <QList>
#include <QWidget>

#include "MixerOverlays.h"

class QGraphicsView;
class QLabel;
class QLineEdit;
class QStackedWidget;

namespace lmms {
class FloatModel;
class MixerChannel;
}

namespace lmms::gui {
class AutomatableButton;
class ConsoleStripSection;
class EffectRackView;
class Fader;
class Knob;
class LcdWidget;
class MacroKnobs;
class MixerView;
class PeakIndicator;
class SendButtonIndicator;


class MixerChannelView : public QWidget
{
	Q_OBJECT
	Q_PROPERTY(QBrush backgroundActive READ backgroundActive WRITE setBackgroundActive)
	Q_PROPERTY(QColor strokeOuterActive READ strokeOuterActive WRITE setStrokeOuterActive)
	Q_PROPERTY(QColor strokeOuterInactive READ strokeOuterInactive WRITE setStrokeOuterInactive)
	Q_PROPERTY(QColor strokeInnerActive READ strokeInnerActive WRITE setStrokeInnerActive)
	Q_PROPERTY(QColor strokeInnerInactive READ strokeInnerInactive WRITE setStrokeInnerInactive)
public:
	MixerChannelView(QWidget* parent, MixerView* mixerView, int channelIndex);
	void paintEvent(QPaintEvent* event) override;
	void contextMenuEvent(QContextMenuEvent*) override;
	bool eventFilter(QObject* watched, QEvent* event) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;
	void keyPressEvent(QKeyEvent* ke) override;

	void reset();

	int channelIndex() const { return m_channelIndex; }
	void setChannelIndex(int index);

	QBrush backgroundActive() const { return m_backgroundActive; }
	void setBackgroundActive(const QBrush& c) { m_backgroundActive = c; }

	QColor strokeOuterActive() const { return m_strokeOuterActive; }
	void setStrokeOuterActive(const QColor& c) { m_strokeOuterActive = c; }

	QColor strokeOuterInactive() const { return m_strokeOuterInactive; }
	void setStrokeOuterInactive(const QColor& c) { m_strokeOuterInactive = c; }

	QColor strokeInnerActive() const { return m_strokeInnerActive; }
	void setStrokeInnerActive(const QColor& c) { m_strokeInnerActive = c; }

	QColor strokeInnerInactive() const { return m_strokeInnerInactive; }
	void setStrokeInnerInactive(const QColor& c) { m_strokeInnerInactive = c; }

	Fader* fader() const { return m_fader; }
	// 0.5.0 L08: what MixerViewOptions shows and hides on a strip.
	Knob* panKnob() const { return m_panKnob; }
	LcdWidget* numberLcd() const { return m_channelNumberLcd; }
	QLineEdit* nameEdit() const { return m_renameLineEdit; }
	//! The name field follows a rename made elsewhere (the Inspector, an agent), unless the strip is mid-rename.
	void syncName();
	QLabel* meterInfo() const { return m_meterInfo; }
	QLabel* insertList() const { return m_insertList; }
	MacroKnobs* macroKnobs() const { return m_macroKnobs; }
	//! Q050-088: the Console section (window.mixer_view 'show_console').
	ConsoleStripSection* consoleSection() const { return m_consoleSection; }
	//! The mixer view's "sends" option: false hides the send row and its space; true gives the space back
	//! (which of its widgets show is the current channel's business, MixerView::updateMixerChannel).
	void setSendsShown(bool shown);
	/*! 0.5.0 theme 6 (sends on faders): the line above the fader saying what it drives while the faders
	 *  show @a returnName's sends - the return's name on a send, "Volume" on the return's own fader and the
	 *  master's, "No send" on a channel without one (its fader stays in place, dimmed and still). With
	 *  @a returnName empty (the channel faders) the line is gone and the fader is as it always was. */
	void showFaderRole(mixeroverlays::FaderRole role, const QString& returnName);
	//! The still model a "No send" fader shows: it drives nothing.
	FloatModel* noSendModel() const { return m_noSendModel; }
	QLabel* faderTarget() const { return m_faderTarget; }
	//! 0.5.0 theme 16 (MixerStripTags.h): the type badge, the routing lines and the mode line.
	QLabel* typeBadge() const { return m_typeBadge; }
	QLabel* routingLine() const { return m_routingLine; }
	QLabel* stateLine() const { return m_stateLine; }
	AutomatableButton* soloButton() const { return m_soloButton; }
	/*! The controls Tab reaches in this strip, in the order the eye reads them (send, solo, mute,
	 *  pan, macros, fader) - only what is visible and enabled now. Ctrl+Tab searches the adjacent
	 *  strip for the same one. */
	QList<QWidget*> keyboardFocusOrder() const;

public slots:
	void renameChannel();
	void resetColor();
	void selectColor();
	void randomizeColor();

private slots:
	void renameFinished();
	void removeChannel();
	void removeUnusedChannels();
	void moveChannelLeft();
	void moveChannelRight();

private:
	bool confirmRemoval(int index);
	QString elideName(const QString& name);
	MixerChannel* mixerChannel() const;
	auto isMasterChannel() const -> bool { return m_channelIndex == 0; }
	//! True when @a key is one of this strip's traversal keys (Tab, Shift+Tab, Ctrl+Tab, Ctrl+Shift+Tab).
	bool handleTraversalKey(QKeyEvent* key, QWidget* from);
	void moveFocusWithinStrip(QWidget* from, bool forward);
	void moveFocusToAdjacentChannel(QWidget* from, bool forward);
	//! A stable identity for @a control across strips (-1 for anything that is not a traversal target).
	int focusRank(const QWidget* control) const;
	//! Each control's name carries the strip's name, so two strips' "Mute" read apart.
	void updateAccessibleNames();

private:
	SendButtonIndicator* m_sendButton;
	QLabel* m_receiveArrow;
	QStackedWidget* m_receiveArrowOrSendButton;
	int m_receiveArrowStackedIndex = -1;
	int m_sendButtonStackedIndex = -1;

	Knob* m_sendKnob;
	LcdWidget* m_channelNumberLcd;
	QLineEdit* m_renameLineEdit;
	QGraphicsView* m_renameLineEditView;
	QLabel* m_sendArrow;
	AutomatableButton* m_muteButton;
	AutomatableButton* m_soloButton;
	PeakIndicator* m_peakIndicator = nullptr;
	QLabel* m_meterInfo = nullptr;  // 0.5.0 L08: the clip light and the gain reduction
	QLabel* m_insertList = nullptr;  // 0.5.0 L10 (MX-073): the inserts, by name
	MacroKnobs* m_macroKnobs = nullptr;  // 0.5.0 L10 (MX-090): the rack's first macros
	ConsoleStripSection* m_consoleSection = nullptr;  // Q050-088: trim, polarity, EQ, the Console's drive and glue
	Knob* m_panKnob;
	Fader* m_fader;
	QLabel* m_faderTarget = nullptr;  // 0.5.0 theme 6: what the fader drives, with the faders on a return's sends
	FloatModel* m_noSendModel = nullptr;  // ... and what it shows when the channel has no send there
	QLabel* m_typeBadge = nullptr;  // 0.5.0 theme 16: what the strip is, in words
	QLabel* m_routingLine = nullptr;  // ... where its signal goes (and, in the routing view, comes from)
	QLabel* m_stateLine = nullptr;  // ... the modes it is in
	EffectRackView* m_effectRackView;
	MixerView* m_mixerView;
	int m_channelIndex = 0;
	bool m_inRename = false;

	QBrush m_backgroundActive;
	QColor m_strokeOuterActive;
	QColor m_strokeOuterInactive;
	QColor m_strokeInnerActive;
	QColor m_strokeInnerInactive;

	friend class MixerView;
};
} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_CHANNEL_VIEW_H
