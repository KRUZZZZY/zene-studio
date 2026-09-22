/*
 * FocusDesk.cpp - the Focus Desk shell: two rails, one stage, one FOCUS strip.
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
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

namespace lmms::gui
{

FocusDesk::FocusDesk(QWidget* parent) :
	QWidget(parent),
	m_rows(FocusDeskModules::v1Register())
{
	setObjectName(QStringLiteral("focusDesk"));
	a11y::announce(this, tr("Focus Desk"),
		tr("The desk shell: a FOCUS strip of module chips, two rails, a stage "
		"and a bottom dock."));
	buildStrip();
	buildBody();
	for (const auto& row : m_rows) { addChip(row); }

	// One explicit setTabOrder() pass over the Focus Desk's own flow (spec §5
	// item 1): register order across the strip's chips, then the density
	// control, then the two splitter handles so keyboard resizing is reachable
	// by tab and not only by click. The pass lives here because the chips only
	// exist once the loop above has run; a card's promote button is created
	// per mount and joins the chain through its own explicit TabFocus policy.
	QWidget* previous = nullptr;
	for (const auto& row : m_rows)
	{
		QToolButton* chip = m_chips.value(row.id, nullptr);
		if (chip == nullptr) { continue; }
		if (previous != nullptr) { setTabOrder(previous, chip); }
		previous = chip;
	}
	// The density control and both splitters are guaranteed non-null here:
	// buildStrip() and buildBody() ran above the addChip loop.
	if (previous != nullptr)
	{
		setTabOrder(previous, m_densityButton);
		previous = m_densityButton;
	}
	if (previous != nullptr)
	{
		setTabOrder(previous, m_splitter);
		previous = m_splitter;
	}
	if (previous != nullptr)
	{
		// The vertical splitter is a local of buildBody(), reachable by the
		// objectName buildBody() gives it rather than by a header change.
		if (auto* rows = findChild<QSplitter*>(QStringLiteral("focusDeskRows")))
		{
			setTabOrder(previous, rows);
		}
	}

	refreshChips();
	applyDensity();
}

FocusDesk::~FocusDesk()
{
	// A mounted widget is not owned by the desk. If the desk is destroyed while
	// modules are still inside it - mounted, or handed back by releaseAll() and
	// not yet re-parented by the caller - detaching them is the only way to keep
	// Qt from deleting a live editor with the shell. A widget a caller has
	// already put back somewhere safe is left alone, which is what isAncestorOf
	// is testing for. They are left parentless and hidden rather than deleted:
	// a leak is recoverable in a way a use-after-free is not, and the contract
	// in FocusDesk.h says releaseAll() must be called.
	for (auto& content : m_hosted)
	{
		if (content != nullptr && isAncestorOf(content))
		{
			content->setParent(nullptr);
			content->hide();
		}
	}
	m_modules.clear();
}

QList<FocusModule> FocusDesk::registerRows() const
{
	return m_rows;
}

QStringList FocusDesk::stripIds() const
{
	QStringList ids;
	ids.reserve(m_rows.size());
	for (const auto& row : m_rows) { ids.append(row.id); }
	return ids;
}

bool FocusDesk::chipEnabled(const QString& moduleId) const
{
	const auto* chip = m_chips.value(moduleId, nullptr);
	return chip != nullptr && chip->isEnabled();
}

QString FocusDesk::chipReason(const QString& moduleId) const
{
	const auto* chip = m_chips.value(moduleId, nullptr);
	return (chip == nullptr || chip->isEnabled()) ? QString() : chip->toolTip();
}

QString FocusDesk::chipState(const QString& moduleId) const
{
	if (!m_modules.contains(moduleId)) { return QStringLiteral("unavailable"); }
	// The *content* decides this, not the card: on the stage the module is shown
	// without its card, so the card has been parked and would answer "parked"
	// about a module the user is looking at.
	if (QWidget* content = m_modules.value(moduleId, nullptr))
	{
		if (content->parentWidget() == m_stageHost) { return QStringLiteral("stage"); }
	}
	const auto* card = m_cards.value(moduleId, nullptr);
	if (card == nullptr) { return QStringLiteral("parked"); }
	if (card->parentWidget() == m_leftHost) { return QStringLiteral("left"); }
	if (card->parentWidget() == m_rightHost) { return QStringLiteral("right"); }
	if (card->parentWidget() == m_bottomHost) { return QStringLiteral("bottom"); }
	return QStringLiteral("parked");
}

QWidget* FocusDesk::regionHost(FocusRegion region) const
{
	switch (region)
	{
		case FocusRegion::Left:   return m_leftHost;
		case FocusRegion::Right:  return m_rightHost;
		case FocusRegion::Bottom: return m_bottomHost;
		case FocusRegion::Centre: return m_stageHost;
	}
	return m_stageHost;
}

void FocusDesk::buildStrip()
{
	m_strip = new QFrame(this);
	m_strip->setObjectName(QStringLiteral("focusDeskStrip"));
	m_strip->setFrameShape(QFrame::StyledPanel);

	auto* layout = new QHBoxLayout(m_strip);
	layout->setContentsMargins(6, 3, 6, 3);
	layout->setSpacing(4);

	auto* label = new QLabel(tr("FOCUS"), m_strip);
	label->setObjectName(QStringLiteral("focusDeskStripLabel"));
	const QString stripTip = tr("One chip per module in the register. A greyed chip "
		"states why its module cannot be shown yet; a chip is never removed.");
	label->setToolTip(stripTip);
	// The label's name falls back to its "FOCUS" text, so it is described, not
	// renamed; the strip around it is named for what it groups.
	a11y::describe(label, stripTip);
	a11y::announce(m_strip, tr("FOCUS strip"), stripTip);
	layout->addWidget(label);

	// The chips are inserted before this stretch by addChip().
	layout->addStretch(1);

	m_densityButton = new QToolButton(m_strip);
	m_densityButton->setObjectName(QStringLiteral("focusDeskDensity"));
	m_densityButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_densityButton->setAutoRaise(true);
	m_densityButton->setFocusPolicy(Qt::TabFocus);
	const QString densityTip = tr("Density preset. A preset hides information, "
		"never capability, and never state.");
	m_densityButton->setToolTip(densityTip);
	a11y::describe(m_densityButton, densityTip);
	connect(m_densityButton, &QToolButton::clicked, this,
		[this]() { setDensity(FocusDeskModules::nextDensity(m_density)); });
	layout->addWidget(m_densityButton);
}

void FocusDesk::addChip(const FocusModule& row)
{
	auto* chip = new QToolButton(m_strip);
	chip->setObjectName(focusActionCommandId(row.id));
	// The visible text leads with a glyph a screen reader would spell out, so
	// the chip is ANNOUNCED with the clean module title; the description
	// follows the mounted/unmounted state in refreshChips().
	chip->setText(row.glyph + QStringLiteral("  ") + row.title);
	chip->setCheckable(true);
	chip->setAutoExclusive(true);
	chip->setToolButtonStyle(Qt::ToolButtonTextOnly);
	chip->setAutoRaise(true);
	chip->setFocusPolicy(Qt::TabFocus);
	a11y::announce(chip, row.title);
	connect(chip, &QToolButton::clicked, this, [this, id = row.id]() { focusModule(id); });

	auto* layout = qobject_cast<QHBoxLayout*>(m_strip->layout());
	if (layout != nullptr && layout->count() > 0)
	{
		layout->insertWidget(layout->count() - 1, chip);
	}
	else if (layout != nullptr)
	{
		layout->addWidget(chip);
	}
	m_chips.insert(row.id, chip);
}

bool FocusDesk::mountModule(const QString& moduleId, QWidget* content)
{
	if (content == nullptr) { return false; }
	const FocusModule* row = FocusDeskModules::find(m_rows, moduleId);
	// §4.2: a module that is not in the register cannot exist. Refusing here is
	// what makes the register the answer to "what may exist" rather than a list
	// the shell happens to agree with.
	if (row == nullptr) { return false; }
	if (m_modules.contains(moduleId)) { return false; }

	QFrame* card = buildCard(*row, content);
	m_cards.insert(moduleId, card);
	m_modules.insert(moduleId, content);
	m_hosted.append(content);
	placeCard(moduleId, homeDestination(row->home));

	if (m_focused.isEmpty() && m_pendingFocus.isEmpty() && row->focusable
		&& row->home == FocusRegion::Centre)
	{
		// The stage's default module is the arrangement (§4.2 row 2), which is
		// the only row homed in the centre region.
		focusModule(moduleId);
	}
	else if (moduleId == m_pendingFocus)
	{
		m_pendingFocus.clear();
		focusModule(moduleId);
	}
	refreshChips();
	return true;
}

int FocusDesk::mountedCount() const
{
	return m_modules.size();
}

QList<QWidget*> FocusDesk::releaseAll()
{
	QList<QWidget*> released;
	released.reserve(m_modules.size());

	for (const auto& row : m_rows)
	{
		QWidget* content = m_modules.take(row.id);
		if (content == nullptr) { continue; }
		// Detach the module from the card BEFORE the card dies: the card is the
		// module's parent, so deleting it first would delete a live editor. The
		// stage is taken out of the layout explicitly rather than left to the
		// re-parent, so no stale item survives if that path ever changes.
		if (m_stageLayout != nullptr) { m_stageLayout->removeWidget(content); }
		content->setParent(m_park);
		content->hide();
		released.append(content);
	}
	for (auto it = m_cards.begin(); it != m_cards.end(); ++it) { it.value()->deleteLater(); }
	m_cards.clear();
	m_bodies.clear();
	m_focused.clear();
	m_pendingFocus.clear();
	refreshChips();
	return released;
}

bool FocusDesk::focusModule(const QString& moduleId)
{
	const FocusModule* row = FocusDeskModules::find(m_rows, moduleId);
	if (row == nullptr)
	{
		emit moduleRefused(moduleId, tr("'%1' is not in the module register").arg(moduleId));
		return false;
	}
	if (!m_modules.contains(moduleId))
	{
		const QString reason = row->unmounted.isEmpty()
			? tr("no widget is mounted for '%1'").arg(moduleId)
			: row->unmounted;
		emit moduleRefused(moduleId, reason);
		return false;
	}
	if (!row->focusable)
	{
		emit moduleRefused(moduleId, tr("'%1' is not focusable").arg(moduleId));
		return false;
	}
	if (m_focused == moduleId) { return true; }

	// The module that had the stage returns to its home region - the rails are
	// §9.4's mitigation for the switch this costs, so this is not optional.
	const QString previous = m_focused;
	if (!previous.isEmpty())
	{
		if (const FocusModule* previousRow = FocusDeskModules::find(m_rows, previous))
		{
			placeCard(previous, homeDestination(previousRow->home));
		}
	}

	m_focused = moduleId;
	placeOnStage(moduleId);
	m_stageTitle->setText(row->glyph + QStringLiteral("  ") + row->title);
	refreshChips();
	emit focusChanged(moduleId);
	return true;
}

void FocusDesk::refreshChips()
{
	for (const auto& row : m_rows)
	{
		auto* chip = m_chips.value(row.id, nullptr);
		if (chip == nullptr) { continue; }

		const bool mounted = m_modules.contains(row.id);
		chip->setEnabled(mounted);
		if (mounted)
		{
			const QString tip = tr("%1 - %2. Press to show it on the stage; the "
				"action is %3.").arg(row.title,
				FocusDeskModules::regionName(row.home), focusActionCommandId(row.id));
			chip->setToolTip(tip);
			a11y::describe(chip, tip);
		}
		else
		{
			const QString reason = row.unmounted.isEmpty()
				? tr("no widget is mounted for '%1' in this build").arg(row.id)
				: row.unmounted;
			const QString tip = tr("%1 - not shown: %2").arg(row.title, reason);
			chip->setToolTip(tip);
			a11y::describe(chip, tip);
		}
		chip->setChecked(row.id == m_focused);
	}
	refreshDensityLabel();
}

QVariantMap FocusDesk::saveLayout() const
{
	QVariantMap out;
	out.insert(QStringLiteral("focus"), m_focused);
	out.insert(QStringLiteral("density"), FocusDeskModules::densityName(m_density));
	for (const auto& row : m_rows)
	{
		if (m_modules.contains(row.id))
		{
			out.insert(QStringLiteral("placed.") + row.id, chipState(row.id));
		}
	}
	return out;
}

bool FocusDesk::restoreLayout(const QVariantMap& layout)
{
	bool ok = true;
	FocusDensity density = m_density;
	if (layout.contains(QStringLiteral("density")))
	{
		if (FocusDeskModules::densityFromName(layout.value(QStringLiteral("density")).toString(),
			&density))
		{
			setDensity(density);
		}
		else
		{
			ok = false;
		}
	}

	const QString focus = layout.value(QStringLiteral("focus")).toString();
	if (!focus.isEmpty())
	{
		if (FocusDeskModules::find(m_rows, focus) == nullptr)
		{
			// A layout names modules, and the register is the only authority on
			// which ones exist (§4.2). A name that is not a row is therefore an
			// invalid layout, not a focus waiting to be claimed - holding it
			// would leave the desk quietly showing the wrong module for ever.
			ok = false;
		}
		else if (m_modules.contains(focus))
		{
			ok = focusModule(focus) && ok;
		}
		else
		{
			// A layout can be restored before its modules are mounted (the shell
			// is built, then the windows are claimed), so a valid but unmounted
			// choice is held rather than dropped.
			m_pendingFocus = focus;
		}
	}
	return ok;
}

} // namespace lmms::gui
