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

#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "AudioBusHandle.h"
#include "ComboBox.h"
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
#include "Knob.h"
#include "LedCheckBox.h"
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

constexpr int kRows = 2;
constexpr int kCellWidth = 76;

ControlResult invoke(const QString& id, const QJsonObject& args)
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString shortLabel(const QString& name)
{
	return QFontMetrics(tokens::font(tokens::TextRole::Small)).elidedText(name, Qt::ElideRight, kCellWidth - 4);
}

QToolButton* headerButton(const QString& icon, const QString& tip, QWidget* parent)
{
	auto* button = new QToolButton(parent);
	button->setIcon(shellicons::icon(icon));
	button->setIconSize(QSize(14, 14));
	button->setToolTip(tip);
	button->setAccessibleName(tip);
	button->setAutoRaise(true);
	return button;
}

QWidget* comboCell(ComboBoxModel* model, const QString& name, QWidget* parent)
{
	auto* cell = new QWidget(parent);
	auto* column = new QVBoxLayout(cell);
	column->setContentsMargins(0, 0, 0, 0);
	column->setSpacing(tokens::space::xxs);
	auto* caption = new QLabel(shortLabel(name), cell);
	caption->setObjectName(QStringLiteral("deviceCaption"));
	auto* box = new ComboBox(cell);
	box->setModel(model);
	box->setMinimumWidth(2 * kCellWidth - tokens::space::sm);
	column->addWidget(caption);
	column->addWidget(box);
	return cell;
}

//! The flat control for one parameter model; nullptr for a kind the card does not draw.
QWidget* parameterWidget(AutomatableModel* model, QWidget* parent)
{
	const QString name = model->displayName();
	QWidget* widget = nullptr;
	if (auto* boolean = qobject_cast<BoolModel*>(model))
	{
		auto* box = new LedCheckBox(shortLabel(name), parent);
		box->setModel(boolean);
		widget = box;
	}
	else if (auto* choice = qobject_cast<ComboBoxModel*>(model)) { widget = comboCell(choice, name, parent); }
	else if (auto* number = qobject_cast<FloatModel*>(model))
	{
		auto* knob = new Knob(KnobType::Bright26, shortLabel(name), parent);
		knob->setFont(tokens::font(tokens::TextRole::Small));
		knob->setModel(number);
		knob->setHintText(name + QLatin1Char(':'), QString());
		widget = knob;
	}
	else if (auto* integer = qobject_cast<IntModel*>(model))
	{
		auto* field = new ReadoutField(shortLabel(name), parent);
		field->bind(integer, [](int v) { return QString::number(v); });
		widget = field;
	}
	if (widget != nullptr)
	{
		widget->setToolTip(name);
		widget->setAccessibleName(name);
	}
	return widget;
}

//! Not drawn in the grid: an effect's own on/off switch (the card's power button) and its
//! silence timeout ("Decay", chain housekeeping); and the numerator and denominator of any
//! tempo-synced knob's custom meter, which belong to that knob's sync menu. The wet/dry mix is
//! drawn.
bool isChainBookkeeping(AutomatableModel* model, Effect* effect)
{
	if (qobject_cast<MeterModel*>(model->parent()) != nullptr) { return true; }
	return effect != nullptr && model->parent() == effect
		&& (qobject_cast<BoolModel*>(model) != nullptr || model == effect->autoQuitModel());
}

bool isSwitch(AutomatableModel* model)
{
	return qobject_cast<BoolModel*>(model) != nullptr || qobject_cast<ComboBoxModel*>(model) != nullptr;
}

InstrumentTrackView* instrumentTrackView(const InstrumentTrack* track)
{
	GuiApplication* gui = getGUI();
	if (gui == nullptr || gui->songEditor() == nullptr) { return nullptr; }
	for (TrackView* view : gui->songEditor()->m_editor->trackViews())
	{
		if (view->getTrack() == track) { return qobject_cast<InstrumentTrackView*>(view); }
	}
	return nullptr;
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

MixerChannel* selectedChannel()
{
	const QString id = ShellSelection::instance()->channel();
	ControlResult error;
	return id.isEmpty() ? nullptr : control::resolveMixerChannel(id, &error);
}

} // namespace

