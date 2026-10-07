/*
 * SessionGridView.cpp - R5.3: the clip-launch grid
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

#include "SessionGridView.h"

#include <algorithm>

#include <QColor>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include "AccessibleWidgets.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "SessionGridCells.h"
#include "SessionGridMenus.h"
#include "SessionTransportBar.h"

namespace lmms::gui
{

namespace
{

constexpr int RefreshMs = 150;
constexpr int HeaderRow = 1;  //!< 0.5.0 theme 14: the column headers take the grid's first row
constexpr int CellWidth = 72;

void setTip(QWidget* widget, const QString& tip)
{
	if (widget->toolTip() != tip) { widget->setToolTip(tip); }
	if (widget->accessibleDescription() != tip) { widget->setAccessibleDescription(tip); }
}

ControlResult run(const QString& command, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(command, args);
}

} // namespace


SessionGridView::SessionGridView(QWidget* parent) :
	QWidget(parent)
{
	setWindowTitle(tr("Clip Launcher"));
	setObjectName(QStringLiteral("SessionGridView"));
	auto* outer = new QVBoxLayout(this);
	auto* statusRow = new QHBoxLayout;
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Clip launcher status"));
	m_status->setWordWrap(true);
	statusRow->addWidget(m_status, 1);
	// 0.5.0 theme 14: after a drop replaced a clip, one press puts it back.
	m_undoReplace = new QToolButton(this);
	m_undoReplace->setText(tr("Undo Replace"));
	m_undoReplace->setAccessibleName(tr("Undo the replacing drop"));
	m_undoReplace->setProperty("controlCommand", QStringLiteral("control.undo"));
	m_undoReplace->setMinimumHeight(tokens::targetSize());
	m_undoReplace->hide();
	connect(m_undoReplace, &QToolButton::clicked, this, &SessionGridView::undoReplace);
	statusRow->addWidget(m_undoReplace);
	outer->addLayout(statusRow);
	// Follow Actions, arrangement record/land, back to arrangement (session.*).
	outer->addWidget(new SessionTransportBar(this));
	auto* gridHost = new QWidget(this);
	m_grid = new QGridLayout(gridHost);
	m_grid->setSpacing(2);
	outer->addWidget(gridHost);
	outer->addStretch();
	// The grid's own menu (size, launch quantisation, follow actions): a right-click anywhere
	// that is not a cell or a scene, and the Grid button, which an empty grid needs.
	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) { showMenu(nullptr, mapToGlobal(at)); });
	m_gridButton = new QToolButton(this);
	m_gridButton->setText(tr("Grid..."));
	m_gridButton->setAccessibleName(tr("Launcher grid settings"));
	connect(m_gridButton, &QToolButton::clicked, this, [this] { showMenu(nullptr, m_gridButton->mapToGlobal(QPoint(0, m_gridButton->height()))); });
	outer->insertWidget(1, m_gridButton, 0, Qt::AlignLeft);
	m_stopAll = makeButton(QStringLiteral("session.stop_all"));
	m_stopAll->setText(tr("\u25A0 Stop all"));
	m_stopAll->setAccessibleName(tr("Stop all clips"));
	connect(m_stopAll, &QToolButton::clicked, this, [this] { report(run(QStringLiteral("session.stop_all"))); });
	connect(&m_timer, &QTimer::timeout, this, &SessionGridView::refresh);
	m_timer.start(RefreshMs);
	refresh();
}


QToolButton* SessionGridView::makeButton(const QString& command)
{
	auto* button = new QToolButton(this);
	button->setProperty("controlCommand", command);
	button->setFocusPolicy(Qt::StrongFocus);
	button->setMinimumSize(CellWidth, tokens::targetSize() + tokens::space::xxs);  // 72 x 26, larger in accessible mode
	button->installEventFilter(this);
	button->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(button, &QWidget::customContextMenuRequested, this,
		[this, button](const QPoint& at) { showMenu(button, button->mapToGlobal(at)); });
	return button;
}


QToolButton* SessionGridView::cellButton(int track, int scene) const
{
	if (track < 0 || scene < 0 || track >= m_tracks || scene >= m_scenes) { return nullptr; }
	return m_cells[static_cast<std::size_t>(track * m_scenes + scene)];
}


QLabel* SessionGridView::columnHeader(int track) const
{
	return track >= 0 && track < m_tracks ? m_headers[static_cast<std::size_t>(track)] : nullptr;
}


QToolButton* SessionGridView::sceneButton(int scene) const
{
	return scene >= 0 && scene < m_scenes ? m_sceneButtons[static_cast<std::size_t>(scene)] : nullptr;
}


QToolButton* SessionGridView::stopButton(int track) const
{
	return track >= 0 && track < m_tracks ? m_stopButtons[static_cast<std::size_t>(track)] : nullptr;
}


void SessionGridView::rebuild(int tracks, int scenes)
{
	// deleteLater, not delete: a rebuild can be reached from a button's own click (the
	// click refreshes), and a button must not be destroyed inside its own signal.
	for (auto* list : {&m_cells, &m_sceneButtons, &m_stopButtons})
	{
		for (QToolButton* button : *list)
		{
			m_grid->removeWidget(button);
			button->hide();
			button->deleteLater();
		}
	}
	for (QLabel* header : m_headers)
	{
		m_grid->removeWidget(header);
		header->deleteLater();
	}
	m_grid->removeWidget(m_stopAll);
	m_cells.clear();
	m_sceneButtons.clear();
	m_stopButtons.clear();
	m_headers.clear();
	m_tracks = tracks;
	m_scenes = scenes;
	m_dropCell = -1;
	m_playingScene.assign(static_cast<std::size_t>(tracks), -1);
	for (int track = 0; track < tracks; ++track)
	{
		// 0.5.0 theme 14: the column's header names its track (paintHeaders); a header is read, not pressed.
		auto* header = new QLabel(this);
		header->setAlignment(Qt::AlignCenter);
		header->setMinimumWidth(CellWidth);
		header->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);  // the cells set the width; the name elides to it
		m_grid->addWidget(header, 0, track);
		m_headers.push_back(header);
		for (int scene = 0; scene < scenes; ++scene)
		{
			QToolButton* cell = makeButton(QStringLiteral("session.launch_slot"));
			cell->setAcceptDrops(true);  // WF-053: an audio file dropped here fills the cell
			connect(cell, &QToolButton::pressed, this, [this, track, scene] { launchCell(track, scene); });
			connect(cell, &QToolButton::released, this, [this, track, scene] {  // WF-063: the release half
				ControlRegistry::instance()->invoke(QStringLiteral("session.release_slot"),
					{{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene}});
				refresh();  // the click has just toggled the checkable cell: draw the session's state back at once
			});
			m_grid->addWidget(cell, HeaderRow + scene, track);
			m_cells.push_back(cell);
		}
		QToolButton* stop = makeButton(QStringLiteral("session.stop_slot"));
		stop->setText(tr("\u25A0 Stop"));
		stop->setAccessibleName(tr("Stop track %1").arg(track + 1));
		connect(stop, &QToolButton::clicked, this, [this, track] { stopColumn(track); });
		m_grid->addWidget(stop, HeaderRow + scenes, track);
		m_stopButtons.push_back(stop);
	}
	for (int scene = 0; scene < scenes; ++scene)
	{
		QToolButton* launch = makeButton(QStringLiteral("session.launch_scene"));
		connect(launch, &QToolButton::clicked, this, [this, scene] {
			report(run(QStringLiteral("session.launch_scene"), {{QStringLiteral("scene"), scene}}));
		});
		m_grid->addWidget(launch, HeaderRow + scene, tracks);
		m_sceneButtons.push_back(launch);
	}
	m_grid->addWidget(m_stopAll, HeaderRow + scenes, tracks);
}


void SessionGridView::refresh()
{
	readTake();
	const ControlResult state = run(QStringLiteral("session.get_state"));
	if (!state.ok)
	{
		m_status->setText(state.errorMessage);
		return;
	}
	const QJsonObject grid = state.result.value(QStringLiteral("grid")).toObject();
	const int tracks = grid.value(QStringLiteral("tracks")).toInt();
	const int scenes = grid.value(QStringLiteral("scenes")).toInt();
	if (tracks != m_tracks || scenes != m_scenes) { rebuild(tracks, scenes); }
	m_status->setText(statusText(tracks, scenes));
	readColumns(state.result.value(QStringLiteral("columns")).toArray());
	m_slots = state.result.value(QStringLiteral("slots")).toArray();
	m_sceneStates = state.result.value(QStringLiteral("scenes")).toArray();
	m_names = cellNames(m_slots);
	paintHeaders();
	for (int track = 0; track < tracks; ++track)
	{
		for (int scene = 0; scene < scenes; ++scene) { paintCell(track, scene); }
	}
	paintScenes(state.result.value(QStringLiteral("scenes")).toArray());
}


QString SessionGridView::statusLine() const
{
	return m_status->text();
}


QString SessionGridView::statusText(int tracks, int scenes) const
{
	if (m_dropCell >= 0) { return dropPreviewText(); }
	if (!m_notice.isEmpty()) { return m_notice; }
	return tracks == 0 || scenes == 0 ? tr("The session grid is empty: Grid... (session.set_grid) sizes it.")
		: tr("%1 tracks, %2 scenes").arg(tracks).arg(scenes);
}


void SessionGridView::report(const ControlResult& result)
{
	m_notice = result.ok ? QString() : result.errorMessage;
	m_undoReplace->hide();  // the next action ends what it could undo
	// A refusal is spoken as well as shown (theme 17's announcements: an error, at once).
	if (!result.ok) { a11y::announceStatus(m_status, m_notice, a11y::Politeness::Assertive); }
	refresh();
}


void SessionGridView::readColumns(const QJsonArray& columns)
{
	std::fill(m_playingScene.begin(), m_playingScene.end(), -1);
	m_phases.assign(static_cast<std::size_t>(std::max(m_tracks, 0)), QString());
	for (const QJsonValue& value : columns)
	{
		const QJsonObject column = value.toObject();
		const int track = column.value(QStringLiteral("track")).toInt();
		if (track < 0 || track >= m_tracks) { continue; }
		m_playingScene[static_cast<std::size_t>(track)] = column.value(QStringLiteral("scene")).toInt();
		m_phases[static_cast<std::size_t>(track)] = column.value(QStringLiteral("phase")).toString();
	}
}


//! Each cell's display name in (track * scenes + scene) order; empty for an empty cell.
std::vector<QString> SessionGridView::cellNames(const QJsonArray& slotList) const
{
	std::vector<QString> names(static_cast<std::size_t>(std::max(m_tracks * m_scenes, 0)));
	for (const QJsonValue& value : slotList)
	{
		const QJsonObject slot = value.toObject();
		const int track = slot.value(QStringLiteral("track")).toInt();
		const int scene = slot.value(QStringLiteral("scene")).toInt();
		if (cellButton(track, scene) == nullptr) { continue; }
		const QString name = slot.value(QStringLiteral("name")).toString();
		names[static_cast<std::size_t>(track * m_scenes + scene)] = name.isEmpty() ? tr("clip") : name;
	}
	return names;
}


void SessionGridView::paintHeaders()
{
	for (int track = 0; track < m_tracks; ++track)
	{
		QLabel* header = m_headers[static_cast<std::size_t>(track)];
		const QString title = sessiongrid::columnTitle(track);
		// Cut to its column's width, never into the next header; the whole name is the tooltip.
		const int room = std::max(CellWidth, header->width()) - 2 * tokens::space::xs;
		const QString shown = header->fontMetrics().elidedText(title, Qt::ElideRight, room);
		if (header->text() != shown) { header->setText(shown); }
		const QString full = tr("Track %1: %2").arg(QString::number(track + 1), title);
		header->setAccessibleName(full);
		setTip(header, full);
	}
}


void SessionGridView::paintCell(int track, int scene)
{
	QToolButton* cell = cellButton(track, scene);
	const int index = track * m_scenes + scene;
	const QString& name = m_names[static_cast<std::size_t>(index)];
	const bool here = m_playingScene[static_cast<std::size_t>(track)] == scene;
	const QString phase = here ? m_phases[static_cast<std::size_t>(track)] : QString();
	const bool playing = phase == QLatin1String("playing");
	// 0.5.0 theme 14: every filled cell carries its play mark - its launch affordance and, by shape, its state.
	cell->setText(name.isEmpty() ? QString() : sessiongrid::cellMark(phase, true) + name);
	const bool taking = m_take.value(QStringLiteral("track")).toInt() == track && m_take.value(QStringLiteral("scene")).toInt() == scene;
	const QString recording = taking ? sessiongrid::recordingWords(m_take.value(QStringLiteral("state")).toString()) : QString();
	if (!recording.isEmpty()) { cell->setText(sessiongrid::recordingMark(m_take.value(QStringLiteral("state")).toString()) + name); }
	cell->setCheckable(true);
	cell->setChecked(playing);
	const QString where = tr("Track %1, scene %2").arg(track + 1).arg(scene + 1);
	cell->setAccessibleName(name.isEmpty() ? tr("%1: empty").arg(where)
		: tr("%1: %2%3").arg(where, name, phase.isEmpty() ? QString() : QStringLiteral(", ") + phase));
	if (!recording.isEmpty()) { cell->setAccessibleName(cell->accessibleName() + QStringLiteral(", ") + recording); }
	setTip(cell, launchNote(track, scene));
	paintDropPreview(cell, index);
}


QString SessionGridView::launchNote(int track, int scene) const
{
	const int on = m_playingScene[static_cast<std::size_t>(track)];
	const QString& name = m_names[static_cast<std::size_t>(track * m_scenes + scene)];
	if (on < 0 || on == scene || on >= m_scenes || m_phases[static_cast<std::size_t>(track)] == QLatin1String("stopping"))
	{
		return name.isEmpty() ? QString() : tr("Press to launch %1").arg(name);
	}
	return sessiongrid::launchReplaces(name, m_names[static_cast<std::size_t>(track * m_scenes + on)]);
}


void SessionGridView::paintScenes(const QJsonArray& sceneStates)
{
	for (int scene = 0; scene < m_scenes; ++scene)
	{
		QToolButton* button = m_sceneButtons[static_cast<std::size_t>(scene)];
		const QString name = sceneStates.at(scene).toObject().value(QStringLiteral("name")).toString();
		const QString label = name.isEmpty() ? tr("Scene %1").arg(scene + 1) : name;
		// 0.5.0 theme 14: the play mark says the row's state by its shape; an empty scene is in italics too.
		const sessiongrid::SceneState state = sessiongrid::sceneState(scene, m_scenes, m_names, m_playingScene, m_phases);
		button->setText(sessiongrid::sceneMark(state) + label);
		if (button->font().italic() != (state == sessiongrid::SceneState::Empty))
		{
			QFont font = button->font();
			font.setItalic(state == sessiongrid::SceneState::Empty);
			button->setFont(font);
		}
		// WF-062: the scene's colour as a bar along the button's left edge.
		// A project file's colour reaches here unchecked: only a valid one, as #rrggbb, goes into the style sheet.
		const QColor color(sceneStates.at(scene).toObject().value(QStringLiteral("color")).toString());
		button->setStyleSheet(!color.isValid() ? QString()
			: QStringLiteral("QToolButton { border-left: 4px solid %1; }").arg(color.name()));
		button->setAccessibleName(tr("Launch %1, %2").arg(label, sessiongrid::stateText(state)));
		setTip(button, tr("Launch %1 (%2)").arg(label, sessiongrid::stateText(state)));
	}
}


void SessionGridView::launchCell(int track, int scene)
{
	// An empty cell launches nothing (session.launch_slot refuses it): the press is
	// still the grid's to answer, and a refusal is shown rather than swallowed.
	// The refusal stays in the status line until the next action succeeds: refresh() runs every
	// 150 ms and would otherwise replace it before anyone could read it.
	report(run(QStringLiteral("session.launch_slot"), {{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene}}));
}


void SessionGridView::stopColumn(int track)
{
	const int scene = track >= 0 && track < m_tracks ? m_playingScene[static_cast<std::size_t>(track)] : -1;
	if (scene < 0) { return; }
	report(run(QStringLiteral("session.stop_slot"), {{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene}}));
}


bool SessionGridView::eventFilter(QObject* watched, QEvent* event)
{
	auto* button = qobject_cast<QToolButton*>(watched);
	if (button != nullptr && dropOnCell(button, event)) { return true; }
	if (event->type() != QEvent::KeyPress || button == nullptr) { return QWidget::eventFilter(watched, event); }
	const auto key = static_cast<QKeyEvent*>(event)->key();
	if (key == Qt::Key_Return || key == Qt::Key_Enter)
	{
		button->click();
		return true;
	}
	QWidget* next = neighbour(button, key);
	if (next == nullptr) { return QWidget::eventFilter(watched, event); }
	next->setFocus(Qt::TabFocusReason);
	return true;
}


void SessionGridView::showMenu(QToolButton* button, const QPoint& global)
{
	QMenu menu(this);
	populateMenu(&menu, button);
	if (!menu.isEmpty()) { menu.exec(global); }
	refresh();
}


void SessionGridView::populateMenu(QMenu* menu, QToolButton* button) const
{
	const auto cell = std::find(m_cells.begin(), m_cells.end(), button);
	const auto scene = std::find(m_sceneButtons.begin(), m_sceneButtons.end(), button);
	if (button != nullptr && cell != m_cells.end())
	{
		const int index = static_cast<int>(cell - m_cells.begin());
		const int track = index / m_scenes;
		const int row = index % m_scenes;
		SessionGridMenus::populateCell(menu, track, row, slotAt(track, row));
		for (QAction* action : menu->actions())  // WF-055: a take this grid starts, it lands
		{
			if (action->property("recordTake").toBool()) { connect(action, &QAction::triggered, this, [this] { m_landTake = true; }); }
		}
	}
	else if (button != nullptr && scene != m_sceneButtons.end())
	{
		const int row = static_cast<int>(scene - m_sceneButtons.begin());
		SessionGridMenus::populateScene(menu, row, m_sceneStates.at(row).toObject());
	}
	else { SessionGridMenus::populateGrid(menu, m_tracks, m_scenes); }
}


void SessionGridView::readTake()
{
	m_take = run(QStringLiteral("session.record_slot_status")).result;
	const QString state = m_take.value(QStringLiteral("state")).toString();
	if (state == QLatin1String("idle")) { m_landTake = false; }
	if (state != QLatin1String("done") || !m_landTake) { return; }
	m_landTake = false;
	const ControlResult landed = run(QStringLiteral("session.record_slot_land"));
	m_notice = landed.ok ? tr("Recorded take landed in track %1, scene %2 (%3 s)").arg(m_take.value(QStringLiteral("track")).toInt() + 1)
		.arg(m_take.value(QStringLiteral("scene")).toInt() + 1).arg(landed.result.value(QStringLiteral("seconds")).toDouble(), 0, 'f', 1)
		: landed.errorMessage;
	a11y::announceStatus(m_status, m_notice, landed.ok ? a11y::Politeness::Polite : a11y::Politeness::Assertive);
	m_take = run(QStringLiteral("session.record_slot_status")).result;
}


QJsonObject SessionGridView::slotAt(int track, int scene) const
{
	for (const QJsonValue& value : m_slots)
	{
		const QJsonObject slot = value.toObject();
		if (slot.value(QStringLiteral("track")).toInt() == track && slot.value(QStringLiteral("scene")).toInt() == scene
			&& slot.value(QStringLiteral("type")).toString() != QStringLiteral("empty"))
		{
			return slot;
		}
	}
	return {};
}


//! The widget an arrow key moves to from @a button, or nullptr (not an arrow, or off the grid).
QWidget* SessionGridView::neighbour(QToolButton* button, int key) const
{
	const int index = m_grid->indexOf(button);
	if (index < 0) { return nullptr; }
	int row = 0;
	int column = 0;
	int rowSpan = 0;
	int columnSpan = 0;
	m_grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
	static const std::pair<int, std::pair<int, int>> moves[] = {{Qt::Key_Left, {0, -1}},
		{Qt::Key_Right, {0, 1}}, {Qt::Key_Up, {-1, 0}}, {Qt::Key_Down, {1, 0}}};
	for (const auto& [arrow, step] : moves)
	{
		if (arrow != key) { continue; }
		// A column header is read, not focused: the arrows stop at the first row of cells.
		QLayoutItem* item = m_grid->itemAtPosition(row + step.first, column + step.second);
		QWidget* next = item != nullptr ? item->widget() : nullptr;
		return next != nullptr && next->focusPolicy() != Qt::NoFocus ? next : nullptr;
	}
	return nullptr;
}

} // namespace lmms::gui
