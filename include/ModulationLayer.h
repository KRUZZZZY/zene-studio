/*
 * ModulationLayer.h - the #602 modulation layer: a song-level set of LFO
 *                     modulators, each driving a SET of parameters by a
 *                     relative amount.
 *
 * What a modulator IS here: a timeline-locked low-frequency oscillator (a shape,
 * a rate in Hz, a phase and a polarity) plus a list of ROUTES. Each route binds
 * one existing AutomatableModel through the SAME address a rack macro target
 * uses - a mixer channel, a chain inside its rack, an effect inside that chain
 * and the parameter's display name - and carries a DEPTH.
 *
 * A depth is a signed fraction of the target's own min..max, never a value in
 * the parameter's units (the rack-macro lane's reason: recording the engine's
 * numbers pins the assignment to the range one build happened to report).
 *
 * The difference from a macro is what the fraction is applied to. A macro
 * REPLACES the parameter's value (an absolute write over a window). A modulator
 * ADDS to it:
 *
 *     written = clamp(base + depth * output * (max - min), min, max)
 *
 * so two parameters with different ranges and different current values move by
 * the same FRACTION of their own range - which is what makes the offset
 * independent of each parameter's own absolute value (the point of #602).
 *
 * Realtime contract (workspace rule 4). The layer is authored on the control
 * thread and read once per audio block by Song::processModulation(). The
 * hand-off is ModulationLayerPublisher, a seqlock - the same technique and the
 * same honest limit the tempo map's publisher documents (include/TempoMap.h):
 * the version bumps are the only atomics, so the value copy is a benign data
 * race by the letter of the C++ memory model, and it is sound here because
 * there is exactly ONE writer (the control thread) and it publishes only when
 * an edit lands, never per block. The reader copies a fixed-capacity value and
 * never allocates, locks or grows.
 *
 * The resolved write targets are QPointer<AutomatableModel> on the CONTROL
 * side (ModulationRuntime), deliberately: a target can be destroyed under the
 * audio thread (a device unloaded, a chain or channel removed, a project
 * opened), and a QPointer nulls itself in ~QObject. The audio thread must not
 * copy a QPointer, though - it shares a reference-counted block, so copying one
 * inside the seqlock while the control thread drops it touches freed memory,
 * and destroying the copy can free on the audio thread (2026-09-28 survey, G1).
 * What the audio thread copies is ModulationAudioView: raw pointers, each
 * paired with a per-slot token. Before a target tears down its value state,
 * the publisher closes that slot and waits on the control thread for existing
 * audio writes to release their atomic pins. The view remains plain bytes.
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

#ifndef LMMS_MODULATION_LAYER_H
#define LMMS_MODULATION_LAYER_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <QDomElement>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "LmmsTypes.h"
#include "ModulationFormula.h"
#include "ModulationVoices.h"
#include "lmms_export.h"

namespace lmms
{

class AutomatableModel;
class Instrument;

//! The fixed LFO vocabulary. A closed set because the layer persists a NAME.
enum class ModulationShape
{
	Sine,
	Triangle,
	Square,
	Saw,
	// 0.5.0 L10 (MX-117): random, smooth random, a step sequence and a retriggered envelope.
	Random,        //!< a new random value every cycle, held (sample and hold)
	SmoothRandom,  //!< random values every cycle, glided between
	Steps,         //!< the source's step values, one a cycle step
	Envelope,      //!< attack to the top over 'attack' of a cycle, then a decay - every cycle
	Formula,       //!< 0.5.0 L10 (MX-118): the modulator's own formula of t and x (ModulationFormula.h)
};

//! Wire name of a shape ("sine", "triangle", "square", "saw"), and its inverse.
LMMS_EXPORT QString modulationShapeName(ModulationShape shape);
//! False when @a name is not one of the shapes (nothing is guessed).
LMMS_EXPORT bool modulationShapeFromName(const QString& name, ModulationShape* shape);
//! Every shape's wire name, in enum order - the schema's enum.
LMMS_EXPORT const QStringList& modulationShapeNames();

/*! One route: an address into a mixer channel's rack, plus a depth.
 *
 *  The address is exactly a RackMacroTarget's address minus the window, so
 *  rack.macro_target_add and modulator.target_set name a parameter the same
 *  way and a reader can carry one intent between them.
 */
