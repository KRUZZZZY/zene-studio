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

#include <algorithm>

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>

#include "Accessibility.h"
#include "ControlScaleShared.h"
#include "DesignTokens.h"
#include "ItemList.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "ShellChrome.h"
#include "ShellIcons.h"

namespace lmms::gui
{

namespace
{

//! The snap choices, in ticks at 192 per whole note; 1 (the tick itself) is "off" - NoteGrid::snap
//! leaves a tick unchanged at a grid of 1.
struct GridChoice
{
	const char* label;
	int ticks;
};

//! FL Studio's piano-roll tools, in its toolbar's order, each with its letter (owner decision 2026-10-03).
struct ToolChoice
{
	NoteTool tool;
	const char* icon;
	const char* text;
	Qt::Key key;
};

constexpr ToolChoice kTools[] = {
	{NoteTool::Draw, "draw", "Draw", Qt::Key_P}, {NoteTool::Paint, "paint", "Paint", Qt::Key_B},
	{NoteTool::Erase, "erase", "Delete", Qt::Key_D}, {NoteTool::Mute, "mute", "Mute", Qt::Key_T},
	{NoteTool::Slice, "slice", "Slice", Qt::Key_C}, {NoteTool::Select, "select", "Select", Qt::Key_E},
	{NoteTool::Audition, "audition", "Play", Qt::Key_Y},
};

//! ED-130: the controllers the lane chooser lists after its five common ones - every other number 0..127, named
//! where MIDI names it.
QString controllerTitle(int cc)
{
	static const struct { int cc; const char* name; } named[] = {{0, "Bank select"}, {4, "Foot"}, {5, "Portamento time"},
		{6, "Data entry"}, {8, "Balance"}, {10, "Pan"}, {32, "Bank select LSB"}, {65, "Portamento"}, {66, "Sostenuto"},
		{67, "Soft pedal"}, {68, "Legato"}, {71, "Resonance"}, {72, "Release"}, {73, "Attack"}, {74, "Cutoff"},
		{84, "Portamento control"}, {91, "Reverb"}, {93, "Chorus"}, {120, "All sound off"}, {121, "Reset controllers"},
		{123, "All notes off"}};
	for (const auto& n : named)
	{
		if (n.cc == cc) { return NoteEditor::tr("%1 (CC %2)").arg(NoteEditor::tr(n.name)).arg(cc); }
	}
	return NoteEditor::tr("CC %1").arg(cc);
}

//! The tool a plain key picks: its FL letter, or 1..7 in NoteTool's order (kept as aliases); false for any other key.
bool toolForKey(const QKeyEvent* event, NoteTool* tool)
{
	if (event->modifiers() != Qt::NoModifier) { return false; }
	if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_7)
	{
		*tool = static_cast<NoteTool>(event->key() - Qt::Key_1);
		return true;
	}
	const auto* it = std::find_if(std::begin(kTools), std::end(kTools), [event](const ToolChoice& c) { return c.key == event->key(); });
	if (it != std::end(kTools)) { *tool = it->tool; }
	return it != std::end(kTools);
}

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
	bar->setFixedHeight(tokens::targetSize() + 2 * tokens::space::xs);  // the buttons' height at the density
	bar->setAutoFillBackground(true);
	QPalette pal = bar->palette();
	pal.setColor(QPalette::Window, tokens::palette().panel);
	bar->setPalette(pal);
	auto* row = new QHBoxLayout(bar);
	row->setContentsMargins(tokens::space::sm, tokens::space::xs, tokens::space::sm, tokens::space::xs);
	// 0.5.0 Q050-141: adjacent targets keep the touch minimum apart in accessible mode.
	row->setSpacing(tokens::targetSpacing(tokens::space::xxs));

	m_clipLabel = new QLabel(bar);
	m_clipLabel->setFont(tokens::font(tokens::TextRole::Title));
	m_clipLabel->setMinimumWidth(120);
	m_clipLabel->setMaximumWidth(240);  // a long name is elided (showClipTitle), not let push the tools off
	row->addWidget(m_clipLabel);
	row->addSpacing(tokens::space::md);
	addToolButtons(bar, row);
	addGridControls(bar, row);
	addFoldControls(bar, row);
	addScaleButton(bar, row);
	addNotesMenu(bar, row);
	addViewButtons(bar, row);
	buildLaneChooser();
	return bar;
}

void NoteEditor::addToolButtons(QWidget* bar, QHBoxLayout* row)
{
	// The tools, FL's letters (1..7 too).
	m_tools = new QActionGroup(this);
	for (const ToolChoice& choice : kTools)
	{
		const QString text = tr(choice.text);
		const int alias = static_cast<int>(choice.tool) + 1;
		QAction* action = checkable(m_tools, QString::fromLatin1(choice.icon), text,
			tr("%1 tool (%2, or %3)").arg(text, QKeySequence(choice.key).toString(QKeySequence::NativeText)).arg(alias));
		action->setData(static_cast<int>(choice.tool));
		action->setProperty("controlCommand", QStringLiteral("window.note_editor_set"));
		m_tools->addAction(action);
		const NoteTool tool = choice.tool;
		connect(action, &QAction::triggered, this, [this, tool]() { setTool(tool); });
		row->addWidget(toolButton(bar, action));
	}
	setTool(m_state.tool);
	row->addSpacing(tokens::space::md);
}

