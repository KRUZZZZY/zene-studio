/*
 * MixerStripTags.cpp - a mixer strip's type badge, routing lines and mode line
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
#include "MixerStripTags.h"

#include <QCoreApplication>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>

#include "AutomatableButton.h"
#include "DesignTokens.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "MixerStrip.h"
#include "MixerViewOptions.h"

namespace lmms::gui::mixerstriptags
{

namespace
{

//! The badge's outline: the accent on the master (its type is also in its words), a muted line elsewhere.
QString badgeSheet(bool master)
{
	const tokens::Palette& pal = tokens::palette();
	return QStringLiteral("QLabel { border: 1px solid %1; border-radius: %2px; padding: 0px %3px; color: %4; }")
		.arg((master ? pal.accent : pal.textMuted).name()).arg(tokens::radius::sm).arg(tokens::space::xxs).arg(pal.text.name());
}

//! The solo mode on every strip's Solo button, as its tooltip and description (the mode bar names it too).
void describeSolo(AutomatableButton* solo)
{
	const QString text = mixerstrip::additiveSolo()
		? QCoreApplication::translate("MixerStripTags", "Solo this channel (additive: it adds to the solos already on)")
		: QCoreApplication::translate("MixerStripTags", "Solo this channel (exclusive: it unsolos the others)");
	if (solo->toolTip() == text) { return; }
	solo->setToolTip(text);
	solo->setAccessibleDescription(text);
}

//! The room the badge's text has inside its outline and padding.
int badgeTextWidth(int width)
{
	return width - 2 * tokens::space::xxs - 2;
}

} // namespace

QLabel* makeBadge(QWidget* parent, const QString& text)
{
	auto* badge = new QLabel(parent);
	badge->setObjectName(QStringLiteral("mixerStripType"));
	badge->setAlignment(Qt::AlignCenter);
	badge->setFont(tokens::font(tokens::TextRole::Caption));
	badge->setStyleSheet(badgeSheet(false));
	if (!text.isEmpty())
	{
		badge->setText(text);
		badge->setAccessibleName(QCoreApplication::translate("MixerStripTags", "Channel type: %1").arg(text));
	}
	return badge;
}

QLabel* makeLine(QWidget* parent, const QString& objectName)
{
	auto* line = new QLabel(parent);
	line->setObjectName(objectName);
	line->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
	line->setFont(tokens::font(tokens::TextRole::Caption));
	line->setStyleSheet(QStringLiteral("color: %1;").arg(tokens::palette().textMuted.name()));
	line->hide();
	return line;
}

void setLines(QLabel* label, const QString& text, int width)
{
	label->setVisible(!text.isEmpty());
	const QFontMetrics metrics(label->font());
	QStringList lines = text.split(QLatin1Char('\n'));
	for (QString& line : lines) { line = metrics.elidedText(line, Qt::ElideRight, width); }
	const QString shown = lines.join(QLatin1Char('\n'));
	if (label->text() == shown && label->toolTip() == text) { return; }  // each set is announced
	label->setText(shown);
	label->setToolTip(text);
	label->setAccessibleName(QString(text).replace(QLatin1Char('\n'), QStringLiteral(", ")));
}

void apply(MixerChannelView* view, const mixerstripinfo::StripInfo& info)
{
	using namespace mixerstripinfo;
	const bool compact = mixerviewoptions::compactDensity();
	const int width = view->nameEdit()->width();
	const bool master = info.type == ChannelType::Master;
	if (view->typeBadge()->property("master").toBool() != master)
	{
		view->typeBadge()->setProperty("master", master);
		view->typeBadge()->setStyleSheet(badgeSheet(master));
	}
	setLines(view->typeBadge(), badgeText(info), badgeTextWidth(width));
	setLines(view->routingLine(), mixerviewoptions::routingView() ? routingDetail(info, compact) : routingSummary(info, compact), width);
	setLines(view->stateLine(), stateLine(info, compact), width);
	describeSolo(view->soloButton());
	const QString spoken = spokenText(info);
	if (view->accessibleDescription() != spoken) { view->setAccessibleDescription(spoken); }
}

void applyAll(Mixer* mixer, const QVector<MixerChannelView*>& views, const mixerstripinfo::Context& context)
{
	for (int i = 0; i < views.size() && i < mixer->numChannels(); ++i)
	{
		// A strip the filter or a folded folder hid is described when it shows again (the next pass).
		MixerChannelView* view = views.at(i);
		if (view == nullptr || (view->testAttribute(Qt::WA_WState_ExplicitShowHide) && view->isHidden())) { continue; }
		apply(view, mixerstripinfo::describe(*mixer, i, context));
	}
}

} // namespace lmms::gui::mixerstriptags
