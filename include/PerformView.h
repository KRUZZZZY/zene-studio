/*
 * PerformView.h - the launcher's live show window: big scene buttons, the setlist, transport and tempo
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

#ifndef LMMS_GUI_PERFORM_VIEW_H
#define LMMS_GUI_PERFORM_VIEW_H

#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QWidget>

#include "lmms_export.h"

class QLabel;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace lmms
{
struct ControlResult;
}

namespace lmms::gui
{

/*! 0.5.0 L02 (WF-071): perform mode - a window for playing live, read across a stage. It shows the setlist's song
 *  and the next one, the transport and the tempo, the scene playing now and the scene Enter launches next, one big
 *  button per scene, what each column plays, and a large Stop All.
 *
 *  REGISTRY-FIRST, as the clip-launch grid is: it keeps no state of its own beyond which scene is selected, and every
 *  press is a command - session.launch_scene, session.stop_all, transport.play / stop, transport.set_tempo (one undo
 *  step), session.setlist_go (asking about unsaved work first, as the grid's Next Song does) and session.bind_learn.
 *  Every refresh is drawn from session.get_state, transport.get_state and session.setlist, so what it shows is what
 *  an agent reading the socket sees.
 *
 *  KEYS, while the window is active: 1-9 launch scenes 1-9; Up and Down select a scene; Enter launches the selected
 *  scene and selects the one after it; Space starts or stops the transport; Escape stops every clip; Page Down and
 *  Page Up step to the next or previous song. They are this window's built-in keys. Since Q050-055.1 stage 1 the
 *  shortcut map also has a `perform` context (PanelShortcuts.h): a key bound there to Stop All, Start/Stop Transport
 *  or Launch Scene 1-2 runs that command instead of the built-in table's meaning, because the router claims it
 *  through ShortcutOverride while Perform holds the focus. MIDI: with Learn MIDI on, the next scene button (or Stop All) pressed binds
 *  the next note the connected controller plays to it, through session.bind_learn - the launcher's existing
 *  controller bindings, which launch a scene or stop everything.
 *
 *  Opened by window.toggle 'perform' (View > Perform Mode, Launcher > Perform Mode). UI thread only. */
class LMMS_EXPORT PerformView : public QWidget
{
	Q_OBJECT
public:
	explicit PerformView(QWidget* parent = nullptr);

	//! The one perform window, parented to the main window; nullptr when there is none and @a create is false.
	static PerformView* instance(bool create = true);
	//! window.toggle's 'perform': show and raise the window, or hide it while it shows.
	static void toggle();

	//! Re-reads the session and the transport and redraws, and about once a second the setlist; the timer calls it.
	void refresh();
	//! Re-reads the setlist (session.setlist) now: on show, after a song step, and for tests.
	void refreshSetlist();

	//! What a test (and the agent-surface sweep) presses and reads.
	QToolButton* sceneButton(int scene) const;
	int sceneButtonCount() const { return static_cast<int>(m_sceneButtons.size()); }
	QToolButton* stopAllButton() const { return m_stopAll; }
	QToolButton* playButton() const { return m_play; }
	QToolButton* stopButton() const { return m_stop; }
	QToolButton* tempoUpButton() const { return m_tempoUp; }
	QToolButton* tempoDownButton() const { return m_tempoDown; }
	QToolButton* nextSongButton() const { return m_nextSong; }
	QToolButton* previousSongButton() const { return m_previousSong; }
	QToolButton* learnButton() const { return m_learn; }
	QString songText() const;
	QString nextSongText() const;
	QString playingText() const;
	QString nextSceneText() const;
	QString tempoText() const;
	QString positionText() const;
	QString columnText(int track) const;
	QString statusLine() const;
	int selectedScene() const { return m_selected; }

	//! The keys above; true when @a key is one of them (it was acted on).
	bool performKey(int key);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;

private:
	void buildHeader(QVBoxLayout* outer);
	void buildTransport(QVBoxLayout* outer);
	QToolButton* makeButton(const QString& text, const QString& command, int scale);
	void rebuildScenes(int scenes);
	void rebuildColumns(int tracks);
	void readSession(const QJsonObject& state);
	void readNames(const QJsonArray& cells);
	void readColumns(const QJsonArray& columns);
	void readTransport();

	void paintScenes(const QJsonArray& sceneStates);
	void paintColumns();
	QString sceneLabel(int scene) const;

	void pressScene(int scene);
	void pressStopAll();
	void launchSelected();
	void select(int scene);
	void moveSelection(int step);
	void toggleTransport();
	void stepTempo(int step);
	void stepSong(const QString& to);
	void report(const ControlResult& result);

	QScrollArea* m_sceneScroll = nullptr;
	QVBoxLayout* m_scenesBox = nullptr;
	QVBoxLayout* m_columnsBox = nullptr;
	QLabel* m_song = nullptr;
	QLabel* m_nextSongLabel = nullptr;
	QLabel* m_playing = nullptr;
	QLabel* m_next = nullptr;
	QLabel* m_tempo = nullptr;
	QLabel* m_position = nullptr;
	QLabel* m_status = nullptr;
	QToolButton* m_previousSong = nullptr;
	QToolButton* m_nextSong = nullptr;
	QToolButton* m_play = nullptr;
	QToolButton* m_stop = nullptr;
	QToolButton* m_tempoDown = nullptr;
	QToolButton* m_tempoUp = nullptr;
	QToolButton* m_learn = nullptr;
	QToolButton* m_stopAll = nullptr;
	std::vector<QToolButton*> m_sceneButtons;
	std::vector<QLabel*> m_columns;
	std::vector<QString> m_names;       //!< each cell's name (track * scenes + scene), empty for an empty cell
	std::vector<int> m_playingScene;    //!< per column, -1 when idle
	std::vector<QString> m_phases;      //!< per column: launching, playing, stopping or empty
	std::vector<QString> m_sceneNames;  //!< each scene's own name, empty when it has none
	QString m_readError;                //!< latest failed state read, until the next full refresh
	QString m_notice;                   //!< the last press's refusal, until one succeeds
	double m_bpm = 0.0;
	bool m_transportRunning = false;
	int m_tracks = -1;
	int m_scenes = -1;
	int m_selected = 0;
	int m_ticks = 0;
	int m_ticksPerBar = 192;
	int m_refreshes = 0;
	QTimer m_timer;
};

} // namespace lmms::gui

#endif // LMMS_GUI_PERFORM_VIEW_H