struct ModulationRoute
{
	int channel = 0;         //!< the mixer channel's persistent id (ch-<n>; SPEC-stable-ids.md slice 2)
	int chain = 0;           //!< chain inside that channel's rack (0 = its own)
	int effect = 0;          //!< effect inside that chain (fx-<n> order)
	QString parameter;       //!< the parameter's display name
	float depth = 0.0f;      //!< -1..1, a fraction of the target's own range
	/*! R1.3: evaluate this route INSIDE the block - the value is published as a ramp of
	 *  knots across the block and read per sample - instead of one value per block (the
	 *  zipper a fast LFO makes). Off by default; saved only when on. */
	bool perSample = false;
	/*! 0.5.0 MX-120: >= 0 makes the route address a parameter of THIS track's instrument (its persistent
	 *  id; 'parameter' is the display name, channel/chain/effect are unused) - the target a per-voice
	 *  modulator sends per-note values to. -1 (the default, and every route before 0.5.0): the rack address. */
	int track = -1;
};

//! The frames and rate of the block being modulated - what a per-sample route needs to
//! place its knots. A default block (0 frames) writes every route once, as before.
struct ModulationBlock
{
	f_cnt_t frames = 0;
	sample_rate_t sampleRate = 0;
};

//! The LFO one modulator runs.
struct ModulatorSource
{
	ModulationShape shape = ModulationShape::Sine;
	float rateHz = 1.0f;     //!< MinRateHz..MaxRateHz
	float phase = 0.0f;      //!< 0..1, where in its cycle the modulator starts
	bool unipolar = false;   //!< false: output -1..1 (bipolar); true: 0..1
	bool active = true;      //!< an inactive modulator drives nothing
	static constexpr int MaxSteps = 16;
	std::array<float, MaxSteps> steps{};  //!< Steps: the values -1..1, one per step (a fixed array: copied to the audio thread)
	int stepCount = 0;       //!< Steps: how many of them are used (0: the shape reads silence)
	float attack = 0.1f;     //!< Envelope: the rising share of a cycle, 0..1
	FormulaProgram formula{};  //!< Formula (MX-118): the compiled program (plain values; the text is Modulator::formula)
	//! MX-121: the track (its persistent id) whose notes restart this modulator's cycle; -1 runs it
	//! free on the timeline.
	int triggerTrack = -1;
	/*! MX-120: one cycle PER NOTE of the trigger track, each note's value sent to the instrument as a
	 *  per-note modulation where the target supports it; where it does not, the route follows the newest
	 *  note (the MX-121 behaviour) and the state reply says so. Needs a trigger track. */
	bool perVoice = false;
};

//! One named modulator: a source and the routes it drives.
struct Modulator
{
	QString name;
	ModulatorSource source;
	std::vector<ModulationRoute> routes;
	QString formula;  //!< MX-118: the formula's text, compiled into source.formula; saved with a Formula shape

	int routeCount() const { return static_cast<int>(routes.size()); }
};

/*! The authored layer: the value the control surface edits and the project
 *  file carries. Bounded (MaxModulators x MaxRoutesPerModulator) so that a
 *  project file can never make the layer unbounded.
 *
 *  Control-thread only. The audio thread reads ModulationRuntime instead.
 */
class LMMS_EXPORT ModulationLayer
{
public:
	static constexpr int MaxModulators = 16;
	static constexpr int MaxRoutesPerModulator = 8;
	static constexpr float MinRateHz = 0.01f;
	static constexpr float MaxRateHz = 20.0f;

	int modulatorCount() const { return static_cast<int>(m_modulators.size()); }
	//! nullptr for an out-of-range index, never UB.
	const Modulator* modulator(int index) const;
	Modulator* modulator(int index);

	//! Appends (or inserts at @a index) and returns the modulator's index, or
	//! -1 when the layer is full. @a source must be valid.
	int addModulator(const QString& name, const ModulatorSource& source);
	bool insertModulator(int index, const Modulator& modulator);
	bool removeModulator(int index);
	bool setSource(int index, const ModulatorSource& source);

