/*
 * NoteEditor.cpp - the 0.5.0 note editor: the reworked piano roll in the shell's detail panel
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

#include "NoteEditor.h"

#include <algorithm>
#include <cstdio>

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QScrollBar>
#include <QVBoxLayout>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlScaleShared.h"
#include "ControlVocabulary.h"
#include "ControllerLaneView.h"
#include "DesignTokens.h"
#include "InstrumentTrack.h"
#include "ItemList.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditorParts.h"
#include "Piano.h"
#include "ShellChrome.h"
#include "ShellSelection.h"
#include "Engine.h"
#include "Song.h"
#include "TrackKeyNames.h"
#include <QTimer>

namespace lmms::gui
{

namespace
{

//! The header's title: the clip's name, elided in the middle to the label's width, with the
//! whole name and its track in the tooltip.
void showClipTitle(QLabel* label, const MidiClip* clip)
{
	const QString name = clip == nullptr ? NoteEditor::tr("No clip")
		: clip->name().isEmpty() ? NoteEditor::tr("Untitled clip") : clip->name();
	const Track* track = clip != nullptr ? clip->getTrack() : nullptr;
	label->setText(label->fontMetrics().elidedText(name, Qt::ElideMiddle, label->maximumWidth()));
	label->setToolTip(track != nullptr ? NoteEditor::tr("%1 (track %2)").arg(name, track->name()) : name);
}

} // namespace

NoteEditor::NoteEditor(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("noteEditor"));
	setFocusPolicy(Qt::StrongFocus);
	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);
	outer->addWidget(buildToolbar());
	buildBody();
	connect(this, &NoteEditor::stateChanged, this, [this]() {
		syncScrollBars();
		for (QWidget* part : {static_cast<QWidget*>(m_ruler), static_cast<QWidget*>(m_keyboard),
			static_cast<QWidget*>(m_canvas), static_cast<QWidget*>(m_lanes), static_cast<QWidget*>(m_ccLane)})
		{
			part->update();
		}
	});
	setClip(nullptr);
	// The playhead: the song position inside the clip, refreshed at 30 Hz while the song plays.
	// The same tick follows a scale changed elsewhere (the socket, a script, undo).
	m_playheadTimer = new QTimer(this);
	m_playheadTimer->setInterval(33);
	connect(m_playheadTimer, &QTimer::timeout, this, [this]() {
		updatePlayhead(false);
		followScale();
	});
	m_playheadTimer->start();
}

void NoteEditor::updatePlayhead(bool always)
{
	const Song* song = Engine::getSong();
	if (m_state.clip == nullptr || song == nullptr || (!always && !song->isPlaying()))
	{
		if (!always) { showPlayhead(-1); }
		return;
	}
	const int tick = song->getPlayPos(Song::PlayMode::Song).getTicks() - m_state.clip->startPosition().getTicks();
	showPlayhead(tick >= 0 && tick <= m_state.clip->length().getTicks() ? tick : -1);
}

void NoteEditor::showPlayhead(int tick)
{
	if (tick == m_canvas->playhead()) { return; }
	m_canvas->setPlayhead(tick);
	// 0.5.0 theme 11: the lanes under the notes draw the same line.
	m_lanes->update();
	m_ccLane->update();
}

int NoteEditor::playheadX() const
{
	const int tick = m_canvas->playhead();
	return tick < 0 ? -1 : static_cast<int>(m_state.grid.xOfTick(tick));
}

void NoteEditor::followScale()
{
	const control::ScaleContext& context = control::scaleContext();
	if (context.root != m_scaleRoot || context.scale != m_scaleSeen) { readScale(); }
}

NoteEditor::~NoteEditor()
{
	auditionRelease(m_auditionKey);  // closing the editor mid-audition must not leave the note sounding
}

void NoteEditor::buildBody()
{
	auto* body = new QWidget(this);
	auto* grid = new QGridLayout(body);
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setSpacing(0);
	m_ruler = new NoteRuler(this);
	m_keyboard = new NoteKeyboardView(this);
	m_canvas = new NoteCanvas(this);
	m_lanes = new NoteLaneView(this);
	m_ccLane = new ControllerLaneView(this);
	m_ccLane->hide();
	m_hScroll = new QScrollBar(Qt::Horizontal, this);
	m_vScroll = new QScrollBar(Qt::Vertical, this);
	m_hScroll->setAccessibleName(tr("Scroll in time"));
	m_vScroll->setAccessibleName(tr("Scroll in pitch"));
	grid->addWidget(m_ruler, 0, 1);
	grid->addWidget(m_keyboard, 1, 0);
	grid->addWidget(m_canvas, 1, 1);
	grid->addWidget(m_vScroll, 1, 2);
	grid->addWidget(m_laneBox, 2, 0, 1, 1, Qt::AlignTop);
	// Theme 10: under the lane chooser while it shows a controller curve, that lane's points as an editable list.
	ShellButton* laneList = itemlists::listButton(QStringLiteral("controller"), this, body);
	laneList->hide();
	connect(this, &NoteEditor::stateChanged, laneList, [this, laneList]() { laneList->setVisible(!m_state.controllerLane.isEmpty()); });
	grid->addWidget(laneList, 2, 0, 1, 1, Qt::AlignBottom | Qt::AlignLeft);
	grid->addWidget(m_lanes, 2, 1);
	grid->addWidget(m_ccLane, 2, 1);
	grid->addWidget(m_hScroll, 3, 1);
	grid->setColumnStretch(1, 1);
	grid->setRowStretch(1, 1);
	m_keyboard->setFixedWidth(m_keyboard->sizeHint().width());
	m_ruler->setFixedHeight(m_ruler->sizeHint().height());
	m_lanes->setFixedHeight(m_lanes->sizeHint().height());
	m_ccLane->setFixedHeight(m_lanes->sizeHint().height());
	layout()->addWidget(body);
	connect(m_hScroll, &QScrollBar::valueChanged, this, [this](int v) {
		if (m_syncing) { return; }
		m_state.grid.setScrollTicks(v);
		emit stateChanged();
	});
	connect(m_vScroll, &QScrollBar::valueChanged, this, [this](int v) {
		if (m_syncing) { return; }
		m_state.grid.setScrollY(v);
		emit stateChanged();
	});
}

QString NoteEditor::clipId() const { return control::clipIdOf(m_state.clip.data()); }

void NoteEditor::setClip(MidiClip* clip)
{
	if (clip != m_state.clip) { cancelGestures(); }
	if (m_state.clip != nullptr) { disconnect(m_state.clip, nullptr, this, nullptr); }
	m_state.clip = clip;
	if (clip != nullptr)
	{
		// The clip's own change signal - which every note command and every piano-roll edit
		// emits - repaints the editor, so an agent's edit on the socket shows at once.
		connect(clip, &MidiClip::dataChanged, this, &NoteEditor::refresh);
		connect(clip, &QObject::destroyed, this, &NoteEditor::onClipDestroyed);
		// The clip being edited is the clip the Inspector and the Device panel follow.
		ShellSelection::instance()->setClip(clip);
	}
	readScale();
	refresh();
	// Fit the clip once the note area has its real size: a panel that is parked or not yet laid
	// out has none, and a fit computed then scrolls to the wrong keys.
	m_pendingFit = clip != nullptr;
	fitIfPending();
	emit clipChanged();
}

void NoteEditor::onClipDestroyed()
{
	// The QPointer is already null; drop everything that read the clip.
	cancelGestures();
	m_state.clip = nullptr;
	refresh();
	emit clipChanged();
}

void NoteEditor::cancelGestures()
{
	auditionRelease(m_auditionKey);
	m_canvas->cancelGesture();
	m_ruler->cancelDrag();
	m_lanes->cancelDrag();
	m_ccLane->cancelDrag();
}

void NoteEditor::refresh()
{
	std::vector<int> used;
	if (m_state.clip != nullptr)
	{
		for (const Note* note : m_state.clip->notes()) { used.push_back(note->key()); }
	}
	m_state.grid.setUsedKeys(used);
	std::vector<int> named;
	if (m_state.clip != nullptr && m_state.clip->getTrack() != nullptr)
	{
		for (const int key : TrackKeyNames::instance().names(m_state.clip->getTrack()->id()).keys()) { named.push_back(key); }
	}
	m_state.grid.setNamedKeys(named);
	showClipTitle(m_clipLabel, m_state.clip);  // a rename is a dataChanged too
	m_canvas->clipChanged();
	emit stateChanged();
}

QJsonObject NoteEditor::run(const QString& command, const QJsonObject& args)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (!result.ok)
	{
		std::fprintf(stderr, "NoteEditor: %s refused: %s\n", command.toUtf8().constData(),
			result.errorMessage.toUtf8().constData());
		announce(this, CommandCall{command, args, {}, {}, false}, result);  // the floating message every refusal gets
		return {};
	}
	return result.result;
}

void NoteEditor::setTool(NoteTool tool)
{
	m_state.tool = tool;
	for (QAction* action : m_tools->actions())
	{
		if (action->data().toInt() == static_cast<int>(tool)) { action->setChecked(true); }
	}
	emit stateChanged();
}

void NoteEditor::setGridTicks(int ticks)
{
	m_state.gridTicks = std::max(1, ticks);
	const int index = m_gridBox->findData(m_state.gridTicks);
	if (index >= 0 && index != m_gridBox->currentIndex()) { m_gridBox->setCurrentIndex(index); }
	emit stateChanged();
}

void NoteEditor::setFold(NoteFold fold)
{
	if (fold == NoteFold::Drums) { refresh(); }  // the track's names may have changed since the clip did
	m_state.grid.setFold(fold);
	const QList<QAction*> actions = m_folds->actions();
	for (QAction* a : actions) { a->setChecked(a->data().toInt() == static_cast<int>(fold)); }
	emit stateChanged();
}

void NoteEditor::setLane(NoteLane lane)
{
	m_state.lane = lane;
	const int index = m_laneBox->findData(static_cast<int>(lane));
	if (index >= 0 && index != m_laneBox->currentIndex()) { m_laneBox->setCurrentIndex(index); }
	emit stateChanged();
}

void NoteEditor::setGhosts(bool on)
{
	m_state.ghosts = on;
	emit stateChanged();
}

void NoteEditor::setNewNote(int length, int velocity)
{
	m_state.newLength = std::max(0, length);
	m_state.lastVelocity = std::clamp(velocity, 1, 200);
	emit stateChanged();
}

void NoteEditor::setAudioBehind(const QString& choice)
{
	m_state.audioBehind = choice;
	emit stateChanged();
}

void NoteEditor::setControllerLane(const QString& id)
{
	m_state.controllerLane = id;
	m_lanes->setVisible(id.isEmpty());
	m_ccLane->setVisible(!id.isEmpty());
	const int index = id.isEmpty() ? m_laneBox->findData(static_cast<int>(m_state.lane)) : m_laneBox->findData(id);
	if (index >= 0 && index != m_laneBox->currentIndex())
	{
		const QSignalBlocker quiet(m_laneBox);
		m_laneBox->setCurrentIndex(index);
	}
	emit stateChanged();
}

void NoteEditor::setColorMode(NoteColor mode)
{
	m_state.colorMode = mode;
	emit stateChanged();
}

void NoteEditor::setKeepInScale(bool on)
{
	m_state.keepInScale = on;
	emit stateChanged();
}

QString NoteEditor::keyLabel(int key) const
{
	const Track* track = m_state.clip != nullptr ? m_state.clip->getTrack() : nullptr;
	return track != nullptr ? TrackKeyNames::instance().name(track->id(), key) : QString();
}

void NoteEditor::setZoom(double pixelsPerTick)
{
	m_pendingFit = false;  // an explicit zoom wins over the deferred fit
	m_state.grid.setPixelsPerTick(pixelsPerTick);
	emit stateChanged();
}

void NoteEditor::zoomToFit()
{
	if (m_state.clip == nullptr) { return; }
	const int length = std::max<int>(DefaultTicksPerBar, m_state.clip->length().getTicks());
	m_state.grid.setPixelsPerTick(std::max(1, m_canvas->width() - 16) / static_cast<double>(length));
	m_state.grid.setScrollTicks(0);
	int lowest = 127;
	int highest = 0;
	for (const Note* note : m_state.clip->notes())
	{
		lowest = std::min(lowest, note->key());
		highest = std::max(highest, note->key());
	}
	m_state.grid.centreOnKey(lowest <= highest ? (lowest + highest) / 2 : DefaultKey);
	emit stateChanged();
}

void NoteEditor::syncScrollBars()
{
	m_syncing = true;
	const NoteGrid& g = m_state.grid;
	const int clipTicks = m_state.clip != nullptr ? static_cast<int>(m_state.clip->length().getTicks()) : 0;
	const int pageTicks = static_cast<int>(g.lastVisibleTick() - g.firstVisibleTick());
	m_hScroll->setRange(0, std::max(clipTicks + DefaultTicksPerBar * 4, static_cast<int>(g.scrollTicks())));
	m_hScroll->setPageStep(std::max(1, pageTicks));
	m_hScroll->setValue(static_cast<int>(g.scrollTicks()));
	m_vScroll->setRange(0, std::max(0, g.contentHeight() - g.viewportHeight()));
	m_vScroll->setPageStep(std::max(1, g.viewportHeight()));
	m_vScroll->setValue(g.scrollY());
	m_syncing = false;
}

void NoteEditor::auditionPress(int key, int velocity)
{
	if (m_state.clip == nullptr || m_state.clip->instrumentTrack() == nullptr || key < 0) { return; }
	// The piano model's key press is the path a MIDI keyboard takes into the instrument: a
	// queued, thread-safe note-on, never a direct touch of the audio thread's state.
	const int midiVelocity = std::clamp(velocity * 127 / 200, 1, 127);
	m_auditionPiano = m_state.clip->instrumentTrack()->pianoModel();
	m_auditionPiano->handleKeyPress(key, midiVelocity);
	m_auditionKey = key;
}

void NoteEditor::auditionRelease(int key)
{
	// The release goes to the instrument the press went to: the clip may have changed or gone since
	// (a null piano is a track that went with it, and its notes with it).
	if (key < 0 || m_auditionPiano == nullptr) { return; }
	m_auditionPiano->handleKeyRelease(key);
	if (m_auditionKey == key) { m_auditionKey = -1; m_auditionPiano = nullptr; }
}

void NoteEditor::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	fitIfPending();
	emit stateChanged();
}

void NoteEditor::fitIfPending()
{
	if (!m_pendingFit || m_canvas->height() < 60 || m_canvas->width() < 60) { return; }
	m_pendingFit = false;
	m_state.grid.setViewport(m_canvas->width(), m_canvas->height());
	zoomToFit();
}

QJsonObject NoteEditor::describe() const
{
	static const char* const tools[] = {"draw", "select", "slice", "erase", "audition", "paint", "mute"};
	static const char* const folds[] = {"none", "scale", "used", "drums"};
	static const char* const colors[] = {"velocity", "pitch", "track"};
	const NoteGrid& g = m_state.grid;
	return QJsonObject{
		{QStringLiteral("clip"), clipId()},
		{QStringLiteral("tool"), QLatin1String(tools[static_cast<int>(m_state.tool)])},
		{QStringLiteral("grid"), m_state.gridTicks},
		{QStringLiteral("fold"), QLatin1String(folds[static_cast<int>(g.fold())])},
		{QStringLiteral("rows"), g.rowCount()},
		{QStringLiteral("lane"), notelanes::spec(m_state.lane).id},
		{QStringLiteral("ghosts"), m_state.ghosts},
		{QStringLiteral("scale_highlight"), m_state.highlightScale},
		{QStringLiteral("scale"), m_state.scaleName},
		{QStringLiteral("color_mode"), QLatin1String(colors[static_cast<int>(m_state.colorMode)])},
		{QStringLiteral("keep_in_scale"), m_state.keepInScale},
		{QStringLiteral("controller_lane"), m_state.controllerLane},
		{QStringLiteral("new_length"), m_state.newLength},
		{QStringLiteral("new_velocity"), m_state.lastVelocity},
		{QStringLiteral("audio_behind"), m_state.audioBehind},
		{QStringLiteral("zoom"), g.pixelsPerTick()},
		{QStringLiteral("scroll_ticks"), g.scrollTicks()},
		{QStringLiteral("scroll_y"), g.scrollY()},
		{QStringLiteral("top_key"), g.keyAtY(0)},
		{QStringLiteral("visible_notes"), m_canvas->visibleRange().second - m_canvas->visibleRange().first},
	};
}

} // namespace lmms::gui
