#!/usr/bin/env bash
# Richer gdb wrapper for the InstrumentTrackView destructor SIGSEGV: on top of
# the signal, the crash frame and every thread's backtrace, it prints the
# view's own fields - `this`, the model (track) pointer it dereferences, and the
# two pointers the faulting statement touches, read through the track.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
REAL="${ZENE_CONTROL_REAL_BINARY:-$ROOT/build-release/zene}"
LOG="${ZENE_GDB_LOG:-$HERE/gdb-trackview-state.log}"
exec gdb -batch -nx \
	-ex "set pagination off" \
	-ex "set confirm off" \
	-ex "set print object on" \
	-ex "run" \
	-ex "echo \n===== SIGNAL =====\n" \
	-ex "info program" \
	-ex "thread" \
	-ex "echo \n===== CRASH FRAME 0 =====\n" \
	-ex "bt 12" \
	-ex "frame 0" \
	-ex "p this" \
	-ex "echo \n===== the view's own fields =====\n" \
	-ex "p ((lmms::gui::TrackView*)this)->m_track" \
	-ex "p ((lmms::gui::TrackView*)this)->m_window" \
	-ex "p ((lmms::gui::TrackView*)this)->m_trackContainerView" \
	-ex "echo \n===== the track the view still points at =====\n" \
	-ex "p ((lmms::InstrumentTrack*)((lmms::gui::TrackView*)this)->m_track)" \
	-ex "p ((lmms::InstrumentTrack*)((lmms::gui::TrackView*)this)->m_track)->m_midiPort" \
	-ex "p ((lmms::InstrumentTrack*)((lmms::gui::TrackView*)this)->m_track)->m_midiPort.m_readablePortsMenu" \
	-ex "p ((lmms::InstrumentTrack*)((lmms::gui::TrackView*)this)->m_track)->m_midiPort.m_writablePortsMenu" \
	-ex "echo \n===== the vtable of the pointer being deleted =====\n" \
	-ex "p *(void**)((lmms::InstrumentTrack*)((lmms::gui::TrackView*)this)->m_track)->m_midiPort.m_readablePortsMenu" \
	-ex "echo \n===== BACKTRACE (all threads) =====\n" \
	-ex "thread apply all bt 12" \
	-ex "quit" \
	--args "$REAL" "$@" > "$LOG" 2>&1
