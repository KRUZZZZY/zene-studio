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

#include <QGridLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

constexpr int RefreshMs = 150;

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
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Clip launcher status"));
	outer->addWidget(m_status);
	auto* gridHost = new QWidget(this);
	m_grid = new QGridLayout(gridHost);
	m_grid->setSpacing(2);
	outer->addWidget(gridHost);
	outer->addStretch();
	m_stopAll = makeButton(QStringLiteral("session.stop_all"));
	m_stopAll->setText(tr("Stop all"));
	m_stopAll->setAccessibleName(tr("Stop all clips"));
	connect(m_stopAll, &QToolButton::clicked, this, [this] { run(QStringLiteral("session.stop_all")); refresh(); });
	connect(&m_timer, &QTimer::timeout, this, &SessionGridView::refresh);
	m_timer.start(RefreshMs);
	refresh();
}


QToolButton* SessionGridView::makeButton(const QString& command)
{
	auto* button = new QToolButton(this);
	button->setProperty("controlCommand", command);
	button->setFocusPolicy(Qt::StrongFocus);
	button->setMinimumSize(72, 26);
	button->installEventFilter(this);
	return button;
}


QToolButton* SessionGridView::cellButton(int track, int scene) const
{
	if (track < 0 || scene < 0 || track >= m_tracks || scene >= m_scenes) { return nullptr; }
	return m_cells[static_cast<std::size_t>(track * m_scenes + scene)];
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
	m_grid->removeWidget(m_stopAll);
	m_cells.clear();
	m_sceneButtons.clear();
	m_stopButtons.clear();
	m_tracks = tracks;
	m_scenes = scenes;
	m_playingScene.assign(static_cast<std::size_t>(tracks), -1);
	for (int track = 0; track < tracks; ++track)
	{
		for (int scene = 0; scene < scenes; ++scene)
		{
			QToolButton* cell = makeButton(QStringLiteral("session.launch_slot"));
			connect(cell, &QToolButton::clicked, this, [this, track, scene] { launchCell(track, scene); });
			m_grid->addWidget(cell, scene, track);
			m_cells.push_back(cell);
		}
		QToolButton* stop = makeButton(QStringLiteral("session.stop_slot"));
		stop->setText(tr("Stop"));
		stop->setAccessibleName(tr("Stop track %1").arg(track + 1));
		connect(stop, &QToolButton::clicked, this, [this, track] { stopColumn(track); });
		m_grid->addWidget(stop, scenes, track);
		m_stopButtons.push_back(stop);
	}
	for (int scene = 0; scene < scenes; ++scene)
	{
		QToolButton* launch = makeButton(QStringLiteral("session.launch_scene"));
		connect(launch, &QToolButton::clicked, this, [this, scene] {
			run(QStringLiteral("session.launch_scene"), {{QStringLiteral("scene"), scene}});
			refresh();
		});
		m_grid->addWidget(launch, scene, tracks);
		m_sceneButtons.push_back(launch);
	}
	m_grid->addWidget(m_stopAll, scenes, tracks);
}