	//! Appends a route to @a modulator, or -1 when it is full. The caller has
	//! already checked the address resolves (@see modulationTargetModel).
	int addRoute(int modulator, const ModulationRoute& route);
	bool insertRoute(int modulator, int route, const ModulationRoute& incoming);
	bool removeRoute(int modulator, int route);
	bool setDepth(int modulator, int route, float depth);

	//! Replaces every route's depth with 0. Used by the deactivate paths.
	void zeroDepths();

	bool validSource(const ModulatorSource& source, QString* why) const;
	//! The first route in the layer that names @a route's address, or nullptr.
	//! Used to refuse a duplicate binding (one parameter, one depth).
	const ModulationRoute* findRoute(const ModulationRoute& route) const;

	/*! Written ONLY when the layer is not empty, so a project that never used a
	 *  modulator re-saves exactly the bytes it has always had - the property
	 *  the release's reproducibility claim rests on (docs/MODULATION.md). */
	bool shouldPersist() const { return !m_modulators.empty(); }

	bool saveSettings(QDomDocument& doc, QDomElement& parent) const;
	//! Reads a <modulation-layer> element. The layer is left EMPTY when the
	//! element holds no validator-passing modulator (nothing is guessed).
	bool loadSettings(const QDomElement& element);

	void clear();

	/*! One modulator's LFO output at @a seconds, in -1..1 (bipolar) or 0..1
	 *  (unipolar). Pure arithmetic: allocation-free, and safe on the audio
	 *  thread. @a seconds is measured from tick 0 at the tempo map's own
	 *  rates, so the modulator follows the timeline rather than the wall. */
	static float outputAt(const ModulatorSource& source, double seconds);

private:
	std::vector<Modulator> m_modulators;
};

/*! What the audio thread reads: the sources, and one resolved ENTRY per route.
 *
 *  Fixed capacity and plain values, so snapshot() is a value copy with a
 *  version re-check - no allocation, no lock, no growth. The entry carries the
 *  target's min/max and the BASE value captured when the route was resolved,
 *  so the block's arithmetic is a multiply-add and two bounds checks.
 */
struct ModulationRuntime
{
	static constexpr int MaxEntries = ModulationLayer::MaxModulators
		* ModulationLayer::MaxRoutesPerModulator;

	/*! The LFO parameters the audio thread runs (ModulatorSource is a plain
	 *  value type - an enum, three floats and two bools - so the runtime can
	 *  carry it verbatim without a conversion or an allocation). */
	using Source = ModulatorSource;

	struct Entry
	{
		//! The model the block writes. Null when the target was destroyed or
		//! the address did not resolve - the audio thread skips it.
		QPointer<AutomatableModel> model;
		int modulator = -1;      //!< index into sources
		float depth = 0.0f;
		float base = 0.0f;       //!< captured when the route was resolved
		float minimum = 0.0f;
		float maximum = 0.0f;
		bool perSample = false;  //!< R1.3, the route's own flag
		//! MX-120: the instrument that takes this route's per-note values, and its handle for the
		//! parameter (Instrument::perNoteTarget); nullptr / -1 when it has no per-note path.
		Instrument* instrument = nullptr;
		std::int64_t noteTarget = -1;
	};

	std::array<Source, ModulationLayer::MaxModulators> sources{};
	int sourceCount = 0;
	std::array<Entry, MaxEntries> entries{};
	int entryCount = 0;

	bool active() const noexcept { return entryCount > 0; }
};

/*! The audio thread's copy of the runtime: the same sources and entries, as
 *  plain values. An entry's model may be written only after its slot's token
 *  and atomic writer gate (ModulationLayerPublisher::targetAlive) are checked.
 */
struct ModulationAudioView
{
	struct Entry
	{
		AutomatableModel* model = nullptr;
		std::uint32_t token = 0; //!< 0 = no target
		int modulator = -1;
		float depth = 0.0f;
		float base = 0.0f;
		float minimum = 0.0f;
		float maximum = 0.0f;
		bool perSample = false;
		Instrument* instrument = nullptr;  //!< MX-120, valid while the entry's token is alive
		std::int64_t noteTarget = -1;
	};

