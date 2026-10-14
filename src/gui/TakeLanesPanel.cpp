/*
 * TakeLanesPanel.cpp - M3 item 6: take lanes with audition and consolidate
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

#include "TakeLanesPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "CompSwipeStrip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ItemList.h"
#include "Engine.h"
#include "MusicalTime.h"
#include "ShellChrome.h"
#include "Song.h"
#include "Track.h"

namespace lmms::gui
{

namespace
{

constexpr int RefreshMs = 300;

/*! Theme 7: a comp bound. Its value is a tick (what comp.select takes); its text is that position in the time
 *  format - bars|beats unless ticks or time is chosen - and it is typed in any of them. The arrows step a beat. */
class PositionBox : public QSpinBox
{
public:
	explicit PositionBox(QWidget* parent) :
		QSpinBox(parent)
	{
		setRange(0, MaxSongLength);
		Song* song = Engine::getSong();
		setSingleStep(std::max(1, song->ticksPerBar() / std::max(1, song->getTimeSigModel().getNumerator())));
		QObject::connect(musicaltime::formatSignal(), &musicaltime::FormatSignal::changed, this,
			[this] { lineEdit()->setText(textFromValue(value())); });
	}

protected:
	QString textFromValue(int value) const override { return musicaltime::positionText(value); }

	int valueFromText(const QString& text) const override
	{
		int tick = value();
		musicaltime::positionFromText(text, &tick);
		return tick;
	}

	QValidator::State validate(QString& text, int&) const override
	{
		int tick = 0;
		return musicaltime::positionFromText(text, &tick) && tick <= maximum() ? QValidator::Acceptable : QValidator::Intermediate;
	}
};

} // namespace


TakeLanesPanel::TakeLanesPanel(const QString& trackId, QWidget* parent) :
	QWidget(parent),
	m_trackId(trackId)
{
	// The track's name, not its control id, when it has one (review ux05 #89).
	ControlResult unknown;
	const Track* track = control::resolveTrack(trackId, &unknown);
	setWindowTitle(tr("Take lanes - %1").arg(track != nullptr ? track->name() : trackId));
	setObjectName(QStringLiteral("TakeLanesPanel"));
	auto* outer = new QVBoxLayout(this);
	m_lanes = new QVBoxLayout;
	outer->addLayout(m_lanes);
	m_addLane = makeButton(QStringLiteral("comp.lane_add"), tr("Add lane"));
	connect(m_addLane, &QToolButton::clicked, this, [this] { run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), m_trackId}}); });
	auto* head = new QHBoxLayout;
	head->addWidget(m_addLane);
	outer->addLayout(head);
	auto* takes = new QHBoxLayout;
	const auto addLabel = [this, takes](const QString& text) {
		auto* label = new QLabel(text, this);
		takes->addWidget(label);
	};
	addLabel(tr("Group"));
	m_takeGroup = new QComboBox(this);
	m_takeGroup->setAccessibleName(tr("Recorded take group"));
	takes->addWidget(m_takeGroup);
	addLabel(tr("Take"));
	m_takeChoice = new QComboBox(this);
	m_takeChoice->setAccessibleName(tr("Active take choice"));
	takes->addWidget(m_takeChoice);
	m_useTake = makeButton(QStringLiteral("comp.take_select"), tr("Use take"));
	m_useTake->setEnabled(false);
	connect(m_useTake, &QToolButton::clicked, this, [this] {
		const QString clip = m_takeChoice->currentData(Qt::UserRole).toString();
		const int lane = m_takeChoice->currentData(Qt::UserRole + 1).toInt();
		if (!clip.isEmpty())
		{
			run(QStringLiteral("comp.take_select"), {{QStringLiteral("clip"), clip},
				{QStringLiteral("lane"), lane}});
		}
	});
	takes->addWidget(m_useTake);
	m_playAllTakes = new QCheckBox(tr("Play all takes"), this);
	m_playAllTakes->setAccessibleName(tr("Play all takes"));
	m_playAllTakes->setProperty("controlCommand", QStringLiteral("comp.play_all_takes"));
	connect(m_playAllTakes, &QCheckBox::toggled, this, [this](bool enabled) {
		run(QStringLiteral("comp.play_all_takes"), {{QStringLiteral("track"), m_trackId},
			{QStringLiteral("enabled"), enabled}});
	});
	takes->addWidget(m_playAllTakes);
	takes->addStretch();
	outer->addLayout(takes);
	connect(m_takeGroup, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		const int groupId = index >= 0 ? m_takeGroup->currentData().toInt() : -1;
		for (const QJsonValue& value : m_takeGroups)
		{
			const QJsonObject group = value.toObject();
			if (group.value(QStringLiteral("group")).toInt() == groupId)
			{
				showTakeChoices(group);
				return;
			}
		}
		showTakeChoices(QJsonObject());
	});

	// 0.5.0 L03 (ED-165 / ED-168): drag along a lane to comp that range, double-click to cycle the take.
	m_strip = new CompSwipeStrip(m_trackId, this);
	m_strip->setToolTip(m_strip->accessibleName());  // the gestures, on hover too (review ux05 #91)
	connect(m_strip, &CompSwipeStrip::ran, this, [this](const QString& status) {
		m_status->setText(status);
		refresh();
	});
	outer->addWidget(m_strip);
	// Theme 10: beside Add lane, the comp's segments as an editable list (window.item_list, as Ctrl+Shift+L on the strip).
	ShellButton* list = itemlists::listButton(QStringLiteral("comp"), m_strip, this);
	list->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	head->addWidget(list);
	head->addStretch();

	m_composite = new QLabel(this);
	m_composite->setAccessibleName(tr("Composite"));
	m_composite->setWordWrap(true);
	outer->addWidget(m_composite);

	auto* comp = new QHBoxLayout;
	m_begin = new PositionBox(this);
	m_end = new PositionBox(this);
	for (QSpinBox* box : {m_begin, m_end})
	{
		box->setToolTip(tr("A position in bars and beats (bar.beat.sixteenth), or ticks or a time when that format is "
			"chosen; type any of them (\"768t\", \"0:08.5\")"));
	}
	m_begin->setAccessibleName(tr("Comp from"));
	m_end->setAccessibleName(tr("Comp to"));
	m_lane = new QComboBox(this);
	m_lane->setAccessibleName(tr("Lane to comp in"));
	m_comp = makeButton(QStringLiteral("comp.select"), tr("Comp"));
	connect(m_comp, &QToolButton::clicked, this, [this] {
		run(QStringLiteral("comp.select"), {{QStringLiteral("track"), m_trackId},
			{QStringLiteral("begin"), m_begin->value()}, {QStringLiteral("end"), m_end->value()},
			{QStringLiteral("lane"), m_lane->currentData().toInt()}});
	});
	comp->addWidget(m_begin);
	comp->addWidget(m_end);
	comp->addWidget(m_lane);
	comp->addWidget(m_comp);
	outer->addLayout(comp);

	m_consolidate = makeButton(QStringLiteral("clip.consolidate"), tr("Consolidate the comp"));
	connect(m_consolidate, &QToolButton::clicked, this, [this] {
		QJsonObject args{{QStringLiteral("track"), m_trackId}};
		if (m_spanEnd > m_spanBegin)
		{
			args.insert(QStringLiteral("start"), m_spanBegin);
			args.insert(QStringLiteral("end"), m_spanEnd);
		}
		run(QStringLiteral("clip.consolidate"), args);
	});
	outer->addWidget(m_consolidate);
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Take lanes status"));
	outer->addWidget(m_status);
	outer->addStretch();

	connect(&m_timer, &QTimer::timeout, this, &TakeLanesPanel::refresh);
	m_timer.start(RefreshMs);
	refresh();
}


