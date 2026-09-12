#!/usr/bin/env bash
# At the crash: the fault address, and whether the dead model's address is now
# one of the live tracks of the Song (i.e. the freed block was handed to a
# track re-created by the same restore).
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
REAL="${ZENE_CONTROL_REAL_BINARY:-$ROOT/build-release/zene}"
LOG="${ZENE_GDB_LOG:-$HERE/gdb-reuse.log}"
exec gdb -batch -nx \
	-ex "set pagination off" \
	-ex "set confirm off" \
	-ex "set print elements 12" \
	-ex "handle SIGPIPE nostop noprint pass" \
	-ex "run" \
	-ex "echo \n===== SIGNAL =====\n" \
	-ex "info program" \
	-ex "p \$_siginfo._sifields._sigfault.si_addr" \
	-ex "p \$pc" \
	-ex "p/x \$rdi" \
	-ex "p/x \$rax" \
	-ex "p/x \$rbx" \
	-ex "echo \n===== the faulting statement, disassembled =====\n" \
	-ex "disassemble /s lmms::gui::InstrumentTrackView::~InstrumentTrackView()" \
	-ex "echo \n===== frame =====\n" \
	-ex "bt 6" \
	-ex "frame 0" \
	-ex "p/x this" \
	-ex "p/x (void*)((lmms::gui::TrackView*)this)->m_track" \
	-ex "p/x (void*)((lmms::gui::TrackView*)this)->m_window" \
	-ex "p (long)((lmms::gui::TrackView*)this)->m_trackContainerView" \
	-ex "echo \n===== the Song's live tracks (is the dead track's address in here?) =====\n" \
	-ex "p lmms::Engine::getSong()->tracks()" \
	-ex "echo \n===== and the song's own pointer =====\n" \
	-ex "p/x lmms::Engine::getSong()" \
	-ex "quit" \
	--args "$REAL" "$@" > "$LOG" 2>&1