	std::array<ModulatorSource, ModulationLayer::MaxModulators> sources{};
	int sourceCount = 0;
	std::array<Entry, ModulationRuntime::MaxEntries> entries{};
	int entryCount = 0;

	bool active() const noexcept { return entryCount > 0; }
};
static_assert(std::is_trivially_copyable_v<ModulationAudioView>,
	"the audio thread's seqlock copy must be plain bytes");

//! One entry's written value for @a seconds: the multiply-add and the bounds
//! both block paths share.
inline float modulatedValue(const ModulatorSource& source, float base, float depth,
	float minimum, float maximum, double seconds)
{
	const float output = ModulationLayer::outputAt(source, seconds);
	return std::clamp(base + depth * output * (maximum - minimum), minimum, maximum);
}

/*! The layer plus the lock-free hand-off of it to the audio thread. Same shape
 *  and the same honest limits as TempoMapPublisher (include/TempoMap.h): one
 *  writer (the control thread), every mutation routed through edit() so a
 *  forgotten publish is impossible, and a reader that copies a fixed-size value
 *  and re-checks the version, retrying while a write is in progress but never
 *  blocking.
 */
class LMMS_EXPORT ModulationLayerPublisher
{
public:
	//! No modulator follows a track yet, and none has been triggered.
	ModulationLayerPublisher();
	//! Drops the destroyed() watches; they point into this object.
	~ModulationLayerPublisher();
	ModulationLayerPublisher(const ModulationLayerPublisher&) = delete;
	ModulationLayerPublisher& operator=(const ModulationLayerPublisher&) = delete;

	/*! One mutation of the layer, published the moment it lands. @a fn receives
	 *  the authored layer and the runtime to rebuild; it returns true when it
	 *  changed the layer. Callers that only clear stale write targets pass
	 *  false. */
	template <typename Fn>
	bool edit(Fn&& fn)
	{
		m_version.fetch_add(1, std::memory_order_acq_rel);
		const bool changed = fn(m_layer, m_runtime);
		publishView();
		m_version.fetch_add(1, std::memory_order_acq_rel);
		return changed;
	}

	//! The authored layer. Control thread only (it is the value being written);
	//! the audio thread uses snapshot().
	ModulationLayer& layer() noexcept { return m_layer; }
	const ModulationLayer& layer() const noexcept { return m_layer; }
	ModulationRuntime& runtime() noexcept { return m_runtime; }
	const ModulationRuntime& runtime() const noexcept { return m_runtime; }

	//! Whether the layer holds any modulator, as of the last edit. The audio
	//! thread's pay-nothing test (it must not read layer(), G2).
	bool hasLayer() const noexcept { return m_hasLayer.load(std::memory_order_acquire); }

	//! Whether @a slot's target is still the one @a token was issued for.
	bool targetAlive(int slot, std::uint32_t token) const noexcept
	{
		if (slot < 0 || slot >= ModulationRuntime::MaxEntries || token == 0) { return false; }
		return m_alive[static_cast<std::size_t>(slot)].load(std::memory_order_acquire) == token
			&& (m_targetWriters[static_cast<std::size_t>(slot)].load(std::memory_order_acquire)
				& targetClosingBit) == 0;
	}
	//! Audio thread: pin a target's value state until endTargetWrite(). No allocation or wait.
	bool beginTargetWrite(int slot, std::uint32_t token) const noexcept
	{
		if (!targetAlive(slot, token)) { return false; }
		auto& writers = m_targetWriters[static_cast<std::size_t>(slot)];
		std::uint32_t state = writers.load(std::memory_order_acquire);
		while ((state & targetClosingBit) == 0)
		{
			if ((state & targetWriterCountMask) == targetWriterCountMask) { return false; }
			if (writers.compare_exchange_weak(state, state + 1, std::memory_order_acq_rel,
				std::memory_order_acquire))
			{
				if (targetAlive(slot, token)) { return true; }
				writers.fetch_sub(1, std::memory_order_release);
				return false;
			}
		}
		return false;
	}
	//! Audio thread: release a pin after the target write.
	void endTargetWrite(int slot) const noexcept
	{
		if (slot >= 0 && slot < ModulationRuntime::MaxEntries)
		{
			m_targetWriters[static_cast<std::size_t>(slot)].fetch_sub(1, std::memory_order_release);
		}
	}