QToolButton* TakeLanesPanel::makeButton(const QString& command, const QString& text)
{
	auto* button = new QToolButton(this);
	button->setText(text);
	button->setAccessibleName(text);
	button->setProperty("controlCommand", command);
	button->setFocusPolicy(Qt::StrongFocus);
	return button;
}


void TakeLanesPanel::run(const QString& command, const QJsonObject& args)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	m_status->setText(result.ok ? tr("%1: done").arg(command) : result.errorMessage);
	refresh();
}


void TakeLanesPanel::audition(int lane)
{
	const int next = m_auditioning == lane ? -1 : lane;
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("comp.audition"),
		{{QStringLiteral("track"), m_trackId}, {QStringLiteral("lane"), next}});
	if (result.ok) { m_auditioning = next; }
	m_status->setText(result.ok ? (next < 0 ? tr("Playing the comp") : tr("Auditioning lane %1").arg(next))
		: result.errorMessage);
	refresh();
}


QToolButton* TakeLanesPanel::auditionButton(int lane) const
{
	for (std::size_t i = 0; i < m_laneIndices.size(); ++i)
	{
		if (m_laneIndices[i] == lane) { return m_auditions[i]; }
	}
	return nullptr;
}


QString TakeLanesPanel::compositeText() const
{
	return m_composite->text();
}


QString TakeLanesPanel::statusText() const
{
	return m_status->text();
}


void TakeLanesPanel::refresh()
{
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("comp.get_state"),
		{{QStringLiteral("track"), m_trackId}});
	// A strip whose state could not be read (the track is gone) takes no swipes on the lanes it last showed.
	m_strip->setEnabled(state.ok);
	if (!state.ok)
	{
		m_status->setText(state.errorMessage);
		return;
	}
	const QJsonArray lanes = state.result.value(QStringLiteral("lanes")).toArray();
	m_strip->setState(state.result);
	showLanes(lanes);
	showTakeGroups(state.result.value(QStringLiteral("take_groups")).toArray());
	{
		const QSignalBlocker blocker(m_playAllTakes);
		m_playAllTakes->setChecked(state.result.value(QStringLiteral("play_all_takes")).toBool());
	}
	showComposite(state.result.value(QStringLiteral("composite")).toObject(), !lanes.isEmpty());
}

