/*
 * MixerChannelView.h - the mixer channel view
 *
 * Copyright (c) 2022 saker <sakertooth@gmail.com>
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

#include "MixerChannelView.h"

#include "DesignTokens.h"

#include <algorithm>

#include <QCheckBox>
#include <QChildEvent>
#include <QFont>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "AutomatableButton.h"
#include "CaptionMenu.h"
#include "ColorChooser.h"
#include "CommandForm.h"
#include "ConfigManager.h"
#include "ControlVocabulary.h"
#include "EffectRackView.h"
#include "Fader.h"
#include "FeedbackModeAction.h"
#include "FontHelper.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "VcaStripView.h"
#include "LcdWidget.h"
#include "MacroKnobs.h"
#include "lmms_math.h"
#include "Mixer.h"
#include "MixerView.h"
#include "PeakIndicator.h"
#include "RackMenu.h"
#include "SendButtonIndicator.h"
#include "MixerHardwareMenu.h"
#include "MixerStripTags.h"
#include "MixerViewOptions.h"
#include "ShellCommandMenus.h"
#include "Song.h"

namespace lmms::gui {
MixerChannelView::MixerChannelView(QWidget* parent, MixerView* mixerView, int channelIndex)
	: QWidget(parent)
	, m_mixerView(mixerView)
	, m_channelIndex(channelIndex)
{
	auto retainSizeWhenHidden = [](QWidget* widget) {
		auto sizePolicy = widget->sizePolicy();
		sizePolicy.setRetainSizeWhenHidden(true);
		widget->setSizePolicy(sizePolicy);
	};

	auto receiveArrowContainer = new QWidget{};
	auto receiveArrowLayout = new QVBoxLayout{receiveArrowContainer};
	m_receiveArrow = new QLabel{};
	m_receiveArrow->setPixmap(embed::getIconPixmap("receive_bg_arrow"));
	receiveArrowLayout->setContentsMargins(0, 0, 0, 0);
	receiveArrowLayout->setSpacing(0);
	receiveArrowLayout->addWidget(m_receiveArrow, 0, Qt::AlignHCenter);

	auto sendButtonContainer = new QWidget{};
	auto sendButtonLayout = new QVBoxLayout{sendButtonContainer};
	m_sendButton = new SendButtonIndicator{this, this, mixerView};
	sendButtonLayout->setContentsMargins(0, 0, 0, 0);
	sendButtonLayout->setSpacing(0);
	sendButtonLayout->addWidget(m_sendButton, 0, Qt::AlignHCenter);

	m_receiveArrowOrSendButton = new QStackedWidget{this};
	m_receiveArrowStackedIndex = m_receiveArrowOrSendButton->addWidget(receiveArrowContainer);
	m_sendButtonStackedIndex = m_receiveArrowOrSendButton->addWidget(sendButtonContainer);
	retainSizeWhenHidden(m_receiveArrowOrSendButton);

	m_sendKnob = new Knob{KnobType::Bright26, this, tr("Channel send amount")};
	retainSizeWhenHidden(m_sendKnob);

	m_sendArrow = new QLabel{};
	m_sendArrow->setPixmap(embed::getIconPixmap("send_bg_arrow"));
	retainSizeWhenHidden(m_sendArrow);

	m_channelNumberLcd = new LcdWidget{2, this};
	m_channelNumberLcd->setValue(channelIndex);
	retainSizeWhenHidden(m_channelNumberLcd);

	const auto mixerChannel = Engine::mixer()->mixerChannel(channelIndex);
	const auto mixerName = mixerChannel->m_name;
	setToolTip(mixerName);
	setAccessibleName(mixerName); // R8.5 (kept in step with the tooltip on rename)

	// 0.5.0 (docs/UI-SHELL.md §1): the channel's name reads horizontally, centred above the
	// strip's controls, as modern mixers show it - in place of the rotated label drawn through a
	// QGraphicsView. It is the same line edit, read-only until a rename starts.
	m_renameLineEdit = new QLineEdit{mixerName, this};
	m_renameLineEdit->setObjectName(QStringLiteral("mixerChannelName"));
	m_renameLineEdit->setFixedWidth(56);
	m_renameLineEdit->setAlignment(Qt::AlignCenter);
	m_renameLineEdit->setReadOnly(true);
	m_renameLineEdit->setFocusPolicy(Qt::NoFocus);
	m_renameLineEdit->installEventFilter(this);
	m_renameLineEditView = nullptr;
	m_renameLineEdit->setText(elideName(mixerName));
	// 0.5.0 L09 (MX-045): a return channel (a bus) reads as one - its own name style and tooltip.
	if (mixerChannel->isBus())
	{
		m_renameLineEdit->setProperty("returnChannel", true);
		m_renameLineEdit->setStyleSheet(QStringLiteral("font-style: italic;"));
		setToolTip(tr("%1 (return - sends into it are pre-fader)").arg(mixerName));
	}

	m_muteButton = new AutomatableButton(this, tr("Mute"));
	m_muteButton->setObjectName("btn-mute");
	m_muteButton->setToolTip(tr("Mute this channel"));
	m_muteButton->setCheckable(true);
	m_muteButton->setModel(&mixerChannel->m_muteModel);

	m_soloButton = new AutomatableButton(this, tr("Solo"));
	m_soloButton->setObjectName("btn-solo");
	m_soloButton->setToolTip(tr("Solo this channel"));
	m_soloButton->setCheckable(true);
	m_soloButton->setModel(&mixerChannel->m_soloModel);

	auto soloMuteLayout = new QVBoxLayout();
	soloMuteLayout->setContentsMargins(0, 2, 0, 2);
	soloMuteLayout->setSpacing(2);
	soloMuteLayout->addWidget(m_soloButton, 0, Qt::AlignHCenter);
	soloMuteLayout->addWidget(m_muteButton, 0, Qt::AlignHCenter);

	// Channel pan (MixerChannel::m_panModel): -1 .. +1, a balance law.
	m_panKnob = new Knob{KnobType::Small17, this, tr("Pan %1").arg(channelIndex)};
	m_panKnob->setModel(&mixerChannel->m_panModel);
	m_panKnob->setHintText(tr("Pan:"), "");
	m_panKnob->setToolTip(tr("Pan this channel (centre = both sides at unity)"));

	m_fader = new Fader{&mixerChannel->m_volumeModel, tr("Fader %1").arg(channelIndex), this};
	// 0.5.0 theme 6: what the fader drives while the faders show a return's sends (MixerOverlays.h).
	m_faderTarget = new QLabel{this};
	m_faderTarget->setObjectName(QStringLiteral("mixerFaderTarget"));
	m_faderTarget->setAlignment(Qt::AlignHCenter);
	m_faderTarget->setFont(tokens::font(tokens::TextRole::Caption));
	m_faderTarget->hide();
	m_noSendModel = new FloatModel{0.f, 0.f, 1.f, 0.001f, nullptr, tr("No send")};  // a send's range, at the bottom
	m_noSendModel->setParent(this);

	m_peakIndicator = new PeakIndicator(this);
	connect(m_fader, &Fader::peakChanged, m_peakIndicator, &PeakIndicator::updatePeak);

	m_effectRackView = new EffectRackView{&mixerChannel->m_fxChain, mixerView->m_racksWidget};
	m_effectRackView->setFixedWidth(EffectRackView::DEFAULT_WIDTH);

	auto mainLayout = new QVBoxLayout{this};
	mainLayout->setContentsMargins(4, 4, 4, 4);
	mainLayout->setSpacing(2);

	// 0.5.0 theme 16: the type badge on top; the routing and mode lines under the name (MixerStripTags.h).
	m_typeBadge = mixerstriptags::makeBadge(this);
	m_routingLine = mixerstriptags::makeLine(this, QStringLiteral("mixerRoutingLine"));
	m_stateLine = mixerstriptags::makeLine(this, QStringLiteral("mixerStateLine"));
	mainLayout->addWidget(m_typeBadge, 0, Qt::AlignHCenter);
	mainLayout->addWidget(m_receiveArrowOrSendButton, 0, Qt::AlignHCenter);
	mainLayout->addWidget(m_sendKnob, 0, Qt::AlignHCenter);
	mainLayout->addWidget(m_sendArrow, 0, Qt::AlignHCenter);
	mainLayout->addWidget(m_channelNumberLcd, 0, Qt::AlignHCenter);
	mainLayout->addWidget(m_renameLineEdit, 0, Qt::AlignHCenter);
	mainLayout->addWidget(m_routingLine, 0, Qt::AlignHCenter);
	mainLayout->addWidget(m_stateLine, 0, Qt::AlignHCenter);
	mainLayout->addLayout(soloMuteLayout);
	mainLayout->addWidget(m_panKnob, 0, Qt::AlignHCenter);
	// 0.5.0 L10: the rack's first four macros (MX-090) and the inserts by name (MX-073); MixerViewOptions shows them.
	m_macroKnobs = new MacroKnobs(control::channelIdOf(mixerChannel), 4, true, this);
	mainLayout->addWidget(m_macroKnobs, 0, Qt::AlignHCenter);
	m_insertList = new QLabel{this};
	m_insertList->setObjectName(QStringLiteral("mixerInsertList"));
	m_insertList->setAlignment(Qt::AlignHCenter);
	m_insertList->setWordWrap(true);
	mainLayout->addWidget(m_insertList);
	mainLayout->addWidget(m_faderTarget, 0, Qt::AlignHCenter);
	mainLayout->addWidget(m_peakIndicator);
	mainLayout->addWidget(m_fader, 1, Qt::AlignHCenter);

	// 0.5.0 L08 (MX-126 / MX-130): the meter line - gain reduction, and a clip light a click clears.
	m_meterInfo = new QLabel{this};
	m_meterInfo->setObjectName(QStringLiteral("mixerMeterInfo"));
	m_meterInfo->setTextFormat(Qt::RichText);
	m_meterInfo->setToolTip(tr("Gain reduction (dB) and the clip light - click CLIP to clear the lights"));
	connect(m_meterInfo, &QLabel::linkActivated, this, [this] {
		runCommand(this, CommandCall{QStringLiteral("mixer.clear_clips"), {}, {}, QString(), false});
		// The lights go out now, not at the next half-second sync (queued: a sync may rebuild this strip).
		QTimer::singleShot(0, m_mixerView, &MixerView::syncWithMixer);
	});
	mainLayout->addWidget(m_meterInfo, 0, Qt::AlignHCenter);

	connect(m_renameLineEdit, &QLineEdit::editingFinished, this, &MixerChannelView::renameFinished);
	updateAccessibleNames();

	setFocusPolicy(Qt::StrongFocus);
	// Child controls receive mouse presses directly, so the strip's mousePressEvent does not see them.
	// Filter every descendant to select this strip before a control handles its press.
	for (QWidget* widget : findChildren<QWidget*>()) { widget->installEventFilter(this); }
}


bool MixerChannelView::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::ChildAdded)
	{
		// Descendant controls can be rebuilt after strip construction (for example, rack macro knobs).
		// Install on the QObject directly so descendants added by it are covered as well.
		static_cast<QChildEvent*>(event)->child()->installEventFilter(this);
	}
	else if (event->type() == QEvent::MouseButtonPress && m_mixerView->currentMixerChannel() != this)
	{
		m_mixerView->setCurrentMixerChannel(this);
	}
	else if (event->type() == QEvent::FocusIn && watched != this && watched != m_sendKnob)
	{
		// The selected strip is the one the keyboard is in: the mixer's own shortcuts (Left/Right,
		// Up/Down) follow the focus, not a strip clicked earlier. A send knob is the selected source's
		// send to this strip, so selecting its destination would hide the very control being focused.
		if (m_mixerView->currentMixerChannel() != this) { m_mixerView->setCurrentMixerChannel(this); }
	}
	else if (event->type() == QEvent::KeyPress)
	{
		auto* widget = qobject_cast<QWidget*>(watched);
		if (widget != nullptr && widget != this && focusRank(widget) >= 0
			&& handleTraversalKey(static_cast<QKeyEvent*>(event), widget))
		{
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}

QList<QWidget*> MixerChannelView::keyboardFocusOrder() const
{
	QList<QWidget*> controls;
	const auto add = [this, &controls](QWidget* control) {
		if (control != nullptr && (control->focusPolicy() & Qt::TabFocus) != 0
			&& control->isEnabled() && control->isVisibleTo(m_mixerView))
		{
			controls.append(control);
		}
	};
	add(m_sendKnob);
	add(m_soloButton);
	add(m_muteButton);
	add(m_panKnob);
	if (m_macroKnobs != nullptr)
	{
		for (Knob* knob : m_macroKnobs->findChildren<Knob*>(QString(), Qt::FindDirectChildrenOnly)) { add(knob); }
	}
	add(m_fader);
	return controls;
}

int MixerChannelView::focusRank(const QWidget* control) const
{
	if (control == nullptr) { return -1; }
	if (control == m_sendKnob) { return 0; }
	if (control == m_soloButton) { return 1; }
	if (control == m_muteButton) { return 2; }
	if (control == m_panKnob) { return 3; }
	if (m_macroKnobs != nullptr)
	{
		const QList<Knob*> knobs = m_macroKnobs->findChildren<Knob*>(QString(), Qt::FindDirectChildrenOnly);
		for (int i = 0; i < knobs.size(); ++i)
		{
			if (knobs.at(i) == control) { return 4 + i; }
		}
	}
	if (control == m_fader) { return 100; }
	return -1;
}

bool MixerChannelView::handleTraversalKey(QKeyEvent* key, QWidget* from)
{
	if (key->key() != Qt::Key_Tab && key->key() != Qt::Key_Backtab) { return false; }
	const bool backward = key->key() == Qt::Key_Backtab || (key->modifiers() & Qt::ShiftModifier);
	if (key->modifiers() & Qt::ControlModifier) { moveFocusToAdjacentChannel(from, !backward); }
	else { moveFocusWithinStrip(from, !backward); }
	key->accept();
	return true;
}

void MixerChannelView::moveFocusWithinStrip(QWidget* from, bool forward)
{
	if (m_mixerView->currentMixerChannel() != this) { m_mixerView->setCurrentMixerChannel(this); }
	const QList<QWidget*> controls = keyboardFocusOrder();
	if (controls.isEmpty()) { return; }
	const int position = controls.indexOf(from);
	const int next = position < 0 ? (forward ? 0 : controls.size() - 1)
		: (position + (forward ? 1 : -1) + controls.size()) % controls.size();
	controls.at(next)->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
}

void MixerChannelView::moveFocusToAdjacentChannel(QWidget* from, bool forward)
{
	const int rank = focusRank(from);
	const int position = keyboardFocusOrder().indexOf(from);
	MixerView* view = m_mixerView;
	for (int index = m_channelIndex + (forward ? 1 : -1); index >= 0 && index < view->channelCount();
		index += forward ? 1 : -1)
	{
		MixerChannelView* other = view->channelView(index);
		if (other == nullptr || !other->isVisibleTo(view)) { continue; }
		// Select first: the send knob's model and visibility depend on the current channel. A focused
		// send knob keeps its source selected while moving between destination strips.
		if (from != m_sendKnob) { view->setCurrentMixerChannel(other); }
		const QList<QWidget*> targets = other->keyboardFocusOrder();
		if (targets.isEmpty()) { continue; }
		QWidget* target = nullptr;
		for (QWidget* candidate : targets)
		{
			if (other->focusRank(candidate) == rank) { target = candidate; break; }
		}
		if (target == nullptr)
		{
			const int clamped = std::clamp(position < 0 ? 0 : position, 0, static_cast<int>(targets.size()) - 1);
			target = targets.at(clamped);
		}
		target->setFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
		return;
	}
	// At the first or last channel the command leaves focus where it is - it never wraps.
}

void MixerChannelView::updateAccessibleNames()
{
	const MixerChannel* channel = mixerChannel();
	if (channel == nullptr) { return; }
	const QString stripName = channel->m_name;
	const auto name = [stripName](QWidget* control, const QString& role) {
		if (control != nullptr)
		{
			const QString text = tr("%1 %2").arg(role, stripName);
			if (control->accessibleName() != text) { control->setAccessibleName(text); }
		}
	};
	name(m_sendKnob, tr("Send to"));
	name(m_soloButton, tr("Solo"));
	name(m_muteButton, tr("Mute"));
	name(m_panKnob, tr("Pan"));
	name(m_fader, tr("Fader"));
	if (m_macroKnobs != nullptr)
	{
		const QString macros = tr("Macros of %1").arg(stripName);
		if (m_macroKnobs->accessibleName() != macros) { m_macroKnobs->setAccessibleName(macros); }
	}
}

void MixerChannelView::setSendsShown(bool shown)
{
	// Shown, the send row keeps its space while one of its widgets is hidden (the strips stay aligned
	// as the current channel changes); hidden by the option, the strip closes up around it.
	for (QWidget* widget : std::initializer_list<QWidget*>{m_receiveArrowOrSendButton, m_sendKnob, m_sendArrow})
	{
		QSizePolicy policy = widget->sizePolicy();
		policy.setRetainSizeWhenHidden(shown);
		widget->setSizePolicy(policy);
		if (!shown) { widget->setVisible(false); }
	}
}

namespace
{

//! Theme 6: the line above a fader - its text, its spoken form (tooltip, accessible name) and colour.
struct FaderLine
{
	QString caption;
	QString spoken;
	QColor colour;
};

//! The line for @a role with the faders on @a returnName's sends; none on the channel faders (empty name).
FaderLine faderLineFor(mixeroverlays::FaderRole role, const QString& returnName)
{
	using mixeroverlays::FaderRole;
	const tokens::Palette& pal = tokens::palette();
	if (returnName.isEmpty()) { return {}; }
	if (role == FaderRole::Send)
	{
		return {QString(QChar(0x2192)) + QLatin1Char(' ') + returnName,
			MixerChannelView::tr("Fader: the send to %1").arg(returnName), pal.accent};
	}
	if (role == FaderRole::NoSend)
	{
		return {MixerChannelView::tr("No send"),
			MixerChannelView::tr("No send to %1 on this channel - the channel menu's Send To makes one").arg(returnName),
			pal.textMuted};
	}
	return {MixerChannelView::tr("Volume"), MixerChannelView::tr("Fader: this channel's volume (not a send)"), pal.text};
}

} // namespace

void MixerChannelView::showFaderRole(mixeroverlays::FaderRole role, const QString& returnName)
{
	const bool noSend = role == mixeroverlays::FaderRole::NoSend;
	m_fader->setEnabled(!noSend);
	// Dimmed, so it reads as nothing to move; the effect exists only while it is (a null one deletes it).
	if (noSend != (m_fader->graphicsEffect() != nullptr))
	{
		auto* dim = noSend ? new QGraphicsOpacityEffect{m_fader} : nullptr;
		if (dim != nullptr) { dim->setOpacity(0.35); }
		m_fader->setGraphicsEffect(dim);
	}
	const FaderLine line = faderLineFor(role, returnName);
	m_faderTarget->setVisible(!line.caption.isEmpty());
	if (m_fader->accessibleDescription() != line.spoken) { m_fader->setAccessibleDescription(line.spoken); }
	const QString text = QFontMetrics{m_faderTarget->font()}.elidedText(line.caption, Qt::ElideRight, m_renameLineEdit->width());
	if (m_faderTarget->text() == text && m_faderTarget->toolTip() == line.spoken) { return; }  // each set is announced
	m_faderTarget->setStyleSheet(line.colour.isValid() ? QStringLiteral("color: %1;").arg(line.colour.name()) : QString());
	m_faderTarget->setText(text);
	m_faderTarget->setToolTip(line.spoken);
	m_faderTarget->setAccessibleName(line.spoken);
}

void MixerChannelView::contextMenuEvent(QContextMenuEvent*)
{
	auto contextMenu = new CaptionMenu(mixerChannel()->m_name, this);

	if (!isMasterChannel()) // no move-options in master
	{
		contextMenu->addAction(tr("Move &left"), this, &MixerChannelView::moveChannelLeft);
		contextMenu->addAction(tr("Move &right"), this, &MixerChannelView::moveChannelRight);
	}

	contextMenu->addAction(tr("Rename &channel"), this, &MixerChannelView::renameChannel);
	contextMenu->addSeparator();
	contextMenu->addAction(m_mixerView->speakMeterAction());
	auto* peakWatch = contextMenu->addMenu(tr("Peak Watch"));
	m_mixerView->populatePeakWatchMenu(peakWatch);
	contextMenu->addSeparator();

	if (!isMasterChannel()) // no remove-option in master
	{
		contextMenu->addAction(
			embed::getIconPixmap("cancel"), tr("R&emove channel"), this, &MixerChannelView::removeChannel);
		contextMenu->addSeparator();
	}

	contextMenu->addAction(
		embed::getIconPixmap("cancel"), tr("Remove &unused channels"), this, &MixerChannelView::removeUnusedChannels);
	contextMenu->addSeparator();

	auto colorMenu = QMenu{tr("Color"), this};
	colorMenu.setIcon(embed::getIconPixmap("colorize"));
	colorMenu.addAction(tr("Change"), this, &MixerChannelView::selectColor);
	colorMenu.addAction(tr("Reset"), this, &MixerChannelView::resetColor);
	colorMenu.addAction(tr("Pick random"), this, &MixerChannelView::randomizeColor);
	contextMenu->addMenu(&colorMenu);

	// M3.7: VCA membership, through the vca.* commands (one undoable step each).
	auto vcaMenu = QMenu{tr("VCA group"), this};
	MixerView* view = m_mixerView;
	populateVcaMenu(&vcaMenu, static_cast<mix_ch_t>(m_channelIndex), [view]() { view->refreshVcaStrips(); });
	contextMenu->addMenu(&vcaMenu);
	// Parallel chains, the chain the rack routes to, and its macros (rack.*).
	addRackMenu(contextMenu, mixerChannel());
	// The cycle-permitted submode (feedback.enable / feedback.disable), mixer-wide.
	addFeedbackModeAction(contextMenu, [view]() {
		for (int i = 0; i < static_cast<int>(Engine::mixer()->numChannels()); ++i) { view->updateMixerChannel(i); }
	});

	// 0.5.0 F1: the channel's hardware output, cue sends and hardware insert (MixerHardwareMenu.h).
	addHardwareMenu(contextMenu, control::channelIdOf(mixerChannel()));
	// 0.5.0 F7 (MX-060): the channel's speaker format, its surround panner and the downmix (MixerHardwareMenu.h).
	addSurroundMenu(contextMenu, control::channelIdOf(mixerChannel()));
	// 0.5.0 W1: sends (pre/post), sidechain, rack zones and wiring - the Inspector's channel menu.
	ShellCommandMenus::populateChannel(contextMenu->addMenu(tr("More")), control::channelIdOf(mixerChannel()));
	contextMenu->exec(QCursor::pos());
	delete contextMenu;
}

void MixerChannelView::paintEvent(QPaintEvent*)
{
	// 0.5.0 (docs/UI-SHELL.md §1): a flat strip - the raised surface when selected, the channel's
	// colour as a slim bar along the top (dimmed when muted), a hairline seam to the next strip -
	// in place of the double bevelled border and the darkened full-strip tint.
	const tokens::Palette& pal = tokens::palette();
	const auto channel = mixerChannel();
	const auto isActive = m_mixerView->currentMixerChannel() == this;
	auto painter = QPainter{this};
	painter.fillRect(rect(), isActive ? pal.raised : pal.panel);
	if (channel->color().has_value())
	{
		QColor bar = *channel->color();
		if (channel->m_muteModel.value()) { bar.setAlpha(80); }
		painter.fillRect(QRect(0, 0, width(), 3), bar);
	}
	painter.fillRect(QRect(width() - 1, 0, 1, height()), pal.base);
	if (isActive)
	{
		painter.setPen(QPen(pal.accent, 1));
		painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -1.5, -0.5));
	}
	// 0.5.0 L08 (MX-005): a channel in the Selected filter's set wears an accent bar along its foot.
	if (mixerviewoptions::isSelected(m_channelIndex)) { painter.fillRect(QRect(0, height() - 3, width() - 1, 3), pal.accent); }
}

void MixerChannelView::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) { return; }
	if (m_mixerView->currentMixerChannel() != this) { m_mixerView->setCurrentMixerChannel(this); }
	// 0.5.0 L08 (MX-005): Ctrl+click puts the channel in the Selected filter's set, or takes it out.
	if (event->modifiers() & Qt::ControlModifier) { mixerviewoptions::toggleSelected(this, m_channelIndex); }
}

void MixerChannelView::mouseDoubleClickEvent(QMouseEvent*)
{
	renameChannel();
}

void MixerChannelView::keyPressEvent(QKeyEvent* ke)
{
	// Focus on the strip (not on one of its controls) enters its controls at either end.
	if (handleTraversalKey(ke, this)) { return; }
	if (ke->key() == Qt::Key_Enter || ke->key() == Qt::Key_Return)
	{
		if (m_inRename)
		{
			renameFinished();
		}
	}
	else
	{
		ke->ignore();
	}
}

void MixerChannelView::setChannelIndex(int index)
{
	m_channelNumberLcd->setValue(index);
	m_channelIndex = index;
}

void MixerChannelView::renameChannel()
{
	m_inRename = true;
	setToolTip("");
	m_renameLineEdit->setReadOnly(false);

	m_channelNumberLcd->hide();
	m_renameLineEdit->setFixedWidth(m_renameLineEdit->width());
	m_renameLineEdit->setText(mixerChannel()->m_name);

	m_renameLineEdit->setFocusPolicy(Qt::StrongFocus);
	m_renameLineEdit->selectAll();
	m_renameLineEdit->setFocus();
}

void MixerChannelView::renameFinished()
{
	m_inRename = false;

	m_renameLineEdit->deselect();
	m_renameLineEdit->setReadOnly(true);
	m_renameLineEdit->setFocusPolicy(Qt::NoFocus);
	m_renameLineEdit->setFixedWidth(m_renameLineEdit->width());
	m_channelNumberLcd->show();

	auto newName = m_renameLineEdit->text();
	setFocus();

	const auto mc = mixerChannel();
	if (!newName.isEmpty() && mc->m_name != newName)
	{
		// 0.5.0 wave 3: through mixer.rename_channel - one undo step, and the same path an agent takes.
		runCommand(this, CommandCall{QStringLiteral("mixer.rename_channel"),
			{{QStringLiteral("channel"), control::channelIdOf(mc)}, {QStringLiteral("name"), newName}}, {}, QString(), false});
	}
	m_renameLineEdit->setText(elideName(mc->m_name));

	setToolTip(mc->m_name);
	setAccessibleName(mc->m_name);
	updateAccessibleNames();
}

void MixerChannelView::resetColor()
{
	mixerChannel()->setColor(std::nullopt);
	Engine::getSong()->setModified();
	update();
}

void MixerChannelView::selectColor()
{
	const auto channel = mixerChannel();

	const auto initialColor = channel->color().value_or(backgroundActive().color());
	const auto* colorChooser = ColorChooser{this}.withPalette(ColorChooser::Palette::Mixer);
	const auto newColor = colorChooser->getColor(initialColor);

	if (!newColor.isValid()) { return; }

	channel->setColor(newColor);

	Engine::getSong()->setModified();
	update();
}

void MixerChannelView::randomizeColor()
{
	auto channel = mixerChannel();
	channel->setColor(ColorChooser::getPalette(ColorChooser::Palette::Mixer)[fastRand(48)]);
	Engine::getSong()->setModified();
	update();
}

bool MixerChannelView::confirmRemoval(int index)
{
	// if config variable is set to false, there is no need for user confirmation
	bool needConfirm = ConfigManager::inst()->value("ui", "mixerchanneldeletionwarning", "1").toInt();
	if (!needConfirm) { return true; }

	// is the channel is not in use, there is no need for user confirmation
	if (!getGUI()->mixerView()->getMixer()->isChannelInUse(index)) { return true; }

	QString messageRemoveTrack = tr("This Mixer Channel is being used.\n"
									"Are you sure you want to remove this channel?\n\n"
									"Warning: This operation can not be undone.");

	QString messageTitleRemoveTrack = tr("Confirm removal");
	QString askAgainText = tr("Don't ask again");
	auto askAgainCheckBox = new QCheckBox(askAgainText, nullptr);
	auto onCheckedStateChanged = [](auto state) {
		// Invert button state, if it's checked we *shouldn't* ask again
		ConfigManager::inst()->setValue("ui", "mixerchanneldeletionwarning", state != Qt::Unchecked ? "0" : "1");
	};

#if (QT_VERSION >= QT_VERSION_CHECK(6, 7, 0))
	connect(askAgainCheckBox, &QCheckBox::checkStateChanged, onCheckedStateChanged);
#else
	connect(askAgainCheckBox, &QCheckBox::stateChanged, onCheckedStateChanged);
#endif

	QMessageBox mb;
	mb.setText(messageRemoveTrack);
	mb.setWindowTitle(messageTitleRemoveTrack);
	mb.setIcon(QMessageBox::Warning);
	mb.addButton(QMessageBox::Cancel);
	mb.addButton(QMessageBox::Ok);
	mb.setCheckBox(askAgainCheckBox);
	mb.setDefaultButton(QMessageBox::Cancel);

	int answer = mb.exec();

	return answer == QMessageBox::Ok;
}

void MixerChannelView::removeChannel()
{
	if (!confirmRemoval(m_channelIndex)) { return; }
	auto mix = getGUI()->mixerView();
	mix->deleteChannel(m_channelIndex);
}

void MixerChannelView::removeUnusedChannels()
{
	auto mix = getGUI()->mixerView();
	mix->deleteUnusedChannels();
}

void MixerChannelView::moveChannelLeft()
{
	auto mix = getGUI()->mixerView();
	mix->moveChannelLeft(m_channelIndex);
}

void MixerChannelView::moveChannelRight()
{
	auto mix = getGUI()->mixerView();
	mix->moveChannelRight(m_channelIndex);
}

void MixerChannelView::syncName()
{
	const MixerChannel* mc = mixerChannel();
	if (m_inRename || mc == nullptr) { return; }
	const QString shown = elideName(mc->m_name);
	if (m_renameLineEdit->text() != shown) { m_renameLineEdit->setText(shown); }
	updateAccessibleNames();
}

QString MixerChannelView::elideName(const QString& name)
{
	const auto maxTextHeight = m_renameLineEdit->width();
	const auto metrics = QFontMetrics{m_renameLineEdit->font()};
	const auto elidedName = metrics.elidedText(name, Qt::ElideRight, maxTextHeight);
	return elidedName;
}

MixerChannel* MixerChannelView::mixerChannel() const
{
	return Engine::mixer()->mixerChannel(m_channelIndex);
}

void MixerChannelView::reset()
{
	m_peakIndicator->resetPeakToMinusInf();
}

} // namespace lmms::gui