	/*! 0.5.0 L10 (MX-121): a note started on track @a trackId - audio thread (any worker), atomics
	 *  only. Every modulator that follows that track restarts its cycle at the current block. */
	void noteStarted(int trackId) noexcept;
	//! The seconds @a modulator has run since its last note at song time @a seconds: 0 before any
	//! note (it waits at the start of its cycle). For a modulator that follows no track, @a seconds.
	double sinceTrigger(int modulator, int triggerTrack, double seconds) const noexcept;
	//! The block's song time, which a note started during it is stamped with (audio thread).
	void setBlockSeconds(double seconds) const noexcept { m_blockSeconds.store(seconds, std::memory_order_relaxed); }

	/*! 0.5.0 MX-120: a note-on (@a on) or note-off reached track @a trackId's instrument on (@a channel,
	 *  @a key), @a offset frames into the block - audio thread, atomics only. A voice is kept only while a
	 *  per-voice modulator follows the track. */
	void voiceChanged(int trackId, bool on, int channel, int key, f_cnt_t offset) noexcept;
	//! The voices per-voice modulators run for (read by the block, and by the state reply).
	const ModulationVoices& voices() const noexcept { return m_voices; }
	//! The block's sample rate, for a note's frame offset (audio thread).
	void setBlockRate(sample_rate_t rate) const noexcept { m_blockRate.store(rate, std::memory_order_relaxed); }

	//! The audio view as of the last completed edit. Lock-free, allocation-free
	//! and bounded; safe to call from the audio thread.
	ModulationAudioView snapshot() const noexcept
	{
		ModulationAudioView copy;
		unsigned version = 0;
		do
		{
			version = m_version.load(std::memory_order_acquire);
			if ((version & 1u) == 0u) { copy = m_view; }  // odd: a write is in progress
			// The copy's reads complete before the version is read again.
			std::atomic_thread_fence(std::memory_order_acquire);
		}
		// An odd version retries too: `continue` used to reach this test with the
		// version unchanged and return the default, empty view mid-edit.
		while ((version & 1u) != 0u || m_version.load(std::memory_order_relaxed) != version);
		return copy;
	}

private:
	static constexpr std::uint32_t targetClosingBit = std::uint32_t{1} << 31;
	static constexpr std::uint32_t targetWriterCountMask = targetClosingBit - 1;
	//! Rebuilds m_view and the liveness watches from m_runtime (control thread,
	//! inside edit()'s write window).
	void publishView();
	//! MX-121: each modulator's trigger track from m_runtime (control thread, from publishView()).
	void publishTriggers();

	ModulationLayer m_layer{};
	ModulationRuntime m_runtime{};
	ModulationAudioView m_view{};
	std::array<std::atomic<std::uint32_t>, ModulationRuntime::MaxEntries> m_alive{};
	mutable std::array<std::atomic<std::uint32_t>, ModulationRuntime::MaxEntries> m_targetWriters{};
	std::array<QMetaObject::Connection, ModulationRuntime::MaxEntries> m_watch{};
	std::uint32_t m_nextToken = 0;
	std::atomic<bool> m_hasLayer{false};
	//! MX-121: each modulator's trigger track (-1: none) as of the last edit, the song time of its
	//! last note (-1: none yet), and the current block's song time; outside the seqlock, atomics.
	std::array<std::atomic<int>, ModulationLayer::MaxModulators> m_triggerTrack{};
	std::array<std::atomic<double>, ModulationLayer::MaxModulators> m_triggeredAt{};
	mutable std::atomic<double> m_blockSeconds{0.0};
	std::atomic<bool> m_anyTrigger{false};
	//! MX-120: each modulator's per-voice flag as of the last edit, and the voices they run for.
	std::array<std::atomic<bool>, ModulationLayer::MaxModulators> m_perVoice{};
	std::atomic<bool> m_anyPerVoice{false};
	ModulationVoices m_voices;
	mutable std::atomic<sample_rate_t> m_blockRate{0};
	std::atomic<unsigned> m_version{0};
};

