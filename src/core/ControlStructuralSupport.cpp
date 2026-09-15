/*
 * ControlStructuralSupport.cpp - the inverse of a STRUCTURAL operation (Zene
 *                                Studio; SPEC A16 deliverable 5, task #664).
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
 */

/*
 * WHY THE DELETED TRACK COMES BACK WITH ITS CLIPS, AND WHY THAT IS THE WHOLE
 * POINT OF THIS FILE
 *
 * `TrackContainer::removeTrack` forgets a pointer; the DESTRUCTION path beside
 * it is what loses the music. `~Track` (src/core/Track.cpp) deletes every one of
 * the track's clips and only then calls `removeTrack(this)`, and `~Clip` takes
 * the notes with it. So a "track removed" journal checkpoint taken at the
 * container - the shape upstream left commented out in
 * `TrackContainerView::deleteTrackView`, `//m_tc->addJournalCheckPoint();` -
 * would restore a name, a mute flag and a colour over an empty clip list: the
 * track would come back and the MUSIC would not. That is the defect this file
 * exists to make impossible, and it is why the capture happens BEFORE the
 * delete, in the one place every deletion path can reach.
 *
 * The captured document is the track's own XML (`Track::saveState`), which
 * carries the clips and their notes because that is what the project file
 * carries - the same bytes, restored by the same call the project loader makes
 * (`Track::create(element, container)`). Nothing here re-implements a loader.
 *
 * TWO LIMITS, stated rather than hidden (both are in docs/KNOWN-LIMITATIONS.md):
 *
 *  1. `clip-<n>` is INDEX-DERIVED (include/ControlEdit.h): the ordinal of the
 *     clip in the WHOLE song's arrangement order. The clips themselves come back
 *     - with their notes, their positions and their lengths - but a clip that
 *     was `clip-3` before the delete can be `clip-2` after the undo, because the
 *     ids of the tracks before it moved. A client must therefore RE-READ the
 *     arrangement after an undo rather than reuse a clip id across one; that is
 *     the slice-2 stable-id contract of feature row 51, and it is the reason
 *     this lane's proof asserts on the CLIPS' CONTENT read back by id, not on an
 *     id it cached before the delete.
 *  2. The track's own `trk-<n>` DOES survive: it is persisted as an `id`
 *     attribute on the track element (SPEC-stable-ids.md 3.1) and
 *     `Track::loadTrack` takes it back, so the restored track is the same
 *     addressable object and a cached trk-7 still names it after the undo.
 */

#include <memory>

#include <QDomDocument>
#include <QDomElement>

