/*
 * MainWindow.cpp - implementation of LMMS-main-window
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

#include "MainWindow.h"

#include <cstdio>

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDomElement>
#include <QFileInfo>
#include <QMdiArea>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>

#include "Accessibility.h"
#include "MenuBarExtras.h"
#include "MeterSpeech.h"
#include "SessionAnnouncements.h"
#include "AboutDialog.h"
#include "AudioEngine.h"
#include "AutomationEditor.h"
#include "BrowserPanel.h"
#include "DevicePanel.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlWindowCommands.h"
#include "RetroMidiCaptureSettings.h"
#include "UnattendedRun.h"
#include "MissingDevicesNotice.h"
#include "UndoHistory.h"
#include "ControllerRackView.h"
#include "DeprecationHelper.h"
#include "DpiHelper.h"
#include "embed.h"
#include "Engine.h"
#include "ExportProjectDialog.h"
#include "FileDialog.h"
#include "MainWindowGeometry.h"
#include "Metronome.h"
#include "MixerView.h"
#include "GuiApplication.h"
#include "ImportFilter.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "InspectorPanel.h"
#include "MicrotunerConfig.h"
#include "SessionGridView.h"
#include "PerformView.h"
#include "MidiLearnGui.h"
#include "MpeInputAction.h"
#include "CrashReportMenu.h"
#include "MidiClockMenu.h"
#include "MidiReconnectAction.h"
#include "PluginQuarantineMenu.h"
#include "RecordingRecoveryMenu.h"
#include "RenderPresetMenu.h"
#include "ScriptBudgetAction.h"
#include "SessionTuningMenu.h"
#include "TrackRenderMenu.h"
#include "AafImportMenu.h"
#include "ControllerTemplateMenu.h"
#include "PatternEditor.h"
#include "NoteEditor.h"
#include "PianoRoll.h"
#include "PianoView.h"
#include "PluginFactory.h"
#include "PluginView.h"
#include "ProjectJournal.h"
#include "ProjectNotes.h"
#include "ProjectRecovery.h"
#include "ProjectRenderer.h"
#include "RecentProjectsMenu.h"
#include "RemotePluginBase.h"
#include "ScriptEngine.h"
#include "SessionCheckpoint.h"
#include "SetupDialog.h"
#include "Shell.h"
#include "ShellChrome.h"
#include "ShellPanel.h"
#include "ShellSelection.h"
#include "TransportReadouts.h"
#include "SongEditor.h"
#include "SubWindow.h"
#include "TemplatesMenu.h"
#include "TextFloat.h"
#include "ToolPlugin.h"
#include "VersionedSaveDialog.h"
#include "VideoDecoder.h"

#include "lmmsversion.h"


namespace lmms::gui
{

namespace
{

//! M3.2 (registry-first actions): the agent-surface gate's declaration - the
//! registered command this action or button implements (SPEC A15).
template <typename Object>
Object* declares(Object* object, const char* commandId)
{
	object->setProperty("controlCommand", QString::fromLatin1(commandId));
	return object;
}

//! The floating window a View-menu entry toggles, by its window.toggle editor name.
QWidget* floatingWindow(const QString& editor)
{
	GuiApplication* gui = getGUI();
	if (gui == nullptr) { return nullptr; }
	if (editor == QLatin1String("controller_rack")) { return gui->getControllerRackView(); }
	if (editor == QLatin1String("project_notes")) { return gui->getProjectNotes(); }
	if (editor == QLatin1String("microtuner")) { return gui->getMicrotunerConfig(); }
	if (editor == QLatin1String("classic_piano_roll")) { return gui->pianoRoll(); }
	if (editor == QLatin1String("perform")) { return PerformView::instance(false); }  // 0.5.0 WF-071
	return nullptr;
}

QAction* windowToggleAction(QMenu* menu, const QPixmap& icon, const QString& text, const char* editor)
{
	const QString name = QString::fromLatin1(editor);
	QAction* action = menu->addAction(icon, text, menu, [name]() { dispatchWindowToggle(name); });
	action->setProperty("controlCommand", QStringLiteral("window.toggle"));
	return action;
}

} // namespace


MainWindow::MainWindow() :
	m_workspace( nullptr ),
	m_toolsMenu( nullptr ),
	m_autoSaveTimer( this ),
	m_viewMenu( nullptr ),
	m_metronomeToggle( 0 ),
	m_session( SessionState::Normal )
{
	setAttribute( Qt::WA_DeleteOnClose );

	// The 0.5.0 shell (docs/UI-SHELL.md, docs/adr/ADR-0001-UI-TOOLKIT.md): one page of fixed
	// regions replaces the sidebar, the MDI workspace and the Focus Desk. The workspace object
	// stays, never shown, as the parent of the secondary windows (instrument and effect
	// dialogs, the controller rack, project notes), which float as top-level windows.
	m_shell = new Shell( this );
	setCentralWidget( m_shell );
	connect( this, &MainWindow::periodicUpdate, m_shell->header()->readouts(), &TransportReadouts::tick );
	m_toolBar = m_shell->toolStrip();
	m_workspace = new MovableQMdiArea( this );
	m_workspace->hide();
	SubWindow::setFloatOutsideWorkspace( true );

	emit initProgress(tr("Preparing browser"));
	m_shell->mount( QStringLiteral( "browser" ), createStandardBrowser( m_shell ) );
	// A single window holds every editor now, so it opens at a working size: 1600 x 1000, or
	// the screen's available area when that is smaller (Qt clamps a new window to it; the
	// shell's first show handles the offscreen platform, Shell::showEvent).
	resize( 1600, 1000 );

	m_updateTimer.start( 1000 / 60, this );  // 60 fps

	if( ConfigManager::inst()->value( "ui", "enableautosave" ).toInt() )
	{
		// connect auto save
		connect(&m_autoSaveTimer, SIGNAL(timeout()), this, SLOT(autoSave()));
		m_autoSaveInterval = ConfigManager::inst()->value(
					"ui", "saveinterval" ).toInt() < 1 ?
						DEFAULT_AUTO_SAVE_INTERVAL :
				ConfigManager::inst()->value(
					"ui", "saveinterval" ).toInt();

		// The auto save function mustn't run until there is a project
		// to save or it will run over recover.mmp if you hesitate at the
		// recover messagebox for a minute. It is now started in main.
		// See autoSaveTimerReset() in MainWindow.h

		// ... and the recovery file must not SURVIVE a clean quit, whichever
		// route the process leaves the event loop by. closeEvent does it for a
		// window close, but a window close is not the only clean exit: Qt5's
		// QCoreApplication::quit() is just exit(0) (Qt 5.15
		// qcoreapplication.cpp), so the control-driven shutdown delivers no
		// QCloseEvent at all and closeEvent - the only place this cleanup lived -
		// never ran. Measured on CI: all three Qt5 jobs red ControlShutdown with
		// "the autosave recovery file survived a clean quit", while a Qt6 box is
		// green because Qt6's quit() sends a QEvent::Quit that makes
		// QApplication::event() close every window. aboutToQuit is emitted by
		// QCoreApplication::exec() itself (execCleanup), after the loop returns
		// and before the deferred deletes that destroy the window, so it fires on
		// both toolkits with the GUI still alive - and it does NOT fire on the
		// last-resort shutdown guard, which leaves via std::_Exit: a forced exit
		// is not a clean one and must keep the marker recoverable.
		connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::sessionCleanup);
	}
	// 0.5.0 theme 22: every clean exit keeps where the window was (ui/main.geometry); the next start opens it there.
	// Not an unattended run's: an agent's or an offscreen instance's window says nothing about the user's screen.
	connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { if (!isUnattendedRun()) { mainwindowgeometry::save(this); } });

	connect( Engine::getSong(), SIGNAL(playbackStateChanged()),
				this, SLOT(updatePlayPauseIcons()));

	connect(Engine::getSong(), SIGNAL(modified()), SLOT(onSongModified()));
	// A recording pass that lost audio to a full capture buffer: a notice, never a modal dialog.
	connect(Engine::getSong(), &Song::recordTakeDropped, this, [this](qulonglong frames) {
		const QString text = tr("%1 frames of the recording were lost to a full capture buffer. "
			"The take is padded with silence where they were lost.").arg(frames);
		TextFloat::displayMessage(tr("Recording dropped audio"), text,
			embed::getIconPixmap("record", 24, 24), 8000);
		statusBar()->showMessage(text, 8000);
	});
	connect(Engine::getSong(), SIGNAL(projectFileNameChanged()), SLOT(onProjectFileNameChanged()));
	// 0.5.0 Q050-018: ONE non-modal notice for the devices an opened project named and this session could not load.
	connect(Engine::getSong(), &Song::projectLoaded, this, [this] {
		if (isAgentInstance()) { return; }
		showMissingDevicesNotice(this, missingdevices::collect(Engine::getSong()));
	});

	maximized = isMaximized();
	new QShortcut(QKeySequence(Qt::Key_F11), this, SLOT(toggleFullscreen()));
	// control.quit reattaches detached editors before it posts the quit (the
	// reason is ControlRegistry::setPreQuitHook's).
	ControlRegistry::setPreQuitHook([this]() { setAllSubWindowsDetached(false); });
	// M3.4: the command palette, application-wide.
	auto* paletteShortcut = new QShortcut(keySequence(Qt::CTRL, Qt::SHIFT, Qt::Key_P), this);
	paletteShortcut->setContext(Qt::ApplicationShortcut);
	connect(paletteShortcut, &QShortcut::activated, this,
		[]() { dispatchShellCommand(QStringLiteral("window.command_palette")); });

	if (ConfigManager::inst()->value("tooltips", "disabled").toInt())
	{
		qApp->installEventFilter(this);
	}

	installEventFilter(this);
	meterspeech::start(this);
	(new SessionAnnouncer(this))->start();
}




MainWindow::~MainWindow()
{
	for( PluginView *view : m_tools )
	{
		delete view->model();
		delete view;
	}
	// TODO: Close tools
	// dependencies are such that the editors must be destroyed BEFORE Song is deletect in Engine::destroy
	//   see issue #2015 on github
	delete getGUI()->automationEditor();
	delete getGUI()->noteEditor();
	delete getGUI()->pianoRoll();
	delete getGUI()->songEditor();
	// destroy engine which will do further cleanups etc.
	Engine::destroy();
}

void MainWindow::finalize()
{
	resetWindowTitle();
	setWindowIcon( embed::getIconPixmap( "icon_small" ) );

	auto addAction = [this](QMenu* menu, std::string_view icon, const QString& text,
		const QKeySequence& shortcut, auto(MainWindow::* slot)()) -> QAction*
	{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 3, 0))
		return menu->addAction(embed::getIconPixmap(icon), text, shortcut, this, slot);
#else
		return menu->addAction(embed::getIconPixmap(icon), text, this, slot, shortcut);
#endif
	};

	// project-popup-menu
	// Each menu is announced with its clean title: these QMenu objects never
	// get a title of their own (only the menu bar's action text does, "&File"
	// and friends), so their accessible name would otherwise fall back to
	// nothing an assistive tool can read (SPEC-zene-ui-v0 §5 item 1).
	a11y::announce(menuBar(), tr("Main menu"),
		tr("The top-level menus: File, Edit, View, Tools and Help."));

	auto project_menu = new QMenu(this);
	a11y::announce(project_menu, tr("File"),
		tr("Create, open, save, import and export projects."));
	menuBar()->addMenu( project_menu )->setText( tr( "&File" ) );

	declares(addAction(project_menu, "project_new", tr("&New"),
		QKeySequence::New, &MainWindow::createNewProject), "project.new");

	auto templates_menu = new TemplatesMenu( this );
	project_menu->addMenu(templates_menu);
	// M3 item 10: the start hub - new, templates, recent and Learn in one place (StartHub).
	declares(project_menu->addAction(embed::getIconPixmap("project_new"), tr("Start Hub..."),
		this, [](){ dispatchShellCommand(QStringLiteral("window.start_hub")); }), "window.start_hub");

	declares(addAction(project_menu, "project_open", tr("&Open..."),
		QKeySequence::Open, &MainWindow::openProject), "project.open");

	project_menu->addMenu(new RecentProjectsMenu(this));

	declares(addAction(project_menu, "project_save", tr("&Save"),
		QKeySequence::Save, &MainWindow::saveProject), "project.save");

	declares(addAction(project_menu, "project_save", tr("Save &As..."),
		keySequence(Qt::CTRL, Qt::SHIFT, Qt::Key_S), &MainWindow::saveProjectAs), "project.save");

	// Q050-007: the opt-in v2 container, as a menu entry of its own (the Save As dialog also carries
	// the choice as a checkbox). The same project.save command, with format "v2".
	declares(addAction(project_menu, "project_save", tr("Save as v2 &Container..."),
		QKeySequence(), &MainWindow::saveProjectAsV2Container), "project.save");

	declares(addAction(project_menu, "project_save", tr("Save as New &Version"),
		keySequence(Qt::CTRL, Qt::ALT, Qt::Key_S), &MainWindow::saveProjectAsNewVersion),
		"project.save_version");

	declares(project_menu->addAction(embed::getIconPixmap("project_save"), tr("Save as default template"),
		this, &MainWindow::saveProjectAsDefaultTemplate), "project.save_as_template");

	project_menu->addSeparator();

	declares(project_menu->addAction(embed::getIconPixmap("project_import"), tr("Import..."),
		this, &MainWindow::onImportProject), "project.import");

	// Lua scripting (spec: docs/specs/SPEC-lua-api-v0.md, G4). The script runs on
	// the ScriptEngine worker thread; engine mutations are applied on this
	// (GUI) thread when the run finishes, so nothing touches the audio thread.
	declares(addAction(project_menu, "tool", tr("Run &Script..."),
		keySequence(Qt::CTRL, Qt::SHIFT, Qt::Key_R), &MainWindow::runScript), "script.run");
	// The Lua memory a run may hold (script.set_memory_budget).
	addScriptBudgetAction(project_menu);

	declares(addAction(project_menu, "project_export", tr("E&xport..."),
		keySequence(Qt::CTRL, Qt::Key_E), &MainWindow::onExportProject), "render.render");

	declares(addAction(project_menu, "project_export", tr("Export &Tracks..."),
		keySequence(Qt::CTRL, Qt::SHIFT, Qt::Key_E), &MainWindow::onExportProjectTracks), "render.stems");

	declares(addAction(project_menu, "midi_file", tr("Export &MIDI..."),
		keySequence(Qt::CTRL, Qt::Key_M), &MainWindow::onExportProjectMidi), "project.export_midi");
	// DAWproject interchange, through dawproject.import / dawproject.export.
	addDawProjectActions(project_menu);
	addAafImportAction(project_menu);  // 0.5.0 F4 (WF-038)
	// Named render presets for render.render (export.preset_*).
	addRenderPresetMenu(project_menu);
	// Captures an abnormal exit left behind (record.recovery_*).
	addRecordingRecoveryMenu(project_menu);

	project_menu->addSeparator();

	// Detached editors are reattached first: a detached editor ignores its close
	// (SubWindow reattaches instead), and one ignored close cancels the whole quit.
	QAction* quitAction = project_menu->addAction(embed::getIconPixmap("exit"), tr("&Quit"),
		this, [this]() { setAllSubWindowsDetached(false); qApp->closeAllWindows(); });
	quitAction->setShortcut(keySequence(Qt::CTRL, Qt::Key_Q));
	// The menu's quit asks about unsaved work first (closeAllWindows); the
	// command it names is the same shutdown, which an agent asks for directly.
	declares(quitAction, "control.quit");

	auto edit_menu = new QMenu(this);
	a11y::announce(edit_menu, tr("Edit"), tr("Undo, redo, settings and MIDI learn."));
	menuBar()->addMenu( edit_menu )->setText( tr( "&Edit" ) );

	m_undoAction = addAction(edit_menu, "edit_undo", tr("Undo"),
		QKeySequence::Undo, &MainWindow::undo);

	m_redoAction = addAction(edit_menu, "edit_redo", tr("Redo"),
		QKeySequence::Redo, &MainWindow::redo);

	// A11/A15: these two actions declare the registry commands they implement.
	m_undoAction->setData(QStringLiteral("control.undo"));
	m_redoAction->setData(QStringLiteral("control.redo"));
	m_undoAction->setShortcutContext(Qt::ApplicationShortcut);
	m_redoAction->setShortcutContext(Qt::ApplicationShortcut);
	declares(edit_menu->addAction(embed::getIconPixmap("clock"), tr("Tempo Map..."),
		this, [](){ dispatchShellCommand(QStringLiteral("window.tempo_map")); }), "window.tempo_map");
	declares(edit_menu->addAction(embed::getIconPixmap("automation"), tr("Modulators..."),
		this, [](){ dispatchShellCommand(QStringLiteral("window.modulators")); }), "window.modulators");
	declares(edit_menu->addAction(embed::getIconPixmap("automation"), tr("Progression..."),
		this, [](){ dispatchShellCommand(QStringLiteral("window.progression")); }), "window.progression");
	// Q050-154: the live-code panel - the socket-only livecode.* group from the interface.
	declares(edit_menu->addAction(embed::getIconPixmap("tool"), tr("Live Code..."),
		this, [](){ dispatchShellCommand(QStringLiteral("window.livecode")); }), "window.livecode");
	declares(edit_menu->addAction(embed::getIconPixmap("edit_undo"), tr("Undo History..."),
		this, [](){ dispatchShellCommand(QStringLiteral("window.undo_history")); }), "window.undo_history");
	declares(edit_menu->addAction(embed::getIconPixmap("setup_general"), tr("Collaborate..."),
		this, [](){ dispatchShellCommand(QStringLiteral("collab.show_panel")); }), "collab.show_panel");  // 0.5.0 F6

	edit_menu->addSeparator();
	windowToggleAction(edit_menu, embed::getIconPixmap("microtuner"), tr("Scales and keymaps"), "microtuner");
	declares(edit_menu->addAction(embed::getIconPixmap("setup_general"), tr("Settings"),
		this, SLOT(showSettingsDialog())), "window.settings");

	// MPE input on/off, through device.mpe_set (re-read each time the menu opens).
	addMpeInputAction(edit_menu);
	// Controller re-connection (midi.reconnect_*): armed state and what it holds, re-read on open.
	addMidiReconnectAction(edit_menu);
	addMidiClockMenu(edit_menu);
	addSessionTuningMenu(edit_menu);
	addPluginQuarantineMenu(edit_menu);

	// Saved MIDI mapping sets (controller.template_*), re-read each time the submenu opens.
	QMenu* templatesMenu = edit_menu->addMenu(tr("Controller Templates"));
	connect(templatesMenu, &QMenu::aboutToShow, templatesMenu, [templatesMenu] { populateControllerTemplateMenu(templatesMenu); });

	// Global MIDI learn: arm this, touch a control, move a hardware knob.
	m_midiLearnAction = edit_menu->addAction(embed::getIconPixmap("setup_midi"), tr("MIDI Learn"),
		this, SLOT(toggleMidiLearn()));
	m_midiLearnAction->setCheckable(true);
	m_midiLearnAction->setToolTip(tr("Arm MIDI learn, then touch a control and move a hardware knob"));
	// A11/A15: the action declares the registry command it implements, and
	// toggleMidiLearn() below invokes that same command - one implementation for
	// the menu item, the shortcut and the agent surface (SPEC-zene-studio.md A11).
	m_midiLearnAction->setData(QStringLiteral("midi.learn_toggle"));
	MidiLearnGui::instance()->setAction(m_midiLearnAction);
	connect(edit_menu, SIGNAL(aboutToShow()), this, SLOT(updateMidiLearnAction()));

	// Retrospective MIDI capture (owner item 14, docs/MIDI-RETRO-CAPTURE.md):
	// arm the bounded recent-event ring, then write what was just played into a
	// clip. Both actions declare the registry command they implement and their
	// slots invoke that same command - one implementation for the menu item, the
	// shortcut and the agent surface (SPEC-zene-studio.md A11/A15), exactly as
	// the MIDI Learn action beside them does.
	//
	// No keyboard shortcut: Ctrl+Shift+M reads naturally next to the other
	// Ctrl+Shift+<letter> bindings, but whether any other file claims it is
	// UNVERIFIED without the built shortcut table (docs/MIDI-RETRO-CAPTURE.md 5,
	// open question 3), and taking an unverified key is worse than taking none.
	m_armRetroCaptureAction = edit_menu->addAction(embed::getIconPixmap("setup_midi"),
		tr("Arm MIDI Capture"), this, SLOT(toggleMidiRetroCapture()));
	m_armRetroCaptureAction->setCheckable(true);
	m_armRetroCaptureAction->setToolTip(tr("Keep the most recent MIDI input events in a bounded "
		"ring, off by default. Arm this, play, then Capture MIDI"));
	m_armRetroCaptureAction->setData(QStringLiteral("midi.retro_capture_arm"));

	m_captureMidiAction = edit_menu->addAction(embed::getIconPixmap("setup_midi"),
		tr("Capture MIDI"), this, SLOT(captureMidiToClip()));
	m_captureMidiAction->setToolTip(tr("Write the captured MIDI events into a new clip on the "
		"track you are playing - the selected clip's, else the monitored one (nothing to capture "
		"unless capture is armed)"));
	m_captureMidiAction->setData(QStringLiteral("midi.retro_capture_to_clip"));
	// R5.4: Live's Capture MIDI key.
	m_captureMidiAction->setShortcut(keySequence(Qt::CTRL, Qt::SHIFT, Qt::Key_C));
	connect(edit_menu, SIGNAL(aboutToShow()), this, SLOT(updateMidiRetroCaptureActions()));

	// The arm switch is persisted (the config key midi/retrocapture), and this is
	// the first point where reading it is safe: qApp exists and the engine has a
	// MIDI client. RetroMidiCapture's own constructor runs before main(), where a
	// ConfigManager read is a null dereference (slice 1, and the header comment of
	// src/core/ControlCommandsMidi.cpp).
	applyPersistedRetroCaptureArm();

	connect(edit_menu, SIGNAL(aboutToShow()), this, SLOT(updateUndoRedoButtons()));

	m_viewMenu = new QMenu( this );
	a11y::announce(m_viewMenu, tr("View"),
		tr("Show, hide and arrange the editors, and switch the Focus Desk."));
	menuBar()->addMenu( m_viewMenu )->setText( tr( "&View" ) );
	connect( m_viewMenu, SIGNAL(aboutToShow()),
		 this, SLOT(updateViewMenu()));
	connect( m_viewMenu, SIGNAL(triggered(QAction*)), this,
		SLOT(updateConfig(QAction*)));
	updateViewMenu();


	m_toolsMenu = new QMenu( this );
	a11y::announce(m_toolsMenu, tr("Tools"), tr("Plugin and tool maintenance."));
	// The Tools menu is populated the first time it is opened (updateToolsMenu),
	// not at start-up: a session that never opens it never pays for plugin
	// discovery. See docs/PLUGIN-SCAN-CACHE.md.
	connect( m_toolsMenu, SIGNAL(aboutToShow()), this, SLOT(updateToolsMenu()));
	connect( m_toolsMenu, SIGNAL(triggered(QAction*)), this, SLOT(showTool(QAction*)));
	menuBar()->addMenu( m_toolsMenu )->setText( tr( "&Tools" ) );


	// help-popup-menu
	auto help_menu = new QMenu(this);
	a11y::announce(help_menu, tr("Help"),
		tr("Help, about and telemetry consent."));
	menuBar()->addMenu( help_menu )->setText( tr( "&Help" ) );
	// May use offline help
	if( true )
	{
		declares(help_menu->addAction( embed::getIconPixmap( "help" ),
						tr( "Online Help" ),
						this, SLOT(browseHelp())), "app.online_help");
	}
	else
	{
		help_menu->addAction( embed::getIconPixmap( "help" ),
							tr( "Help" ),
							this, SLOT(help()));
	}

	// M3 item 4: every shortcut, read from these very menus (ShortcutsPage).
	declares(help_menu->addAction(embed::getIconPixmap("help"), tr("Keyboard Shortcuts"),
		this, [](){ dispatchShellCommand(QStringLiteral("window.shortcuts")); }), "window.shortcuts");
	addCrashReportMenu(help_menu);
	help_menu->addSeparator();
	declares(help_menu->addAction( embed::getIconPixmap( "icon_small" ), tr( "About" ),
				  this, SLOT(aboutLMMS())), "app.about");

#ifdef ZENE_TELEMETRY_ENABLED
	// Opt-in telemetry: default off, and this screen is where a user turns it
	// on, sees the exact payload, and turns it off again. See
	// docs/TELEMETRY-V1.md.
	help_menu->addSeparator();
	QAction * telemetryAction = help_menu->addAction( embed::getIconPixmap( "setup_general" ),
				  tr( "Telemetry - what we send..." ) );
	// A11/A15: the action declares the registry command it implements, and the
	// slot below opens the SAME consent screen that command implements
	// (lmms::openTelemetryConsentScreen) - one implementation for the menu item
	// and the agent surface, so the two cannot drift into two screens. The
	// declaration goes in the dynamic property "controlCommand" rather than
	// objectName() (load-bearing for this action elsewhere) or data() (the
	// declaration channel of last resort: the gate accepts it only when it
	// already resolves; see tests/agent-surface-gate.py).
	//
	// telemetry.consent declares `requires: display, human`, so the registry
	// refuses it for any unattended caller before the handler runs: an agent can
	// read the telemetry state with telemetry.status but can never consent on
	// the user's behalf, and can never open a modal screen nobody is there to
	// answer.
	telemetryAction->setProperty( "controlCommand", QStringLiteral( "telemetry.consent" ) );
	connect( telemetryAction, &QAction::triggered, this, [] {
		openTelemetryConsentScreen();
	} );
#endif

	// The header's project and metronome buttons. Each names the command it implements
	// (M3.2), and the window switches that used to sit beside them are the footer's now.
	ShellHeader* header = m_shell->header();
	auto projectButton = [this, header](const char* icon, const QString& text, auto slot,
		const char* commandId) {
		ShellButton* button = header->addButton(QString::fromLatin1(icon), text);
		button->setProperty("controlCommand", QString::fromLatin1(commandId));
		connect(button, &QToolButton::clicked, this, slot);
		return button;
	};
	projectButton("new", tr("Create new project"), &MainWindow::createNewProject, "project.new");
	ShellButton* recent = projectButton("open", tr("Open existing project"), &MainWindow::openProject, "project.open");
	recent->setMenu(new RecentProjectsMenu(this));
	recent->setPopupMode(QToolButton::MenuButtonPopup);
	projectButton("save", tr("Save current project"), &MainWindow::saveProject, "project.save");
	projectButton("export", tr("Export current project"), &MainWindow::onExportProject, "render.render");
	m_metronomeToggle = header->addButton(QStringLiteral("metronome"), tr("Metronome"));
	m_metronomeToggle->setProperty("controlCommand", QStringLiteral("transport.set_metronome"));
	m_metronomeToggle->setCheckable(true);
	m_metronomeToggle->setChecked(Engine::getSong()->metronome().active());
	connect(m_metronomeToggle, &QToolButton::clicked, this, &MainWindow::onToggleMetronome);

	// setup-dialog opened before?
	//
	// Both dialogs below are questions for a human, asked BEFORE app->exec().
	// In an unattended run (--control-socket, or no display at all) nobody can
	// answer them, so Qt parks the startup path in a nested event loop: the
	// instance stays alive, keeps answering `control.ping` with
	// engine_ready=false, and never becomes usable (task #625, measured on
	// 6b01b98eb). Ask only when a human is actually there; otherwise say it on
	// stderr, where the log and the operator can see it.
	const bool interactive = !lmms::isUnattendedRun();

	if( !ConfigManager::inst()->value( "app", "configured" ).toInt() )
	{
		// no, so show it that user can setup everything
		if( interactive )
		{
			SetupDialog sd;
			sd.exec();
			// Only a run that actually showed the dialog counts as configured. An unattended run
			// skipped it, so recording it here would suppress the dialog on the next launch even
			// though the user has never been asked.
			ConfigManager::inst()->setValue( "app", "configured", "1" );
		}
		else
		{
			fprintf( stderr, "MainWindow: unattended run: skipping the first-run setup dialog; "
				"configure audio and MIDI in the --config file\n" );
			fflush( stderr );
		}
	}
	// look whether the audio engine failed to start the audio device selected by the
	// user and is using AudioDummy as a fallback
	// or the audio device is set to invalid one
	if( Engine::audioEngine()->audioDevStartFailed() || !AudioEngine::isAudioDevNameValid(
		ConfigManager::inst()->value( "audioengine", "audiodev" ) ) )
	{

		if( interactive )
		{
			QMessageBox::critical(nullptr, "Audio device setup failed",
				tr("Failed to setup audio device for playback. Try adjusting your audio device settings (e.g. the sample rate), then restart Zene Studio."));

			// if so, offer the audio settings section of the setup dialog
			SetupDialog sd( SetupDialog::ConfigTab::AudioSettings );
			sd.exec();
		}
		else
		{
			// The engine already fell back to the dummy device and keeps working
			// (render, edit, save); an agent needs the reason, not a prompt. Two lines,
			// both from this one event, because they serve two readers:
			//   1. the sentence control.ping reports as audio.message (agent-facing, one source);
			//   2. the structured diagnostic the headless no-audio-device test parses
			//      (backend / start-failed / name-known / device), which is what makes the
			//      fallback auditable from a log rather than only from the socket.
			// Merge reconciliation of #625 x #626, 2026-09-12: both contracts are kept.
			const QString requested = ConfigManager::inst()->value( "audioengine", "audiodev" );
			const bool failed = Engine::audioEngine()->audioDevStartFailed();
			const bool known = AudioEngine::isAudioDevNameValid( requested );
			fprintf( stderr, "MainWindow: audio device setup failed: %s\n",
				Engine::audioEngine()->audioDevStartReason().toUtf8().constData() );
			fprintf( stderr, "MainWindow: audio-device-setup backend=\"%s\" "
				"device_start_failed=%d name_known=%d device=\"%s\" "
				"(unattended run: no dialog, the engine continues)\n",
				requested.toUtf8().constData(), failed ? 1 : 0, known ? 1 : 0,
				Engine::audioEngine()->audioDevName().toUtf8().constData() );
			fflush( stderr );
		}
	}

	// Every editor is a panel's content now (docs/UI-SHELL.md §3): the shell decides which
	// region shows it, per view, and the footer and View menu switch it.
	GuiApplication* gui = getGUI();
	m_shell->mount(QStringLiteral("arranger"), gui->songEditor());
	m_shell->mount(QStringLiteral("detail"), gui->noteEditor());
	// The classic piano roll stays, as a floating window, for what the note editor does not do
	// yet - MIDI recording, step input, chord stamping (docs/NOTE-EDITOR.md §7).
	addWindowedWidget(gui->pianoRoll());
	gui->pianoRoll()->parentWidget()->hide();
	m_shell->mount(QStringLiteral("automation"), gui->automationEditor());
	m_shell->mount(QStringLiteral("pattern"), gui->patternEditor());
	m_shell->mount(QStringLiteral("mixer"), gui->mixerView());
	m_shell->mount(QStringLiteral("launcher"), clipLauncher());
	m_shell->mount(QStringLiteral("inspector"), new InspectorPanel(m_shell));
	m_shell->mount(QStringLiteral("devices"), new DevicePanel(m_shell));
	ShellSelection::instance()->trackClicks();
	// The MDI era's Ctrl+1..5 editor keys, kept as second shortcuts on the panels they became.
	const std::pair<const char*, QKeySequence> legacyKeys[] = {
		{"arranger", keySequence(Qt::CTRL, Qt::Key_1)}, {"pattern", keySequence(Qt::CTRL, Qt::Key_2)},
		{"detail", keySequence(Qt::CTRL, Qt::Key_3)}, {"automation", keySequence(Qt::CTRL, Qt::Key_4)},
		{"mixer", keySequence(Qt::CTRL, Qt::Key_5)}};
	for (const auto& [id, key] : legacyKeys)
	{
		if (QAction* action = m_shell->panelAction(QString::fromLatin1(id)))
		{
			action->setShortcuts({action->shortcut(), key});
		}
	}
	m_shell->restoreConfiguredLayout();
}


int MainWindow::addWidgetToToolBar( QWidget * _w, int _row, int _col )
{
	// The header is one row: the song editor's tempo, time, meter and master controls follow
	// one another in the order they are added. The returned index keeps the old signature.
	Q_UNUSED(_row)
	Q_UNUSED(_col)
	m_shell->header()->addToStrip( _w );
	return m_shell->header()->stripCount();
}




void MainWindow::addSpacingToToolBar( int _size )
{
	m_shell->header()->addStripSpacing( _size );
}


SubWindow* MainWindow::addWindowedWidget(QWidget *w, Qt::WindowFlags windowFlags)
{
	// wrap the widget in our own *custom* window that patches some errors in QMdiSubWindow
	auto win = new SubWindow(m_workspace->viewport(), windowFlags);
	connect(this, &MainWindow::detachAllSubWindows, win, &SubWindow::setDetached);
	win->setWidget(w);

	if (w)
	{
		// TODO: somehow make this work on any setWidget
		connect(w, &QWidget::destroyed, win, &SubWindow::deleteLater);

		if (w->sizeHint().isValid())
		{
			auto titleBarHeight = win->titleBarHeight();
			auto frameWidth = win->frameWidth();
			QSize delta(2* frameWidth, titleBarHeight + frameWidth);
			win->resize(delta + w->sizeHint());
		}
	}

	m_workspace->addSubWindow(win);
	return win;
}



void MainWindow::setAllSubWindowsDetached(bool detached)
{
	emit detachAllSubWindows(detached);
}


void MainWindow::resetWindowTitle()
{
	QString title(tr( "Untitled" ));

	if( Engine::getSong()->projectFileName() != "" )
	{
		title = QFileInfo( Engine::getSong()->projectFileName()
							).completeBaseName();
	}

	if( Engine::getSong()->isModified() )
	{
		title += '*';
	}

	if( getSession() == SessionState::Recover )
	{
		title += " - " + tr( "Recover session. Please save your work!" );
	}

	setWindowTitle( title + " - " + tr( "Zene Studio %1" ).arg( LMMS_VERSION ) );
}




bool MainWindow::mayChangeProject(bool stopPlayback)
{
	if( stopPlayback )
	{
		Engine::getSong()->stop();
	}

	if( !Engine::getSong()->isModified() && getSession() != SessionState::Recover )
	{
		return( true );
	}

	// A control-surface quit (control.quit) has nobody to ask. It states its
	// intent up front, so answer the same question the dialog below would ask
	// exactly as a user would, minus the dialog. Without this the question blocks
	// in a nested event loop that nothing can dismiss, and the process never
	// finishes quitting - the whole of reproduction (a) in task #626 (the stack
	// was QCoreApplication::quit -> QApplication::closeAllWindows ->
	// MainWindow::closeEvent -> this dialog).
	const ControlRegistry::QuitPromptAnswer quitAnswer = ControlRegistry::quitPromptAnswer();
	if( quitAnswer != ControlRegistry::QuitPromptAnswer::Ask )
	{
		if( quitAnswer == ControlRegistry::QuitPromptAnswer::Save )
		{
			return( saveProject() );
		}
		if( getSession() == SessionState::Recover )
		{
			sessionCleanup();
		}
		return( true );
	}

	// Separate message strings for modified and recovered files
	QString messageTitleRecovered = tr( "Recovered project not saved" );
	QString messageRecovered = tr( "This project was recovered from the "
					"previous session. It is currently "
					"unsaved and will be lost if you don't "
					"save it. Do you want to save it now?" );

	QString messageTitleUnsaved = tr( "Project not saved" );
	QString messageUnsaved = tr( "The current project was modified since "
					"last saving. Do you want to save it "
								"now?" );

	const auto& title = getSession() == SessionState::Recover
		? messageTitleRecovered
		: messageTitleUnsaved;

	const auto& text = getSession() == SessionState::Recover
		? messageRecovered
		: messageUnsaved;

	const auto buttons = QMessageBox::Save
		| QMessageBox::Discard
		| QMessageBox::Cancel;

	auto mb = QMessageBox{QMessageBox::Question, title, text, buttons, this};
	int answer = mb.exec();

	if( answer == QMessageBox::Save )
	{
		return( saveProject() );
	}
	else if( answer == QMessageBox::Discard )
	{
		if( getSession() == SessionState::Recover )
		{
			sessionCleanup();
		}
		return( true );
	}

	return( false );
}




void MainWindow::clearKeyModifiers()
{
	m_keyMods.m_ctrl = false;
	m_keyMods.m_shift = false;
	m_keyMods.m_alt = false;
}




void MainWindow::saveWidgetState(QWidget* w, QDomElement& de)
{
	// TODO: Only use one of these
	auto win = qobject_cast<SubWindow*>(w);
	if (!win)
	{
		// Fall back on parent
		win = qobject_cast<SubWindow*>(w->parentWidget());
		if (!win)
		{
			// Still could not find the window - soft fail
			return;
		}
	}

	de.setAttribute("visible", bool{win->widget() && win->widget()->isVisible()});
	de.setAttribute("maximized", win->isMaximized());

	QRect normalGeometry = win->getTrueNormalGeometry();
	de.setAttribute("x", normalGeometry.x());
	de.setAttribute("y", normalGeometry.y() );
	de.setAttribute("width", normalGeometry.width());
	de.setAttribute("height", normalGeometry.height());
}

//! The slice of the MDI virtual desktop currently scrolled into view, in MDI
//! coordinates. A subwindow outside it is unreachable - the bug the owner hit
//! live: "when a thing goes off screen there's no way to get it back".
static QRect visibleMdiRegion(QMdiArea* workspace)
{
	if (workspace == nullptr) { return QRect(); }
	const QWidget* view = workspace->viewport();
	return QRect(workspace->horizontalScrollBar()->value(),
		workspace->verticalScrollBar()->value(),
		view->width(), view->height());
}

//! Keep a grabbable sliver (48px of the title bar) inside \a area; a window
//! entirely outside jumps back to the area's origin + cascade margin.
QRect MainWindow::clampToVisibleArea(QRect geometry, const QRect& area)
{
	// An area that cannot hold the sliver is no place to clamp into: with the Focus Desk
	// on, the MDI workspace is hidden and its viewport a few pixels, so `right() - 48`
	// fell below `left()` - qBound's precondition, a Q_ASSERT abort on every project
	// reopen in a Debug build (BUGS_FOUND 11.30) and a window shoved to a meaningless
	// spot in a release one. The saved geometry is kept for when the workspace returns.
	constexpr int sliver = 48;
	if (area.isEmpty() || area.width() <= sliver || area.height() <= sliver) { return geometry; }
	if (!geometry.intersects(area))
	{
		geometry.moveTopLeft(area.topLeft() + QPoint(24, 24));
		return geometry;
	}
	geometry.setLeft(qBound(area.left(), geometry.left(), area.right() - sliver));
	geometry.setTop(qBound(area.top(), geometry.top(), area.bottom() - sliver));
	return geometry;
}

void MainWindow::restoreWidgetState(QWidget* w, const QDomElement& de)
{
	// TODO: Only use one of these
	auto win = qobject_cast<SubWindow*>(w);
	if (!win)
	{
		// Fall back on parent
		win = qobject_cast<SubWindow*>(w->parentWidget());

		// Still could not find the window - soft fail
		if (!win) { return; }
	}

	// A layout saved on a monitor that is gone (or a window dragged out of the
	// workspace) must come back REACHABLE: clamp the restored geometry into the
	// visible MDI region so its title bar can be grabbed again. The workspace
	// comes from the window's own parent chain (this function is static).
	QMdiArea* workspace = nullptr;
	for (QWidget* ancestor = win->parentWidget(); ancestor != nullptr; ancestor = ancestor->parentWidget())
	{
		if ((workspace = qobject_cast<QMdiArea*>(ancestor)) != nullptr) { break; }
	}
	const auto normalGeometry = clampToVisibleArea(QRect{
		de.attribute("x").toInt(),
		de.attribute("y").toInt(),
		de.attribute("width").toInt(),
		de.attribute("height").toInt(),
	}, visibleMdiRegion(workspace));

	// First restore the window, as attempting to resize a maximized window can cause graphical glitches.
	win->setWindowState(win->windowState() & ~(Qt::WindowMaximized | Qt::WindowMinimized));

	// Then resize it. `QWidget::setGeometry` adjusts the size if it is invalid / under the minimum size.
	win->setGeometry(normalGeometry);

	// Set the window to its correct "maximized?" state.
	auto winState = win->windowState();
	winState.setFlag(Qt::WindowMaximized, de.attribute("maximized").toInt());
	win->setWindowState(winState);

	if (const auto visible = de.attribute("visible"); !visible.isEmpty())
	{
		win->setVisible(visible.toInt());
	}
}

void MainWindow::emptySlot()
{
}



void MainWindow::createNewProject()
{
	if( mayChangeProject(true) )
	{
		// M3.2: the menu, the toolbar and the socket share project.new.
		dispatchShellCommand(QStringLiteral("project.new"));
	}
}




void MainWindow::openProjectFile(const QString& path)
{
	setCursor( Qt::WaitCursor );
	QString refusal;
	const bool opened = control::loadProjectWithCheckpoint( path, QStringList(), nullptr, &refusal );
	setCursor( Qt::ArrowCursor );
	if( !opened )
	{
		QMessageBox::warning( this, tr( "Project not opened" ), refusal );
	}
}




void MainWindow::openProject()
{
	if( mayChangeProject(false) )
	{
		FileDialog ofd( this, tr( "Open Project" ), "", tr( "Zene Studio (*.mmp *.mmpz)" ) );

		ofd.setDirectory( ConfigManager::inst()->userProjectsDir() );
		ofd.setFileMode( FileDialog::ExistingFiles );
		if( ofd.exec () == QDialog::Accepted &&
						!ofd.selectedFiles().isEmpty() )
		{
			Song *song = Engine::getSong();

			song->stop();
			openProjectFile( ofd.selectedFiles()[0] );
		}
	}
}




void MainWindow::runScript()
{
	const QString path = FileDialog::getOpenFileName(this, tr("Run Lua Script"),
		ConfigManager::inst()->workingDir(),
		tr("Lua scripts (*.lua);;All files (*)"));

	if (path.isEmpty())
	{
		return;
	}

	// The menu and the socket share script.run (Q050-044): the project is captured first, the run is
	// bounded by the undo budget, and everything the script changed is one undo step.
	const ControlResult result = ControlRegistry::instance()->invoke(
		QStringLiteral("script.run"), QJsonObject{{QStringLiteral("path"), path}});

	if (!result.ok)
	{
		QMessageBox::warning(this, tr("Script failed"),
			tr("Running \"%1\" failed:\n\n%2")
				.arg(QFileInfo(path).fileName(), result.errorMessage));
	}
	else
	{
		TextFloat::displayMessage(tr("Script finished"),
			QFileInfo(path).fileName(), embed::getIconPixmap("tool"), 2000);
	}
}




bool MainWindow::saveProject()
{
	if( Engine::getSong()->projectFileName() == "" )
	{
		return( saveProjectAs() );
	}
	else if( this->guiSaveProject() )
	{
		if( getSession() == SessionState::Recover )
		{
			sessionCleanup();
		}
		return true;
	}
	return false;
}




bool MainWindow::saveProjectAs()
{
	return saveProjectAsWithFormat(false);
}




bool MainWindow::saveProjectAsV2Container()
{
	return saveProjectAsWithFormat(true);
}




bool MainWindow::saveProjectAsWithFormat(bool preferV2)
{
	// Q050-007: the dialog's v2 checkbox starts at what a plain Save of this project would
	// write - ticked for the entry above, for a project that already is a container, and
	// when the global setting is on - so unticking it is the way back to the legacy blob.
	Song * song = Engine::getSong();
	const QString current = song->projectFileName();
	song->getSaveOptions().saveAsV2Container.setValue(
		preferV2 || song->plannedSaveFormat(current.isEmpty() ? QStringLiteral(".mmpz") : current)
			== Song::SaveFormat::V2Container);
	auto optionsWidget = new SaveOptionsWidget(Engine::getSong()->getSaveOptions());
	VersionedSaveDialog sfd( this, optionsWidget, tr( "Save Project" ), "",
			tr( "Zene Studio Project" ) + " (*.mmpz *.mmp);;" +
				tr( "Zene Studio Project Template" ) + " (*.mpt)" );
	QString f = Engine::getSong()->projectFileName();
	if( f != "" )
	{
		sfd.setDirectory( QFileInfo( f ).absolutePath() );
		sfd.selectFile( QFileInfo( f ).fileName() );
	}
	else
	{
		sfd.setDirectory( ConfigManager::inst()->userProjectsDir() );
	}

	// Don't write over file with suffix if no suffix is provided.
	QString suffix = ConfigManager::inst()->value( "app",
							"nommpz" ).toInt() == 0
						? "mmpz"
						: "mmp" ;
	sfd.setDefaultSuffix( suffix );

	if( sfd.exec () == FileDialog::Accepted &&
		!sfd.selectedFiles().isEmpty() && sfd.selectedFiles()[0] != "" )
	{
		QString fname = sfd.selectedFiles()[0] ;
		if( sfd.selectedNameFilter().contains( "(*.mpt)" ) )
		{
			// Remove the default suffix
			fname.remove( "." + suffix );
			if( !sfd.selectedFiles()[0].endsWith( ".mpt" ) )
			{
				if( VersionedSaveDialog::fileExistsQuery( fname + ".mpt",
						tr( "Save project template" ) ) )
				{
					fname += ".mpt";
				}
			}
		}
		// The dialog was answered: its v2 checkbox now decides the format (see SaveOptions).
		// A template (.mpt) is never a container, whatever the box says.
		Song::SaveOptions & options = song->getSaveOptions();
		if( fname.endsWith( ".mpt" ) ) { options.saveAsV2Container.setValue( false ); }
		options.formatChosen = true;
		const bool v2 = options.saveAsV2Container.value();
		const QString target = DataFile( DataFile::Type::SongProject ).nameWithExtension( fname );

		// Q050-007: every save whose format the person (or the project) put on the v2 side
		// of the line is the registered project.save command - the one the agent surface
		// calls, with its revision rotation and its transaction - and not a private GUI
		// writer. A plain legacy Save As keeps the dialog's own path (and its bundle and
		// MIDI-connection options, which project.save does not carry).
		const bool viaCommand = !options.saveAsProjectBundle.value() && !fname.endsWith( ".mpt" )
			&& ( v2 || song->plannedSaveFormat( target ) == Song::SaveFormat::V2Container );
		const bool saved = viaCommand ? saveThroughCommand( target, v2 ) : this->guiSaveProjectAs( fname );
		options.setDefaultOptions();
		if( saved )
		{
			if( getSession() == SessionState::Recover )
			{
				sessionCleanup();
			}
			return true;
		}
		return false;
	}
	song->getSaveOptions().setDefaultOptions();
	return false;
}




bool MainWindow::saveThroughCommand( const QString & target, bool v2 )
{
	const ControlResult saved = ControlRegistry::instance()->invoke( QStringLiteral( "project.save" ),
		QJsonObject{ { QStringLiteral( "path" ), target },
			{ QStringLiteral( "format" ), v2 ? QStringLiteral( "v2" ) : QStringLiteral( "legacy" ) } } );
	handleSaveResult( target, saved.ok );
	if( !saved.ok ) { statusBar()->showMessage( saved.errorMessage, 8000 ); }
	return saved.ok;
}




bool MainWindow::saveProjectAsNewVersion()
{
	QString fileName = Engine::getSong()->projectFileName();
	if( fileName == "" )
	{
		return saveProjectAs();
	}
	else
	{
		// M3.2: project.save_version computes the next free version and writes it;
		// the window then does what it does after any save (title, recent list).
		const ControlResult saved = ControlRegistry::instance()->invoke(
			QStringLiteral("project.save_version"));
		const QString written = saved.ok ? saved.result.value(QStringLiteral("file")).toString() : fileName;
		handleSaveResult(written, saved.ok);
		return saved.ok;
	}
}




void MainWindow::saveProjectAsDefaultTemplate()
{
	QString defaultTemplate = ConfigManager::inst()->userTemplateDir() + "default.mpt";

	QFileInfo fileInfo(defaultTemplate);
	if (fileInfo.exists())
	{
		if (QMessageBox::warning(this,
					 tr("Overwrite default template?"),
					 tr("This will overwrite your current default template."),
					 QMessageBox::Ok,
					 QMessageBox::Cancel) != QMessageBox::Ok)
		{
			return;
		}
	}

	// M3.2: the question above is the window's; the write is project.save_as_template.
	dispatchShellCommand(QStringLiteral("project.save_as_template"));
}




void MainWindow::showSettingsDialog()
{
	// M3.2: window.settings opens the dialog - non-modally, so the menu and the
	// socket share one path and neither holds the other while it is open.
	dispatchShellCommand(QStringLiteral("window.settings"));
}




void MainWindow::aboutLMMS()
{
	dispatchShellCommand(QStringLiteral("app.about"));
}




void MainWindow::help()
{
	QMessageBox::information( this, tr( "Help not available" ),
				  tr( "Currently there's no help "
						  "available in Zene Studio.\n"
						  "Please visit "
						  "https://github.com/KRUZZZZY/zene-studio "
						  "for documentation." ),
				  QMessageBox::Ok );
}




void MainWindow::toggleWindow( QWidget *window, bool forceShow )
{
	// Only the secondary windows come here - the controller rack, project notes, the
	// microtuner - and they float (SubWindow::setFloatOutsideWorkspace): showing one shows and
	// raises its top-level window, toggling a visible one hides it. Visibility alone decides:
	// a floating window is never the active one while the View menu or the main window's
	// shortcut is used, so an "or inactive" test made the toggle unable to hide it.
	auto parent = dynamic_cast<QMdiSubWindow*>(window->parentWidget());
	if (parent == nullptr) { return; }
	if (forceShow || !window->isVisible())
	{
		parent->show();
		window->show();
		window->raise();
		window->activateWindow();
	}
	else
	{
		window->hide();
		refocus();
	}
}

bool MainWindow::togglePanel(const QString& id)
{
	return m_shell->togglePanel(id);
}

void MainWindow::revealPanel(const QString& id)
{
	m_shell->revealPanel(id);
}



void MainWindow::toggleFullscreen()
{
	if ( !isFullScreen() )
	{
		maximized = isMaximized();
		showFullScreen();
	}
	else
	{
		maximized ? showMaximized() : showNormal();
	}
}



/*
 * When an editor window with focus is toggled off, attempt to set focus
 * to the next visible editor window, or if none are visible, set focus
 * to the parent window.
 */
