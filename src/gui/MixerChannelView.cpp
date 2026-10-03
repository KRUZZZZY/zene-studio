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

#include <QCheckBox>
#include <QFont>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QStackedWidget>
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

	m_peakIndicator = new PeakIndicator(this);
	connect(m_fader, &Fader::peakChanged, m_peakIndicator, &PeakIndicator::updatePeak);

	m_effectRackView = new EffectRackView{&mixerChannel->m_fxChain, mixerView->m_racksWidget};
	m_effectRackView->setFixedWidth(EffectRackView::DEFAULT_WIDTH);

	auto mainLayout = new QVBoxLayout{this};
	mainLayout->setContentsMargins(4, 4, 4, 4);
	mainLayout->setSpacing(2);

	mainLayout->addWidget(m_receiveArrowOrSendButton, 0, Qt::AlignHCenter);
	mainLayout->addWidget(m_sendKnob, 0, Qt::AlignHCenter);
	mainLayout->addWidget(m_sendArrow, 0, Qt::AlignHCenter);
	mainLayout->addWidget(m_channelNumberLcd, 0, Qt::AlignHCenter);
	mainLayout->addWidget(m_renameLineEdit, 0, Qt::AlignHCenter);
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
	mainLayout->addWidget(m_peakIndicator);
	mainLayout->addWidget(m_fader, 1, Qt::AlignHCenter);

	// 0.5.0 L08 (MX-126 / MX-130): the meter line - gain reduction, and a clip light a click clears.
	m_meterInfo = new QLabel{this};
	m_meterInfo->setObjectName(QStringLiteral("mixerMeterInfo"));
	m_meterInfo->setTextFormat(Qt::RichText);
	m_meterInfo->setToolTip(tr("Gain reduction (dB) and the clip light - click CLIP to clear the lights"));
	connect(m_meterInfo, &QLabel::linkActivated, this, [this] {
		runCommand(this, CommandCall{QStringLiteral("mixer.clear_clips"), {}, {}, QString(), false});
	});
	mainLayout->addWidget(m_meterInfo, 0, Qt::AlignHCenter);

	connect(m_renameLineEdit, &QLineEdit::editingFinished, this, &MixerChannelView::renameFinished);

	setFocusPolicy(Qt::StrongFocus);
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
}

void MixerChannelView::mousePressEvent(QMouseEvent*)
{
	if (m_mixerView->currentMixerChannel() != this) { m_mixerView->setCurrentMixerChannel(this); }
}

void MixerChannelView::mouseDoubleClickEvent(QMouseEvent*)
{
	renameChannel();
}

void MixerChannelView::keyPressEvent(QKeyEvent* ke)
{
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
		mc->m_name = newName;
		m_renameLineEdit->setText(elideName(newName));
		Engine::getSong()->setModified();
	}

	setToolTip(mc->m_name);
	setAccessibleName(mc->m_name);
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
