/*
 * ShellOverflow.h - labelled, compact and overflowing rows of the 0.5.0 shell's chrome
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

#ifndef LMMS_GUI_SHELL_OVERFLOW_H
#define LMMS_GUI_SHELL_OVERFLOW_H

#include <functional>

#include <QList>
#include <QPointer>
#include <QWidget>

#include "ShellChrome.h"
#include "lmms_export.h"

class QHBoxLayout;
class QMenu;
class QToolButton;

namespace lmms::gui
{

/*! Design theme 8 (a clear home for each tool): a crowded row of the shell's chrome never cuts a control off. It
 *  labels its buttons (the icon, then the name) while they fit; narrower, it shows their icons only; narrower still,
 *  the buttons that do not fit move into the row's "More" button, last first, so every control stays one click
 *  away. A button the user hid (the "zeneUserHidden" property, window.header_set) stays hidden whatever. */
namespace shelloverflow
{

//! How a row is fitting into its width.
enum class Fit
{
	Labelled,   //!< every button, each with its name beside its icon
	Compact,    //!< every button, icons only
	Overflowing //!< icons only, and some buttons in the More menu
};

//! The property a button carries while the user has hidden it; the fitting never shows such a button.
LMMS_EXPORT const char* userHiddenProperty();

//! Fit @a buttons (in the order they hide: the last first) into @a available pixels, @a measure() being the row's
//! width as it stands: labelled when @a labels allows and that fits, else compact, else hiding buttons until the
//! rest fit beside @a more, which then lists them. Returns the fit.
LMMS_EXPORT Fit fit(const QList<QToolButton*>& buttons, class ShellOverflowButton* more, int available,
	const std::function<int()>& measure, bool labels);

//! The fit as window.get_state spells it: labelled, compact or overflowing.
LMMS_EXPORT QString fitName(Fit fit);

} // namespace shelloverflow

//! The "More" button that ends a crowded row. Whatever did not fit is in its menu: a button with an action as that
//! action (its check and its command come along), a button whose click only opens a menu as that menu, any other
//! button as an entry that clicks it and declares the command the button declares. Hidden while nothing overflows.
class LMMS_EXPORT ShellOverflowButton : public ShellButton
{
	Q_OBJECT

public:
	ShellOverflowButton(const QString& name, QWidget* parent);

	void setOverflowed(const QList<QToolButton*>& buttons);
	QList<QToolButton*> overflowed() const;

private:
	void fillMenu(QMenu* menu) const;

	QList<QPointer<QToolButton>> m_overflowed;
};

//! A row of the shell's buttons that fits itself into its width (shelloverflow::fit) whenever it is resized. The
//! footer's panel switches are one. Its minimum is the More button alone, so it never forces the window wider.
class LMMS_EXPORT ShellOverflowRow : public QWidget
{
	Q_OBJECT

public:
	ShellOverflowRow(const QString& moreName, QWidget* parent);

	//! Append @a button. A button with a lower @a keep moves into the More menu before one with a higher; among
	//! equals, the last first.
	void addButton(QToolButton* button, int keep = 0);
	void addSpacing(int pixels);
	//! Whether the buttons may show their names when there is room (on by default).
	void setLabels(bool on);
	shelloverflow::Fit fitting() const { return m_fit; }
	ShellOverflowButton* moreButton() const { return m_more; }
	//! Fit the buttons to the width the row has now.
	void refit();

	QSize minimumSizeHint() const override;

protected:
	void resizeEvent(QResizeEvent* event) override;

private:
	//! The buttons in the order they move into the More menu.
	QList<QToolButton*> hideOrder() const;

	QHBoxLayout* m_row;
	QList<QPair<QToolButton*, int>> m_buttons;
	ShellOverflowButton* m_more;
	shelloverflow::Fit m_fit = shelloverflow::Fit::Labelled;
	bool m_labels = true;
	bool m_fitting = false;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_OVERFLOW_H
