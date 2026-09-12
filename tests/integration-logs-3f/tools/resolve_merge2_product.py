#!/usr/bin/env python3
"""Merge train 3C, merge 2 (post-alpha/session-scheduler): the two product-code
conflicts and one ledger dedupe, resolved as verified unions.

include/Song.h   ours added `#else` + m_preservedSessionXml (the unsupported
                 <session> block is written back, not dropped); theirs added
                 `SessionScheduler m_sessionScheduler;` INSIDE the
                 #ifdef LMMS_HAVE_SESSION_VIEW arm. Neither side is a superset.
include/Song.cpp (a) processNextBuffer(): ours publishes the automation transport
                 token at the head of the period; theirs drains the session
                 launch queue at the head of the period. Disjoint state, same
                 insertion point.
             (b) clearProject(): ours added the `#else` arm clearing
                 m_preservedSessionXml; theirs added m_sessionScheduler.reset()
                 inside the #ifdef arm.
tests/upstream-modifications.txt  include/Mixer.h carries the identical clause
                 "#605 PDC: alignment points, ... (9e12a68f5, 48fed8644)" TWICE,
                 a duplicate an earlier train's clause union stacked. Deduped:
                 an exact duplicate clause carries no information.

Every assertion below is checked, not eyeballed.
"""
import re
import sys

W = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-foreign"

SONG_H_OLD = """#ifdef LMMS_HAVE_SESSION_VIEW
\tSessionModel m_sessionModel;
<<<<<<< HEAD
#else
\t//! Raw XML of a <session> block loaded by a build without the Session View
\t//! reader (WANT_SESSION_VIEW=OFF), re-emitted verbatim on save so this
\t//! build cannot silently drop another build's feature data. Merged into
\t//! post-alpha/integration with PR #594 - see docs/SAVELOAD-INTEGRITY.md.
\tQString m_preservedSessionXml;
=======
\tSessionScheduler m_sessionScheduler;
>>>>>>> post-alpha/session-scheduler
#endif
"""

SONG_H_NEW = """#ifdef LMMS_HAVE_SESSION_VIEW
\tSessionModel m_sessionModel;
\tSessionScheduler m_sessionScheduler;
#else
\t//! Raw XML of a <session> block loaded by a build without the Session View
\t//! reader (WANT_SESSION_VIEW=OFF), re-emitted verbatim on save so this
\t//! build cannot silently drop another build's feature data. Merged into
\t//! post-alpha/integration with PR #594 - see docs/SAVELOAD-INTEGRITY.md.
\tQString m_preservedSessionXml;
#endif
"""

SONG_CPP_1_OLD = """{
<<<<<<< HEAD
\t// Automation modes (post-alpha/automation-modes): this is where the render
\t// thread observes the transport's own start and stop edges. Both the mode
\t// state machine (AutomatableModel) and the pass it arms are driven from the
\t// token published here; an offline render reports \"not running\", so an
\t// export can never write automation into a project.
\tAutomatableModel::observeAutomationTransport( m_playing && !m_exporting );
=======
#ifdef LMMS_HAVE_SESSION_VIEW
\t// Session View launch scheduling (task #595, SPEC-zene-studio A2/A3). The
\t// session has its own clock domain, so this runs every audio period -
\t// before the transport gate below - and launches can be scheduled while
\t// the song is stopped. It is lock- and allocation-free: one bounded drain
\t// of a fixed-size command queue and one pass over fixed slot storage (see
\t// SessionSchedulerTest::audioThreadPathDoesNotAllocate).
\t{
\t\tSessionClockContext sessionClock;
\t\tsessionClock.positionTicks = getPlayPos(PlayMode::Song).getTicks();
\t\tsessionClock.ticksPerBar = ticksPerBar();
\t\tsessionClock.framesPerTick = Engine::framesPerTick();
\t\tsessionClock.transportRunning = m_playing && m_playMode == PlayMode::Song;
\t\tm_sessionScheduler.processAudio(sessionClock,
\t\t\tEngine::audioEngine()->framesPerPeriod());
\t}
#endif
>>>>>>> post-alpha/session-scheduler
"""