void MainWindow::refocus()
{
	// The panel that last had focus gets it back, else the window.
	const QString focused = m_shell->model().focused();
	if (ShellPanel* frame = m_shell->panel(focused); frame != nullptr && frame->isVisible() && frame->content() != nullptr)
	{
		frame->content()->setFocus();
		return;
	}
	this->setFocus();
}




void MainWindow::togglePatternEditorWin( bool forceShow )
{
	if (forceShow) { revealPanel(QStringLiteral("pattern")); }
	else { togglePanel(QStringLiteral("pattern")); }
}

void MainWindow::toggleSongEditorWin() { togglePanel(QStringLiteral("arranger")); }

void MainWindow::toggleProjectNotesWin() { toggleWindow( getGUI()->getProjectNotes() ); }

void MainWindow::togglePianoRollWin() { togglePanel(QStringLiteral("detail")); }

void MainWindow::toggleClassicPianoRollWin() { toggleWindow( getGUI()->pianoRoll() ); }

void MainWindow::toggleAutomationEditorWin() { togglePanel(QStringLiteral("automation")); }

void MainWindow::toggleMixerWin() { togglePanel(QStringLiteral("mixer")); }

void MainWindow::toggleMicrotunerWin() { toggleWindow( getGUI()->getMicrotunerConfig() ); }

