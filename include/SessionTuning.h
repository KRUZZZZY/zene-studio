/*
 * SessionTuning.h - ONE session-wide tuning table every instrument reads at
 *                    render (board card #712, MTS-ESP host-wide dynamic
 *                    tuning)
 *
 * WHAT THIS ADDS, AND WHAT IT DOES NOT. Upstream microtuning ships already:
 * Interval/Scale/Keymap stay the per-instrument Microtuner's (see
 * docs/MIDI-DEPTH.md:20 and FEATURE-LIST-0.3.0.md:386), and this file does not
 * re-implement them - it stores their product, once, for the WHOLE session: a
 * 128-entry frequency table. NotePlayHandle::updateFrequency() reads it AT
 * RENDER when it is active, so a change to the table retunes every sounding
 * instrument with the transport still playing, instead of being a per-note
 * property that only the next note-on could see. When the table is inactive
 * (the default), updateFrequency() takes exactly the branches it took before
 * this card existed - behaviour-preserving by construction.
 *
 * FEEDING IT. The existing .scl/.kbm parsing in src/gui/MicrotunerConfig.cpp
 * feeds this object (gui may call core; core gains no gui dependency): the
 * dialog's applyScale()/applyKeymap() hand the Scale/Keymap they just applied
 * to the session. The control surface drives it directly through the mts.*
 * group (src/core/ControlCommandsMts.cpp), which parses the same file formats
 * in core - the socket path never needs a dialog.
 *
 * REALTIME RULE (AGENTS.md #4): the audio thread only ever calls
 * isActive() and noteToFreq(). Those read an atomic index and a plain POD
 * buffer - no allocation, no locking, no growth. Every writer runs on the
 * control/UI thread and commits a whole table with ONE double-buffer flip
 * (write the back buffer, release-store the index), then marks the sounding
 * NotePlayHandles for recomputation on their next play() and, when armed,
 * republishes to MTS-ESP. A reader spans at most one flip in practice (two
 * control commands would have to complete inside one noteToFreq call); the
 * writer never writes the buffer a reader can be holding.
 *
 * BOUNDS, STATED (the snapshot class needs them): the whole mutable state is
 * 128 finite positive frequencies + one active flag + one source string of at
 * most 256 characters. mts.set_tuning is the inverse command every mutating
 * mts.* verb records, and it refuses anything outside that cap.
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
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_SESSIONTUNING_H
#define LMMS_SESSIONTUNING_H

#include <array>
#include <atomic>
#include <memory>

#include <QJsonObject>
#include <QString>

#include "Note.h"

namespace lmms
{

class Keymap;
class Scale;

/*! The session-wide dynamic-tuning table (board card #712).
 *
 *  A process-wide singleton on the MidiClock::instance() pattern: it is read
 *  from the audio thread, the control thread and the GUI thread at once, so it
 *  is deliberately never destroyed either.
 */
class LMMS_EXPORT SessionTuning
{
public:
	//! The table is one entry per MIDI key, like MTS-ESP's own note table.
	static constexpr int TableSize = NumKeys;
	//! The cap on the source string a snapshot may carry (see the file header).
	static constexpr int MaxSourceLength = 256;

	/*! The one instance. Never destroyed - the audio thread may be inside
	 *  noteToFreq() while any other thread acts, and a singleton torn down at
	 *  static-destruction time is exactly the shape that gets reached late. */
	static SessionTuning* instance();

	// ---------------------------------------------------------------------
	// The AUDIO THREAD's side: header-inline, no allocation, no locking.
	// ---------------------------------------------------------------------
	//! True when the session table - not the per-track Microtuner / 12-TET -
	//! decides render frequencies.
	bool isActive() const noexcept
	{
		return m_tables[m_front.load(std::memory_order_acquire)].active;
	}

	/*! Frequency for \a key from the ACTIVE table, in Hz.
	 *  0 means "unmapped / table inactive" - the caller falls back to its own
	 *  path (NotePlayHandle::updateFrequency only takes the session branch
	 *  when isActive() is true, so a 0 there means the keymap does not map
	 *  the key and the note stays silent, exactly like the Microtuner's
	 *  keyToFreq() contract). */
	float noteToFreq(int key) const noexcept
	{
		if (key < 0 || key >= TableSize) { return 0.f; }
		const auto& table = m_tables[m_front.load(std::memory_order_acquire)];
		if (!table.active) { return 0.f; }
		return static_cast<float>(table.frequencies[key]);
	}

	// ---------------------------------------------------------------------
	// The CONTROL THREAD's side (commands and the Microtuner dialog; every
	// one of these ends in one buffer flip + retune walk + MTS-ESP publish).
	// ---------------------------------------------------------------------

	//! Parse an .scl file (the format MicrotunerConfig's dialog parses), build
	//! the table from it with the CURRENT keymap, and ACTIVATE the session
	//! table. false + \a error (typed, nothing written) on any refusal.
	bool loadScaleFile(const QString& path, QString* error);
	//! Parse a .kbm file the way the dialog does and rebuild the table's
	//! content with it. Does NOT change the active flag - content only.
	bool loadKeymapFile(const QString& path, QString* error);

