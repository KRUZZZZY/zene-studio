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
 * The resolved write targets are QPointer<AutomatableModel>, deliberately: a
 * target can be destroyed under the audio thread (a device unloaded, a chain or
 * channel removed, a project opened), and a QPointer nulls itself in
 * ~QObject, so the audio thread skips a destroyed target instead of
 * dereferencing it. That is the same guarded-key technique the Darwin fix
 * applied to Song::m_oldAutomatedValues.
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

#include <array>
#include <atomic>
#include <vector>

#include <QDomElement>
#include <QPointer>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

class AutomatableModel;

//! The fixed LFO vocabulary. A closed set because the layer persists a NAME.
enum class ModulationShape
{
	Sine,
	Triangle,
	Square,
	Saw,
};

//! Wire name of a shape ("sine", "triangle", "square", "saw"), and its inverse.
LMMS_EXPORT QString modulationShapeName(ModulationShape shape);
//! False when @a name is not one of the four (nothing is guessed).
LMMS_EXPORT bool modulationShapeFromName(const QString& name, ModulationShape* shape);

/*! One route: an address into a mixer channel's rack, plus a depth.
 *
 *  The address is exactly a RackMacroTarget's address minus the window, so
 *  rack.macro_target_add and modulator.target_set name a parameter the same
 *  way and a reader can carry one intent between them.
 */
struct ModulationRoute
{
	int channel = 0;         //!< mixer channel index (ch-<n>)
	int chain = 0;           //!< chain inside that channel's rack (0 = its own)
	int effect = 0;          //!< effect inside that chain (fx-<n> order)
	QString parameter;       //!< the parameter's display name
	float depth = 0.0f;      //!< -1..1, a fraction of the target's own range
};

//! The LFO one modulator runs.
struct ModulatorSource
{
	ModulationShape shape = ModulationShape::Sine;
	float rateHz = 1.0f;     //!< MinRateHz..MaxRateHz
	float phase = 0.0f;      //!< 0..1, where in its cycle the modulator starts
	bool unipolar = false;   //!< false: output -1..1 (bipolar); true: 0..1
	bool active = true;      //!< an inactive modulator drives nothing
};

//! One named modulator: a source and the routes it drives.
struct Modulator
{
	QString name;
	ModulatorSource source;
	std::vector<ModulationRoute> routes;

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
	};

	std::array<Source, ModulationLayer::MaxModulators> sources{};
	int sourceCount = 0;
	std::array<Entry, MaxEntries> entries{};
	int entryCount = 0;

	bool active() const noexcept { return entryCount > 0; }
};

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
	/*! One mutation of the layer, published the moment it lands. @a fn receives
	 *  the authored layer and the runtime to rebuild; it returns true when it
	 *  changed the layer. Callers that only clear stale write targets pass
	 *  false. */
	template <typename Fn>
	bool edit(Fn&& fn)
	{
		m_version.fetch_add(1, std::memory_order_acq_rel);
		const bool changed = fn(m_layer, m_runtime);
		m_version.fetch_add(1, std::memory_order_acq_rel);
		return changed;
	}

	//! The authored layer. Control thread only (it is the value being written);
	//! the audio thread uses snapshot().
	ModulationLayer& layer() noexcept { return m_layer; }
	const ModulationLayer& layer() const noexcept { return m_layer; }
	ModulationRuntime& runtime() noexcept { return m_runtime; }
	const ModulationRuntime& runtime() const noexcept { return m_runtime; }

	//! The runtime as of the last completed edit. Lock-free, allocation-free
	//! and bounded; safe to call from the audio thread.
	ModulationRuntime snapshot() const noexcept
	{
		ModulationRuntime copy;
		unsigned version = 0;
		do
		{
			version = m_version.load(std::memory_order_acquire);
			if ((version & 1u) != 0u) { continue; }  // a write is in progress
			copy = m_runtime;
		}
		while (m_version.load(std::memory_order_acquire) != version);
		return copy;
	}

private:
	ModulationLayer m_layer{};
	ModulationRuntime m_runtime{};
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
LMMS_EXPORT void applyModulationBlock(const ModulationRuntime& runtime, double seconds);

} // namespace lmms

#endif // LMMS_MODULATION_LAYER_H
