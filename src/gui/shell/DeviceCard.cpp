/*
 * DeviceCard.cpp - one device on the Device panel, drawn from its own parameter models
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

#include <QComboBox>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include "AudioPortsModel.h"
#include "ComboBox.h"
#include "ParameterAliases.h"
#include "CommandForm.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Effect.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"
#include "DeviceActionMenu.h"
#include "DeviceCardState.h"
#include "GuiApplication.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "Knob.h"
#include "LedCheckBox.h"
#include "MainWindow.h"
#include "MeterModel.h"
#include "PinConnector.h"
#include "ShellIcons.h"
#include "SongEditor.h"
#include "SubWindow.h"
#include "TrackView.h"
#include "TransportReadouts.h"

namespace lmms::gui
{

namespace
{

constexpr int kRows = 2;
constexpr int kCellWidth = DeviceCard::CellWidth;

ControlResult invoke(const QString& id, const QJsonObject& args)
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A header button's command: a refusal is shown over @a parent (runCommand), not swallowed.
ControlResult dispatch(QWidget* parent, const QString& id, const QJsonObject& args)
{
	return runCommand(parent, CommandCall{id, args, {}, QString(), false});
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
	button->setMinimumSize(tokens::targetSize(), tokens::targetSize());  // never below the clickable target
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
	box->setAccessibleName(name);  // the menu itself carries the name, not only its cell
	caption->setBuddy(box);
	box->setMinimumWidth(2 * kCellWidth - tokens::space::sm);
	column->addWidget(caption);
	column->addWidget(box);
	return cell;
}

//! The flat control for one parameter model, captioned @a name (its alias when it has one, MX-097);
//! nullptr for a kind the card does not draw.
QWidget* parameterWidget(AutomatableModel* model, const QString& name, QWidget* parent)
{
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
		const QString own = model->displayName();
		widget->setToolTip(name == own ? own : QStringLiteral("%1 (%2)").arg(name, own));
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

//! Theme 12: the card's Actions button - the same menu as a right-click on the card (DeviceActionMenu.h).
QToolButton* actionsButton(DeviceCard* card, QWidget* parent)
{
	auto* actions = new QToolButton(parent);
	actions->setObjectName(QStringLiteral("deviceActions"));
	actions->setText(QStringLiteral("\u22EF"));  // the same "more" mark the Inspector's section menus show
	actions->setToolTip(DeviceCard::tr("Actions for %1").arg(card->spec().title));
	actions->setAccessibleName(actions->toolTip());
	actions->setAutoRaise(true);
	actions->setMinimumSize(tokens::targetSize(), tokens::targetSize());
	actions->setPopupMode(QToolButton::InstantPopup);
	auto* menu = new QMenu(actions);
	QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, card] {
		menu->clear();
		fillDeviceActionMenu(menu, card);  // built as it opens: the bypass check and the move ends are current
	});
	actions->setMenu(menu);
	return actions;
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
	// Theme 12: the device's state in words under its header (bypassed, A/B, latency, hosting, preset).
	m_state = new QLabel(this);
	m_state->setObjectName(QStringLiteral("deviceState"));
	m_state->setWordWrap(true);
	m_state->setContentsMargins(tokens::space::sm, 0, tokens::space::sm, 0);
	m_state->hide();  // shown once there is something to say (refreshStateLine)
	column->addWidget(m_state);
	m_body = buildParameters(parameters);
	column->addWidget(m_body);
	column->addStretch(1);
	// MX-113: a folded device shows its header only.
	m_body->setVisible(spec.effect == nullptr || !spec.effect->deviceOptions().folded);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	refreshStateLine();
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
		m_power = power;
		connect(power, &QToolButton::clicked, this, [this, power](bool on) {
			const QJsonObject args{{QStringLiteral("target"), m_spec.target}, {QStringLiteral("plugin"), m_spec.plugin},
				{QStringLiteral("bypass"), !on}};
			if (!dispatch(this, QStringLiteral("plugin.bypass"), args).ok) { power->setChecked(!on); }
		});
		row->addWidget(power);
		m_activity = new QLabel(QStringLiteral("\u25CF"), header);  // MX-095: lit while the device processes audio
		row->addWidget(m_activity);
		refreshActivity();
	}
	auto* titles = new QVBoxLayout;
	titles->setSpacing(0);
	auto* title = new QLabel(m_spec.title, header);
	title->setObjectName(QStringLiteral("deviceCardTitle"));
	title->setToolTip(m_spec.title);  // the whole name when the card cuts it off
	auto* subtitle = new QLabel(m_spec.subtitle, header);
	subtitle->setObjectName(QStringLiteral("deviceCaption"));
	subtitle->setToolTip(m_spec.subtitle);
	titles->addWidget(title);
	titles->addWidget(subtitle);
	row->addLayout(titles, 1);
	addStateButtons(row, header);
	if (m_spec.effect != nullptr && m_spec.effect->audioPortsModel() != nullptr)
	{
		QToolButton* pins = headerButton(QStringLiteral("pins"), tr("Route %1's audio pins").arg(m_spec.title), header);
		connect(pins, &QToolButton::clicked, this, &DeviceCard::openPins);
		row->addWidget(pins);
	}
	if (m_spec.effect != nullptr)
	{
		QToolButton* fold = headerButton(QStringLiteral("fold"), tr("Fold or open %1").arg(m_spec.title), header);
		fold->setCheckable(true);
		fold->setChecked(m_spec.effect->deviceOptions().folded);
		m_fold = fold;
		connect(fold, &QToolButton::clicked, this, [this](bool folded) { foldCard(folded); });
		row->addWidget(fold);
		// MX-080: hear only what the device changes (plugin.delta, a monitor switch).
		auto* delta = new QToolButton(header);
		delta->setText(QStringLiteral("\u0394"));
		delta->setToolTip(tr("Hear only what %1 changes").arg(m_spec.title));
		delta->setAccessibleName(delta->toolTip());
		delta->setAutoRaise(true);
		delta->setMinimumSize(tokens::targetSize(), tokens::targetSize());
		delta->setCheckable(true);
		delta->setChecked(m_spec.effect->deltaTap().enabled());
		m_delta = delta;
		connect(delta, &QToolButton::clicked, this, [this, delta](bool on) {
			const QJsonObject args{{QStringLiteral("target"), m_spec.target}, {QStringLiteral("plugin"), m_spec.plugin},
				{QStringLiteral("on"), on}};
			if (!dispatch(this, QStringLiteral("plugin.delta"), args).ok) { delta->setChecked(!on); }
		});
		row->addWidget(delta);
	}
	QToolButton* editor = headerButton(QStringLiteral("editor"), tr("Open %1's own editor").arg(m_spec.title), header);
	connect(editor, &QToolButton::clicked, this, &DeviceCard::openEditor);
	row->addWidget(editor);
	if (m_spec.effect != nullptr)
	{
		QToolButton* remove = headerButton(QStringLiteral("close"), tr("Remove %1").arg(m_spec.title), header);
		connect(remove, &QToolButton::clicked, this, [this, target = m_spec.target, plugin = m_spec.plugin] {
			dispatch(this, QStringLiteral("plugin.unload"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin}});
		});
		row->addWidget(remove);
	}
	row->addWidget(actionsButton(this, header));
	return header;
}

//! Fold or open the card's body (MX-113). The command decides; the card folds itself too, because a
//! rack chain's card is not redrawn by its chain's signal.
void DeviceCard::foldCard(bool folded)
{
	const QJsonObject args{{QStringLiteral("target"), m_spec.target}, {QStringLiteral("plugin"), m_spec.plugin},
		{QStringLiteral("folded"), folded}};
	// The dispatch runs a command, and a command can retire this card: the card is guarded, so a card
	// that is gone by then is not touched (its body would be a dangling pointer).
	const QPointer<DeviceCard> self(this);
	if (dispatch(this, QStringLiteral("plugin.fold"), args).ok)
	{
		if (!self.isNull()) { m_body->setVisible(!folded); }
	}
	else if (!self.isNull()) { m_fold->setChecked(!folded); }
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
	// W6: an alias is found by the parameter's stable key (ParameterAliases.h), all of the card's at once.
	const QStringList names = m_spec.effect != nullptr ? parameteraliases::shownNamesOf(m_spec.effect, parameters) : QStringList();
	for (int index = 0; index < parameters.size(); ++index)
	{
		AutomatableModel* model = parameters.at(index);
		if (isChainBookkeeping(model, m_spec.effect)) { continue; }
		++m_total;
		const QString name = names.value(index, model->displayName());
		QWidget* widget = m_shown < MaxParameters ? parameterWidget(model, name, body) : nullptr;
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

void DeviceCard::contextMenuEvent(QContextMenuEvent* event)
{
	// Theme 12: the card's one action menu - the same entries, in the same order, as its Actions button.
	// The menu is built as the card's child, so its entries name the card as the widget their dialogs
	// belong to, and it is let go of before it runs: one of the entries (Remove) retires the card, and a
	// card deleted while this menu's own event loop runs would take the menu - a stack object - with it.
	QMenu menu(this);
	fillDeviceActionMenu(&menu, this);
	if (menu.isEmpty()) { QFrame::contextMenuEvent(event); return; }
	menu.setParent(nullptr);
	menu.exec(event->globalPos());
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
	const ControlResult result = invoke(QStringLiteral("plugin.editor_open"), args);
	if (result.ok) { setEditorError(QString()); return; }
	// The built-in control dialog is the fallback for a device with no editor of its own.
	if (openEffectDialog()) { setEditorError(QString()); return; }
	// Neither opened: the user is told why the first try failed - a silent no-op says nothing
	// (the reason is the command's own, not a word of ours).
	setEditorError(result.errorMessage);
	announce(this, CommandCall{QStringLiteral("plugin.editor_open"), args, {}, QString(), false}, result);
}

bool DeviceCard::openEffectDialog()
{
	if (m_editor.isNull())
	{
		EffectControlDialog* dialog = m_spec.effect->controls()->createView();
		if (dialog == nullptr) { return false; }
		m_editor = getGUI()->mainWindow()->addWindowedWidget(dialog);
		m_editor->setWindowFlag(Qt::WindowMaximizeButtonHint, false);
	}
	m_editor->show();
	m_editor->raise();
	return true;
}

void DeviceCard::openPins()
{
	if (m_pins.isNull())
	{
		const AudioPortsModel* model = m_spec.effect->audioPortsModel();
		if (model == nullptr) { return; }
		auto* view = model->instantiateView();
		view->setWindowTitle(tr("%1 - pins").arg(m_spec.title));
		m_pins = getGUI()->mainWindow()->addWindowedWidget(view);
	}
	m_pins->show();
	m_pins->raise();
}

void DeviceCard::closeEditor()
{
	if (!m_editor.isNull()) { delete m_editor.data(); }
	if (!m_pins.isNull()) { delete m_pins.data(); }
}

void DeviceCard::syncHeader()
{
	// setChecked emits no clicked(), so nothing is dispatched again; an unchanged state is a no-op.
	const bool folded = m_spec.effect->deviceOptions().folded;
	if (m_power != nullptr) { m_power->setChecked(m_spec.effect->isEnabled()); }
	if (m_fold != nullptr) { m_fold->setChecked(folded); }
	if (m_delta != nullptr) { m_delta->setChecked(m_spec.effect->deltaTap().enabled()); }
	if (m_body != nullptr) { m_body->setVisible(!folded); }
}

void DeviceCard::refreshActivity()
{
	refreshStateLine();
	if (m_activity == nullptr || m_spec.effect == nullptr) { return; }
	syncHeader();
	const bool on = m_spec.effect->isProcessingAudio();
	if (m_activity->property("lit").toBool() == on && m_activity->property("lit").isValid()) { return; }
	m_activity->setProperty("lit", on);
	// MX-095: the state in words too, not by colour alone.
	m_activity->setToolTip(on ? tr("Processing audio") : tr("Not processing audio"));
	m_activity->setAccessibleName(m_activity->toolTip());
	m_activity->setStyleSheet(QStringLiteral("color: %1;").arg((on ? tokens::palette().accent : tokens::palette().textMuted).name()));
}

} // namespace lmms::gui
