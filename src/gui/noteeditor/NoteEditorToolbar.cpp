/*
 * NoteEditorToolbar.cpp - the note editor's header: tools, grid, quantise, fold, scale, ghosts
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

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>

#include "Accessibility.h"
#include "DesignTokens.h"
#include "NoteEditor.h"
#include "ShellChrome.h"
#include "ShellIcons.h"

namespace lmms::gui
{

namespace
{

//! The snap choices, in ticks at 192 per bar; 0 is "off".
struct GridChoice
{
	const char* label;
	int ticks;
};

constexpr GridChoice kGrid[] = {
	{"1/1", 192}, {"1/2", 96}, {"1/4", 48}, {"1/8", 24}, {"1/16", 12}, {"1/32", 6},
	{"1/4 T", 32}, {"1/8 T", 16}, {"1/16 T", 8}, {"Off", 1},
};

ShellButton* toolButton(QWidget* parent, QAction* action)
{
	auto* button = new ShellButton(parent);
	button->setDefaultAction(action);
	a11y::announce(button, action->text(), action->toolTip());
	return button;
}

QAction* checkable(QObject* owner, const QString& icon, const QString& text, const QString& tip)
{
	auto* action = new QAction(shellicons::icon(icon), text, owner);
	action->setCheckable(true);
	action->setToolTip(tip);
	return action;
}

} // namespace

QWidget* NoteEditor::buildToolbar()
{
	auto* bar = new QWidget(this);
	bar->setObjectName(QStringLiteral("noteEditorToolbar"));
	bar->setFixedHeight(tokens::size::target + 2 * tokens::space::xs);
	bar->setAutoFillBackground(true);
	QPalette pal = bar->palette();
	pal.setColor(QPalette::Window, tokens::palette().panel);
	bar->setPalette(pal);
	auto* row = new QHBoxLayout(bar);
	row->setContentsMargins(tokens::space::sm, tokens::space::xs, tokens::space::sm, tokens::space::xs);
	row->setSpacing(tokens::space::xxs);

	m_clipLabel = new QLabel(bar);
	m_clipLabel->setFont(tokens::font(tokens::TextRole::Title));
	m_clipLabel->setMinimumWidth(120);
	row->addWidget(m_clipLabel);
	row->addSpacing(tokens::space::md);
	addToolButtons(bar, row);
	addGridControls(bar, row);
	addFoldControls(bar, row);
	addScaleButton(bar, row);
	addViewButtons(bar, row);
	buildLaneChooser();
	return bar;
}

void NoteEditor::addToolButtons(QWidget* bar, QHBoxLayout* row)
{
	// The tools, keys 1..5.
	m_tools = new QActionGroup(this);
	const struct { const char* icon; QString text; } tools[] = {
		{"draw", tr("Draw")}, {"select", tr("Select")}, {"slice", tr("Slice")}, {"erase", tr("Erase")},
		{"audition", tr("Audition")}};
	for (int i = 0; i < 5; ++i)
	{
		QAction* action = checkable(m_tools, QString::fromLatin1(tools[i].icon), tools[i].text,
			tr("%1 tool (%2)").arg(tools[i].text).arg(i + 1));
		m_tools->addAction(action);
		connect(action, &QAction::triggered, this, [this, i]() { setTool(static_cast<NoteTool>(i)); });
		row->addWidget(toolButton(bar, action));
	}
	m_tools->actions().first()->setChecked(true);
	row->addSpacing(tokens::space::md);
}

void NoteEditor::addGridControls(QWidget* bar, QHBoxLayout* row)
{
	// Grid and quantise.
	m_gridBox = new QComboBox(bar);
	for (const GridChoice& choice : kGrid) { m_gridBox->addItem(tr(choice.label), choice.ticks); }
	m_gridBox->setCurrentIndex(m_gridBox->findData(m_state.gridTicks));
	a11y::announce(m_gridBox, tr("Grid"), tr("The snap grid for drawing, moving and resizing notes; Alt while dragging turns it off."));
	connect(m_gridBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
		[this](int index) { setGridTicks(m_gridBox->itemData(index).toInt()); });
	row->addWidget(m_gridBox);
	auto* quantise = new QAction(shellicons::icon(QStringLiteral("quantize")), tr("Quantise"), this);
	quantise->setToolTip(tr("Quantise the clip's notes to the grid (groove.quantize)"));
	quantise->setProperty("controlCommand", QStringLiteral("groove.quantize"));
	connect(quantise, &QAction::triggered, this, [this]() {
		if (m_state.clip == nullptr) { return; }
		run(QStringLiteral("groove.quantize"), {{QStringLiteral("clip"), clipId()}, {QStringLiteral("grid"), std::max(1, m_state.gridTicks)}});
	});
	row->addWidget(toolButton(bar, quantise));
	row->addSpacing(tokens::space::md);
}

void NoteEditor::addFoldControls(QWidget* bar, QHBoxLayout* row)
{
	// Fold: to the scale, or to the keys in use (both off = every key).
	m_folds = new QActionGroup(this);
	m_folds->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
	QAction* foldScale = checkable(m_folds, QStringLiteral("scale"), tr("Fold to scale"),
		tr("Show only the keys of the scale"));
	foldScale->setData(static_cast<int>(NoteFold::Scale));
	QAction* foldUsed = checkable(m_folds, QStringLiteral("fold"), tr("Fold to used keys"),
		tr("Show only the keys the clip uses"));
	foldUsed->setData(static_cast<int>(NoteFold::Used));
	for (QAction* action : {foldScale, foldUsed})
	{
		m_folds->addAction(action);
		connect(action, &QAction::toggled, this, [this]() {
			QAction* on = m_folds->checkedAction();
			m_state.grid.setFold(on != nullptr ? static_cast<NoteFold>(on->data().toInt()) : NoteFold::None);
			emit stateChanged();
		});
		row->addWidget(toolButton(bar, action));
	}
}

void NoteEditor::addScaleButton(QWidget* bar, QHBoxLayout* row)
{
	// The scale: its name, a menu of scales and roots (scale.set / scale.root_set).
	auto* scaleButton = new ShellButton(bar);
	scaleButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	scaleButton->setPopupMode(QToolButton::InstantPopup);
	scaleButton->setProperty("controlCommand", QStringLiteral("scale.set"));
	auto* scaleMenu = new QMenu(scaleButton);
	scaleButton->setMenu(scaleMenu);
	connect(scaleMenu, &QMenu::aboutToShow, this, [this, scaleMenu]() { fillScaleMenu(scaleMenu); });
	m_scaleLabel = new QLabel(bar);  // kept for describe(); the button shows the same text
	m_scaleLabel->hide();
	connect(this, &NoteEditor::stateChanged, scaleButton, [this, scaleButton]() {
		scaleButton->setText(m_state.scaleName.isEmpty() ? tr("No scale") : m_state.scaleName);
		a11y::announce(scaleButton, tr("Scale: %1").arg(scaleButton->text()), tr("Choose the scale and its root."));
		scaleButton->updateGeometry();
	});
	row->addWidget(scaleButton);
	row->addSpacing(tokens::space::md);
}

void NoteEditor::addViewButtons(QWidget* bar, QHBoxLayout* row)
{
	QAction* ghosts = checkable(this, QStringLiteral("ghost"), tr("Ghost notes"), tr("Show the track's other clips"));
	ghosts->setChecked(m_state.ghosts);
	connect(ghosts, &QAction::toggled, this, &NoteEditor::setGhosts);
	row->addWidget(toolButton(bar, ghosts));
	auto* fit = new QAction(shellicons::icon(QStringLiteral("follow")), tr("Zoom to fit"), this);
	fit->setToolTip(tr("Fit the clip in view"));
	connect(fit, &QAction::triggered, this, &NoteEditor::zoomToFit);
	row->addWidget(toolButton(bar, fit));
	row->addStretch(1);
}

void NoteEditor::buildLaneChooser()
{
	// The lane chooser sits under the keys, beside the lane (NoteEditor::buildBody places it).
	m_laneBox = new QComboBox(this);
	for (const NoteLaneSpec& spec : notelanes::all()) { m_laneBox->addItem(spec.title, static_cast<int>(spec.lane)); }
	a11y::announce(m_laneBox, tr("Expression lane"), tr("Which per-note value the lane below the notes edits."));
	m_laneBox->setFixedWidth(84);
	connect(m_laneBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
		[this](int index) { setLane(static_cast<NoteLane>(m_laneBox->itemData(index).toInt())); });
}

void NoteEditor::fillScaleMenu(QMenu* menu)
{
	// What scale.list offers, each item the command it names (scale.root_set / scale.set).
	menu->clear();
	const QJsonObject listed = run(QStringLiteral("scale.list"), {});
	const auto nameOf = [](const QJsonValue& v) {
		return v.isObject() ? v.toObject().value(QStringLiteral("name")).toString() : v.toString();
	};
	QMenu* roots = menu->addMenu(tr("Root"));
	for (const QJsonValue& root : listed.value(QStringLiteral("roots")).toArray())
	{
		const QString name = nameOf(root);
		roots->addAction(name, this, [this, name]() { run(QStringLiteral("scale.root_set"), {{QStringLiteral("root"), name}}); readScale(); });
	}
	for (const QJsonValue& scale : listed.value(QStringLiteral("scales")).toArray())
	{
		const QString name = nameOf(scale);
		menu->addAction(name, this, [this, name]() { run(QStringLiteral("scale.set"), {{QStringLiteral("scale"), name}}); readScale(); });
	}
}

void NoteEditor::readScale()
{
	// The scale context (scale.get_state): the mask both highlights and folds.
	const QJsonObject state = run(QStringLiteral("scale.get_state"), {});
	const QString mask = state.value(QStringLiteral("mask")).toString();
	std::array<bool, 12> bits{};
	for (int i = 0; i < 12 && i < mask.size(); ++i) { bits[static_cast<std::size_t>(i)] = mask.at(i) == QLatin1Char('1'); }
	const bool set = state.value(QStringLiteral("scale_set")).toBool();
	m_state.grid.setScaleMask(set ? bits : std::array<bool, 12>{}, state.value(QStringLiteral("root")).toInt());
	m_state.scaleName = set ? QStringLiteral("%1 %2").arg(state.value(QStringLiteral("root_name")).toString(),
		state.value(QStringLiteral("scale")).toString()) : QString();
	emit stateChanged();
}

} // namespace lmms::gui
