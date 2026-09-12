#!/usr/bin/env bash
# The faulting memory access: signal info, the instruction, and the scalars the
# destructor was working with. Deliberately small output - object dumps are not
# read, only addresses and pointers.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
REAL="${ZENE_CONTROL_REAL_BINARY:-$ROOT/build-release/zene}"
LOG="${ZENE_GDB_LOG:-$HERE/gdb-fault.log}"
exec gdb -batch -nx \
	-ex "set pagination off" \
	-ex "set confirm off" \
	-ex "set print elements 8" \
	-ex "set print max-depth 1" \
	-ex "handle SIGPIPE nostop noprint pass" \
	-ex "run" \
	-ex "echo \n===== SIGNAL / FAULT ADDRESS =====\n" \
	-ex "info program" \
	-ex "p \$_siginfo" \
	-ex "p \$pc" \
	-ex "p \$rdi" \
	-ex "echo \n===== INSTRUCTIONS AROUND THE FAULT =====\n" \
	-ex "x/6i \$pc-12" \
	-ex "echo \n===== FRAME =====\n" \
	-ex "bt 8" \
	-ex "frame 0" \
	-ex "p this" \
	-ex "p (void*)((lmms::gui::TrackView*)this)->m_track" \
	-ex "p ((lmms::gui::TrackView*)this)->m_window" \
	-ex "p (void*)((lmms::InstrumentTrack*)((lmms::gui::TrackView*)this)->m_track)->m_midiPort.m_readablePortsMenu" \
	-ex "p (void*)((lmms::InstrumentTrack*)((lmms::gui::TrackView*)this)->m_track)->m_midiPort.m_writablePortsMenu" \
	-ex "echo \n===== the vtable word at the model pointer =====\n" \
	-ex "p *(void**)((lmms::gui::TrackView*)this)->m_track" \
	-ex "p *(void**)((char*)((lmms::gui::TrackView*)this)->m_track + 8)" \
	-ex "echo \n===== who is in the tcache / heap around it =====\n" \
	-ex "info proc mappings" \
	-ex "quit" \
	--args "$REAL" "$@" > "$LOG" 2>&1