DeviceCard::DeviceCard(const Spec& spec, const QList<AutomatableModel*>& parameters, QWidget* parent)
	: QFrame(parent)
	, m_spec(spec)
{
	setObjectName(QStringLiteral("deviceCard"));
	setAccessibleName(tr("Device %1").arg(spec.title));
	auto* column = new QVBoxLayout(this);
	column->setContentsMargins(0, 0, 0, tokens::space::md);
	column->setSpacing(tokens::space::sm);
	column->addWidget(buildHeader());
	column->addWidget(buildParameters(parameters));
	column->addStretch(1);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

DeviceCard::~DeviceCard() { closeEditor(); }

QWidget* DeviceCard::buildHeader()
{
	auto* header = new QWidget(this);
	header->setObjectName(QStringLiteral("deviceCardHeader"));
	auto* row = new QHBoxLayout(header);
	row->setContentsMargins(tokens::space::sm, tokens::space::xs, tokens::space::xs, tokens::space::xs);
	row->setSpacing(tokens::space::xs);
	if (m_spec.effect != nullptr)
	{
		QToolButton* power = headerButton(QStringLiteral("power"), tr("Switch %1 on or off").arg(m_spec.title), header);
		power->setCheckable(true);
		power->setChecked(m_spec.enabled);
		connect(power, &QToolButton::clicked, this, [this, power](bool on) {
			const QJsonObject args{{QStringLiteral("target"), m_spec.target}, {QStringLiteral("plugin"), m_spec.plugin},
				{QStringLiteral("bypass"), !on}};
			if (!invoke(QStringLiteral("plugin.bypass"), args).ok) { power->setChecked(!on); }
		});
		row->addWidget(power);
	}
	auto* titles = new QVBoxLayout;
	titles->setSpacing(0);
	auto* title = new QLabel(m_spec.title, header);
	title->setObjectName(QStringLiteral("deviceCardTitle"));
	auto* subtitle = new QLabel(m_spec.subtitle, header);
	subtitle->setObjectName(QStringLiteral("deviceCaption"));
	titles->addWidget(title);
	titles->addWidget(subtitle);
	row->addLayout(titles, 1);
	QToolButton* editor = headerButton(QStringLiteral("editor"), tr("Open %1's own editor").arg(m_spec.title), header);
	connect(editor, &QToolButton::clicked, this, &DeviceCard::openEditor);
	row->addWidget(editor);
	if (m_spec.effect != nullptr)
	{
		QToolButton* remove = headerButton(QStringLiteral("close"), tr("Remove %1").arg(m_spec.title), header);
		connect(remove, &QToolButton::clicked, this, [target = m_spec.target, plugin = m_spec.plugin] {
			invoke(QStringLiteral("plugin.unload"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin}});
		});
		row->addWidget(remove);
	}
	return header;
}

QWidget* DeviceCard::buildParameters(const QList<AutomatableModel*>& parameters)
{
	auto* body = new QWidget(this);
	auto* column = new QVBoxLayout(body);
	column->setContentsMargins(tokens::space::md, 0, tokens::space::md, 0);
	column->setSpacing(tokens::space::md);
	auto* grid = new QGridLayout;
	grid->setHorizontalSpacing(tokens::space::xs);
	grid->setVerticalSpacing(tokens::space::md);
	auto* switches = new QHBoxLayout;
	switches->setSpacing(tokens::space::md);
	int knobs = 0;
	for (AutomatableModel* model : parameters)
	{
		if (isChainBookkeeping(model, m_spec.effect)) { continue; }
		++m_total;
		QWidget* widget = m_shown < MaxParameters ? parameterWidget(model, body) : nullptr;
		if (widget == nullptr) { continue; }
		++m_shown;
		// Numbers fill a two-row grid of knobs, column by column; switches and menus sit below.
		if (isSwitch(model)) { switches->addWidget(widget); continue; }
		grid->addWidget(widget, knobs % kRows, knobs / kRows, Qt::AlignHCenter | Qt::AlignTop);
		grid->setColumnMinimumWidth(knobs / kRows, kCellWidth);
		++knobs;
	}
	switches->addStretch(1);
	column->addLayout(grid);
	column->addLayout(switches);
	if (m_total == 0 || m_shown < m_total)
	{
		auto* note = new QLabel(m_total == 0 ? tr("No parameters - open the editor")
			: tr("+%1 more in the editor").arg(m_total - m_shown), body);
		note->setObjectName(QStringLiteral("deviceCaption"));
		column->addWidget(note);
	}
	return body;
}

