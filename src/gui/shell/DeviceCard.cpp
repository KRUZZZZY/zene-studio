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
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include "AudioPortsModel.h"
#include "ComboBox.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Effect.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"
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
	if (m_spec.effect != nullptr && m_spec.effect->audioPortsModel() != nullptr)
	{
		QToolButton* pins = headerButton(QStringLiteral("pins"), tr("Route %1's audio pins").arg(m_spec.title), header);
		connect(pins, &QToolButton::clicked, this, &DeviceCard::openPins);
		row->addWidget(pins);
	}
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

} // namespace lmms::gui
