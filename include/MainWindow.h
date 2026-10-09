/*
 * MainWindow.h - declaration of class MainWindow, the main window of LMMS
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_GUI_MAIN_WINDOW_H
#define LMMS_GUI_MAIN_WINDOW_H

#include <QBasicTimer>
#include <QTimer>
#include <QList>
#include <QMainWindow>
#include <QMdiArea>

#include "ConfigManager.h"

class QAction;
class QDomElement;

namespace lmms
{

namespace gui
{

class SessionGridView;
class PluginView;
class Shell;
class ShellButton;
class SubWindow;


class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	//! The single-window shell (docs/UI-SHELL.md): its panels, views and layout.
	Shell* shell() const { return m_shell; }
	//! Show or hide a shell panel by id (ShellLayout::panels()); the visibility afterwards.
	bool togglePanel(const QString& id);
	//! Bring a panel into view - switching view if the current one does not offer it - and
	//! give its editor focus: what opening a clip in the note editor does.
	void revealPanel(const QString& id);

	//! The never-shown parent of the floating secondary windows. Kept so that widgets created
	//! with it as their parent (dialogs, plugin views) keep a valid owner.
	QMdiArea* workspace()
	{
		return static_cast<QMdiArea*>(m_workspace);
	}

	QWidget* toolBar()
	{
		return m_toolBar;
	}

	int addWidgetToToolBar( QWidget * _w, int _row = -1, int _col = -1 );
	void addSpacingToToolBar( int _size );

	// wrap the widget in a SubWindow, which floats as its own window when shown
	LMMS_EXPORT SubWindow* addWindowedWidget(QWidget *w, Qt::WindowFlags windowFlags = QFlag(0));


	void refocus();

	///
	/// \brief	Asks whether changes made to the project are to be saved.
	///
	/// Opens a dialog giving the user the choice to (a) confirm his choice
	/// (such as opening a new file), (b) save the current project before
	/// proceeding or (c) cancel the process.
	///
	/// Every function that replaces the current file (e.g. creates new file,
	/// opens another file...) must call this before and may only proceed if
	/// this function returns true.
	///
	/// \param	stopPlayback whether playback should be stopped upon prompting.  If set to false, the caller should ensure that Engine::getSong()->stop() is called before unloading/loading a song.
	///
	/// \return	true if the user allows the software to proceed, false if they
	///         cancel the action.
	///
	bool mayChangeProject(bool stopPlayback);

	/// Opens \a path over the running session exactly as project.open does (Q050-044): a session with
	/// unsaved changes is captured first and the open is one undo step; a checkpoint over the undo
	/// budget refuses the open with a message and changes nothing. Callers ask mayChangeProject() first.
	void openProjectFile(const QString& path);

	// Auto save timer intervals. The slider in SetupDialog.cpp wants
	// minutes and the rest milliseconds.
	static const int DEFAULT_SAVE_INTERVAL_MINUTES = 2;
	static const int DEFAULT_AUTO_SAVE_INTERVAL = DEFAULT_SAVE_INTERVAL_MINUTES * 60 * 1000;

	static const int m_autoSaveShortTime = 10 * 1000; // 10s short loop

	void autoSaveTimerReset( int msec = ConfigManager::inst()->
					value( "ui", "saveinterval" ).toInt()
						* 60 * 1000 )
	{
		if( msec < m_autoSaveShortTime ) // No 'saveinterval' in .zenestudio.xml
		{
			msec = DEFAULT_AUTO_SAVE_INTERVAL;
		}
		m_autoSaveTimer.start( msec );
	}

	int getAutoSaveTimerInterval()
	{
		return m_autoSaveTimer.interval();
	}

	enum class SessionState
	{
		Normal,
		Recover
	};

	void setSession( SessionState session )
	{
		m_session = session;
	}

	SessionState getSession()
	{
		return m_session;
	}

	void sessionCleanup();

	void clearKeyModifiers();

	bool isShiftPressed() const { return m_keyMods.m_shift; }

	static void saveWidgetState( QWidget * _w, QDomElement & _de );
	static void restoreWidgetState( QWidget * _w, const QDomElement & _de );
	//! Keeps a grabbable 48 px sliver of a restored window inside \a area. An area too
	//! small to hold that sliver - the never-shown workspace - leaves the geometry as saved
	//! (BUGS_FOUND 11.30).
	static QRect clampToVisibleArea(QRect geometry, const QRect& area);
	void setAllSubWindowsDetached(bool detached);

	bool eventFilter(QObject* watched, QEvent* event) override;

signals:
	void detachAllSubWindows(bool detached);

public slots:
	void resetWindowTitle();

	void emptySlot();
	void createNewProject();
	void openProject();
	bool saveProject();
	bool saveProjectAs();
	bool saveProjectAsNewVersion();
	void saveProjectAsDefaultTemplate();
	void showSettingsDialog();
	void aboutLMMS();
	void help();
	void toggleAutomationEditorWin();
	void togglePatternEditorWin(bool forceShow = false);
	void toggleSongEditorWin();
	void toggleProjectNotesWin();
	void toggleMicrotunerWin();
	//! R5.3: the clip-launch grid, created on first use (SessionGridView).
	void toggleClipLauncherWin();
	void toggleMixerWin();
	void togglePianoRollWin();
	//! The classic piano roll, a floating window beside the note editor (docs/NOTE-EDITOR.md §7).
	void toggleClassicPianoRollWin();
	void toggleControllerRack();
	void toggleFullscreen();

	void updatePlayPauseIcons();

	void updateUndoRedoButtons();
	void undo();
	void redo();

	void toggleMidiLearn();
	void updateMidiLearnAction();

	// Retrospective MIDI capture (owner item 14): the Edit menu's two actions.
	// Both invoke the registry command they declare - one implementation for the
	// menu item and the agent surface (SPEC A11/A15).
	void toggleMidiRetroCapture();
	void captureMidiToClip();
	void updateMidiRetroCaptureActions();

	void autoSave();

private slots:
	void onExportProjectMidi();

protected:
	void closeEvent( QCloseEvent * _ce ) override;
	void keyPressEvent( QKeyEvent * _ke ) override;
	void keyReleaseEvent( QKeyEvent * _ke ) override;
	void timerEvent( QTimerEvent * _ev ) override;


public:
	//! R5.3: the clip-launch grid's widget, the launcher panel's content.
	SessionGridView* clipLauncher();

private:
	SessionGridView* m_clipLauncher = nullptr;
	MainWindow();
	MainWindow( const MainWindow & );
	~MainWindow() override;

	void finalize();

	void toggleWindow( QWidget *window, bool forceShow = false );
	//! View-menu actions for the floating windows (controller rack Ctrl+6, project notes
	//! Ctrl+7, scales and keymaps), created once and owned by this window.
	const QList<QAction*>& floatingWindowActions();

	void exportProject(bool multiExport = false);
	void handleSaveResult(QString const & filename, bool songSavedSuccessfully);
	bool guiSaveProject();
	bool guiSaveProjectAs( const QString & filename );

	class MovableQMdiArea : public QMdiArea
	{
	public:
		MovableQMdiArea(QWidget* parent = nullptr);
		~MovableQMdiArea() {}
	protected:
		void mousePressEvent(QMouseEvent* event) override;
		void mouseMoveEvent(QMouseEvent* event) override;
		void mouseReleaseEvent(QMouseEvent* event) override;
	private:
		bool m_isBeingMoved;
		int m_lastX;
		int m_lastY;
	};

	MovableQMdiArea * m_workspace;

	QWidget * m_toolBar;

	struct keyModifiers
	{
		keyModifiers() :
			m_ctrl( false ),
			m_shift( false ),
			m_alt( false )
		{
		}
		bool m_ctrl;
		bool m_shift;
		bool m_alt;
	} m_keyMods;

	QMenu * m_toolsMenu;
	QAction * m_undoAction;
	QAction * m_redoAction;
	QAction * m_midiLearnAction;
	QAction * m_armRetroCaptureAction;
	QAction * m_captureMidiAction;
	QList<PluginView *> m_tools;
	bool m_toolsMenuPopulated = false;

	QBasicTimer m_updateTimer;
	QTimer m_autoSaveTimer;
	int m_autoSaveInterval;

	friend class GuiApplication;

	QMenu * m_viewMenu;

	Shell * m_shell = nullptr;
	QList<QAction*> m_floatingWindowActions;

	ShellButton * m_metronomeToggle;

	SessionState m_session;
	
	bool maximized;

private slots:
	void browseHelp();
	void showTool( QAction * _idx );
	void runScript();
	void updateViewMenu();
	void updateToolsMenu();
	void updateConfig( QAction * _who );
	void onToggleMetronome();
	void onExportProject();
	void onExportProjectTracks();
	void onImportProject();
	void onSongModified();
	void onProjectFileNameChanged();

signals:
	void periodicUpdate();
	void initProgress(const QString &msg);

} ;

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_MAIN_WINDOW_H