#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Plugin.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms
{
namespace control
{

namespace
{

//! The element a captured device document wraps its body in; written by
//! controlEffectStateXml(), read back here. One name, two files.
const QString StateRootElement = QStringLiteral("zenepluginstate");

//! The sub-plugin key element controlEffectStateXml() appends to the body.
const QString KeyElement = QStringLiteral("key");

/*! True while a recorded structural step is applying a reorder.
 *
 * `TrackContainer::moveTrack` journals every reorder it performs - that is what
 * makes the product's own drag undoable - so a replay that went through it
 * would push a NEW step for every unwind. The stack would grow while it
 * unwinds, `control.undo` would then undo its own replay, and the depth a client
 * reads would be nonsense. The guard is set for the duration of
 * moveTrackToIndex() and nothing else sets it.
 *
 * UI thread only, like every helper here: a reorder is a control-thread
 * operation (it takes the audio engine's model-change mutex itself).
 */
bool g_structuralReplay = false;

//! RAII: holds the flag for exactly one call, including on an exception path.
struct StructuralReplayGuard
{
	StructuralReplayGuard() { g_structuralReplay = true; }
	~StructuralReplayGuard() { g_structuralReplay = false; }
};

/*! The track's XML, or an empty string when it could not be captured.
 *
 * Bounded at StructuralSnapshotLimit: a track whose serialized state is larger
 * than the bound is REFUSED rather than captured truncated, because the restore
 * would then rebuild a corrupt track.
 */
QString captureTrack(const Track* track)
{
	QString xml;
	if (!captureTrackXml(track, &xml, StructuralSnapshotLimit)) { return QString(); }
	return xml;
}

} // namespace

bool structuralReplayInProgress() { return g_structuralReplay; }

int trackIndexIn(const TrackContainer* container, const Track* track)
{
	if (container == nullptr || track == nullptr) { return -1; }
	const TrackContainer::TrackList& list = container->tracks();
	for (int i = 0; i < static_cast<int>(list.size()); ++i)
	{
		if (list[i] == track) { return i; }
	}
	return -1;
}

bool moveTrackToIndex(Track* track, int index)
{
	Song* song = Engine::getSong();
	if (song == nullptr || track == nullptr || index < 0) { return false; }
	const int count = static_cast<int>(song->tracks().size());
	if (index >= count) { return false; }
	// A track that is not in THIS container is refused: the product's own
	// TrackContainer::moveTrack() erases with std::find and inserts
	// unconditionally, so a foreign pointer would be INSERTED into the song -
	// a duplicated track, from a command that only meant to reorder.
	if (trackIndexIn(song, track) < 0) { return false; }
	// The move is the replay, not a new edit: hold the flag so the journalling
	// inside TrackContainer::moveTrack does not record it a second time.
	StructuralReplayGuard guard;
	song->moveTrack(track, index);
	return true;
}

bool journalTrackMove(Track* track, int fromIndex, int toIndex)
{
	Song* song = Engine::getSong();
	if (song == nullptr || track == nullptr || fromIndex < 0 || toIndex < 0) { return false; }

	auto holder = std::make_shared<Track*>(track);
	addStructuralUndoStep(
		[holder, fromIndex]() {
			if (*holder != nullptr) { moveTrackToIndex(*holder, fromIndex); }
		},
		[holder, toIndex]() {
			if (*holder != nullptr) { moveTrackToIndex(*holder, toIndex); }
		},
		0);
	return true;
}

bool journalTrackRemoval(Track* track)
{
	Song* song = Engine::getSong();
	if (song == nullptr || track == nullptr) { return false; }

	const int index = trackIndexIn(song, track);
	if (index < 0) { return false; }

	const QString xml = captureTrack(track);
	// No capture, no inverse: a step that claimed to restore the track but held
	// no document would make control.undo take back an OLDER edit instead.
	if (xml.isEmpty()) { return false; }

	auto holder = std::make_shared<Track*>(track);
	addStructuralUndoStep(
		[song, xml, index, holder]() {
			*holder = restoreTrackFromXml(xml, song);
			// ... and where it was, not at the end: the restore path appends,
			// the same way the project loader does when it builds a song.
			if (*holder != nullptr) { moveTrackToIndex(*holder, index); }
		},
		[holder]() {
			if (*holder != nullptr) { removeTrack(*holder); *holder = nullptr; }
		},
		xml.size());
	return true;
}

//! The sub-plugin key a captured device document carries, if any.
//!
//! controlEffectStateXml() appends the key as an element of the body, so a
//! HOSTED device - whose descriptor name ("ladspaeffect", "lv2effect") is shared
//! by every plugin of its format - is re-instantiated as the SAME plugin and not
//! as an arbitrary sibling. False when the document carries no key with
//! attributes, i.e. for a built-in whose name alone identifies it.
bool deviceKeyFromState(const QDomElement& root,
	Plugin::Descriptor::SubPluginFeatures::Key* key)
{
	const QDomNodeList keys = root.elementsByTagName(KeyElement);
	if (keys.isEmpty() || !keys.item(0).isElement()) { return false; }
	*key = Plugin::Descriptor::SubPluginFeatures::Key(keys.item(0).toElement());
	return !key->attributes.isEmpty();
}

//! Puts \a effect, freshly appended to \a chain (its end), back at \a index by
//! walking it up through the chain's own reorder - the call the rack's move
//! buttons make. A no-op when it is already there or the index is out of range.
void placeEffectAt(EffectChain* chain, Effect* effect, int index)
{
	const int count = static_cast<int>(chain->effects().size());
	for (int i = count - 1; i > index && i > 0; --i) { chain->moveUp(effect); }
}

Effect* recreateEffectFromState(EffectChain* chain, const QString& stateXml, int index)
{
	if (chain == nullptr || stateXml.isEmpty()) { return nullptr; }

	QDomDocument document;
	if (!document.setContent(stateXml)) { return nullptr; }
	const QDomElement root = document.documentElement();
	if (root.tagName() != StateRootElement) { return nullptr; }

	const QString pluginName = root.attribute(QStringLiteral("plugin"));
	if (pluginName.isEmpty()) { return nullptr; }

	ControlResult error;
	// The check that keeps Plugin::instantiate()'s modal error box out of a
	// headless undo: a device this build cannot load must fail here, silently,
	// not park an unattended instance on a dialog.
	if (!controlPluginIsInstantiable(pluginName, &error)) { return nullptr; }

	Plugin::Descriptor::SubPluginFeatures::Key key;
	const bool hosted = deviceKeyFromState(root, &key);

	Effect* effect = Effect::instantiate(pluginName, chain, hosted ? &key : nullptr);
	if (effect == nullptr) { return nullptr; }

	// appendEffect() puts it at the END of the chain, which is where a project
	// load leaves it too; the recorded index is restored afterwards.
	chain->appendEffect(effect);
	placeEffectAt(chain, effect, index);

	// The settings last, and only onto a device that is already in the chain:
	// an unrestored (default) device is a wrong-sounding device, an absent one
	// is a missing device. A document the DEVICE refuses (it was written for
	// another plugin) leaves the fresh instance at its defaults - the structural
	// half of the inverse, which is the plugin itself, still holds.
	controlRestoreEffectState(effect, stateXml.toUtf8());
	return effect;
}

bool journalEffectRemoval(EffectChain* chain, Effect* effect, int index)
{
	if (chain == nullptr || effect == nullptr || index < 0) { return false; }

	const QString xml = controlEffectStateXml(effect);
	// Bounded like every other captured document: an unbounded payload on a
	// stack with a declared byte budget is a bound that does not hold. A device
	// whose state document is over the cap refuses the inverse; the command then
	// reports the removal as snapshot-only instead of claiming a reversibility
	// it did not record.
	if (xml.isEmpty() || xml.size() > StructuralSnapshotLimit) { return false; }

	auto holder = std::make_shared<Effect*>(effect);
	addStructuralUndoStep(
		[chain, xml, index, holder]() {
			*holder = recreateEffectFromState(chain, xml, index);
		},
		[chain, holder]() {
			if (*holder != nullptr)
			{
				chain->removeEffect(*holder);
				// Same lifetime as the rack's own delete and plugin.unload's:
				// the audio engine may still hold the pointer for the period in
				// flight.
				(*holder)->deleteLater();
				*holder = nullptr;
			}
		},
		xml.size());
	return true;
}

} // namespace control

} // namespace lmms