SessionGridView* MainWindow::clipLauncher()
{
	if( m_clipLauncher == nullptr ) { m_clipLauncher = new SessionGridView; }
	return m_clipLauncher;
}

void MainWindow::toggleClipLauncherWin() { togglePanel(QStringLiteral("launcher")); }




const QList<QAction*>& MainWindow::floatingWindowActions()
{
	if (!m_floatingWindowActions.isEmpty()) { return m_floatingWindowActions; }
	struct Row { const char* icon; QString text; const char* editor; QKeySequence key; };
	const Row rows[] = {
		{"controller", tr("Controller Rack"), "controller_rack", keySequence(Qt::CTRL, Qt::Key_6)},
		{"project_notes", tr("Project Notes"), "project_notes", keySequence(Qt::CTRL, Qt::Key_7)},
		{"microtuner", tr("Scales and Keymaps"), "microtuner", QKeySequence()},
		{"piano", tr("Classic Piano Roll"), "classic_piano_roll", QKeySequence()},
		{"play", tr("Perform Mode"), "perform", QKeySequence()},  // 0.5.0 WF-071: the launcher's live show window
	};
	for (const Row& row : rows)
	{
		const QString name = QString::fromLatin1(row.editor);
		auto* action = new QAction(embed::getIconPixmap(row.icon), row.text, this);
		action->setShortcut(row.key);
		action->setCheckable(true);  // checked while the window is open (updateViewMenu)
		action->setProperty("controlCommand", QStringLiteral("window.toggle"));
		action->setProperty("floatingWindow", name);
		connect(action, &QAction::triggered, this, [name]() { dispatchWindowToggle(name); });
		addAction(action);
		m_floatingWindowActions << action;
	}
	return m_floatingWindowActions;
}

