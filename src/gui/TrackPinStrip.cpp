/*
 * TrackPinStrip.cpp - the rows of pinned folders, held above the scrolling track list
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

#include "TrackPinStrip.h"

#include <QVBoxLayout>

#include "TrackFolder.h"
#include "TrackView.h"

namespace lmms::gui
{

namespace
{

//! Whether @a layout holds exactly @a widgets, in that order.
bool holdsExactly(const QLayout* layout, const QList<QWidget*>& widgets)
{
	if (layout->count() != widgets.size()) { return false; }
	for (int i = 0; i < widgets.size(); ++i)
	{
		if (layout->itemAt(i)->widget() != widgets.at(i)) { return false; }
	}
	return true;
}

/*! Empties @a layout and fills it with @a widgets in order - unless it already holds them. Each widget
 *  keeps whether it was hidden: changing a widget's parent hides it, and the caller (realignTracks)
 *  has already decided which rows show. Taking an item out never deletes its widget. */
void refill(QVBoxLayout* layout, const QList<QWidget*>& widgets)
{
	if (holdsExactly(layout, widgets)) { return; }
	while (QLayoutItem* item = layout->takeAt(0)) { delete item; }
	for (QWidget* widget : widgets)
	{
		const bool hidden = widget->isHidden();
		layout->addWidget(widget);
		widget->setVisible(!hidden);
	}
}

} // namespace

TrackPinStrip::TrackPinStrip(QWidget* parent) :
	QWidget(parent),
	m_layout(new QVBoxLayout(this))
{
	setObjectName(QStringLiteral("trackPinStrip"));
	setAccessibleName(tr("Pinned tracks"));
	setAccessibleDescription(tr("Pinned folders, kept in view while the track list scrolls"));
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->setSpacing(0);
	// As tall as the rows it holds (each row's height is fixed by its track), never stretched.
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	hide();
}

bool TrackPinStrip::pins(const TrackView* view)
{
	const auto* folder = dynamic_cast<const TrackFolder*>(view->getTrack());
	return folder != nullptr && folder->isPinned();
}

QWidget* TrackPinStrip::placeholderFor(const TrackView* view)
{
	QWidget*& placeholder = m_placeholders[view];
	if (placeholder == nullptr)
	{
		placeholder = new QWidget;
		placeholder->setFixedHeight(0);
	}
	return placeholder;
}

QList<QWidget*> TrackPinStrip::scrollOrder(const QList<TrackView*>& views)
{
	QList<QWidget*> order;
	for (TrackView* view : views) { order.append(pins(view) ? placeholderFor(view) : view); }
	return order;
}

void TrackPinStrip::dropStalePlaceholders(const QList<TrackView*>& views)
{
	for (auto it = m_placeholders.begin(); it != m_placeholders.end();)
	{
		// Membership first: a key whose row is gone is a dead pointer, compared and never read.
		const bool live = views.contains(const_cast<TrackView*>(it.key())) && pins(it.key());
		if (live) { ++it; continue; }
		delete it.value();  // already out of the layout: sync() refilled it without this one
		it = m_placeholders.erase(it);
	}
}

void TrackPinStrip::sync(const QList<TrackView*>& views, QVBoxLayout* scrollLayout)
{
	refill(scrollLayout, scrollOrder(views));
	QList<QWidget*> pinned;
	for (TrackView* view : views)
	{
		if (pins(view)) { pinned.append(view); }
	}
	refill(m_layout, pinned);
	dropStalePlaceholders(views);
	updateShown();
}

QList<TrackView*> TrackPinStrip::pinnedViews() const
{
	QList<TrackView*> views;
	for (int i = 0; i < m_layout->count(); ++i)
	{
		if (auto* view = qobject_cast<TrackView*>(m_layout->itemAt(i)->widget())) { views.append(view); }
	}
	return views;
}

void TrackPinStrip::updateShown()
{
	// A pinned folder a visibility set hides (or one under a collapsed folder) is hidden here too; the
	// strip itself takes no room while nothing in it shows - always, outside the song editor.
	bool any = false;
	for (const TrackView* view : pinnedViews()) { any = any || !view->isHidden(); }
	setVisible(any);
}

} // namespace lmms::gui
