/*
 * FocusDeskPlacement.cpp - card placement, body construction and density for
 * the Focus Desk shell. buildBody() and makeRail() live here beside the card
 * builder because they construct the same layout the placement code moves
 * cards through; keeping them in FocusDesk.cpp would push that file past the
 * fork's 500-line gate (Gate 7), which is a limit this change may not spend.
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

#include "Accessibility.h"
#include "FocusDesk.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSplitter>
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

//! A rail: a scrollable container, because §5.1 keeps regions as splitters and
//! lets more than one module share a region (stacked or tabbed) - a rail that
//! held exactly one widget could not. Named and given an explicit focus policy
//! here so both come with the widget rather than being remembered by callers
//! (SPEC-zene-ui-v0 §5 item 1).
QScrollArea* makeRail(QWidget* parent, QWidget** host, QVBoxLayout** layout,
	const QString& name, const QString& description)
{
	auto* scroll = new QScrollArea(parent);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setFocusPolicy(Qt::WheelFocus);
	lmms::a11y::announce(scroll, name, description);

	*host = new QWidget(scroll);
	*layout = new QVBoxLayout(*host);
	(*layout)->setContentsMargins(6, 6, 6, 6);
	(*layout)->setSpacing(6);
	// Deliberately no trailing stretch. A card mounted in a region *is* that
	// region's content, so it takes the region and shares it with the other
	// cards when several are stacked - which is the shell's reading of
	// `flex: 1 1 auto` for rail and stage panels in the mockup
	// (research/ui/mockups/B-desk.html :33). A trailing stretch would instead
	// split the region between the card and empty space, and a mounted editor
	// would sit at its size hint with the rest of the rail blank.
	scroll->setWidget(*host);
	return scroll;
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

void FocusDesk::applyStripTabOrder()
{
	// Register order across the strip's chips, then each strip control in
	// build order, then the horizontal splitter. The chips and controls all
	// exist by the time the constructor calls this (buildStrip, buildBody and
	// addChip ran first), so a null check here would only hide a call-order
	// bug - the density/splitter comment at the original site said the same.
	QWidget* previous = nullptr;
	for (const auto& row : m_rows)
	{
		QToolButton* chip = m_chips.value(row.id, nullptr);
		if (chip == nullptr) { continue; }
		if (previous != nullptr) { setTabOrder(previous, chip); }
		previous = chip;
	}
	if (previous != nullptr) { setTabOrder(previous, m_densityButton); previous = m_densityButton; }
	if (previous != nullptr) { setTabOrder(previous, m_workspaceButton); previous = m_workspaceButton; }
	if (previous != nullptr) { setTabOrder(previous, m_commandsButton); previous = m_commandsButton; }
	if (previous != nullptr) { setTabOrder(previous, m_splitter); previous = m_splitter; }
	if (previous != nullptr)
	{
		// The vertical splitter is a local of buildBody(), reachable by the
		// objectName buildBody() gives it rather than by a header change.
		if (auto* rows = findChild<QSplitter*>(QStringLiteral("focusDeskRows")))
		{
			setTabOrder(previous, rows);
		}
	}
}

FocusDesk::Destination FocusDesk::mountDestination(const FocusModule& row,
	const QString& moduleId)
{
	// The register's home is the default; a current workspace that does not
	// declare this row a member parks it instead - §6.1 membership is
	// declared data, read at mount time too, never inferred from what is
	// mounted. With no workspace applied the register's rule stands alone.
	if (!m_workspace.isEmpty() && !workspaceContains(moduleId)) { return Destination::Park; }
	return homeDestination(row.home);
}

void FocusDesk::buildBody()
{
	// §5.1's four regions: three across the top, the bottom dock beneath them.
	// Every row of the register is homed in one of the four, so none of them is
	// a place a module can be told to live and then not be shown.
	m_splitter = new QSplitter(Qt::Horizontal, this);
	m_splitter->setObjectName(QStringLiteral("focusDeskSplitter"));
	m_splitter->setChildrenCollapsible(false);
	// StrongFocus so the handles take the arrow keys after a click or a tab;
	// "unreachable by keyboard" is the defect SPEC-zene-ui-v0 §5 item 1 removes.
	m_splitter->setFocusPolicy(Qt::StrongFocus);
	a11y::announce(m_splitter, tr("Focus Desk regions"),
		tr("Splits the left rail, the stage and the right rail; focus a handle "
		"and use the arrow keys to resize."));

	// Creation order is the splitter's child order.
	makeRail(m_splitter, &m_leftHost, &m_leftLayout, tr("Left rail"),
		tr("Stacked module cards; each module's chip states where its card is."));

	auto* stage = new QFrame(m_splitter);
	stage->setObjectName(QStringLiteral("focusDeskStage"));
	stage->setFrameShape(QFrame::StyledPanel);
	a11y::announce(stage, tr("Stage"),
		tr("The stage shows the module whose chip is selected."));
	auto* stageLayout = new QVBoxLayout(stage);
	stageLayout->setContentsMargins(6, 6, 6, 6);
	stageLayout->setSpacing(4);
	m_stageTitle = new QLabel(tr("Stage"), stage);
	m_stageTitle->setObjectName(QStringLiteral("focusDeskStageTitle"));
	stageLayout->addWidget(m_stageTitle);
	m_stageHost = new QWidget(stage);
	m_stageLayout = new QVBoxLayout(m_stageHost);
	m_stageLayout->setContentsMargins(0, 0, 0, 0);
	m_stageLayout->setSpacing(0);
	// No trailing stretch: the stage holds exactly one module and one module
	// fills the stage (§4.3, and the mockup's `flex: 1 1 auto`). With a stretch
	// the module would share the stage with empty space at half size.
	stageLayout->addWidget(m_stageHost, 1);

	makeRail(m_splitter, &m_rightHost, &m_rightLayout, tr("Right rail"),
		tr("Stacked module cards; each module's chip states where its card is."));

	// The bottom dock is the fourth region (§5.1). It is a rail like the other
	// two - a stack, not a single slot - because the mixer, the detail editor
	// and automation are all homed here.
	auto* rows = new QSplitter(Qt::Vertical, this);
	rows->setObjectName(QStringLiteral("focusDeskRows"));
	rows->setChildrenCollapsible(false);
	rows->setFocusPolicy(Qt::StrongFocus);
	a11y::announce(rows, tr("Focus Desk dock split"),
		tr("Splits the top regions from the bottom dock; focus the handle and "
		"use the arrow keys to resize."));
	rows->addWidget(m_splitter);

	auto* dock = new QFrame(rows);
	dock->setObjectName(QStringLiteral("focusDeskDock"));
	dock->setFrameShape(QFrame::StyledPanel);
	a11y::announce(dock, tr("Bottom dock"),
		tr("Stacked module cards for the mixer, the detail editor and automation."));
	auto* dockLayout = new QVBoxLayout(dock);
	dockLayout->setContentsMargins(6, 6, 6, 6);
	dockLayout->setSpacing(4);
	auto* dockTitle = new QLabel(tr("Bottom"), dock);
	dockTitle->setObjectName(QStringLiteral("focusDeskDockTitle"));
	dockLayout->addWidget(dockTitle);
	m_bottomHost = new QWidget(dock);
	m_bottomLayout = new QVBoxLayout(m_bottomHost);
	m_bottomLayout->setContentsMargins(0, 0, 0, 0);
	m_bottomLayout->setSpacing(6);
	// Like the rails, the dock has no trailing stretch: the modules homed here
	// (the mixer, the detail editor, automation) share its height.
	dockLayout->addWidget(m_bottomHost, 1);
	rows->addWidget(dock);

	// The holding area for mounted modules that are neither on a rail nor on
	// the stage. It is never shown; chipState() reports "parked" instead, so the
	// module is not on screen but its position is still stated (X4).
	m_park = new QWidget(this);
	m_park->setObjectName(QStringLiteral("focusDeskPark"));
	m_park->hide();
	m_parkLayout = new QVBoxLayout(m_park);

	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);
	outer->addWidget(m_strip);
	outer->addWidget(rows, 1);

	m_splitter->setStretchFactor(0, 0);
	m_splitter->setStretchFactor(1, 1);
	m_splitter->setStretchFactor(2, 0);
	m_splitter->setSizes({240, 900, 260});
	rows->setStretchFactor(0, 1);
	rows->setStretchFactor(1, 0);
	rows->setSizes({720, 200});
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
	const QString promoteTip = tr("Promote '%1' to the stage - the same action the "
		"FOCUS strip dispatches (%2)").arg(row.title, focusActionCommandId(row.id));
	focusButton->setToolTip(promoteTip);
	// The visible label is identical on every card, so the announced NAME is
	// the module this button would promote; the dispatch command stays in the
	// description. Explicit TabFocus: the card is built late, per mount (§5.1).
	focusButton->setFocusPolicy(Qt::TabFocus);
	lmms::a11y::announce(focusButton,
		tr("Show %1 on stage").arg(row.title), promoteTip);
	// The promote button is the `mod:` action's second mount (§9.4): the same
	// dispatchAction() path as the chip and View ▸ Modules, not a shortcut to
	// focusModule() that could drift from the command.
	connect(focusButton, &QToolButton::clicked, this,
		[this, id = row.id]() { dispatchAction(focusActionCommandId(id)); });
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
	const QString text = tr("Density: %1").arg(FocusDeskModules::densityName(m_density));
	m_densityButton->setText(text);
	// Re-announced with the text on every change: the current preset IS the
	// state, and a construction-time name would freeze it (spec §5 item 1).
	lmms::a11y::announce(m_densityButton, text);
}

void FocusDesk::setDensity(FocusDensity density)
{
	if (density == m_density) { return; } // one writer, no redundant collapse churn
	m_density = density;
	applyDensity();
	// The pane persists this; an external `settings.set ui/focusdesk.density`
	// arrives through the pane's observer and lands in this same method, so
	// strip button, generated-menu record and agent are one implementation.
	emit densityChanged(FocusDeskModules::densityName(m_density));
}

void FocusDesk::applyDensity()
{
	// Minimal collapses the rails' card bodies; Standard and Complete show them.
	// The bodies are collapsed, never removed, and the chip for each module says
	// where it went - which is the `reveal-hint` §4.3(4) asks for; the strip's
	// m_revealHint below makes that hint a visible sentence too (§8.1(4)).
	// State controls are exempt (§4.3(5)) and none of them live in a rail body,
	// so nothing here can hide state. The per-module contract is row.minDensity:
	// the global preset may hide a body, and a row's own floor may exempt it -
	// a floor can only keep a body VISIBLE, never hide one the preset shows
	// ("A preset hides information, never capability").
	const bool bodiesVisible = railBodyVisible();
	for (auto it = m_bodies.begin(); it != m_bodies.end(); ++it)
	{
		const FocusModule* row = FocusDeskModules::find(m_rows, it.key());
		const FocusDensity floor = (row != nullptr) ? row->minDensity : FocusDensity::Standard;
		it.value()->setVisible(bodiesVisible || m_density >= floor);
	}
	// Shown exactly when something is hidden, naming the one-click way back.
	if (m_revealHint != nullptr) { m_revealHint->setVisible(!bodiesVisible); }
	refreshDensityLabel();
}

bool FocusDesk::railBodyVisible() const
{
	return m_density != FocusDensity::Minimal;
}

} // namespace lmms::gui