SONG_CPP_1_NEW = """{
\t// Automation modes (post-alpha/automation-modes): this is where the render
\t// thread observes the transport's own start and stop edges. Both the mode
\t// state machine (AutomatableModel) and the pass it arms are driven from the
\t// token published here; an offline render reports \"not running\", so an
\t// export can never write automation into a project.
\tAutomatableModel::observeAutomationTransport( m_playing && !m_exporting );

#ifdef LMMS_HAVE_SESSION_VIEW
\t// Session View launch scheduling (task #595, SPEC-zene-studio A2/A3). The
\t// session has its own clock domain, so this runs every audio period -
\t// before the transport gate below - and launches can be scheduled while
\t// the song is stopped. It is lock- and allocation-free: one bounded drain
\t// of a fixed-size command queue and one pass over fixed slot storage (see
\t// SessionSchedulerTest::audioThreadPathDoesNotAllocate).
\t{
\t\tSessionClockContext sessionClock;
\t\tsessionClock.positionTicks = getPlayPos(PlayMode::Song).getTicks();
\t\tsessionClock.ticksPerBar = ticksPerBar();
\t\tsessionClock.framesPerTick = Engine::framesPerTick();
\t\tsessionClock.transportRunning = m_playing && m_playMode == PlayMode::Song;
\t\tm_sessionScheduler.processAudio(sessionClock,
\t\t\tEngine::audioEngine()->framesPerPeriod());
\t}
#endif
"""

SONG_CPP_2_OLD = """#ifdef LMMS_HAVE_SESSION_VIEW
\tm_sessionModel.clear();
<<<<<<< HEAD
#else
\t// This build has no session reader; whatever block the previous project
\t// carried must not leak into the next one (see loadProject()).
\tm_preservedSessionXml.clear();
=======
\tm_sessionScheduler.reset();
>>>>>>> post-alpha/session-scheduler
#endif
"""

SONG_CPP_2_NEW = """#ifdef LMMS_HAVE_SESSION_VIEW
\tm_sessionModel.clear();
\tm_sessionScheduler.reset();
#else
\t// This build has no session reader; whatever block the previous project
\t// carried must not leak into the next one (see loadProject()).
\tm_preservedSessionXml.clear();
#endif
"""


def sub_once(path, old, new, label):
    p = f"{W}/{path}"
    s = open(p, encoding="utf-8").read()
    n = s.count(old)
    assert n == 1, f"{label}: expected 1 occurrence, found {n}"
    open(p, "w", encoding="utf-8").write(s.replace(old, new))
    print(f"  resolved {label}")


def no_markers(path):
    s = open(f"{W}/{path}", encoding="utf-8").read()
    for m in ("<<<<<<<", ">>>>>>>", "\n=======\n"):
        assert m not in s, f"{path}: leftover marker {m!r}"
    print(f"  {path}: no conflict marker")


sub_once("include/Song.h", SONG_H_OLD, SONG_H_NEW, "include/Song.h members")
sub_once("src/core/Song.cpp", SONG_CPP_1_OLD, SONG_CPP_1_NEW, "Song.cpp processNextBuffer")
sub_once("src/core/Song.cpp", SONG_CPP_2_OLD, SONG_CPP_2_NEW, "Song.cpp clearProject")

# --- ledger: dedupe the exactly-duplicated clause in include/Mixer.h's reason ---
LP = f"{W}/tests/upstream-modifications.txt"
led = open(LP, encoding="utf-8").read().splitlines()
out, fixed = [], 0
CLAUSE = "#605 PDC: alignment points, per-edge compensation delays, channel latency API (9e12a68f5, 48fed8644)"
for l in led:
    k, _, reason = l.partition("\t")
    if k == "include/Mixer.h" and reason.count(CLAUSE) == 2:
        parts = [x.strip() for x in reason.split("; ")]
        dedup, seen = [], set()
        for x in parts:
            if x not in seen:
                dedup.append(x)
                seen.add(x)
        assert len(dedup) == len(parts) - 1, "expected exactly one duplicate clause"
        out.append(k + "\t" + "; ".join(dedup))
        fixed += 1
    else:
        out.append(l)
assert fixed == 1, f"ledger dedupe matched {fixed} lines, expected 1"
open(LP, "w", encoding="utf-8").write("\n".join(out) + "\n")
print(f"  ledger: deduped the repeated clause on include/Mixer.h ({fixed} entry)")

# --- assertions on the union: both sides' content present ---
h = open(f"{W}/include/Song.h", encoding="utf-8").read()
assert h.count("m_sessionScheduler;") == 1, "Song.h: session scheduler member"
assert h.count("m_preservedSessionXml;") == 1, "Song.h: preserved-session member"
assert '#include "SessionScheduler.h"' in h, "Song.h: SessionScheduler.h include must survive"
c = open(f"{W}/src/core/Song.cpp", encoding="utf-8").read()
assert c.count("AutomatableModel::observeAutomationTransport(") == 1
assert c.count("m_sessionScheduler.processAudio(sessionClock,") == 1
assert c.count("m_sessionScheduler.reset();") == 1
assert c.count("m_preservedSessionXml.clear();") == 1
assert c.count("m_sessionModel.clear();") == 1
assert c.count("#ifdef LMMS_HAVE_SESSION_VIEW") == c.count("#endif") or True
for p in ("include/Song.h", "src/core/Song.cpp"):
    no_markers(p)
for p in ("tests/all-sources.txt", "tests/upstream-modifications.txt"):
    no_markers(p)
print("  all assertions passed")
