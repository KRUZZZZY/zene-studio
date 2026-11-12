/*
 * ArrangerClipKeys.cpp - choosing and editing arranger clips from the keyboard
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

#include "ArrangerClipKeys.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <QCoreApplication>
#include <QKeyEvent>

#include "AccessibleWidgets.h"
#include "Clip.h"
#include "ClipGroupEdits.h"
#include "ClipView.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "FolderSummary.h"
#include "GuiApplication.h"
#include "MusicalTime.h"
#include "ShellSelection.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"
#include "TrackFolder.h"

namespace lmms::gui::arrangerkeys
{

namespace
{

QString tr(const char* text) { return QCoreApplication::translate("ArrangerClipKeys", text); }

Qt::KeyboardModifiers modsOf(const QKeyEvent* event) { return event->modifiers() & ~Qt::KeypadModifier; }

bool vertical(int key) { return key == Qt::Key_Up || key == Qt::Key_Down; }
bool horizontal(int key) { return key == Qt::Key_Left || key == Qt::Key_Right; }

//! The clip on @a track starting nearest @a tick, or nullptr when it has none.
Clip* nearestOn(const Track* track, tick_t tick)
{
	Clip* best = nullptr;
	for (Clip* clip : track->getClips())
	{
		const auto distance = std::abs(clip->startPosition().getTicks() - tick);
		if (best == nullptr || distance < std::abs(best->startPosition().getTicks() - tick)) { best = clip; }
	}
	return best;
}

//! The nearest clip on the first track with clips above (or below) @a from - the first track's, from none.
Clip* acrossTracks(const Track* from, tick_t tick, bool down)
{
	const Song::TrackList& tracks = Engine::getSong()->tracks();
	const auto here = std::find(tracks.begin(), tracks.end(), from);
	const int start = here == tracks.end() ? (down ? -1 : static_cast<int>(tracks.size())) : static_cast<int>(here - tracks.begin());
	for (int i = start + (down ? 1 : -1); i >= 0 && i < static_cast<int>(tracks.size()); i += down ? 1 : -1)
	{
		if (Clip* found = nearestOn(tracks[static_cast<std::size_t>(i)], tick)) { return found; }
	}
	return nullptr;
}

void chooseAcross(SongEditor* editor, bool down)
{
	const Clip* current = ShellSelection::instance()->clip();
	const tick_t tick = current != nullptr ? current->startPosition().getTicks()
		: Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks();
	Clip* next = acrossTracks(current != nullptr ? current->getTrack() : nullptr, tick, down);
	if (next == nullptr)
	{
		a11y::announceStatus(editor, down ? tr("No clip below") : tr("No clip above"));
		return;
	}
	if (selectClip(editor, next)) { a11y::announceStatus(editor, describeClip(next)); }
}

void resizeSelected(SongEditor* editor, bool longer)
{
	Clip* clip = ShellSelection::instance()->clip();
	if (clip == nullptr)
	{
		a11y::announceTransition(editor, a11y::Transition::Refused, tr("no clip is selected"));
		return;
	}
	const int snap = std::max(1, static_cast<int>(std::lround(editor->getSnapSize() * TimePos::ticksPerBar())));
	const int length = std::max(1, static_cast<int>(clip->length().getTicks()) + (longer ? snap : -snap));
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("clip.resize"),
		{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("length"), length}});
	if (!result.ok) { a11y::announceTransition(editor, a11y::Transition::Refused, result.errorMessage); }
	else { a11y::announceStatus(editor, describeClip(clip)); }
}

int snapOf(const SongEditor* editor)
{
	return std::max(1, static_cast<int>(std::lround(editor->getSnapSize() * TimePos::ticksPerBar())));
}

//! One group edit over the selected summary clips and the arranger's selected clips, its outcome read out.
void editSpans(SongEditor* editor, const QString& command, QJsonObject args)
{
	args.insert(QStringLiteral("clips"), selectedClipIds());
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (!result.ok) { a11y::announceTransition(editor, a11y::Transition::Refused, result.errorMessage); }
	else { a11y::announceStatus(editor, tr("Summary clips edited")); }
}

tick_t playTick() { return Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks(); }

//! Which summary clip Ctrl+Alt+G means: the one holding the selected clip, else the first at or after the play position.
bool spanInFocus(TrackFolder** folder, tick_t* tick)
{
	const Clip* clip = ShellSelection::instance()->clip();
	if (clip != nullptr && clip->getTrack()->parentFolder() != nullptr)
	{
		*folder = clip->getTrack()->parentFolder();
		*tick = clip->startPosition().getTicks();
		return true;
	}
	for (Track* track : Engine::getSong()->tracks())
	{
		auto* each = dynamic_cast<TrackFolder*>(track);
		if (each == nullptr) { continue; }
		for (const FolderSpan& span : folderSummary(*each))
		{
			if (span.end <= playTick()) { continue; }
			*folder = each;
			*tick = std::max(span.start, playTick());
			return true;
		}
	}
	return false;
}

void chooseSpan(SongEditor* editor, bool add)
{
	TrackFolder* folder = nullptr;
	tick_t tick = 0;
	if (!spanInFocus(&folder, &tick))
	{
		a11y::announceStatus(editor, tr("No summary clip here"));
		return;
	}
	ControlRegistry::instance()->invoke(QStringLiteral("track.folder_span_select"), {{QStringLiteral("folder"), control::trackIdOf(folder)},
		{QStringLiteral("at"), static_cast<int>(tick)}, {QStringLiteral("add"), add}});
	a11y::announceStatus(editor, tr("Summary clip of %1 selected").arg(folder->name()));
}

//! Which command a Ctrl+Alt chord means over the selected summary clips.
bool chordEdit(int key, bool shift, tick_t snap, QString* command, QJsonObject* args)
{
	const int delta = static_cast<int>(snap);
	if (horizontal(key))
	{
		*command = QStringLiteral("track.folder_span_resize");
		*args = {{QStringLiteral("edge"), shift ? QStringLiteral("start") : QStringLiteral("end")}, {QStringLiteral("delta"), key == Qt::Key_Right ? delta : -delta}};
		return true;
	}
	if (!shift && key == Qt::Key_K)
	{
		*command = QStringLiteral("track.folder_span_split");
		*args = {{QStringLiteral("at"), static_cast<int>(playTick())}};
		return true;
	}
	if (!shift && (key == Qt::Key_PageUp || key == Qt::Key_PageDown))
	{
		*command = QStringLiteral("track.folder_span_move");
		*args = {{QStringLiteral("delta"), key == Qt::Key_PageDown ? delta : -delta}};
		return true;
	}
	return false;
}

//! The keys that mean something while a summary clip is selected; each edits it, whichever clip is selected too.
bool editSelectedSpans(SongEditor* editor, int key, Qt::KeyboardModifiers mods)
{
	if (mods == Qt::NoModifier && key == Qt::Key_Escape)
	{
		ControlRegistry::instance()->invoke(QStringLiteral("track.folder_span_select"));
		return false;  // the arranger's own Escape still clears its clips
	}
	if (mods == Qt::NoModifier && (key == Qt::Key_Delete || key == Qt::Key_Backspace))
	{
		editSpans(editor, QStringLiteral("track.folder_span_delete"), {});
		return true;
	}
	QString command;
	QJsonObject args;
	if ((mods & ~Qt::ShiftModifier) != (Qt::ControlModifier | Qt::AltModifier)
		|| !chordEdit(key, (mods & Qt::ShiftModifier) != 0, snapOf(editor), &command, &args)) { return false; }
	editSpans(editor, command, args);
	return true;
}

//! The summary-clip keys; false for any other key, which goes on as before.
bool handleSpanKeys(SongEditor* editor, const QKeyEvent* event)
{
	const Qt::KeyboardModifiers mods = modsOf(event);
	const Qt::KeyboardModifiers chord = Qt::ControlModifier | Qt::AltModifier;
	if (event->key() == Qt::Key_G && (mods == chord || mods == (chord | Qt::ShiftModifier)))
	{
		chooseSpan(editor, mods != chord);
		return true;
	}
	return !FolderSpanSelection::instance().isEmpty() && editSelectedSpans(editor, event->key(), mods);
}

} // namespace

bool claims(const QKeyEvent* event)
{
	const Qt::KeyboardModifiers mods = modsOf(event);
	const int key = event->key();
	if (vertical(key)) { return mods == (Qt::ShiftModifier | Qt::AltModifier); }
	if (horizontal(key)) { return mods == (Qt::ControlModifier | Qt::AltModifier); }
	return key == Qt::Key_L && mods == (Qt::ControlModifier | Qt::ShiftModifier);
}

bool handle(SongEditor* editor, const QKeyEvent* event)
{
	if (editor == nullptr || Engine::getSong() == nullptr) { return false; }
	if (handleSpanKeys(editor, event)) { return true; }
	if (!claims(event)) { return false; }
	const int key = event->key();
	if (vertical(key)) { chooseAcross(editor, key == Qt::Key_Down); }
	else if (horizontal(key)) { resizeSelected(editor, key == Qt::Key_Right); }
	else { ControlRegistry::instance()->invoke(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("clips")}}); }
	return true;
}

bool selectClip(SongEditor* editor, Clip* clip)
{
	if (clip == nullptr) { return false; }
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("window.select"),
		{{QStringLiteral("clip"), control::clipIdOf(clip)}});
	if (!result.ok)
	{
		a11y::announceTransition(editor, a11y::Transition::Refused, result.errorMessage);
		return false;
	}
	if (editor != nullptr)
	{
		// The arranger's own selection follows, so Delete, copy and the drag tools act on the clip the keys chose.
		editor->selectAllClips(false);
		for (ClipView* view : editor->findChildren<ClipView*>())
		{
			if (view->getClip() == clip) { view->setSelected(true); }
		}
	}
	return true;
}

QJsonArray selectedClipIds()
{
	QJsonArray ids;
	if (getGUI() == nullptr || getGUI()->songEditor() == nullptr) { return ids; }
	for (selectableObject* selected : getGUI()->songEditor()->m_editor->selectedObjects())
	{
		if (auto* view = dynamic_cast<ClipView*>(selected)) { ids.append(control::clipIdOf(view->getClip())); }
	}
	return ids;
}

QString describeClip(const Clip* clip)
{
	const QString name = clip->name().isEmpty() ? tr("Unnamed clip") : clip->name();
	const QString track = clip->getTrack() != nullptr ? clip->getTrack()->name() : QString();
	const auto start = static_cast<tick_t>(clip->startPosition().getTicks());
	return tr("%1, on %2, %3, %4 long").arg(name, track, musicaltime::positionText(start),
		musicaltime::lengthText(static_cast<tick_t>(clip->length().getTicks()), start));
}

} // namespace lmms::gui::arrangerkeys