void MainWindow::updateViewMenu()
{
	m_viewMenu->clear();
	// The shell's own actions (views Alt+1..3, panels Alt+letter): the same QAction objects the
	// footer's buttons show, so the menu, the footer and the shortcut cannot disagree.
	for (QAction* action : m_shell->viewActions()) { m_viewMenu->addAction(action); }
	m_viewMenu->addSeparator();
	for (QAction* action : m_shell->panelActions()) { m_viewMenu->addAction(action); }
	m_viewMenu->addAction(m_shell->floatAction());  // 0.5.0 theme 8: float or dock the focused panel
	m_viewMenu->addSeparator();
	for (QAction* action : floatingWindowActions())
	{
		const QWidget* window = floatingWindow(action->property("floatingWindow").toString());
		action->setChecked(window != nullptr && window->isVisible());
		m_viewMenu->addAction(action);
	}
	// 0.5.0 ED-054: the project's video in its floating window (greyed out in a build without FFmpeg).
	declares(m_viewMenu->addAction(tr("Video Window"), this, []() { dispatchShellCommand(QStringLiteral("video.window_show")); }),
		"video.window_show")->setEnabled(VideoDecoder::available());
	m_viewMenu->addSeparator();

	declares(m_viewMenu->addAction(embed::getIconPixmap( "fullscreen" ),
				tr( "Fullscreen" ) + "\tF11",
				this, [](){ dispatchShellCommand(QStringLiteral("window.fullscreen")); }
		), "window.fullscreen");
	declares(m_viewMenu->addAction(embed::getIconPixmap("tool"), tr("Command Palette...") + "\tCtrl+Shift+P",
		this, [](){ dispatchShellCommand(QStringLiteral("window.command_palette")); }),
		"window.command_palette");
	m_viewMenu->addSeparator();

	// Here we should put all look&feel -stuff from configmanager
	// that is safe to change on the fly. There is probably some
	// more elegant way to do this.
	// These two write a config key; the command they route through (updateConfig)
	// is settings.set, the same write the settings dialog makes on OK.
	auto qa = declares(new QAction(tr("Smooth scroll"), this), "settings.set");
	qa->setData("smoothscroll");
	qa->setCheckable( true );
	qa->setChecked( ConfigManager::inst()->value( "ui", "smoothscroll" ).toInt() );
	m_viewMenu->addAction(qa);

	qa = declares(new QAction(tr( "Enable note labels in piano roll" ), this), "settings.set");
	qa->setData("printnotelabels");
	qa->setCheckable( true );
	qa->setChecked( ConfigManager::inst()->value( "ui", "printnotelabels" ).toInt() );
	m_viewMenu->addAction(qa);
	// 0.5.0 theme 17: what a screen reader is told unasked (view.announcements), the level in force checked.
	addAnnouncementsMenu(m_viewMenu);

}




