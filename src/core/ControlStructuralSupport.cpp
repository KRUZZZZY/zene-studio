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

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

#include <QDomDocument>
#include <QDomElement>
#include <QJsonObject>
#include <QPointer>

#include "ControlDevicePlacement.h"
#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "Plugin.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"
#include "TrackFolder.h"

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

/*! The failure of the last device restore a structural step attempted, if any.
 *
 * The record the header declares: the journal's structural callbacks are `void`,
 * so a recreate that could not put the captured settings back has no other way to
 * reach control.undo. UI thread only, like every helper here (an undo is a
 * control-thread operation).
 */
std::optional<ControlResult> g_lastRestoreFailure;

//! Records a failed recreate and answers nullptr, so every failure exit of
//! recreateEffectFromState() records itself in one line and none can forget to.
Effect* restoreFailed(ControlErrorKind kind, const QString& message)
{
	g_lastRestoreFailure = ControlResult::failure(kind, message);
	return nullptr;
}

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

std::optional<ControlResult> takeStructuralRestoreFailure()
{
	const std::optional<ControlResult> failure = g_lastRestoreFailure;
	g_lastRestoreFailure.reset();
	return failure;
}

void clearStructuralRestoreFailure() { g_lastRestoreFailure.reset(); }

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

	// By id, never by pointer: a later removal and its undo rebuild the track as a new object (review W3-batchB #1).
	const int id = track->id();
	addStructuralUndoStep(
		[id, fromIndex]() {
			if (Track* live = trackWithId(id)) { moveTrackToIndex(live, fromIndex); }
		},
		[id, toIndex]() {
			if (Track* live = trackWithId(id)) { moveTrackToIndex(live, toIndex); }
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

	// The restored track's id, not its pointer (review W3-batchB #1): the redo finds it by id when it runs.
	auto id = std::make_shared<int>(-1);
	addStructuralUndoStep(
		[song, xml, index, id]() {
			Track* restored = restoreTrackFromXml(xml, song);
			*id = restored != nullptr ? restored->id() : -1;
			// ... and where it was, not at the end: the restore path appends,
			// the same way the project loader does when it builds a song.
			if (restored != nullptr) { moveTrackToIndex(restored, index); }
			// A restored routing folder's `mixch` names the channel it had when the
			// checkpoint was taken, and the delete this step reverses gave that
			// channel back - so the index is somebody else's now. The folder is
			// given one of its own rather than left adopting a strip it does not own.
			if (auto* folder = dynamic_cast<TrackFolder*>(restored))
			{
				// The saved index is stale by construction (see above), even when it
				// is still in range - it then names whichever strip came down into it.
				folder->setMixerChannel(-1);
				QString error;
				folder->ensureRoutingChannel(&error);
			}
		},
		[id]() {
			if (Track* live = trackWithId(*id)) { removeTrack(live); }
			*id = -1;
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

Effect* instantiateRecordedDevice(EffectChain* chain, const QString& stateXml)
{
	if (chain == nullptr || stateXml.isEmpty())
	{
		return restoreFailed(ControlErrorKind::InvalidArgs, QStringLiteral(
			"the recorded device document is missing, so the device cannot be re-created"));
	}

	QDomDocument document;
	if (!document.setContent(stateXml))
	{
		return restoreFailed(ControlErrorKind::InvalidArgs, QStringLiteral(
			"the recorded device document is not valid XML, so the device cannot be re-created"));
	}
	const QDomElement root = document.documentElement();
	if (root.tagName() != StateRootElement)
	{
		return restoreFailed(ControlErrorKind::InvalidArgs,
			QStringLiteral("the recorded device document is not a <%1> document (root <%2>), "
				"so the device cannot be re-created").arg(StateRootElement, root.tagName()));
	}

	const QString pluginName = root.attribute(QStringLiteral("plugin"));
	if (pluginName.isEmpty())
	{
		return restoreFailed(ControlErrorKind::InvalidArgs, QStringLiteral(
			"the recorded device document names no plugin, so the device cannot be re-created"));
	}

	ControlResult error;
	// The check that keeps Plugin::instantiate()'s modal error box out of a
	// headless undo: a device this build cannot load must fail here, silently,
	// not park an unattended instance on a dialog.
	if (!controlPluginIsInstantiable(pluginName, &error))
	{
		return restoreFailed(error.errorKind, error.errorMessage);
	}

	Plugin::Descriptor::SubPluginFeatures::Key key;
	const bool hosted = deviceKeyFromState(root, &key);

	// The plug-in load and the settings restore. Neither needs the device to be in
	// a chain (the restore reads the device's own parameters and state, and the
	// device's chain pointer is set at instantiation), and the device is not in
	// any chain yet, so the render thread never runs it: no audio change lock is
	// taken here.
	Effect* effect = Effect::instantiate(pluginName, chain, hosted ? &key : nullptr);
	if (effect == nullptr)
	{
		return restoreFailed(ControlErrorKind::Refused,
			QStringLiteral("the device '%1' could not be re-instantiated, so the removal was "
				"not undone").arg(pluginName));
	}
	// A document the DEVICE refuses (it was written for another plugin) is NOT a
	// half-restored device to keep: a device at its DEFAULTS is a wrong-sounding
	// device, and it must not be put in a chain at all. So the fresh instance is
	// deleted and the failure is recorded for control.undo to answer with -
	// reporting success here was the SPEC A16 lie BUG-CTL-1 records.
	const ControlResult restored = controlRestoreEffectState(effect, stateXml.toUtf8());
	if (!restored.ok)
	{
		effect->deleteLater();
		return restoreFailed(restored.errorKind, restored.errorMessage);
	}
	return effect;
}

void attachRecordedDevice(EffectChain* chain, Effect* effect, int index)
{
	// The only change to the chain, and the only part that needs the audio change
	// lock: appendEffect() puts the device at the END of the chain, which is where a
	// project load leaves it too, and the recorded index is restored afterwards.
	chain->appendEffect(effect);
	placeEffectAt(chain, effect, index);
}

Effect* recreateEffectFromState(EffectChain* chain, const QString& stateXml, int index)
{
	Effect* effect = instantiateRecordedDevice(chain, stateXml);
	if (effect == nullptr) { return nullptr; }
	// Callers of this composition may hold no change lock, so the attach takes
	// one; the load and the restore above have already run without it.
	const auto changes = Engine::audioEngine()->requestChangesGuard();
	attachRecordedDevice(chain, effect, index);
	return effect;
}

namespace
{

//! The plugin.* address of @a chain on a mixer channel: its insert chain, its post-fader inserts or rack chain n.
//! An empty object when no channel owns it.
QJsonObject channelChainArgs(const EffectChain* chain)
{
	Mixer* mixer = Engine::mixer();
	for (int i = 0; mixer != nullptr && i < static_cast<int>(mixer->numChannels()); ++i)
	{
		MixerChannel* channel = mixer->mixerChannel(i);
		const QString id = channelIdOf(channel);
		if (&channel->m_fxChain == chain) { return {{QStringLiteral("target"), id}}; }
		if (&channel->m_postInserts.chain() == chain) { return {{QStringLiteral("target"), postinserts::targetOf(id)}}; }
		for (int c = 1; c < channel->m_rack.chainCount(); ++c)
		{
			if (channel->m_rack.chain(c) == chain) { return {{QStringLiteral("target"), id}, {QStringLiteral("chain"), c}}; }
		}
	}
	return {};
}

//! The plugin.* address of @a chain - {target}, plus {chain} for a rack chain - or an empty object when no Song
//! track or mixer channel owns it.
QJsonObject chainArgs(const EffectChain* chain)
{
	for (Track* track : Engine::getSong()->tracks())
	{
		ControlTarget target;
		ControlResult ignored;
		const QString id = trackIdOf(track);
		if (resolveControlTarget(id, &target, &ignored) && target.chain == chain) { return {{QStringLiteral("target"), id}}; }
	}
	return channelChainArgs(chain);
}

} // namespace

//! @a chain, found again by its owner's address every time the result is called (nullptr once the owner is gone):
//! a track.remove and its undo rebuild the track and its chain as NEW objects under the same id. A chain no target
//! owns has no address, so it is GUARDED instead - null once it is deleted.
std::function<EffectChain*()> recordedChainFinder(EffectChain* chain)
{
	const QJsonObject args = chainArgs(chain);
	if (args.isEmpty()) { return [guarded = QPointer<EffectChain>(chain)]() -> EffectChain* { return guarded.data(); }; }
	return [args]() -> EffectChain* {
		ControlTarget target;
		ControlResult ignored;
		return resolveChainTarget(args, &target, &ignored) ? target.chain : nullptr;
	};
}

namespace
{

QString pluginNameOf(const Effect* effect) { return QString::fromUtf8(effect->descriptor()->name); }

//! The device a recorded step made, on @a chain: @a made while it is still there, else the device at @a slot when it
//! is the same plugin. A rebuilt chain carries rebuilt devices (their fx ids renumber), so the slot is what still
//! names it. nullptr when neither does.
Effect* madeEffect(const EffectChain* chain, const QPointer<Effect>& made, int slot, const QString& plugin)
{
	if (chain == nullptr) { return nullptr; }
	const std::vector<Effect*>& effects = chain->effects();
	if (!made.isNull() && std::find(effects.begin(), effects.end(), made.data()) != effects.end()) { return made.data(); }
	if (slot < 0 || slot >= static_cast<int>(effects.size())) { return nullptr; }
	Effect* atSlot = effects[static_cast<std::size_t>(slot)];
	return pluginNameOf(atSlot) == plugin ? atSlot : nullptr;
}

} // namespace

//! Take the device madeEffect() finds off @a chain. Same lifetime as plugin.unload's: the audio engine may still
//! hold the pointer for the period in flight, so it is deleted later.
void removeRecordedEffect(EffectChain* chain, QPointer<Effect>& made, int slot, const QString& plugin)
{
	if (Effect* effect = madeEffect(chain, made, slot, plugin))
	{
		chain->removeEffect(effect);
		effect->deleteLater();
	}
	made = nullptr;
}

namespace
{

//! Suspends the journal for one scope. An undo or a redo writes no checkpoints
//! of its own: the step being run is the record, and a checkpoint pushed here
//! would sit on the undo stack above the step it reverses.
class JournalPause
{
public:
	JournalPause() : m_journal(Engine::projectJournal())
		, m_was(m_journal != nullptr && m_journal->isJournalling())
	{
		if (m_journal != nullptr) { m_journal->setJournalling(false); }
	}
	~JournalPause()
	{
		if (m_journal != nullptr) { m_journal->setJournalling(m_was); }
	}
	JournalPause(const JournalPause&) = delete;
	JournalPause& operator=(const JournalPause&) = delete;

private:
	ProjectJournal* m_journal;
	bool m_was;
};

} // namespace

void detachEffect(EffectChain* chain, Effect* effect)
{
	if (chain == nullptr) { return; }
	// Both branches of the caller's decision take the same pause: the flag write
	// of an emptying removal is never a checkpoint of its own.
	JournalPause quiet;
	chain->removeEffect(effect);
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

	// No pointer is kept (see journalEffectLoad in the header): the chain by its owner's address, the device by its slot.
	// The enabled flag is part of the record: appending the device switches the
	// chain on, so the undo puts back the value it had when the device was taken
	// off. Both closures run with the journal paused, so neither pushes a
	// checkpoint of its own.
	const bool wasEnabled = chain->isEnabled();
	const auto chainOf = recordedChainFinder(chain);
	const QString plugin = pluginNameOf(effect);
	auto made = std::make_shared<QPointer<Effect>>();
	addStructuralUndoStep(
		[chainOf, xml, index, made, wasEnabled]() {
			JournalPause quiet;
			EffectChain* live = chainOf();
			if (live == nullptr) { return; }
			// The load and the settings restore first, with NO change lock held: the
			// render thread takes that lock every period, so work held under it stalls
			// rendering for its whole duration (the realtime rule). Only the append,
			// the placement and the flag restore are one model change, made under a
			// single bracket (the lock is recursive, so appendEffect's own lock nests).
			// Nothing is touched when no device was built.
			Effect* device = instantiateRecordedDevice(live, xml);
			if (device == nullptr) { *made = nullptr; return; }
			const auto changes = Engine::audioEngine()->requestChangesGuard();
			attachRecordedDevice(live, device, index);
			*made = device;
			live->setEnabledUnjournalled(wasEnabled);
		},
		[chainOf, made, index, plugin]() {
			JournalPause quiet;
			removeRecordedEffect(chainOf(), *made, index, plugin);
		},
		xml.size());
	return true;
}

void journalEffectLoad(EffectChain* chain, Effect* effect, const ControlDeviceEntry& entry, int slot)
{
	const auto chainOf = recordedChainFinder(chain);
	const QString plugin = pluginNameOf(effect);
	auto made = std::make_shared<QPointer<Effect>>(effect);
	addUndoStep(
		[chainOf, made, slot, plugin]() { removeRecordedEffect(chainOf(), *made, slot, plugin); },
		[chainOf, made, entry, slot]() {
			EffectChain* live = chainOf();
			ControlResult ignored;
			*made = live != nullptr ? controlInstantiateDevice(entry, live, &ignored) : nullptr;
			if (!made->isNull()) { placeEffect(live, made->data(), slot); }
		});
}

} // namespace control

} // namespace lmms