//! The lane buttons (rebuilt when the lane set changes) and their labels.
void TakeLanesPanel::showLanes(const QJsonArray& lanes)
{
	std::vector<int> indices;
	for (const QJsonValue& value : lanes) { indices.push_back(value.toObject().value(QStringLiteral("lane")).toInt()); }
	if (indices != m_laneIndices)
	{
		for (QToolButton* button : m_auditions) { button->deleteLater(); }
		m_auditions.clear();
		m_lane->clear();
		m_laneIndices = indices;
		for (int lane : indices)
		{
			QToolButton* button = makeButton(QStringLiteral("comp.audition"), QString());
			button->setCheckable(true);
			connect(button, &QToolButton::clicked, this, [this, lane] { audition(lane); });
			m_lanes->addWidget(button);
			m_auditions.push_back(button);
			m_lane->addItem(tr("Lane %1").arg(lane), lane);
		}
	}
	for (std::size_t i = 0; i < m_auditions.size(); ++i)
	{
		const QJsonObject lane = lanes.at(static_cast<int>(i)).toObject();
		const QString name = lane.value(QStringLiteral("name")).toString();
		const int takes = lane.value(QStringLiteral("takes")).toArray().size();
		const QString label = tr("Lane %1%2 - %n take(s)", nullptr, takes).arg(m_laneIndices[i])
			.arg(name.isEmpty() ? QString() : QStringLiteral(" (") + name + QStringLiteral(")"));
		m_auditions[i]->setText(tr("Audition: %1").arg(label));
		m_auditions[i]->setAccessibleName(tr("Audition %1").arg(label));
		m_auditions[i]->setChecked(m_laneIndices[i] == m_auditioning);
	}
}


void TakeLanesPanel::showTakeGroups(const QJsonArray& groups)
{
	const int previousGroup = m_takeGroup->currentIndex() >= 0
		? m_takeGroup->currentData(Qt::UserRole).toInt() : -1;
	m_takeGroups = groups;
	const QSignalBlocker blocker(m_takeGroup);
	m_takeGroup->clear();
	int selected = -1;
	for (int i = 0; i < groups.size(); ++i)
	{
		const QJsonObject group = groups.at(i).toObject();
		const int groupId = group.value(QStringLiteral("group")).toInt();
		m_takeGroup->addItem(tr("Group %1").arg(groupId), groupId);
		if (groupId == previousGroup) { selected = i; }
	}
	if (selected < 0 && m_takeGroup->count() > 0) { selected = 0; }
	m_takeGroup->setCurrentIndex(selected);
	const int selectedGroup = selected >= 0 ? m_takeGroup->currentData().toInt() : -1;
	for (const QJsonValue& value : groups)
	{
		const QJsonObject group = value.toObject();
		if (group.value(QStringLiteral("group")).toInt() == selectedGroup)
		{
			showTakeChoices(group);
			return;
		}
	}
	showTakeChoices(QJsonObject());
}


void TakeLanesPanel::showTakeChoices(const QJsonObject& group)
{
	const QSignalBlocker blocker(m_takeChoice);
	m_takeChoice->clear();
	const int activeLane = group.value(QStringLiteral("active_lane")).toInt(-1);
	int selected = -1;
	const QJsonArray takes = group.value(QStringLiteral("takes")).toArray();
	for (const QJsonValue& value : takes)
	{
		const QJsonObject take = value.toObject();
		const QString clip = take.value(QStringLiteral("clip")).toString();
		const int lane = take.value(QStringLiteral("lane")).toInt();
		m_takeChoice->addItem(tr("Lane %1 (%2)").arg(lane).arg(clip), clip);
		m_takeChoice->setItemData(m_takeChoice->count() - 1, lane, Qt::UserRole + 1);
		if (lane == activeLane && selected < 0) { selected = m_takeChoice->count() - 1; }
	}
	if (selected < 0 && m_takeChoice->count() > 0) { selected = 0; }
	m_takeChoice->setCurrentIndex(selected);
	m_useTake->setEnabled(selected >= 0);
}


void TakeLanesPanel::showComposite(const QJsonObject& composite, bool hasLanes)
{
	m_spanBegin = composite.value(QStringLiteral("begin")).toInt();
	m_spanEnd = composite.value(QStringLiteral("end")).toInt();
	QStringList segments;
	for (const QJsonValue& value : composite.value(QStringLiteral("segments")).toArray())
	{
		const QJsonObject segment = value.toObject();
		segments << tr("%1 to %2: lane %3").arg(musicaltime::positionText(segment.value(QStringLiteral("begin")).toInt()),
			musicaltime::positionText(segment.value(QStringLiteral("end")).toInt())).arg(segment.value(QStringLiteral("lane")).toInt());
	}
	m_composite->setText(segments.isEmpty() ? tr("No comp yet: pick a range and a lane, then Comp.")
		: tr("Comp: %1").arg(segments.join(QStringLiteral(", "))));
	m_consolidate->setEnabled(!segments.isEmpty() || hasLanes);
}

} // namespace lmms::gui