void DeviceCard::openEditor()
{
	if (m_spec.effect == nullptr)
	{
		if (InstrumentTrackView* view = instrumentTrackView(m_spec.instrumentTrack))
		{
			view->getInstrumentTrackWindow()->toggleVisibility(true);
		}
		return;
	}
	// A hosted plug-in's own window first (VST3 / CLAP); a built-in effect's control dialog when
	// the device has no native editor.
	const QJsonObject args{{QStringLiteral("target"), m_spec.target}, {QStringLiteral("plugin"), m_spec.plugin}};
	if (!invoke(QStringLiteral("plugin.editor_open"), args).ok) { openEffectDialog(); }
}

void DeviceCard::openEffectDialog()
{
	if (m_editor.isNull())
	{
		EffectControlDialog* dialog = m_spec.effect->controls()->createView();
		if (dialog == nullptr) { return; }
		m_editor = getGUI()->mainWindow()->addWindowedWidget(dialog);
		m_editor->setWindowFlag(Qt::WindowMaximizeButtonHint, false);
	}
	m_editor->show();
	m_editor->raise();
}

void DeviceCard::closeEditor()
{
	if (!m_editor.isNull()) { delete m_editor.data(); }
}

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
	m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_row = new QWidget(m_scroll);
	m_cards = new QHBoxLayout(m_row);
	m_cards->setContentsMargins(tokens::space::md, tokens::space::md, tokens::space::md, tokens::space::md);
	m_cards->setSpacing(tokens::space::md);
	m_empty = new QLabel(tr("Select a track to see its devices."), m_row);
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
	add->setPopupMode(QToolButton::InstantPopup);
	add->setAccessibleName(tr("Add a device to the selected track"));
	auto* menu = new QMenu(add);
	connect(menu, &QMenu::aboutToShow, menu, [this, menu] {
		menu->clear();
		const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
		for (int i = 0; i < catalogue.size(); ++i)
		{
			const ControlDeviceEntry& entry = catalogue.at(i);
			if (entry.kind != QLatin1String("effect") || !entry.loadable || entry.format != QLatin1String("builtin")) { continue; }
			menu->addAction(entry.displayName, this, [this, id = control::deviceId(i)] {
				invoke(QStringLiteral("plugin.load"), {{QStringLiteral("target"), m_target},
					{QStringLiteral("device"), id}});
			});
		}
	});
	add->setMenu(menu);
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

void DevicePanel::dropCards()
{
	// Hidden and released now - before the device whose models a card draws is deleted - and
	// destroyed once control returns to the event loop, since the click that removed a device
	// came from a button on its own card.
	for (int i = m_cards->count() - 1; i >= 0; --i)
	{
		QWidget* widget = m_cards->itemAt(i)->widget();
		if (widget == nullptr || widget == m_empty || widget == m_add) { continue; }
		if (auto* card = qobject_cast<DeviceCard*>(widget)) { card->closeEditor(); }
		m_cards->removeWidget(widget);
		widget->hide();
		widget->deleteLater();
	}
	m_modulators.clear();
}

void DevicePanel::scheduleRebuild()
{
	dropCards();
	m_pending.start();
}

void DevicePanel::watchChain(EffectChain* chain)
{
	if (chain == nullptr) { return; }
	m_watch << connect(chain, &EffectChain::dataChanged, this, &DevicePanel::scheduleRebuild);
	m_watch << connect(chain, &EffectChain::aboutToClear, this, &DevicePanel::scheduleRebuild);
}

