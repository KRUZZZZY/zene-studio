/*
 * FocusDeskPlacement.cpp - card placement and density for the Focus Desk shell.
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

#include "FocusDesk.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace lmms::gui
{

namespace
{

QString headerObjectName(const QString& moduleId)
{
	return QStringLiteral("focusDeskHeader_") + moduleId;
}

QString cardObjectName(const QString& moduleId)
{
	return QStringLiteral("focusDeskCard_") + moduleId;
}
} // namespace

QWidget* FocusDesk::hostFor(Destination where) const
{
	switch (where)
	{
		case Destination::LeftRail:   return m_leftHost;
		case Destination::RightRail:  return m_rightHost;
		case Destination::BottomDock: return m_bottomHost;
		case Destination::Stage:      return m_stageHost;
		case Destination::Park:       return m_park;
	}
	return m_park;
}

QVBoxLayout* FocusDesk::layoutFor(Destination where) const
{
	switch (where)
	{
		case Destination::LeftRail:   return m_leftLayout;
		case Destination::RightRail:  return m_rightLayout;
		case Destination::BottomDock: return m_bottomLayout;
		case Destination::Stage:      return m_stageLayout;
		case Destination::Park:       return m_parkLayout;
	}
	return m_parkLayout;
}

FocusDesk::Destination FocusDesk::homeDestination(FocusRegion home)
{
	switch (home)
	{
		case FocusRegion::Left:   return Destination::LeftRail;
		case FocusRegion::Right:  return Destination::RightRail;
		case FocusRegion::Bottom: return Destination::BottomDock;
		// The centre region *is* the stage, and the stage holds exactly one
		// module - the focus module. A displaced centre row therefore has no
		// free home to return to and parks, where its chip reports it.
		case FocusRegion::Centre: return Destination::Park;
	}
	return Destination::Park;
}

QFrame* FocusDesk::buildCard(const FocusModule& row, QWidget* content)
{
	auto* card = new QFrame(m_park);
	card->setObjectName(cardObjectName(row.id));
	card->setFrameShape(QFrame::StyledPanel);
	auto* cardLayout = new QVBoxLayout(card);
	cardLayout->setContentsMargins(4, 4, 4, 4);
	cardLayout->setSpacing(2);

	auto* header = new QWidget(card);
	header->setObjectName(headerObjectName(row.id));
	auto* headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(0, 0, 0, 0);
	headerLayout->setSpacing(4);
	headerLayout->addWidget(new QLabel(row.glyph + QStringLiteral("  ") + row.title, header));
	headerLayout->addStretch(1);

	auto* focusButton = new QToolButton(header);
	focusButton->setObjectName(QStringLiteral("focusDeskPromote_") + row.id);
	focusButton->setText(tr("Show on stage"));
	focusButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	focusButton->setAutoRaise(true);
	focusButton->setToolTip(tr("Promote '%1' to the stage - the same action the "
		"FOCUS strip dispatches (%2)").arg(row.title, focusActionCommandId(row.id)));
	connect(focusButton, &QToolButton::clicked, this,
		[this, id = row.id]() { focusModule(id); });
	headerLayout->addWidget(focusButton);
	cardLayout->addWidget(header);

	auto* body = new QWidget(card);
	body->setObjectName(QStringLiteral("focusDeskBody_") + row.id);
	auto* bodyLayout = new QVBoxLayout(body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	bodyLayout->addWidget(content, 1);
	cardLayout->addWidget(body, 1);

	m_bodies.insert(row.id, body);
	return card;
}

void FocusDesk::placeCard(const QString& moduleId, Destination where)
{
	QFrame* card = m_cards.value(moduleId, nullptr);
	if (card == nullptr) { return; }

	// The stage is the one destination that shows the module itself rather than
	// the card around it (see FocusDesk.h), so the content is moved first and
	// the card follows it into the park. The card must not be laid out on the
	// stage: the stage holds exactly one module, and a card there would split
	// the stage with an empty frame - its header is hidden and its body was
	// emptied by the lift. chipState() asks the content, so the module still
	// reports itself as being on the stage while its card is parked and hidden.
	if (where == Destination::Stage) { liftContentToStage(moduleId); }
	else { returnContentToCard(moduleId); }

	detachFromLayouts(card);

	const Destination cardHome = (where == Destination::Stage) ? Destination::Park : where;
	QVBoxLayout* target = layoutFor(cardHome);
	QWidget* host = hostFor(cardHome);
	if (target == nullptr || host == nullptr) { return; }

	card->setParent(host);
	// Stretch 1: the card takes the region and shares it with its neighbours,
	// rather than sitting at its size hint above a blank region.
	target->addWidget(card, 1);

	// A card's own header is the rail's label. On the stage the stage carries
	// the title, so the header would say it twice.
	if (QWidget* header = card->findChild<QWidget*>(headerObjectName(moduleId)))
	{
		header->setVisible(where != Destination::Stage);
	}
	card->setVisible(cardHome != Destination::Park);
}

void FocusDesk::liftContentToStage(const QString& moduleId)
{
	QWidget* content = m_modules.value(moduleId, nullptr);
	if (content == nullptr || m_stageHost == nullptr || m_stageLayout == nullptr) { return; }
	if (content->parentWidget() == m_stageHost) { return; }

	// Out of the card body first: a layout left holding a widget that has since
	// been re-parented is a stale item, and reparenting is what this does.
	if (QWidget* body = m_bodies.value(moduleId, nullptr))
	{
		if (auto* bodyLayout = qobject_cast<QVBoxLayout*>(body->layout()))
		{
			bodyLayout->removeWidget(content);
		}
	}
	content->setParent(m_stageHost);
	// The stage layout holds items only, so the module is the whole stage.
	m_stageLayout->addWidget(content, 1);
	content->show();
}

void FocusDesk::returnContentToCard(const QString& moduleId)
{
	QWidget* content = m_modules.value(moduleId, nullptr);
	if (content == nullptr) { return; }
	QWidget* body = m_bodies.value(moduleId, nullptr);
	if (body == nullptr) { return; }
	if (content->parentWidget() == body) { return; }

	if (m_stageLayout != nullptr) { m_stageLayout->removeWidget(content); }
	content->setParent(body);
	if (auto* bodyLayout = qobject_cast<QVBoxLayout*>(body->layout()))
	{
		bodyLayout->addWidget(content, 1);
	}
	content->show();
}

void FocusDesk::placeOnStage(const QString& moduleId)
{
	placeCard(moduleId, Destination::Stage);
}

void FocusDesk::detachFromLayouts(QFrame* card)
{
	for (QVBoxLayout* layout : {m_leftLayout, m_stageLayout, m_rightLayout, m_bottomLayout, m_parkLayout})
	{
		if (layout != nullptr) { layout->removeWidget(card); }
	}
}

void FocusDesk::refreshDensityLabel()
{
	if (m_densityButton == nullptr) { return; }
	m_densityButton->setText(tr("Density: %1").arg(FocusDeskModules::densityName(m_density)));
}

void FocusDesk::setDensity(FocusDensity density)
{
	m_density = density;
	applyDensity();
}

void FocusDesk::applyDensity()
{
	// Minimal collapses the rails' card bodies; Standard and Complete show them.
	// The bodies are collapsed, never removed, and the chip for each module says
	// where it went - which is the `reveal-hint` §4.3(4) asks for. State
	// controls are exempt (§4.3(5)) and none of them live in a rail body, so
	// nothing here can hide state.
	const bool bodiesVisible = railBodyVisible();
	for (auto it = m_bodies.begin(); it != m_bodies.end(); ++it)
	{
		it.value()->setVisible(bodiesVisible);
	}
	refreshDensityLabel();
}

bool FocusDesk::railBodyVisible() const
{
	return m_density != FocusDensity::Minimal;
}

} // namespace lmms::gui
