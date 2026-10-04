/*
 * DevicePanel.cpp - the shell's Device panel: the selected track's devices as cards
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

#include "DevicePanel.h"

#include <algorithm>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "AudioBusHandle.h"
#include "ChannelLevelMeter.h"
#include "ComboBox.h"
#include "CommandForm.h"
#include "ControlDeviceSupport.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "EffectControlDialog.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "AudioPortsModel.h"
#include "Knob.h"
#include "PinConnector.h"
#include "ParameterAliases.h"
#include "DeviceCardState.h"
#include "DevicePicker.h"
#include "LedCheckBox.h"
#include "MacroKnobs.h"
#include "MainWindow.h"
#include "MeterModel.h"
#include "Mixer.h"
#include "SampleTrack.h"
#include "ShellIcons.h"
#include "ShellSelection.h"
#include "SongEditor.h"
#include "SubWindow.h"
#include "TransportReadouts.h"

namespace lmms::gui
{

namespace
{

ControlResult invoke(const QString& id, const QJsonObject& args)
{
	return ControlRegistry::instance()->invoke(id, args);
}

EffectChain* trackChain(Track* track)
{
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track)) { return instrument->audioBusHandle()->effects(); }
	if (auto* sample = dynamic_cast<SampleTrack*>(track)) { return sample->audioBusHandle()->effects(); }
	return nullptr;
}

//! The mixer channel @a track plays into, when it is one of its own: the master's inserts are
//! every track's, so they are not drawn as this track's devices.
MixerChannel* trackChannel(Track* track)
{
	IntModel* channel = track != nullptr ? track->mixerChannelModel() : nullptr;
	if (channel == nullptr || Engine::mixer() == nullptr) { return nullptr; }
	const int index = channel->value();
	return index > 0 && index < static_cast<int>(Engine::mixer()->numChannels())
		? Engine::mixer()->mixerChannel(index) : nullptr;
}

//! The property a placed widget carries its key in (place()).
constexpr const char* KeyProperty = "devicePanelKey";

//! What a card is drawn from - its device, target, title and parameter captions: a card whose key still matches is
//! kept as it is when the panel redraws (theme 12).
QString cardKey(const char* kind, const void* device, const QString& target, const QString& title, const QStringList& names)
{
	return QStringLiteral("%1:%2|%3|%4|%5").arg(QLatin1String(kind), QString::number(reinterpret_cast<quintptr>(device), 16),
		target, title, names.join(QChar(0x1f)));
}

MixerChannel* selectedChannel()
{
	const QString id = ShellSelection::instance()->channel();
	ControlResult error;
	return id.isEmpty() ? nullptr : control::resolveMixerChannel(id, &error);
}

} // namespace

DevicePanel::DevicePanel(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("devicePanel"));
	setAccessibleName(tr("Devices"));
	auto* column = new QVBoxLayout(this);
	column->setContentsMargins(0, 0, 0, 0);
	m_scroll = new QScrollArea(this);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setWidgetResizable(true);
	m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);  // a tall card stays reachable
	m_row = new QWidget(m_scroll);
	m_cards = new QHBoxLayout(m_row);
	m_cards->setContentsMargins(tokens::space::md, tokens::space::md, tokens::space::md, tokens::space::md);
	m_cards->setSpacing(tokens::space::md);
	m_empty = new QLabel(tr("Select a track or a mixer channel to see its devices."), m_row);
	m_empty->setObjectName(QStringLiteral("deviceCaption"));
	m_add = buildAddButton();
	m_cards->addWidget(m_empty);
	m_cards->addWidget(m_add);
	m_cards->addStretch(1);
	m_scroll->setWidget(m_row);
	column->addWidget(m_scroll);
	connect(ShellSelection::instance(), &ShellSelection::changed, this, &DevicePanel::scheduleRebuild);
	m_pending.setSingleShot(true);
	m_pending.setInterval(0);
	connect(&m_pending, &QTimer::timeout, this, &DevicePanel::rebuild);
	m_poll.setInterval(500);
	connect(&m_poll, &QTimer::timeout, this, &DevicePanel::poll);
	rebuild();
}

QWidget* DevicePanel::buildAddButton()
{
	auto* add = new QToolButton(m_row);
	add->setObjectName(QStringLiteral("deviceAdd"));
	add->setIcon(shellicons::icon(QStringLiteral("plus")));
	add->setText(tr("Add device"));
	add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	add->setAccessibleName(tr("Add a device to the selected track or channel"));
	add->setMinimumHeight(tokens::targetSize());
	// Theme 12: the searchable picker (DevicePicker.h) - every loadable effect of every format, with its vendor and
	// state in words - in place of a menu of the built-in effects only.
	connect(add, &QToolButton::clicked, this, [this] {
		const QString device = DevicePicker::pick(this, DevicePicker::Kind::Effect, tr("Add a device to %1").arg(m_target));
		if (device.isEmpty() || m_target.isEmpty()) { return; }
		// runCommand: a refused load is shown, not swallowed.
		runCommand(this, CommandCall{QStringLiteral("plugin.load"), {{QStringLiteral("target"), m_target},
			{QStringLiteral("device"), device}}, {}, QString(), false});
	});
	return add;
}

void DevicePanel::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	m_poll.start();
	poll();
}

void DevicePanel::hideEvent(QHideEvent* event)
{
	QWidget::hideEvent(event);
	m_poll.stop();
}

void DevicePanel::retire(QWidget* widget)
{
	// Hidden and released now - before the device whose models a card draws is deleted - and destroyed once control
	// returns to the event loop, since the click that removed a device came from a button on its own card.
	if (auto* card = qobject_cast<DeviceCard*>(widget)) { card->closeEditor(); }
	m_cards->removeWidget(widget);
	widget->hide();
	widget->deleteLater();
}

void DevicePanel::dropCards()
{
	for (int i = m_cards->count() - 1; i >= 0; --i)
	{
		QWidget* widget = m_cards->itemAt(i)->widget();
		if (widget != nullptr && widget != m_empty && widget != m_add) { retire(widget); }
	}
	m_modulators.clear();
}

void DevicePanel::dropLeavingCards(const EffectChain* clearing)
{
	for (int i = m_cards->count() - 1; i >= 0; --i)
	{
		auto* card = qobject_cast<DeviceCard*>(m_cards->itemAt(i)->widget());
		const Effect* effect = card != nullptr ? card->spec().effect : nullptr;
		if (effect == nullptr) { continue; }
		// Every card's device is alive here: a device's removal signals its chain before the device is deleted, and
		// that signal retired its card.
		const EffectChain* chain = effect->effectChain();
		const bool leaving = chain == nullptr || chain == clearing
			|| std::find(chain->effects().begin(), chain->effects().end(), effect) == chain->effects().end();
		if (leaving) { retire(card); }
	}
}

void DevicePanel::dropInstrumentCards()
{
	for (int i = m_cards->count() - 1; i >= 0; --i)
	{
		auto* card = qobject_cast<DeviceCard*>(m_cards->itemAt(i)->widget());
		if (card != nullptr && card->spec().instrumentTrack != nullptr) { retire(card); }
	}
}

void DevicePanel::scheduleRebuild()
{
	// Theme 12: what is shown stays until the redraw, which keeps what has not changed; only a card whose device is
	// going was dropped already (dropLeavingCards).
	m_pending.start();
}

void DevicePanel::place(const QString& key, const std::function<QWidget*()>& make, Qt::Alignment alignment)
{
	QWidget* widget = m_reusable.take(key);
	if (widget == nullptr)
	{
		widget = make();
		widget->setProperty(KeyProperty, key);
		if (auto* card = qobject_cast<DeviceCard*>(widget)) { card->refreshState(m_hosting); }
	}
	m_cards->insertWidget(m_cards->indexOf(m_add), widget, 0, alignment);
	widget->show();
}

void DevicePanel::watchChain(EffectChain* chain)
{
	if (chain == nullptr) { return; }
	m_watch << connect(chain, &EffectChain::dataChanged, this, [this] { dropLeavingCards(); scheduleRebuild(); });
	m_watch << connect(chain, &EffectChain::aboutToClear, this, [this, chain] { dropLeavingCards(chain); scheduleRebuild(); });
}

void DevicePanel::watch(Track* track, MixerChannel* channel)
{
	for (const QMetaObject::Connection& c : m_watch) { disconnect(c); }
	m_watch.clear();
	m_track = track;
	if (channel != nullptr)
	{
		watchChain(&channel->m_fxChain);
		// The rack's parallel chains too: an alias or option set on one of their devices redraws its card.
		for (int c = 1; c < channel->m_rack.chainCount(); ++c) { watchChain(channel->m_rack.chain(c)); }
	}
	if (track == nullptr) { return; }
	m_watch << connect(track, &QObject::destroyed, this, [this] { dropCards(); scheduleRebuild(); });
	watchChain(trackChain(track));
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track))
	{
		m_watch << connect(instrument, &InstrumentTrack::instrumentChanged, this, [this] { dropInstrumentCards(); scheduleRebuild(); });
		if (instrument->instrument() != nullptr)
		{
			m_watch << connect(instrument->instrument(), &QObject::destroyed, this, [this] { dropInstrumentCards(); scheduleRebuild(); });
		}
	}
	if (IntModel* channel = track->mixerChannelModel())
	{
		m_watch << connect(channel, &Model::dataChanged, this, &DevicePanel::scheduleRebuild);
	}
}

void DevicePanel::addSection(const QString& text)
{
	place(QStringLiteral("section:") + text, [this, text] {
		auto* label = new QLabel(text, m_row);
		label->setObjectName(QStringLiteral("deviceSection"));
		label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
		label->setWordWrap(true);
		label->setFixedWidth(DeviceCard::CellWidth + tokens::space::xl);
		return label;
	});
}

void DevicePanel::addInstrumentCard(InstrumentTrack* track)
{
	Instrument* instrument = track->instrument();
	if (instrument == nullptr) { return; }
	DeviceCard::Spec spec;
	spec.target = control::trackIdOf(track);
	spec.plugin = QStringLiteral("inst");
	spec.title = instrument->displayName();
	spec.subtitle = tr("Instrument");
	spec.instrumentTrack = track;
	const QList<AutomatableModel*> parameters = controlInstrumentParameters(instrument);
	QStringList names;
	for (const AutomatableModel* model : parameters) { names << model->displayName(); }
	place(cardKey("inst", instrument, spec.target, spec.title, names), [this, spec, parameters] {
		return new DeviceCard(spec, parameters, m_row);
	});
}

void DevicePanel::addModulators(const QJsonArray& modulators, const QString& target, int chainIndex, int effectIndex)
{
	for (const QJsonValue& value : modulators)
	{
		const QJsonObject modulator = value.toObject();
		bool drives = false;
		for (const QJsonValue& t : modulator.value(QStringLiteral("targets")).toArray())
		{
			const QJsonObject route = t.toObject();
			drives = drives || (route.value(QStringLiteral("channel")).toString() == target
				&& route.value(QStringLiteral("chain")).toInt() == chainIndex && route.value(QStringLiteral("effect")).toInt() == effectIndex);
		}
		const QString id = modulator.value(QStringLiteral("id")).toString();
		if (!drives || m_modulators.contains(id)) { continue; }
		m_modulators << id;
		// Its whole description is its key: a changed rate or target makes a new card, an unchanged one stays.
		const QString key = QStringLiteral("mod:") + QString::fromUtf8(QJsonDocument(modulator).toJson(QJsonDocument::Compact));
		place(key, [this, modulator] { return new ModulatorCard(modulator, m_row); });
	}
}

void DevicePanel::addChain(EffectChain* chain, const QString& target, const QJsonArray& modulators, int chainIndex)
{
	if (chain == nullptr) { return; }
	const std::vector<Effect*>& effects = chain->effects();
	for (int i = 0; i < static_cast<int>(effects.size()); ++i)
	{
		Effect* effect = effects[i];
		DeviceCard::Spec spec;
		spec.target = target;
		spec.plugin = control::effectIdOf(effect);
		spec.title = effect->displayName();
		spec.subtitle = target.startsWith(QLatin1String("ch-")) ? tr("Insert on %1").arg(target) : tr("Audio effect");
		spec.enabled = effect->isEnabled();
		spec.effect = effect;
		const QList<AutomatableModel*> parameters = controlEffectParameters(effect);
		place(cardKey("fx", effect, target, spec.title, parameteraliases::shownNamesOf(effect, parameters)),
			[this, spec, parameters] { return new DeviceCard(spec, parameters, m_row); });
		addModulators(modulators, target, chainIndex, i);
	}
}

void DevicePanel::addChannel(MixerChannel* channel)
{
	const QJsonArray modulators = invoke(QStringLiteral("modulator.get_state"), {}).result
		.value(QStringLiteral("modulators")).toArray();
	addSection(tr("Mixer %1 - %2").arg(m_channel, channel->m_name));
	// 0.5.0 L10 (MX-101): the level of the channel these devices play into, beside its heading.
	place(QStringLiteral("meter:") + m_channel, [this] { return new ChannelLevelMeter(m_channel, m_row); }, Qt::AlignTop);
	// 0.5.0 L10 (MX-104): the rack's macros as knobs, before the devices they drive.
	place(QStringLiteral("macros:") + m_channel, [this] { return new MacroKnobs(m_channel, 8, false, m_row); });
	addChain(&channel->m_fxChain, m_channel, modulators);
	// 0.5.0 L10 (MX-102): the rack's parallel chains, each after its own heading; their devices are
	// addressed by fx-<n> on the channel like the chain's own.
	for (int c = 1; c < channel->m_rack.chainCount(); ++c)
	{
		addSection(tr("Parallel chain %1").arg(c));
		addChain(channel->m_rack.chain(c), m_channel, modulators, c);  // a modulator can drive a rack chain's device
	}
}

void DevicePanel::collectReusable()
{
	m_modulators.clear();
	m_reusable.clear();
	for (int i = m_cards->count() - 1; i >= 0; --i)
	{
		QWidget* widget = m_cards->itemAt(i)->widget();
		const QString key = widget != nullptr ? widget->property(KeyProperty).toString() : QString();
		if (key.isEmpty()) { continue; }
		m_cards->removeWidget(widget);
		m_reusable.insert(key, widget);
	}
}

void DevicePanel::rebuild()
{
	m_pending.stop();
	Track* track = ShellSelection::instance()->track();
	MixerChannel* channel = track != nullptr ? trackChannel(track) : selectedChannel();
	watch(track, channel);
	m_channel = control::channelIdOf(channel);
	m_target = track != nullptr ? control::trackIdOf(track) : m_channel;
	m_empty->setVisible(m_target.isEmpty());
	m_add->setVisible(!m_target.isEmpty() && (track == nullptr || trackChain(track) != nullptr));
	m_signature = signature();
	// Theme 12: everything shown is taken out of the row and placed again in the new order when its key still matches;
	// what is left over afterwards is what the selection or the chain no longer has.
	collectReusable();
	if (track != nullptr)
	{
		if (auto* instrument = dynamic_cast<InstrumentTrack*>(track)) { addInstrumentCard(instrument); }
		addChain(trackChain(track), m_target, {});
	}
	if (channel != nullptr) { addChannel(channel); }
	const QList<QWidget*> leftOver = m_reusable.values();
	m_reusable.clear();
	for (QWidget* widget : leftOver) { retire(widget); }
}

QString DevicePanel::signature() const
{
	// What the panel draws, cheaply: the selection, the channel's devices and the modulators. A
	// difference means something changed that sent no signal (a modulator added over the socket).
	QString text = m_target + QLatin1Char('|') + m_channel;
	ControlResult error;
	if (MixerChannel* channel = m_channel.isEmpty() ? nullptr : control::resolveMixerChannel(m_channel, &error))
	{
		for (const Effect* effect : channel->m_fxChain.effects()) { text += control::effectIdOf(effect); }
		for (int c = 1; c < channel->m_rack.chainCount(); ++c)
		{
			text += QStringLiteral("|chain%1:").arg(c);
			for (const Effect* effect : channel->m_rack.chain(c)->effects()) { text += control::effectIdOf(effect); }
		}
	}
	const QJsonObject modulators = invoke(QStringLiteral("modulator.get_state"), {}).result;
	return text + QString::fromUtf8(QJsonDocument(modulators.value(QStringLiteral("modulators")).toArray()).toJson(QJsonDocument::Compact));
}

void DevicePanel::poll()
{
	if (Engine::getSong() == nullptr) { return; }
	// The selection itself is compared too: a deleted channel or a renumbered one reads differently.
	const Track* track = ShellSelection::instance()->track();
	const QString target = track != nullptr ? control::trackIdOf(track) : control::channelIdOf(selectedChannel());
	if (target != m_target || signature() != m_signature) { rebuild(); return; }
	for (MacroKnobs* macros : m_row->findChildren<MacroKnobs*>()) { macros->refresh(); }  // values set elsewhere
	// Theme 12: a crashed or quarantined host, every fourth beat (two seconds) - a report, not a command that mutates.
	if (m_polls++ % 4 == 0) { m_hosting = devicestate::hostingProblems(); }
	// MX-095. Only the cards in the row: a dropped card stays a child until its deferred delete, and the device it
	// points at may be gone already.
	for (int i = 0; i < m_cards->count(); ++i)
	{
		if (auto* card = qobject_cast<DeviceCard*>(m_cards->itemAt(i)->widget())) { card->refreshState(m_hosting); }
	}
}

QJsonObject DevicePanel::describe() const
{
	QJsonArray cards;
	for (int i = 0; i < m_cards->count(); ++i)
	{
		const auto* card = qobject_cast<const DeviceCard*>(m_cards->itemAt(i)->widget());
		if (card == nullptr) { continue; }
		cards.append(QJsonObject{{QStringLiteral("target"), card->spec().target},
			{QStringLiteral("plugin"), card->spec().plugin}, {QStringLiteral("name"), card->spec().title},
			{QStringLiteral("enabled"), card->spec().effect == nullptr || card->spec().effect->isEnabled()},
			{QStringLiteral("processing"), card->spec().effect != nullptr && card->spec().effect->isProcessingAudio()},
			{QStringLiteral("parameters"), card->totalParameters()}, {QStringLiteral("shown"), card->shownParameters()},
			{QStringLiteral("state"), card->stateJson()}});
	}
	return {{QStringLiteral("track"), control::trackIdOf(m_track)}, {QStringLiteral("channel"), m_channel}, {QStringLiteral("cards"), cards},
		{QStringLiteral("modulators"), QJsonArray::fromStringList(m_modulators)}};
}

} // namespace lmms::gui