void MainWindow::updateConfig( QAction * _who )
{
	QString tag = _who->data().toString();
	bool checked = _who->isChecked();

	if (tag == "tooltips")
	{
		ConfigManager::inst()->setValue( "tooltips", "disabled",
						 QString::number(!checked) );

		if (checked) { qApp->removeEventFilter(this); }
		else { qApp->installEventFilter(this); }

	}
	else if ( tag == "smoothscroll" )
	{
		// M3.2: the same config write the settings dialog makes, through settings.set.
		dispatchShellCommand(QStringLiteral("settings.set"),
			QJsonObject{{QStringLiteral("key"), QStringLiteral("ui/smoothscroll")},
				{QStringLiteral("value"), QString::number(checked)}});
	}
	else if ( tag == "oneinstrument" )
	{
		ConfigManager::inst()->setValue( "ui", "oneinstrumenttrackwindow",
						 QString::number(checked) );
	}
	else if ( tag == "printnotelabels" )
	{
		dispatchShellCommand(QStringLiteral("settings.set"),
			QJsonObject{{QStringLiteral("key"), QStringLiteral("ui/printnotelabels")},
				{QStringLiteral("value"), QString::number(checked)}});
	}
}



void MainWindow::onToggleMetronome()
{
	dispatchShellCommand(QStringLiteral("transport.set_metronome"),
		QJsonObject{{QStringLiteral("enabled"), m_metronomeToggle->isChecked()}});
}




