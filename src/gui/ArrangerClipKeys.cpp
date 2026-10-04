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
#include "ClipView.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MusicalTime.h"
#include "ShellSelection.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"

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
	if (editor == nullptr || !claims(event) || Engine::getSong() == nullptr) { return false; }
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

QString describeClip(const Clip* clip)
{
	const QString name = clip->name().isEmpty() ? tr("Unnamed clip") : clip->name();
	const QString track = clip->getTrack() != nullptr ? clip->getTrack()->name() : QString();
	const auto start = static_cast<tick_t>(clip->startPosition().getTicks());
	return tr("%1, on %2, %3, %4 long").arg(name, track, musicaltime::positionText(start),
		musicaltime::lengthText(static_cast<tick_t>(clip->length().getTicks()), start));
}

} // namespace lmms::gui::arrangerkeys
