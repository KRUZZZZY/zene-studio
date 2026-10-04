/*
 * BrowserPanel.h - the shell's Browser: devices, presets, samples and projects in one panel
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

#ifndef LMMS_GUI_BROWSER_PANEL_H
#define LMMS_GUI_BROWSER_PANEL_H

#include <QList>
#include <QPointer>
#include <QWidget>

#include "lmms_export.h"

class QAbstractItemView;
class QComboBox;
class QKeyEvent;
class QLineEdit;
class QStackedWidget;

namespace lmms::gui
{

class SideBarWidget;

//! One browser with a source selector, in place of the MDI era's sidebar of seven
//! collapsible tabs. The sources are the existing PluginBrowser and FileBrowsers - their
//! search, preview, drag-and-drop, tag menu (browser.tag_*) and peaks are unchanged; only
//! the chrome around them is new.
//!
//! Theme 3 (0.5.0 UX, the owner's decisions): search is the first control - the current source's
//! own search field (a QLineEdit named "browserSearch"), shown above the selector, filtering that
//! source's list live and nothing else; the arrow keys in it step through the list and Enter loads
//! the highlighted result (the first Enter highlights the first result when none is); a click or
//! Space previews; and the preview stops when the result, the query, the tab or the location changes,
//! or when focus leaves the Browser. A list the field steps through carries the property
//! "browserResults"; whatever has a stopPreview() slot (the file lists, the search view's results) is
//! stopped by stopPreviews().
class LMMS_EXPORT BrowserPanel : public QWidget
{
	Q_OBJECT

public:
	explicit BrowserPanel(QWidget* parent = nullptr);
	~BrowserPanel() override;

	//! Add a source; its title is what the selector lists.
	void addSource(SideBarWidget* source);
	int sourceCount() const;
	QString currentSource() const;
	//! Select a source by its title; false when there is none by that title.
	bool selectSource(const QString& title);
	//! 0.5.0 L11 (WF-023): the view the browser is shown in (arrange, mix, edit). The source remembered
	//! for it (browser.sources) is selected, and a source the user picks is remembered for it
	//! (browser.remember_source).
	void setContext(const QString& context);
	QString context() const { return m_context; }
	//! The selected sample's waveform under the list (WF-010).
	class WaveformPreview* waveform() const { return m_wave; }
	//! The search field shown first: the current source's own, nullptr when it has none.
	QLineEdit* searchField() const;
	//! Theme 3: stop every preview the sources play (the file lists', the search view's).
	void stopPreviews();
	//! Put the keyboard in the search field, its text selected (Ctrl+F in the Browser).
	void focusSearch();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	//! Show @a source's search field in the slot above the selector (an empty page when it has none).
	void takeSearchField(SideBarWidget* source);
	//! Stop the preview when a list of @a source changes its current row or is refilled; remember its previewers.
	void watchResults(SideBarWidget* source);
	void showSource(int index);
	bool insideBrowser(const QWidget* widget) const;
	//! The arrow keys and Enter in the search field, acting on the list it filters; false when not taken.
	bool searchKey(const QKeyEvent* key);
	//! The list the current source shows its results in, nullptr when none.
	QAbstractItemView* currentResults() const;
	//! Tab reaches the search field first, then the selector, then the source.
	void orderFocus();

	QStackedWidget* m_searchSlot;
	QComboBox* m_selector;
	QStackedWidget* m_stack;
	class WaveformPreview* m_wave = nullptr;
	QString m_context;
	bool m_restoring = false;
	QList<QPointer<QObject>> m_previewers;
	QList<QMetaObject::Connection> m_watches;  //!< what calls stopPreviews(), cut when the panel goes
};

//! The browser with the product's standard sources, as MainWindow's sidebar had them.
LMMS_EXPORT BrowserPanel* createStandardBrowser(QWidget* parent);

} // namespace lmms::gui

#endif // LMMS_GUI_BROWSER_PANEL_H
