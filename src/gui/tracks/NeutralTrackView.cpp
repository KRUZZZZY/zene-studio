/*
 * NeutralTrackView.cpp - the header of a neutral arrangement row (Q050-283.1)
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
 */


#include "NeutralTrackView.h"

#include <QPainter>

#include "NeutralTrack.h"
#include "TrackLabelButton.h"

namespace lmms::gui
{

NeutralTrackView::NeutralTrackView(NeutralTrack* track, TrackContainerView* tcv)
	: TrackView(track, tcv)
{
	setFixedHeight(trackLaneHeight());
	auto* label = new TrackLabelButton(this, getTrackSettingsWidget());
	label->setIcon(QIcon(icon()));
	label->move(3, 1);
	label->show();
	setModel(track);
}

QPixmap NeutralTrackView::icon()
{
	QPixmap pixmap(20, 20);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	QPen pen(QColor(160, 164, 170));
	pen.setWidth(2);
	painter.setPen(pen);
	painter.drawRoundedRect(QRectF(3, 5, 14, 10), 2, 2);
	return pixmap;
}

} // namespace lmms::gui