	//! Replace the whole state - the inverse every mutating mts.* verb
	//! records: 128 finite positive frequencies, the active flag, the source.
	bool setState(const QJsonObject& state, QString* error);

	//! One note's frequency (MTS-ESP's per-note dynamic tuning); activates the
	//! table. false + \a error outside the cap. \a source records where the
	//! edit came from (empty = "manual (mts.set_note)").
	bool setNoteFrequency(int key, double frequency, const QString& source, QString* error);

	//! Back to a 12-TET-content, INACTIVE table: the render path is then
	//! byte-for-byte the pre-#712 one again.
	void reset();

	//! The dialog's feed: the Scale / Keymap applyScale()/applyKeymap() just
	//! applied to the Song. Content only - feeding never activates (switching
	//! the session table on is the mts.* group's job; there is no interface
	//! for that yet, see docs/KNOWN-LIMITATIONS.md).
	void feedScale(std::shared_ptr<const Scale> scale, const QString& source);
	void feedKeymap(std::shared_ptr<const Keymap> keymap, const QString& source);

	//! A snapshot for __transaction's before-state and for mts.get_state:
	//! {active, source, frequencies[TableSize]}.
	QJsonObject stateJson() const;
	//! The whole state as one value (for undo/redo closures).
	QJsonObject state() const { return stateJson(); }

	// The Scale / Keymap the table was BUILT from (own copies, shared with the
	// Song on request) - the parity test compares this against the Microtuner
	// reading the same objects. Null scale = 12-TET content.
	std::shared_ptr<const Scale> scale() const { return m_scale; }
	std::shared_ptr<const Keymap> keymap() const { return m_keymap; }

	// ---------------------------------------------------------------------
	// MTS-ESP (the vendored thirdparty/mts-esp pair - see its README).
	// ---------------------------------------------------------------------
	/*! True when the separately-installed MTS-ESP IPC core (libMTS.so /
	 *  libMTS.dylib / LIBMTS.dll - upstream's own install paths, the ones
	 *  thirdparty/mts-esp/*.cpp dlopen at static init) is present in THIS
	 *  process. The wrappers make every master call an inert no-op without
	 *  it, and expose no "am I connected" call, so the probe asks the loader
	 *  directly (POSIX: dlopen(..., RTLD_NOLOAD) on the two paths the wrapper
	 *  tries; Windows: GetModuleHandleW after the wrapper's Known-Folder load).
	 *  POSIX-only probe: on a Windows builder this returns false and
	 *  mts.master_set's refusal says so (docs/KNOWN-LIMITATIONS.md). */
	static bool mtsEspLibraryPresent();

	bool mtsMasterEnabled() const { return m_masterEnabled; }
	//! Arm / disarm publication of the table through MTS-ESP. Arming with no
	//! library present fails with \a error (the typed refusal lives in the
	//! command); disarming is always idempotent.
	bool setMtsMaster(bool enabled, QString* error);

	//! get_state's MTS-ESP fields: "library-absent" / "library-present" and
	//! the registered flag with the connected client count.
	QJsonObject mtsEspJson() const;

private:
	SessionTuning();

	//! The POD one buffer flip swaps: plain data so a reader on the audio
	//! thread never touches anything a writer is mutating.
	struct Table
	{
		std::array<double, TableSize> frequencies{};
		bool active = false;
	};

	//! 12-TET content (the default, and what reset() puts back).
	static Table twelveTetTable();
	/*! The table for \a scale over \a keymap - the SAME arithmetic
	 *  Microtuner::keyToFreq() performs (keymap degree/octave + scale
	 *  intervals + the keymap's own base note/frequency), evaluated for all
	 *  128 keys. The parity test asserts the two agree key-for-key; this is
	 *  table CONSTRUCTION from the Microtuner's own Scale/Keymap objects, not
	 *  a second scale engine. */
	static bool buildTable(const Scale* scale, const Keymap& keymap,
		Table* out, QString* error);
	//! Commit \a next into the back buffer, flip, retune sounding notes,
	//! publish to MTS-ESP if armed. Control thread only.
	void commit(const Table& next, bool nextActive, const QString& source);
	//! Walk every InstrumentTrack and mark its playing NotePlayHandles so the
	//! next audio period recomputes them from this table.
	void retuneSoundingNotes();
	//! MTS_SetNoteTunings + MTS_SetScaleName when armed and the library is
	//! there; a no-op otherwise (and when the table carries unmapped keys).
	void publishToMtsEsp();

	std::atomic<int> m_front{0};
	Table m_tables[2];

	std::shared_ptr<const Scale> m_scale;
	std::shared_ptr<const Keymap> m_keymap;
	QString m_source{QStringLiteral("12-TET (session table inactive)")};
	bool m_masterEnabled = false;
};

} // namespace lmms

#endif // LMMS_SESSIONTUNING_H
