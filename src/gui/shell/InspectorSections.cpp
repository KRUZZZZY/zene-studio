/*
 * InspectorSections.cpp - the Inspector's sections, kept or replaced in place as the selection moves
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

/*! 0.5.0 theme 13: the Inspector keeps its context. A section (channel, track, clip) is keyed by the object it shows:
 *  while the selection still names that object the section is left exactly as it is - its focus, its half-typed
 *  field and the scroll position with it. A section that must change is replaced where it stood, the keyboard focus
 *  put back on the control with the same accessible name (the "Volume" knob of the next track, the "Start" field of
 *  the next clip) and the scroll position kept. Only a section whose object is being deleted goes at once; every
 *  other change waits for the next event-loop turn, so nothing blinks out between a click and its redraw. */

#include <algorithm>

#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "Clip.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "InspectorPanel.h"
#include "Mixer.h"
#include "MidiClip.h"
#include "ShellSelection.h"
#include "Track.h"
#include "TransportReadouts.h"

namespace lmms::gui
{

namespace
{

QString pointerKey(const void* object) { return QString::number(reinterpret_cast<quintptr>(object), 16); }

//! The control that has the keyboard focus inside @a section, by what makes it findable again in a new section.
struct FocusMemo
{
	QString name;
	QByteArray type;
};

FocusMemo focusWithin(const QWidget* section)
{
	const QWidget* focus = QApplication::focusWidget();
	if (section == nullptr || focus == nullptr || !section->isAncestorOf(focus) || focus->accessibleName().isEmpty()) { return {}; }
	return {focus->accessibleName(), focus->metaObject()->className()};
}

void restoreFocus(QWidget* section, const FocusMemo& memo)
{
	if (memo.name.isEmpty()) { return; }
	for (QWidget* child : section->findChildren<QWidget*>())
	{
		if (child->accessibleName() == memo.name && memo.type == child->metaObject()->className())
		{
			child->setFocus(Qt::OtherFocusReason);
			return;
		}
	}
}

QString trackKey(const Track* track)
{
	return QStringLiteral("track|%1|%2|%3").arg(pointerKey(track)).arg(static_cast<int>(track->type())).arg(track->takeLanes().isEmpty() ? 0 : 1);
}

QString clipKey(const Clip* clip)
{
	return QStringLiteral("clip|%1|%2").arg(pointerKey(clip)).arg(dynamic_cast<const MidiClip*>(clip) != nullptr ? 1 : 0);
}

} // namespace

void InspectorPanel::retireSection(Section& slot)
{
	QWidget* widget = slot.widget.data();
	slot = Section{};
	if (widget == nullptr) { return; }
	m_sections->removeWidget(widget);
	widget->hide();
	widget->deleteLater();
	// The section's live fields and toggles go with it; kept, the list would grow by every replacement.
	m_liveFields.removeIf([widget](const QPointer<ReadoutField>& field) { return field.isNull() || widget->isAncestorOf(field); });
	if (m_name != nullptr && widget->isAncestorOf(m_name))
	{
		m_name = nullptr;
		m_mute = nullptr;
		m_solo = nullptr;
	}
}

void InspectorPanel::placeSection(Section& slot, const QString& key, const std::function<QWidget*()>& make, int index)
{
	if (!slot.widget.isNull() && slot.key == key) { return; }  // the same object: left exactly as it is
	const FocusMemo focus = focusWithin(slot.widget);
	QScrollBar* bar = m_scroll->verticalScrollBar();
	const int scrolled = bar->value();
	if (!slot.widget.isNull()) { index = m_sections->indexOf(slot.widget); }
	retireSection(slot);
	QWidget* made = make();
	m_sections->insertWidget(std::clamp(index, 0, m_sections->count()), made);
	made->show();
	slot = Section{key, made};
	restoreFocus(made, focus);
	// The layout settles on the next turn; the position is put back now and once more then.
	bar->setValue(scrolled);
	QTimer::singleShot(0, bar, [bar, scrolled] { bar->setValue(scrolled); });
}

void InspectorPanel::dropContent()
{
	for (Section* slot : {&m_empty, &m_channelSection, &m_trackSection, &m_clipSection}) { retireSection(*slot); }
}

void InspectorPanel::scheduleRebuild()
{
	// Theme 13: what is shown stays until the redraw; a section whose object is going was retired already.
	m_pending.start();
}

void InspectorPanel::refreshChannelSection()
{
	m_channelSection.key.clear();  // made again, in place, on the next turn
	m_pending.start();
}

void InspectorPanel::rebuild()
{
	m_pending.stop();
	if (!m_track.isNull()) { disconnect(m_track, nullptr, this, nullptr); }
	if (!m_clip.isNull()) { disconnect(m_clip, nullptr, this, nullptr); }
	m_track = ShellSelection::instance()->track();
	m_clip = ShellSelection::instance()->clip();
	ControlResult error;
	const QString channelId = ShellSelection::instance()->channel();
	MixerChannel* channel = channelId.isEmpty() ? nullptr : control::resolveMixerChannel(channelId, &error);
	if (channel != nullptr || m_track.isNull())
	{
		retireSection(m_trackSection);
		retireSection(m_clipSection);
	}
	if (channel != nullptr)
	{
		retireSection(m_empty);
		placeSection(m_channelSection, QStringLiteral("channel|%1|%2").arg(channelId, pointerKey(channel)),
			[this, channel, channelId] { return channelSection(channel, channelId); }, 0);
	}
	else if (m_track.isNull())
	{
		retireSection(m_channelSection);
		placeSection(m_empty, QStringLiteral("empty"), [this] {
			QLabel* note = new QLabel(tr("Select a track, a clip or a mixer channel to see its properties here."), m_content);
			note->setObjectName(QStringLiteral("deviceCaption"));
			note->setWordWrap(true);
			return note;
		}, 0);
	}
	else { placeTrackAndClip(); }
	syncToggles();
}

void InspectorPanel::placeTrackAndClip()
{
	retireSection(m_empty);
	retireSection(m_channelSection);
	// A deleted track or clip takes its section at once - its controls must not paint a freed model.
	connect(m_track, &QObject::destroyed, this, [this] {
		retireSection(m_trackSection);
		retireSection(m_clipSection);
		scheduleRebuild();
	});
	placeSection(m_trackSection, trackKey(m_track), [this] { return trackSection(m_track); }, 0);
	if (m_clip.isNull())
	{
		retireSection(m_clipSection);
		return;
	}
	connect(m_clip, &QObject::destroyed, this, [this] { retireSection(m_clipSection); scheduleRebuild(); });
	placeSection(m_clipSection, clipKey(m_clip), [this] { return clipSection(m_clip); }, 1);
}

void InspectorPanel::syncChannelName()
{
	if (m_channelName.isNull() || m_channelName->hasFocus()) { return; }
	ControlResult error;
	const MixerChannel* channel = control::resolveMixerChannel(m_channelName->property("channel").toString(), &error);
	if (channel != nullptr && m_channelName->text() != channel->m_name) { m_channelName->setText(channel->m_name); }
}

QJsonObject InspectorPanel::describe() const
{
	QJsonObject out{{QStringLiteral("track"), control::trackIdOf(m_track)}, {QStringLiteral("clip"), control::clipIdOf(m_clip)},
		{QStringLiteral("channel"), ShellSelection::instance()->channel()}};
	if (!m_track.isNull())
	{
		out.insert(QStringLiteral("name"), m_track->name());
		out.insert(QStringLiteral("muted"), m_track->isMuted());
		out.insert(QStringLiteral("soloed"), m_track->isSolo());
	}
	QJsonArray sections;
	for (const auto& [slot, name] : {std::pair{&m_empty, "empty"}, std::pair{&m_channelSection, "channel"},
			std::pair{&m_trackSection, "track"}, std::pair{&m_clipSection, "clip"}})
	{
		if (!slot->widget.isNull()) { sections.append(QLatin1String(name)); }
	}
	out.insert(QStringLiteral("sections"), sections);
	return out;
}

} // namespace lmms::gui