void SessionGridView::refresh()
{
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
	m_status->setText(tracks == 0 || scenes == 0
		? tr("The session grid is empty: session.set_grid sizes it.")
		: tr("%1 tracks, %2 scenes").arg(tracks).arg(scenes));

	std::fill(m_playingScene.begin(), m_playingScene.end(), -1);
	std::vector<QString> phases(static_cast<std::size_t>(tracks));
	for (const QJsonValue& value : state.result.value(QStringLiteral("columns")).toArray())
	{
		const QJsonObject column = value.toObject();
		const int track = column.value(QStringLiteral("track")).toInt();
		if (track < 0 || track >= tracks) { continue; }
		m_playingScene[static_cast<std::size_t>(track)] = column.value(QStringLiteral("scene")).toInt();
		phases[static_cast<std::size_t>(track)] = column.value(QStringLiteral("phase")).toString();
	}
	std::vector<QString> names(static_cast<std::size_t>(tracks * scenes));
	std::vector<bool> filled(names.size(), false);
	for (const QJsonValue& value : state.result.value(QStringLiteral("slots")).toArray())
	{
		const QJsonObject slot = value.toObject();
		const int track = slot.value(QStringLiteral("track")).toInt();
		const int scene = slot.value(QStringLiteral("scene")).toInt();
		if (track < 0 || scene < 0 || track >= tracks || scene >= scenes) { continue; }
		const auto index = static_cast<std::size_t>(track * scenes + scene);
		filled[index] = true;
		const QString name = slot.value(QStringLiteral("name")).toString();
		names[index] = name.isEmpty() ? tr("clip") : name;
	}
	for (int track = 0; track < tracks; ++track)
	{
		for (int scene = 0; scene < scenes; ++scene)
		{
			const auto index = static_cast<std::size_t>(track * scenes + scene);
			QToolButton* cell = m_cells[index];
			const bool here = m_playingScene[static_cast<std::size_t>(track)] == scene;
			const QString phase = here ? phases[static_cast<std::size_t>(track)] : QString();
			const QString marker = phase == QLatin1String("playing") ? QStringLiteral("▶ ")
				: phase == QLatin1String("launching") ? QStringLiteral("… ") : QString();
			cell->setText(filled[index] ? marker + names[index] : QString());
			cell->setCheckable(true);
			cell->setChecked(here && phase == QLatin1String("playing"));
			cell->setAccessibleName(filled[index]
				? tr("Track %1, scene %2: %3%4").arg(track + 1).arg(scene + 1).arg(names[index])
					.arg(phase.isEmpty() ? QString() : QStringLiteral(", ") + phase)
				: tr("Track %1, scene %2: empty").arg(track + 1).arg(scene + 1));
		}
	}
	const QJsonArray sceneStates = state.result.value(QStringLiteral("scenes")).toArray();
	for (int scene = 0; scene < scenes; ++scene)
	{
		const QString name = sceneStates.at(scene).toObject().value(QStringLiteral("name")).toString();
		const QString label = name.isEmpty() ? tr("Scene %1").arg(scene + 1) : name;
		m_sceneButtons[static_cast<std::size_t>(scene)]->setText(QStringLiteral("▶ ") + label);
		m_sceneButtons[static_cast<std::size_t>(scene)]->setAccessibleName(tr("Launch %1").arg(label));
	}
}


void SessionGridView::launchCell(int track, int scene)
{
	// An empty cell launches nothing (session.launch_slot refuses it): the press is
	// still the grid's to answer, and a refusal is shown rather than swallowed.
	const ControlResult launched = run(QStringLiteral("session.launch_slot"),
		{{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene}});
	if (!launched.ok) { m_status->setText(launched.errorMessage); }
	refresh();
}


void SessionGridView::stopColumn(int track)
{
	const int scene = track >= 0 && track < m_tracks ? m_playingScene[static_cast<std::size_t>(track)] : -1;
	if (scene < 0) { return; }
	run(QStringLiteral("session.stop_slot"), {{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene}});
	refresh();
}


bool SessionGridView::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() != QEvent::KeyPress) { return QWidget::eventFilter(watched, event); }
	auto* button = qobject_cast<QToolButton*>(watched);
	const auto key = static_cast<QKeyEvent*>(event)->key();
	if (button != nullptr && (key == Qt::Key_Return || key == Qt::Key_Enter))
	{
		button->click();
		return true;
	}
	int row = 0;
	int column = 0;
	int rowSpan = 0;
	int columnSpan = 0;
	const int index = button != nullptr ? m_grid->indexOf(button) : -1;
	if (index < 0) { return QWidget::eventFilter(watched, event); }
	m_grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
	switch (key)
	{
		case Qt::Key_Left: --column; break;
		case Qt::Key_Right: ++column; break;
		case Qt::Key_Up: --row; break;
		case Qt::Key_Down: ++row; break;
		default: return QWidget::eventFilter(watched, event);
	}
	if (QLayoutItem* item = m_grid->itemAtPosition(row, column); item != nullptr && item->widget() != nullptr)
	{
		item->widget()->setFocus(Qt::TabFocusReason);
	}
	return true;
}

} // namespace lmms::gui