void NoteEditor::addGridControls(QWidget* bar, QHBoxLayout* row)
{
	// Grid and quantise.
	m_gridBox = new QComboBox(bar);
	for (const GridChoice& choice : kGrid) { m_gridBox->addItem(tr(choice.label), choice.ticks); }
	m_gridBox->setCurrentIndex(m_gridBox->findData(m_state.gridTicks));
	m_gridBox->setMinimumHeight(tokens::targetSize());  // 0.5.0 Q050-141: a target a finger can hit
	a11y::announce(m_gridBox, tr("Grid"), tr("The snap grid for drawing, moving and resizing notes; Alt while dragging turns it off. "
		"Right-click it to set the length a new note takes."));
	// FL: right-clicking the snap control sets a default length for new notes instead of the last note's.
	m_gridBox->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_gridBox, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
		QMenu menu(this);
		fillNewLengthMenu(&menu);
		menu.exec(m_gridBox->mapToGlobal(at));
	});
	connect(m_gridBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
		[this](int index) { setGridTicks(m_gridBox->itemData(index).toInt()); });
	row->addWidget(m_gridBox);
	auto* quantise = new QAction(shellicons::icon(QStringLiteral("quantize")), tr("Quantise"), this);
	quantise->setToolTip(tr("Quantise the selected notes, or every note when none is selected, to the grid (groove.quantize)"));
	quantise->setProperty("controlCommand", QStringLiteral("groove.quantize"));
	connect(quantise, &QAction::triggered, this, [this]() {
		if (m_state.clip == nullptr) { return; }
		// The same scope Notes > Quantise... acts on.
		run(QStringLiteral("groove.quantize"), {{QStringLiteral("clip"), clipId()}, {QStringLiteral("grid"), std::max(1, m_state.gridTicks)},
			{QStringLiteral("scope"), noteScope()}});
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
	QAction* foldDrums = checkable(m_folds, QStringLiteral("drums"), tr("Drum view"),
		tr("Show only the keys the track names (Notes > Key Names) and the keys in use, by name"));
	foldDrums->setData(static_cast<int>(NoteFold::Drums));
	for (QAction* action : {foldScale, foldUsed, foldDrums})
	{
		m_folds->addAction(action);
		connect(action, &QAction::toggled, this, [this, foldDrums]() {
			QAction* on = m_folds->checkedAction();
			if (on == foldDrums) { refresh(); }
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
		const QString text = m_state.scaleName.isEmpty() ? tr("No scale") : m_state.scaleName;
		if (text == scaleButton->text()) { return; }  // stateChanged also fires on every scroll and zoom
		scaleButton->setText(text);
		a11y::announce(scaleButton, tr("Scale: %1").arg(text), tr("Choose the scale and its root."));
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
	// window.note_editor_set can change it too: the button follows the state, not only its own clicks.
	connect(this, &NoteEditor::stateChanged, ghosts, [this, ghosts]() {
		if (ghosts->isChecked() == m_state.ghosts) { return; }
		const QSignalBlocker quiet(ghosts);
		ghosts->setChecked(m_state.ghosts);
	});
	row->addWidget(toolButton(bar, ghosts));
	auto* fit = new QAction(shellicons::icon(QStringLiteral("follow")), tr("Zoom to selection"), this);
	fit->setToolTip(tr("Fit the selected notes in view, or the whole clip when none is selected (Z)"));
	fit->setProperty("controlCommand", QStringLiteral("window.note_editor_set"));
	connect(fit, &QAction::triggered, this, &NoteEditor::zoomToSelection);
	row->addWidget(toolButton(bar, fit));
	// Theme 10: the clip's notes as an editable list (window.item_list, as Ctrl+Shift+L on the notes).
	row->addWidget(itemlists::listButton(QStringLiteral("notes"), this, bar));
	row->addStretch(1);
}

void NoteEditor::buildLaneChooser()
{
	// The lane chooser sits under the keys, beside the lane (NoteEditor::buildBody places it).
	m_laneBox = new QComboBox(this);
	for (const NoteLaneSpec& spec : notelanes::all()) { m_laneBox->addItem(spec.title, static_cast<int>(spec.lane)); }
	// The clip's controller lanes (ED-130): the curves it plays as MIDI, by id.
	m_laneBox->insertSeparator(m_laneBox->count());
	const struct { const char* title; const char* id; } controllers[] = {{"Mod wheel (CC 1)", "cc:1"},
		{"Breath (CC 2)", "cc:2"}, {"Volume (CC 7)", "cc:7"}, {"Expression (CC 11)", "cc:11"}, {"Sustain (CC 64)", "cc:64"},
		{"Pitch bend", "pitchbend"}, {"Aftertouch", "aftertouch"}, {"Program change", "program"}};
	for (const auto& c : controllers) { m_laneBox->addItem(tr(c.title), QString::fromLatin1(c.id)); }
	// Then any other controller number (ED-130): the five above are the common ones, not the only ones.
	m_laneBox->insertSeparator(m_laneBox->count());
	for (int cc = 0; cc <= 127; ++cc)
	{
		const QString id = QStringLiteral("cc:%1").arg(cc);
		if (m_laneBox->findData(id) < 0) { m_laneBox->addItem(controllerTitle(cc), id); }
	}
	m_laneBox->setMaxVisibleItems(24);
	a11y::announce(m_laneBox, tr("Expression lane"), tr("Which per-note value, or which controller curve of the clip, the lane below the notes edits."));
	m_laneBox->setFixedWidth(84);
	m_laneBox->setMinimumHeight(tokens::targetSize());  // 0.5.0 Q050-141: a target a finger can hit
	m_laneBox->setStyleSheet(QStringLiteral("QComboBox { padding: 2px 16px 2px 6px; }"));
	// The box is as narrow as the keys; its list opens wide enough for every name, and the
	// tooltip names the lane shown.
	m_laneBox->view()->setMinimumWidth(m_laneBox->view()->sizeHintForColumn(0) + 2 * tokens::space::md);
	m_laneBox->setToolTip(m_laneBox->currentText());
	connect(m_laneBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
		[this](int index) {
			m_laneBox->setToolTip(m_laneBox->itemText(index));
			const QVariant data = m_laneBox->itemData(index);
			if (data.userType() == QMetaType::QString) { setControllerLane(data.toString()); return; }
			setControllerLane(QString());
			setLane(static_cast<NoteLane>(data.toInt()));
		});
}

void NoteEditor::fillScaleMenu(QMenu* menu)
{
	// What scale.list offers, each item the command it names (scale.root_set / scale.set). The
	// current root and scale are checked (scale.list carries the context), and No Scale clears it.
	menu->clear();
	const QJsonObject listed = run(QStringLiteral("scale.list"), {});
	const bool set = listed.value(QStringLiteral("scale_set")).toBool();
	const auto nameOf = [](const QJsonValue& v) {
		return v.isObject() ? v.toObject().value(QStringLiteral("name")).toString() : v.toString();
	};
	const auto choice = [this](QMenu* into, const QString& label, bool checked, const QString& command, const QJsonObject& args) {
		QAction* action = into->addAction(label, this, [this, command, args]() { run(command, args); readScale(); });
		action->setCheckable(true);
		action->setChecked(checked);
	};
	QMenu* roots = menu->addMenu(tr("Root"));
	for (const QJsonValue& root : listed.value(QStringLiteral("roots")).toArray())
	{
		const QString name = nameOf(root);
		choice(roots, name, name == listed.value(QStringLiteral("root_name")).toString(), QStringLiteral("scale.root_set"),
			{{QStringLiteral("root"), name}});
	}
	choice(menu, tr("No scale"), !set, QStringLiteral("scale.set"), {{QStringLiteral("scale"), QString()}});
	for (const QJsonValue& scale : listed.value(QStringLiteral("scales")).toArray())
	{
		const QString name = nameOf(scale);
		choice(menu, name, set && name == listed.value(QStringLiteral("scale")).toString(), QStringLiteral("scale.set"),
			{{QStringLiteral("scale"), name}});
	}
}

void NoteEditor::readScale()
{
	// The scale context (scale.get_state): the mask both highlights and folds.
	m_scaleRoot = control::scaleContext().root;  // what followScale() compares against
	m_scaleSeen = control::scaleContext().scale;
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

bool NoteEditor::handlesKey(const QKeyEvent* event)
{
	NoteTool tool = NoteTool::Draw;
	return toolForKey(event, &tool) || (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_Z);
}

void NoteEditor::keyPressEvent(QKeyEvent* event)
{
	NoteTool tool = NoteTool::Draw;
	if (toolForKey(event, &tool)) { setTool(tool); }
	else if (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_Z) { zoomToSelection(); }
	else
	{
		QWidget::keyPressEvent(event);
		return;
	}
	event->accept();
}

void NoteEditor::zoomToSelection()
{
	if (m_state.clip == nullptr) { return; }
	int first = -1;
	int last = 0;
	int lowest = 127;
	int highest = 0;
	for (const Note* note : m_state.clip->notes())
	{
		if (!note->selected()) { continue; }
		first = first < 0 ? note->pos().getTicks() : std::min<int>(first, note->pos().getTicks());
		last = std::max<int>(last, note->endPos().getTicks());
		lowest = std::min(lowest, note->key());
		highest = std::max(highest, note->key());
	}
	if (first < 0)
	{
		zoomToFit();
		return;
	}
	// A beat of room on each side, so the notes do not touch the edges.
	const int room = DefaultTicksPerBar / 4;
	m_pendingFit = false;
	m_state.grid.setPixelsPerTick(std::max(1, m_canvas->width() - 16) / static_cast<double>(last - first + 2 * room));
	m_state.grid.setScrollTicks(std::max(0, first - room));
	m_state.grid.centreOnKey((lowest + highest) / 2);
	emit stateChanged();
}

} // namespace lmms::gui