/*! The model a route names, or nullptr with @a why set.
 *
 *  Reuses the rack lane's own resolver so a modulator route and a macro target
 *  cannot disagree about what a parameter name means. Defined in
 *  ModulationLayer.cpp; the caller is on the control thread, which is why this
 *  one is allowed to build a QString.
 */
LMMS_EXPORT AutomatableModel* modulationTargetModel(const ModulationRoute& route, QString* why);
//! 0.5.0 MX-120: the instrument a track route (ModulationRoute::track) addresses, or nullptr.
LMMS_EXPORT Instrument* modulationTargetInstrument(const ModulationRoute& route);

/*! Rebuilds @a runtime from @a layer (control thread): resolves every route,
 *  captures each target's min/max and the BASE value it currently has, and
 *  copies the sources. A route whose address no longer resolves, or whose
 *  parameter is not an AutomatableModel, is left out and counted in the
 *  runtime's entryCount - never guessed at.
 *
 *  \a restoreBases writes every target @a runtime currently holds back to its
 *  recorded base FIRST, then re-captures. That is the rule every layer edit
 *  follows, and it is why re-capturing is idempotent: the models are at their
 *  unmodulated values when the new base is read, so editing a modulator cannot
 *  bake a modulated value into the base. It also hands the parameter back to
 *  the user when a route is deleted or a modulator is switched off.
 */
LMMS_EXPORT void rebuildModulationRuntime(const ModulationLayer& layer,
	ModulationRuntime* runtime);
//! Restore-then-rebuild, in place. \a runtime is both the source of the bases
//! to restore and the destination of the new one, which is deliberate: the
//! layer's own runtime IS the record of what was written.
LMMS_EXPORT void rebuildModulationRuntimeRestoring(const ModulationLayer& layer,
	ModulationRuntime* runtime);

//! Writes every target @a runtime holds back to its recorded base, and leaves
//! the runtime alone. Song::stop() is the caller: a transport that stops must
//! hand the parameters back, but the layer keeps its resolved routes so the
//! next play starts from the same bases.
LMMS_EXPORT void restoreModulationBases(const ModulationRuntime& runtime);

/*! Writes @a model back to @a base with the automation flag (no journal
 *  checkpoint). The one write path, shared by the deactivate paths and so that
 *  there is a single place that decides how a modulation write is spelled. */
LMMS_EXPORT void writeModulationBase(AutomatableModel* model, float base);

//! The block's write set: every entry's value for @a seconds, applied to its
//! model. Allocation-free, lock-free, bounded.
//! A per-sample entry (R1.3) is written as a ramp across @a block; with the default
//! block every entry is written once, as before.
LMMS_EXPORT void applyModulationBlock(const ModulationRuntime& runtime, double seconds,
	ModulationBlock block = ModulationBlock{});
//! The audio thread's form: the same writes from @a view, skipping any entry
//! whose target @a publisher no longer holds alive.
LMMS_EXPORT void applyModulationBlock(const ModulationLayerPublisher& publisher,
	const ModulationAudioView& view, double seconds, ModulationBlock block = ModulationBlock{});
/*! R1.3: one entry's write for the block. A per-sample entry over a real block publishes
 *  AutomationRamp::MaxKnots knots spread across it (linear between them, per sample in
 *  AutomatableModel::valueBuffer) and writes the first as the scalar value; any other entry
 *  writes its one value, exactly the pre-R1.3 write. Allocation-free. */
LMMS_EXPORT void writeModulatedEntry(AutomatableModel* model, const ModulatorSource& source, float base,
	float depth, float minimum, float maximum, double seconds, bool perSample, ModulationBlock block);

} // namespace lmms

#endif // LMMS_MODULATION_LAYER_H
