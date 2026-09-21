/*
 * FocusDesk.h - the Focus Desk shell: two rails, one stage, one FOCUS strip.
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

#ifndef LMMS_GUI_FOCUS_DESK_H
#define LMMS_GUI_FOCUS_DESK_H

#include <QHash>
#include <QList>
#include <QPointer>
#include <QVariantMap>
#include <QWidget>

#include "FocusDeskModules.h"

class QFrame;
class QLabel;
class QSplitter;
class QToolButton;
class QVBoxLayout;

namespace lmms::gui
{

//! The Focus Desk shell (UI plan §9.4, Direction B).
//!
//! "One thing at a time, at full size." Two rails hold the small modules you
//! cross-reference while you work; the stage holds exactly one module, chosen
//! from the labelled FOCUS strip. Switching a module onto the stage is one
//! action - `focusModule()` - whichever module you are looking at, which is
//! §9.4's claim that promoting beats opening a window.
//!
//! The four regions are §5.1's - left rail, stage (centre), right rail, and a
//! bottom dock - so every row of the register has a home: a module the table
//! keeps out of the centre region still lands somewhere the user can see it.
//! Only the centre region is single-occupancy, because the stage is what "one
//! thing at a time" means; the rails and the dock stack their modules, which is
//! why §4.2's "regions are splitters" and its "more than one module per region"
//! both hold.
//!
//! This class owns no module: it is handed a widget per register row by
//! `mountModule()` and gives it back through `releaseAll()`. That keeps the
//! shell testable without an engine, and keeps every decision about *which*
//! widget a module is in MainWindow, where the MDI windows already live.
//!
//! A row's chip is never removed. A row with no mounted widget renders greyed
//! with the register's stated reason (§4.1 X4), so a staged module is visible
//! and explained instead of silently missing.
class FocusDesk : public QWidget
{
	Q_OBJECT

public:
	explicit FocusDesk(QWidget* parent = nullptr);
	~FocusDesk() override;

	//! The register this desk was built from (UI plan §4.2).
	QList<FocusModule> registerRows() const;

	//! Chip ids in FOCUS-strip order - every row, mountable or not.
	QStringList stripIds() const;
	bool chipEnabled(const QString& moduleId) const;
	//! The reason a chip is greyed, empty when it is not.
	QString chipReason(const QString& moduleId) const;
	//! "stage", "left", "right", "bottom", "parked" or "unavailable" (§4.2's
	//! answer to "where is this module", the last being "it cannot be here yet",
	//! since a region is only that module's if its row allows it).
	QString chipState(const QString& moduleId) const;

	//! Bind a live widget to a register row. Returns false - and takes nothing -
	//! when the id is not in the register, because §4.2's rule is that a module
	//! which is not in the register cannot exist. The desk never takes
	//! ownership: the widget is re-parented, not deleted.
	bool mountModule(const QString& moduleId, QWidget* content);
	int mountedCount() const;
	QWidget* mountedWidget(const QString& moduleId) const
	{
		return m_modules.value(moduleId, nullptr);
	}

	//! Hand every mounted widget back. Each returned widget is left parented to
	//! this desk's hidden holding area, so the caller MUST re-parent it (the
	//! SubWindows in MainWindow do that with setWidget()) before the desk dies.
	QList<QWidget*> releaseAll();

	//! The `mod:` action: show this module as the focus module. The previous
	//! focus returns to its home region, which is §9.4's "the rails are the
	//! mitigation" made mechanical.
	bool focusModule(const QString& moduleId);
	QString focused() const { return m_focused; }
	QWidget* stagedWidget() const { return m_modules.value(m_focused, nullptr); }
	//! The container a region's modules are placed in, for inspection. The four
	//! §5.1 regions each have their own host; Centre's is the stage itself.
	QWidget* regionHost(FocusRegion region) const;

	void setDensity(FocusDensity density);
	FocusDensity density() const { return m_density; }
	//! Minimal collapses rail bodies. Nothing is removed: the card header and
	//! the chip both stay, which is the `reveal-hint` §4.3(4) requires.
	bool railBodyVisible() const;

	//! The desk's persistent state, as data - no config file is touched.
	QVariantMap saveLayout() const;
	bool restoreLayout(const QVariantMap& layout);

signals:
	void focusChanged(const QString& moduleId);
	void moduleRefused(const QString& moduleId, const QString& reason);

private:
	//! Where a card is put. Deliberately not `FocusRegion`: a region says where
	//! a module *may* be, and Centre means the stage only for the focus module -
	//! every other Centre module parks. Collapsing the two in one enum is how a
	//! promoted module's predecessor would end up on the stage with it.
	enum class Destination { LeftRail, RightRail, BottomDock, Stage, Park };

	void buildStrip();
	void buildBody();
	void addChip(const FocusModule& row);
	QFrame* buildCard(const FocusModule& row, QWidget* content);
	//! The region a card goes back to when it is not the focus module. Centre is
	//! the stage, which holds one module, so a displaced centre row has no home
	//! to return to and parks - reported, never silent.
	static Destination homeDestination(FocusRegion home);
	void placeCard(const QString& moduleId, Destination where);
	void placeOnStage(const QString& moduleId);
	//! The stage shows the module itself, not a card: the card exists to carry a
	//! rail label and a promote button, and on the stage the header is hidden and
	//! the frame would be decoration. So promoting a module moves its *content*
	//! onto the stage host and parks the now-empty card.
	void liftContentToStage(const QString& moduleId);
	void returnContentToCard(const QString& moduleId);
	void refreshChips();
	void refreshDensityLabel();
	void applyDensity();
	QVBoxLayout* layoutFor(Destination where) const;
	QWidget* hostFor(Destination where) const;
	void detachFromLayouts(QFrame* card);

	QList<FocusModule> m_rows;
	QHash<QString, QToolButton*> m_chips;
	QHash<QString, QFrame*> m_cards;
	QHash<QString, QWidget*> m_bodies;
	//! Borrowed, never owned: these are the product's editors (see mountModule).
	//!
	//! QPointer, not a raw pointer, because a borrowed widget can die before this
	//! desk does. `MainWindow::~MainWindow` deletes `automationEditor`,
	//! `pianoRoll` and `songEditor` outright - it has to, references to them are
	//! held by Song - and that happens *before* the pane holding this desk is
	//! destroyed, so the desk is asked to hand back three widgets that no longer
	//! exist. A raw pointer read back at that moment is freed memory (measured:
	//! SIGSEGV in releaseAll's `content->setParent(m_park)`); a QPointer becomes
	//! null and the `content == nullptr` guard already in releaseAll() skips it.
	//! The same convention as m_hosted below.
	QHash<QString, QPointer<QWidget>> m_modules;
	//! Every widget this desk was ever handed, so the destructor can tell the
	//! ones still inside it - mounted, or handed back and not yet re-parented -
	//! from the ones a caller has already put somewhere safe.
	QList<QPointer<QWidget>> m_hosted;

	QFrame* m_strip = nullptr;
	QToolButton* m_densityButton = nullptr;
	QSplitter* m_splitter = nullptr;
	QLabel* m_stageTitle = nullptr;

	QWidget* m_leftHost = nullptr;
	QWidget* m_stageHost = nullptr;
	QWidget* m_rightHost = nullptr;
	QWidget* m_bottomHost = nullptr;
	//! Where a mounted module waits while it is not on a rail or the stage.
	//! Hidden, but its chip says so, so nothing is hidden *and unreported*.
	QWidget* m_park = nullptr;
	QVBoxLayout* m_leftLayout = nullptr;
	QVBoxLayout* m_stageLayout = nullptr;
	QVBoxLayout* m_rightLayout = nullptr;
	QVBoxLayout* m_bottomLayout = nullptr;
	QVBoxLayout* m_parkLayout = nullptr;

	QString m_focused;
	//! A focus choice restored before its module is mounted, applied on mount.
	QString m_pendingFocus;
	FocusDensity m_density = FocusDensity::Standard;
};

} // namespace lmms::gui

#endif // LMMS_GUI_FOCUS_DESK_H