void MainWindow::toggleControllerRack()
{
	toggleWindow( getGUI()->getControllerRackView() );
}




void MainWindow::updatePlayPauseIcons()
{
	getGUI()->songEditor()->setPauseIcon( false );
	getGUI()->automationEditor()->setPauseIcon( false );
	getGUI()->patternEditor()->setPauseIcon( false );
	getGUI()->pianoRoll()->setPauseIcon( false );

	if( Engine::getSong()->isPlaying() )
	{
		switch( Engine::getSong()->playMode() )
		{
			case Song::PlayMode::Song:
				getGUI()->songEditor()->setPauseIcon( true );
				break;

			case Song::PlayMode::AutomationClip:
				getGUI()->automationEditor()->setPauseIcon( true );
				break;

			case Song::PlayMode::Pattern:
				getGUI()->patternEditor()->setPauseIcon( true );
				break;

			case Song::PlayMode::MidiClip:
				getGUI()->pianoRoll()->setPauseIcon( true );
				break;

			default:
				break;
		}
	}
}


void MainWindow::updateUndoRedoButtons()
{
	// when the edit menu is shown, grey out the undo/redo buttons if there's nothing to undo/redo
	// else, un-grey them
	m_undoAction->setEnabled(Engine::projectJournal()->canUndo());
	m_redoAction->setEnabled(Engine::projectJournal()->canRedo());
}




void MainWindow::toggleMidiLearn()
{
	// A11: the menu item drives the SAME registry command an agent calls, so the
	// mode has one implementation rather than two that can drift. The handler
	// arms MidiLearnGui and syncs this action's tick (MidiLearnGui holds it).
	const ControlResult result = ControlRegistry::instance()->invoke(
		QStringLiteral("midi.learn_toggle"));
	if( !result.ok )
	{
		// Nothing armed - there is no GUI yet, or the engine is still starting.
		// Qt has already flipped the tick, so put it back where the real state
		// is: the menu must never claim a mode that was not armed.
		updateMidiLearnAction();
	}
}




void MainWindow::updateMidiLearnAction()
{
	// A successful learn disarms MidiLearn from the MIDI input thread, so the
	// action re-reads the real state whenever the edit menu is opened.
	m_midiLearnAction->setChecked(MidiLearnGui::instance()->isArmed());
}