void DevicePanel::watch(Track* track, MixerChannel* channel)
{
	for (const QMetaObject::Connection& c : m_watch) { disconnect(c); }
	m_watch.clear();
	m_track = track;
	if (channel != nullptr) { watchChain(&channel->m_fxChain); }
	if (track == nullptr) { return; }
	m_watch << connect(track, &QObject::destroyed, this, &DevicePanel::scheduleRebuild);
	watchChain(trackChain(track));
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track))
	{
		m_watch << connect(instrument, &InstrumentTrack::instrumentChanged, this, &DevicePanel::scheduleRebuild);
		if (instrument->instrument() != nullptr)
		{
			m_watch << connect(instrument->instrument(), &QObject::destroyed, this, &DevicePanel::scheduleRebuild);
		}
	}
	if (IntModel* channel = track->mixerChannelModel())
	{
		m_watch << connect(channel, &Model::dataChanged, this, &DevicePanel::scheduleRebuild);
	}
}

void DevicePanel::addSection(const QString& text)
{
	auto* label = new QLabel(text, m_row);
	label->setObjectName(QStringLiteral("deviceSection"));
	label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	label->setWordWrap(true);
	label->setFixedWidth(kCellWidth + tokens::space::xl);
	m_cards->insertWidget(m_cards->indexOf(m_add), label);
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
	auto* card = new DeviceCard(spec, controlInstrumentParameters(instrument), m_row);
	m_cards->insertWidget(m_cards->indexOf(m_add), card);
}

void DevicePanel::addModulators(const QJsonArray& modulators, const QString& target, int effectIndex)
{
	for (const QJsonValue& value : modulators)
	{
		const QJsonObject modulator = value.toObject();
		bool drives = false;
		for (const QJsonValue& t : modulator.value(QStringLiteral("targets")).toArray())
		{
			const QJsonObject route = t.toObject();
			drives = drives || (route.value(QStringLiteral("channel")).toString() == target
				&& route.value(QStringLiteral("chain")).toInt() == 0 && route.value(QStringLiteral("effect")).toInt() == effectIndex);
		}
		const QString id = modulator.value(QStringLiteral("id")).toString();
		if (!drives || m_modulators.contains(id)) { continue; }
		m_modulators << id;
		m_cards->insertWidget(m_cards->indexOf(m_add), new ModulatorCard(modulator, m_row));
	}
}

void DevicePanel::addChain(EffectChain* chain, const QString& target, const QJsonArray& modulators)
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
		m_cards->insertWidget(m_cards->indexOf(m_add), new DeviceCard(spec, controlEffectParameters(effect), m_row));
		addModulators(modulators, target, i);
	}
}

void DevicePanel::rebuild()
{
	m_pending.stop();
	dropCards();
	Track* track = ShellSelection::instance()->track();
	MixerChannel* channel = track != nullptr ? trackChannel(track) : selectedChannel();
	watch(track, channel);
	m_channel = control::channelIdOf(channel);
	m_target = track != nullptr ? control::trackIdOf(track) : m_channel;
	m_empty->setVisible(m_target.isEmpty());
	m_add->setVisible(!m_target.isEmpty() && (track == nullptr || trackChain(track) != nullptr));
	m_signature = signature();
	if (track != nullptr)
	{
		if (auto* instrument = dynamic_cast<InstrumentTrack*>(track)) { addInstrumentCard(instrument); }
		addChain(trackChain(track), m_target, {});
	}
	if (channel == nullptr) { return; }
	const QJsonArray modulators = invoke(QStringLiteral("modulator.get_state"), {}).result
		.value(QStringLiteral("modulators")).toArray();
	addSection(tr("Mixer %1 - %2").arg(m_channel, channel->m_name));
	addChain(&channel->m_fxChain, m_channel, modulators);
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
	if (target != m_target || signature() != m_signature) { rebuild(); }
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
			{QStringLiteral("parameters"), card->totalParameters()}, {QStringLiteral("shown"), card->shownParameters()}});
	}
	return {{QStringLiteral("track"), control::trackIdOf(m_track)}, {QStringLiteral("channel"), m_channel}, {QStringLiteral("cards"), cards},
		{QStringLiteral("modulators"), QJsonArray::fromStringList(m_modulators)}};
}

} // namespace lmms::gui