void MainWindow::toggleMidiRetroCapture()
{
	// A11: the menu item drives the SAME registry command an agent calls. Qt has
	// already flipped the tick; the command is told which state the user asked
	// for, so the mode cannot disagree with the check mark.
	const bool wanted = m_armRetroCaptureAction->isChecked();
	const ControlResult result = ControlRegistry::instance()->invoke(
		QStringLiteral("midi.retro_capture_arm"),
		QJsonObject{{QStringLiteral("armed"), wanted}});
	if( !result.ok )
	{
		// Nothing armed - there is no MIDI client yet, or the engine is still
		// starting. Put the tick back where the real state is: the menu must
		// never claim a mode that was not armed.
		updateMidiRetroCaptureActions();
	}
	else
	{
		m_armRetroCaptureAction->setChecked(
			result.result.value(QStringLiteral("armed")).toBool());
	}
}


void MainWindow::captureMidiToClip()
{
	// A11: the menu item invokes the registry command; the command owns the
	// target-track rule (the selected clip's track, else the song's first
	// instrument track) so the menu and an agent cannot disagree about where a
	// capture lands.
	const ControlResult result = ControlRegistry::instance()->invoke(
		QStringLiteral("midi.retro_capture_to_clip"));
	if( result.ok )
	{
		const QString clip = result.result.value(QStringLiteral("clip")).toString();
		const int written = result.result.value(QStringLiteral("events_written")).toInt();
		const int unmatched = result.result.value(QStringLiteral("unmatched_ons")).toInt();
		// Non-modal feedback: a transient status line, never a box the user has
		// to dismiss after a successful capture. An unmatched note-on means the
		// window was cut off, so it is said out loud rather than left in the log.
		QString message = tr( "Captured %1 note(s) into %2 on %3" ).arg( written ).arg( clip )
			.arg( result.result.value( QStringLiteral( "track" ) ).toString() );
		if( unmatched > 0 )
		{
			message += tr( " - %1 note-on(s) had no release inside the window and were closed "
				"at its end" ).arg( unmatched );
		}
		statusBar()->showMessage( message, 8000 );
		return;
	}
	// A refusal is normal (no capture armed, nothing played, no instrument
	// track). Say it where it can be read - a modal box only when a human is
	// actually there (the same rule the first-run setup dialog follows).
	if( lmms::isUnattendedRun() )
	{
		fprintf( stderr, "MainWindow: capture MIDI refused: %s\n",
			result.errorMessage.toUtf8().constData() );
		fflush( stderr );
		return;
	}
	QMessageBox::warning( this, tr( "Capture MIDI" ), result.errorMessage );
}


void MainWindow::updateMidiRetroCaptureActions()
{
	// Armed state is engine state, and the capture can be disarmed from outside
	// this window (an agent calling midi.retro_capture_arm), so the action
	// re-reads the real state whenever the edit menu is opened.
	const ControlResult status = ControlRegistry::instance()->invoke(
		QStringLiteral("midi.retro_capture_status"));
	if( !status.ok )
	{
		m_armRetroCaptureAction->setChecked(false);
		m_captureMidiAction->setEnabled(false);
		return;
	}
	m_armRetroCaptureAction->setChecked(
		status.result.value(QStringLiteral("armed")).toBool());
	const int buffered = status.result.value(QStringLiteral("events_buffered")).toInt();
	m_captureMidiAction->setEnabled(buffered > 0);
	// What the window holds, on the item itself: how many events and how many seconds back.
	m_captureMidiAction->setText(buffered > 0
		? tr("Capture MIDI (%n event(s), %1 s)", nullptr, buffered)
			.arg(status.result.value(QStringLiteral("seconds_span")).toDouble(), 0, 'f', 1)
		: tr("Capture MIDI"));
}



void MainWindow::undo()
{
	Engine::projectJournal()->undo();
}




void MainWindow::redo()
{
	Engine::projectJournal()->redo();
}




void MainWindow::closeEvent( QCloseEvent * _ce )
{
	if( mayChangeProject(true) )
	{
		// A detached editor is a top-level window of its own: bring every one
		// back into the workspace so it closes with the main window instead of
		// keeping the application alive (and a Qt6 quit from being cancelled).
		setAllSubWindowsDetached(false);
		// delete recovery file
		if( ConfigManager::inst()->
				value( "ui", "enableautosave" ).toInt() )
		{
			sessionCleanup();
			_ce->accept();
		}
	}
	else
	{
		_ce->ignore();
	}
}




void MainWindow::sessionCleanup()
{
	// delete the recovery session files: the recovery project file and the
	// identity sidecar autoSave() writes beside it
	ProjectRecovery::removeRecovery( ConfigManager::inst()->recoveryFile() );
	setSession( SessionState::Normal );
}




bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
	const auto isWinDeactivate = [](QEvent* event) -> bool
	{
		if (event->type() == QEvent::WindowDeactivate) { return true; }

		if (event->type() == QEvent::FocusOut)
		{
			auto* fe = static_cast<QFocusEvent*>(event);
			switch (fe->reason())
			{
			case Qt::ActiveWindowFocusReason: [[fallthrough]];
			case Qt::PopupFocusReason: [[fallthrough]];
			case Qt::OtherFocusReason:
				return true;
			default:
				break;
			}
		}

		return false;
	};

	// Clear modifiers when the window has been unfocused (install event filter on the window itself)
	if (dynamic_cast<MainWindow*>(watched) != nullptr && isWinDeactivate(event))
	{
		clearKeyModifiers();
		return QObject::eventFilter(watched, event);
	}

	// Block tooltips globally (install event filter on QApplication instance)
	if (event->type() == QEvent::ToolTip) { return true; }

	return QObject::eventFilter(watched, event);
}




void MainWindow::keyPressEvent( QKeyEvent * _ke )
{
	switch( _ke->key() )
	{
		case Qt::Key_Control: m_keyMods.m_ctrl = true; break;
		case Qt::Key_Shift: m_keyMods.m_shift = true; break;
		case Qt::Key_Alt: m_keyMods.m_alt = true; break;
		case Qt::Key_Space:
		{
			if (_ke->isAutoRepeat()) { break; }
			if (Editor::lastPlayedEditor() != nullptr)
			{
				if (m_keyMods.m_shift) { Editor::lastPlayedEditor()->togglePause(); }
				else { Editor::lastPlayedEditor()->togglePlayStop(); }
			}
			break;
		}
		default:
		{
			InstrumentTrackWindow * w =
						InstrumentTrackView::topLevelInstrumentTrackWindow();
			if( w )
			{
				w->pianoView()->keyPressEvent( _ke );
			}
			if( !_ke->isAccepted() )
			{
				QMainWindow::keyPressEvent( _ke );
			}
		}
	}
}




void MainWindow::keyReleaseEvent( QKeyEvent * _ke )
{
	switch( _ke->key() )
	{
		case Qt::Key_Control: m_keyMods.m_ctrl = false; break;
		case Qt::Key_Shift: m_keyMods.m_shift = false; break;
		case Qt::Key_Alt: m_keyMods.m_alt = false; break;
		default:
			if( InstrumentTrackView::topLevelInstrumentTrackWindow() )
			{
				InstrumentTrackView::topLevelInstrumentTrackWindow()->
					pianoView()->keyReleaseEvent( _ke );
			}
			if( !_ke->isAccepted() )
			{
				QMainWindow::keyReleaseEvent( _ke );
			}
	}
}




void MainWindow::timerEvent( QTimerEvent * _te)
{
	emit periodicUpdate();
}





void MainWindow::updateToolsMenu()
{
	if( m_toolsMenuPopulated ) { return; }
	m_toolsMenuPopulated = true;

	for( const Plugin::Descriptor* desc : getPluginFactory()->descriptors(Plugin::Type::Tool) )
	{
		m_toolsMenu->addAction( desc->logo->pixmap(), desc->displayName );
		m_tools.push_back( ToolPlugin::instantiate( desc->name, /*this*/nullptr )
					   ->createView(this) );
	}
	// A build with no tool plugins does not keep an empty menu in the bar.
	if( m_toolsMenu->isEmpty() )
	{
		menuBar()->removeAction( m_toolsMenu->menuAction() );
	}
}


void MainWindow::showTool( QAction * _idx )
{
	PluginView * p = m_tools[m_toolsMenu->actions().indexOf( _idx )];
	p->show();
	p->parentWidget()->show();
	p->setFocus();
}




void MainWindow::browseHelp()
{
	// M3.2: app.online_help opens the documentation (the menu is its human). It declares
	// `requires: human`, which the registry refuses for EVERY caller, so dispatching it here
	// meant the entry did nothing at all - not even the handler's own `opened: false` reached
	// anyone. The action calls the command's implementation instead, the way the Telemetry
	// action above calls openTelemetryConsentScreen, so the two still share one.
	openOnlineHelp(this);
}




void MainWindow::autoSave()
{
	if( !Engine::getSong()->isExporting() &&
		!Engine::getSong()->isLoadingProject() &&
		!RemotePluginBase::isMainThreadWaiting() &&
		!QApplication::mouseButtons() &&
		( ConfigManager::inst()->value( "ui",
				"enablerunningautosave" ).toInt() ||
			! Engine::getSong()->isPlaying() ) )
	{
		// Write the recovery file, then the sidecar that records WHICH project it
		// came from, so the next launch can tell this session's recovery from a
		// stale or unrelated one (ProjectRecovery::decideRecovery). Both are
		// plain writes on this GUI thread -- no audio-thread callback is added.
		QString const recoveryFile = ConfigManager::inst()->recoveryFile();
		if( Engine::getSong()->saveProjectFile( recoveryFile ) )
		{
			ProjectRecovery::writeRecoveryIdentity( recoveryFile,
					Engine::getSong()->projectFileName(), QDateTime::currentDateTimeUtc() );
		}
		autoSaveTimerReset();  // Reset timer
	}
	else
	{
		// try again in 10 seconds
		if( getAutoSaveTimerInterval() != m_autoSaveShortTime )
		{
			autoSaveTimerReset( m_autoSaveShortTime );
		}
	}
}

void MainWindow::onExportProjectMidi()
{
	FileDialog efd( this );

	efd.setFileMode( FileDialog::AnyFile );

	QStringList types;
	types << tr("MIDI File (*.mid)");
	efd.setNameFilters( types );
	QString base_filename;
	QString const & projectFileName = Engine::getSong()->projectFileName();
	if( !projectFileName.isEmpty() )
	{
		efd.setDirectory( QFileInfo( projectFileName ).absolutePath() );
		base_filename = QFileInfo( projectFileName ).completeBaseName();
	}
	else
	{
		efd.setDirectory( ConfigManager::inst()->userProjectsDir() );
		base_filename = tr( "untitled" );
	}
	efd.selectFile( base_filename + ".mid" );
	efd.setDefaultSuffix( "mid");
	efd.setWindowTitle( tr( "Select file for project-export..." ) );

	efd.setAcceptMode( FileDialog::AcceptSave );


	if( efd.exec() == QDialog::Accepted && !efd.selectedFiles().isEmpty() && !efd.selectedFiles()[0].isEmpty() )
	{
		const QString suffix = ".mid";

		QString export_filename = efd.selectedFiles()[0];
		if (!export_filename.endsWith(suffix)) export_filename += suffix;

		// M3.2: the dialog is the window's; the export is project.export_midi.
		dispatchShellCommand(QStringLiteral("project.export_midi"),
			QJsonObject{{QStringLiteral("path"), export_filename}});
	}
}

void MainWindow::exportProject(bool multiExport)
{
	QString const & projectFileName = Engine::getSong()->projectFileName();

	FileDialog efd( getGUI()->mainWindow() );

	if ( multiExport )
	{
		efd.setFileMode( FileDialog::Directory);
		efd.setWindowTitle( tr( "Select directory for writing exported tracks..." ) );
		if( !projectFileName.isEmpty() )
		{
			efd.setDirectory( QFileInfo( projectFileName ).absolutePath() );
		}
	}
	else
	{
		efd.setFileMode( FileDialog::AnyFile );
		int idx = 0;
		QStringList types;
		while( ProjectRenderer::fileEncodeDevices[idx].m_fileFormat != ProjectRenderer::ExportFileFormat::Count)
		{
			if(ProjectRenderer::fileEncodeDevices[idx].isAvailable()) {
				types << tr(ProjectRenderer::fileEncodeDevices[idx].m_description);
			}
			++idx;
		}
		efd.setNameFilters( types );
		QString baseFilename;
		if( !projectFileName.isEmpty() )
		{
			efd.setDirectory( QFileInfo( projectFileName ).absolutePath() );
			baseFilename = QFileInfo( projectFileName ).completeBaseName();
		}
		else
		{
			efd.setDirectory( ConfigManager::inst()->userProjectsDir() );
			baseFilename = tr( "untitled" );
		}
		efd.selectFile( baseFilename + ProjectRenderer::fileEncodeDevices[0].m_extension );
		efd.setWindowTitle( tr( "Select file for project-export..." ) );
	}

	QString suffix = "wav";
	efd.setDefaultSuffix( suffix );
	efd.setAcceptMode( FileDialog::AcceptSave );

	if( efd.exec() == QDialog::Accepted && !efd.selectedFiles().isEmpty() &&
					 !efd.selectedFiles()[0].isEmpty() )
	{

		QString exportFileName = efd.selectedFiles()[0];
		if ( !multiExport )
		{
			int stx = efd.selectedNameFilter().indexOf( "(*." );
			int etx = efd.selectedNameFilter().indexOf( ")" );

			if ( stx > 0 && etx > stx )
			{
				// Get first extension from selected dropdown.
				// i.e. ".wav" from "WAV-File (*.wav), Dummy-File (*.dum)"
				suffix = efd.selectedNameFilter().mid( stx + 2, etx - stx - 2 ).split( " " )[0].trimmed();

				Qt::CaseSensitivity cs = Qt::CaseSensitive;
#if defined(LMMS_BUILD_APPLE) || defined(LMMS_BUILD_WIN32)
				cs = Qt::CaseInsensitive;
#endif
				exportFileName.remove( "." + suffix, cs );
				if ( efd.selectedFiles()[0].endsWith( suffix ) )
				{
					if( VersionedSaveDialog::fileExistsQuery( exportFileName + suffix,
							tr( "Save project" ) ) )
					{
						exportFileName += suffix;
					}
				}
			}
		}

		ExportProjectDialog epd(exportFileName,
			multiExport ? ExportProjectDialog::Mode::ExportTracks : ExportProjectDialog::Mode::ExportProject,
			getGUI()->mainWindow());
		epd.exec();
	}
}

void MainWindow::handleSaveResult(QString const & filename, bool songSavedSuccessfully)
{
	// WF-033 (docs/UNDO-HISTORY-PERSISTENCE.md): an undo history the save could not keep is SAID, never
	// dropped in silence - the same text project.save's undo_history.warning carries.
	const QString historyWarning = songSavedSuccessfully ? UndoHistory::instance().lastSave().warning : QString();
	if (songSavedSuccessfully && !historyWarning.isEmpty())
	{
		TextFloat::displayMessage( tr( "Project saved - undo history not fully saved" ), historyWarning,
				embed::getIconPixmap( "project_save", 24, 24 ), 8000 );
		statusBar()->showMessage( historyWarning, 8000 );
	}
	else if (songSavedSuccessfully)
	{
		TextFloat::displayMessage( tr( "Project saved" ), tr( "The project %1 is now saved.").arg( filename ),
				embed::getIconPixmap( "project_save", 24, 24 ), 2000 );
		// Q050-007: a v2 container is said to be one, with its cost, every time.
		if (Engine::getSong()->lastSavedFormat() == Song::SaveFormat::V2Container)
		{
			statusBar()->showMessage( Song::containerV2Note(), 8000 );
		}
	}
	if (songSavedSuccessfully)
	{
		ConfigManager::inst()->addRecentlyOpenedProject(filename);
		resetWindowTitle();
	}
	else
	{
		TextFloat::displayMessage( tr( "Project NOT saved." ), tr( "The project %1 was not saved!" ).arg(filename),
				embed::getIconPixmap( "error" ), 4000 );
	}
}

bool MainWindow::guiSaveProject()
{
	Song * song = Engine::getSong();
	bool const songSaveResult = song->guiSaveProject();
	handleSaveResult(song->projectFileName(), songSaveResult);

	return songSaveResult;
}

bool MainWindow::guiSaveProjectAs( const QString & filename )
{
	Song * song = Engine::getSong();
	bool const songSaveResult = song->guiSaveProjectAs(filename);
	handleSaveResult(filename, songSaveResult);

	return songSaveResult;
}

void MainWindow::onExportProject()
{
	this->exportProject();
}

void MainWindow::onExportProjectTracks()
{
	this->exportProject(true);
}

void MainWindow::onImportProject()
{
	Song * song = Engine::getSong();

	if (song)
	{
		FileDialog ofd( nullptr, tr( "Import file" ),
				ConfigManager::inst()->userProjectsDir(),
				tr("MIDI sequences") +
				" (*.mid *.midi *.rmi);;" +
				tr("Hydrogen projects") +
				" (*.h2song);;" +
				tr("All file types") +
				" (*.*)");

		ofd.setFileMode( FileDialog::ExistingFiles );
		if( ofd.exec () == QDialog::Accepted && !ofd.selectedFiles().isEmpty() )
		{
			// M3.2: project.import is the import; the window keeps only the failure box.
			const ControlResult imported = ControlRegistry::instance()->invoke(
				QStringLiteral("project.import"),
				QJsonObject{{QStringLiteral("path"), ofd.selectedFiles()[0]}});
			if (!imported.ok)
			{
				QMessageBox::information(this, tr("Couldn't import file"), imported.errorMessage);
			}
		}

		song->setLoadOnLaunch(false);
	}
}

void MainWindow::onSongModified()
{
	// Only update the window title if the code is executed from the GUI main thread.
	// The assumption seems to be that the Song can also be set as modified from other
	// threads. This is not a good design! Copied from the original implementation of
	// Song::setModified.
	if(QThread::currentThread() == this->thread())
	{
		this->resetWindowTitle();
	}
}

void MainWindow::onProjectFileNameChanged()
{
	this->resetWindowTitle();
}


MainWindow::MovableQMdiArea::MovableQMdiArea(QWidget* parent) :
	QMdiArea(parent),
	m_isBeingMoved(false),
	m_lastX(0),
	m_lastY(0)
{}

void MainWindow::MovableQMdiArea::mousePressEvent(QMouseEvent* event)
{
	const auto pos = position(event);
	m_lastX = pos.x();
	m_lastY = pos.y();
	m_isBeingMoved = true;
	setCursor(Qt::ClosedHandCursor);
}

void MainWindow::MovableQMdiArea::mouseMoveEvent(QMouseEvent* event)
{
	if (m_isBeingMoved == false) { return; }

	int minXBoundary = window()->width() - 100;
	int maxXBoundary = 100;
	int minYBoundary = window()->height() - 100;
	int maxYBoundary = 100;

	int minX = minXBoundary;
	int maxX = maxXBoundary;
	int minY = minYBoundary;
	int maxY = maxYBoundary;

	auto subWindows = subWindowList();
	for (auto* curWindow : subWindows)
	{
		if (curWindow->isVisible())
		{
			minX = std::min(minX, curWindow->x());
			maxX = std::max(maxX, curWindow->x() + curWindow->width());
			minY = std::min(minY, curWindow->y());
			maxY = std::max(maxY, curWindow->y() + curWindow->height());
		}
	}

	const auto pos = position(event);
	int scrollX = m_lastX - pos.x();
	int scrollY = m_lastY - pos.y();

	scrollX = scrollX < 0 && minX >= minXBoundary ? 0 : scrollX;
	scrollX = scrollX > 0 && maxX <= maxXBoundary ? 0 : scrollX;
	scrollY = scrollY < 0 && minY >= minYBoundary ? 0 : scrollY;
	scrollY = scrollY > 0 && maxY <= maxYBoundary ? 0 : scrollY;

	for (auto* curWindow : subWindows)
	{
		// if widgets are maximized, then they shouldn't be moved
		// moving a maximized window's normalGeometry is not implemented because of difficulties
		if (curWindow->isMaximized() == false)
		{
			curWindow->move(curWindow->x() - scrollX, curWindow->y() - scrollY);
		}
	}

	m_lastX = pos.x();
	m_lastY = pos.y();
}

void MainWindow::MovableQMdiArea::mouseReleaseEvent(QMouseEvent* event)
{
	setCursor(Qt::ArrowCursor);
	m_isBeingMoved = false;
}

} // namespace lmms::gui
