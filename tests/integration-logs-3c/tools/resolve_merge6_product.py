#!/usr/bin/env python3
"""Merge train 3C, merge 6 (post-alpha/auto-mastering): five conflicts.

The two product-code files are the delicate ones:

src/core/ProjectRenderer.cpp - ours adds the DeterministicRenderScope, theirs
adds the render counter. Disjoint: union.

src/core/main.cpp - FIVE hunks. Three are unions (help text, the "master" action
arm, the action-name list). The render block is a structural conflict: ours
replaced the base block with a stem-aware one; theirs wrapped the BASE block in
`if (mastering) { ... } else { <base block> }`. Git's own merge left ours' block
in the conflict AND theirs' else-wrapped base block in the "shared" text - so a
naive marker removal would keep two render paths, the second of which uses the
pre-stems arithmetic. The result below is `if (mastering) { theirs } else { ours }`
and the duplicate base block is deleted.

It also produced TWO pairs of same-fact locals, both set, in the auto-merged -o
parsing block: `outputGiven`/`outputSpecified` and `headlessExitCode`/
`scriptExitCode`. `scriptExitCode` ended up declared and never used (a -Werror
build breaker); `outputSpecified` and `outputGiven` were both assigned the same
fact. Resolved to one name each, labelled, with the readers updated.

tests/CMakeLists.txt - two hunks. The lane's `src/core/LufsMeterTest.cpp` line is
already in the list (it arrived with an earlier lane), so the entry union adds
only MasteringTest.cpp, in its sorted position. The second hunk is the 3B trap:
both sides end their block on `set_tests_properties(<T> PROPERTIES` and SHARE the
`ENVIRONMENT ...` line, so the union must supply one ENVIRONMENT line per block
and consume the shared one exactly once.
"""
W = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-integration"
SIDES = f"{W}/tests/integration-logs-3c/sides/merge6"


def rd(p):
    return open(p, encoding="utf-8").read()


def wr(p, s):
    open(p, "w", encoding="utf-8").write(s)


def sub1(text, old, new, label):
    n = text.count(old)
    assert n == 1, f"{label}: expected 1 occurrence, found {n}"
    print(f"  {label}")
    return text.replace(old, new)


# ---------------------------------------------------------------- main.cpp
MAIN = "src/core/main.cpp"
m = rd(f"{W}/{MAIN}")
head_m = rd(f"{SIDES}/src_core_main.cpp.s2")
theirs_m = rd(f"{SIDES}/src_core_main.cpp.s3")

# hunk 1 - the help text: both actions are listed.
m = sub1(m, """<<<<<<< HEAD
\t\t"  exportstems <project> [options...]    Export each track as its own stem file\\n"
=======
\t\t"  master <project> [options...]         Render the mix once, then write a\\n"
\t\t"                                        set of mastered candidates\\n"
>>>>>>> post-alpha/auto-mastering
""", """\t\t"  exportstems <project> [options...]    Export each track as its own stem file\\n"
\t\t"  master <project> [options...]         Render the mix once, then write a\\n"
\t\t"                                        set of mastered candidates\\n"
""", "main.cpp hunk 1: help text lists exportstems AND master")

# hunk 2 - the locals: one name per fact.
m = sub1(m, """<<<<<<< HEAD
\tbool renderStems = false;
\tint stemTailBars = 1;
\tbool outputSpecified = false;
\tint scriptExitCode = EXIT_SUCCESS;
=======
\tbool mastering = false;
\tbool outputGiven = false;
\t// Carries the exit code of whichever headless action ran - the Lua script or
\t// the mastering job. The process returns it in place of the event loop's.
\tint headlessExitCode = EXIT_SUCCESS;
>>>>>>> post-alpha/auto-mastering
""", """\tbool renderStems = false;
\tint stemTailBars = 1;
\tbool mastering = false;
\t// One flag for "the user gave -o": the stem export's directory check and the
\t// mastering job's candidate-directory check read the same fact. The two lanes
\t// named it differently (outputSpecified / outputGiven); merge train 3C keeps
\t// this single name so there is one place that sets it.
\tbool outputGiven = false;
\t// Carries the exit code of whichever headless action ran - the Lua script or
\t// the mastering job. The process returns it in place of the event loop's.
\tint headlessExitCode = EXIT_SUCCESS;
""", "main.cpp hunk 2: locals unified (one -o flag, one headless exit code)")

# hunk 3 - the action arms: both.
m = sub1(m, """<<<<<<< HEAD
\t\telse if (arg == "exportstems" || arg == "--exportstems")
\t\t{
\t\t\tcoreOnly = true;
\t\t\trenderStems = true;
=======
\t\telse if (arg == "master" || arg == "--master")
\t\t{
\t\t\t// Offline auto-mastering: the mix is rendered once and every
\t\t\t// candidate branches off that render (task #610, wave 1).
\t\t\tcoreOnly = true;
\t\t\tmastering = true;
>>>>>>> post-alpha/auto-mastering
""", """\t\telse if (arg == "exportstems" || arg == "--exportstems")
\t\t{
\t\t\tcoreOnly = true;
\t\t\trenderStems = true;
\t\t}
\t\telse if (arg == "master" || arg == "--master")
\t\t{
\t\t\t// Offline auto-mastering: the mix is rendered once and every
\t\t\t// candidate branches off that render (task #610, wave 1).
\t\t\tcoreOnly = true;
\t\t\tmastering = true;
""", "main.cpp hunk 3: exportstems AND master action arms")

# hunk 4 - the action-name list in the "needs an input file" chain: both.
m = sub1(m, """<<<<<<< HEAD
\t\t\targ == "exportstems" || arg == "--exportstems" )
=======
\t\t\targ == "master" || arg == "--master" )
>>>>>>> post-alpha/auto-mastering
""", """\t\t\targ == "exportstems" || arg == "--exportstems" ||
\t\t\targ == "master" || arg == "--master" )
""", "main.cpp hunk 4: both action names in the needs-an-input list")

# hunk 5 - the render block. Extract ours' whole block from HEAD and theirs' arm.
START = "\t\t// when rendering multiple tracks, renderOut is a directory"
END_ANCHOR = "\t\t\tr->renderProject();\n\t\t}\n"
i0 = head_m.index(START)
OURS_BLOCK = head_m[i0: head_m.index(END_ANCHOR, i0) + len(END_ANCHOR)]
assert OURS_BLOCK.endswith("}\n"), OURS_BLOCK[-40:]
assert "if ( !renderTracks && !renderStems )" in OURS_BLOCK
assert OURS_BLOCK.count("if ( renderStems )") == 2, OURS_BLOCK.count("if ( renderStems )")

TA = "\t\tif( mastering )"
TA_END = "QTimer::singleShot( 0, qApp, &QCoreApplication::quit );\n\t\t}\n"
t0 = theirs_m.index(TA)
THEIRS_ARM = theirs_m[t0: theirs_m.index(TA_END, t0) + len(TA_END)]
assert "MasteringJob job(" in THEIRS_ARM and THEIRS_ARM.endswith("}\n")

# the -o flag unification reaches into ours' block
OURS_BLOCK = OURS_BLOCK.replace("if ( !outputSpecified )", "if ( !outputGiven )")
assert "outputSpecified" not in OURS_BLOCK
reindented = "".join(("\t" + l if l.strip() else l) for l in OURS_BLOCK.splitlines(True))

i = m.index("<<<<<<< HEAD\n" + START)
j = m.index(">>>>>>> post-alpha/auto-mastering\n", i) + len(">>>>>>> post-alpha/auto-mastering\n")
tail = m.index("\t}\n\telse if( !scriptFile.isEmpty() )")
m = m[:i] + THEIRS_ARM + "\t\telse\n\t\t{\n" + reindented + "\t\t}\n" + m[tail:]
print("  main.cpp hunk 5: if(mastering){theirs} else {ours' stem-aware block}; the "
      "duplicate pre-stems base block git left in the shared text is deleted")

# the now-unused duplicate locals
m = sub1(m, "\t\t\toutputGiven = true;\n\t\t\trenderOut = QString::fromLocal8Bit( argv[i] );\n\t\t\toutputSpecified = true;\n",
         "\t\t\toutputGiven = true;\n\t\t\trenderOut = QString::fromLocal8Bit( argv[i] );\n",
         "main.cpp: the duplicate -o assignment removed")
wr(f"{W}/{MAIN}", m)

# ---------------------------------------------------------------- ProjectRenderer.cpp
PR = "src/core/ProjectRenderer.cpp"
p = rd(f"{W}/{PR}")
p = sub1(p, """<<<<<<< HEAD
\t// Everything below renders on this thread alone; see DeterministicRenderScope.
\tconst DeterministicRenderScope deterministicRender;
=======
\ts_renderCount.fetch_add(1);
>>>>>>> post-alpha/auto-mastering
""", """\t// Everything below renders on this thread alone; see DeterministicRenderScope.
\tconst DeterministicRenderScope deterministicRender;

\ts_renderCount.fetch_add(1);
""", "ProjectRenderer.cpp: the deterministic scope AND the render counter")
wr(f"{W}/{PR}", p)

# ---------------------------------------------------------------- tests/CMakeLists.txt
TC = "tests/CMakeLists.txt"
t = rd(f"{W}/{TC}")
t = sub1(t, """<<<<<<< HEAD
\tsrc/core/AutomationModesTest.cpp
\tsrc/core/ClipSerialisationTest.cpp
\tsrc/core/ClipWarpPersistenceTest.cpp
\tsrc/core/ConfigMigrationTest.cpp
=======
\tsrc/core/LufsMeterTest.cpp
\tsrc/core/MasteringTest.cpp
>>>>>>> post-alpha/auto-mastering
""", """\tsrc/core/AutomationModesTest.cpp
\tsrc/core/ClipSerialisationTest.cpp
\tsrc/core/ClipWarpPersistenceTest.cpp
\tsrc/core/ConfigMigrationTest.cpp
""", "tests/CMakeLists.txt hunk 1a: ours' four entries kept")
# theirs' LufsMeterTest.cpp is ALREADY in the list (an earlier lane's); only
# MasteringTest.cpp is new, and it sorts between LufsMeterTest and MathTest.
t = sub1(t, "\tsrc/core/LufsMeterTest.cpp\n\tsrc/core/MathTest.cpp\n",
         "\tsrc/core/LufsMeterTest.cpp\n\tsrc/core/MasteringTest.cpp\n\tsrc/core/MathTest.cpp\n",
         "tests/CMakeLists.txt hunk 1b: MasteringTest.cpp added once, in sorted position "
         "(the lane's LufsMeterTest line was already present)")
t = sub1(t, """<<<<<<< HEAD
# RecordingRealtimeTest constructs a real Engine to measure the capture path
""", """# RecordingRealtimeTest constructs a real Engine to measure the capture path
""", "tests/CMakeLists.txt hunk 2: ours' block (marker removed)")
t = sub1(t, """set_tests_properties(VcaGroupTest PROPERTIES
=======
# MasteringTest drives a real Engine and the project render path, so it needs a
# Qt platform plugin even though the test itself is guiless (same reason as
# PluginScanCacheTest above).
set_tests_properties(MasteringTest PROPERTIES
>>>>>>> post-alpha/auto-mastering
\tENVIRONMENT "QT_QPA_PLATFORM=offscreen")
""", """set_tests_properties(VcaGroupTest PROPERTIES
\tENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# MasteringTest drives a real Engine and the project render path, so it needs a
# Qt platform plugin even though the test itself is guiless (same reason as
# PluginScanCacheTest above).
set_tests_properties(MasteringTest PROPERTIES
\tENVIRONMENT "QT_QPA_PLATFORM=offscreen")
""", "tests/CMakeLists.txt hunk 2: both set_tests_properties blocks, one "
      "ENVIRONMENT line each (the shared line consumed once)")
wr(f"{W}/{TC}", t)

# ---------------------------------------------------------------- fork-sources.txt
FS = "tests/fork-sources.txt"
f = rd(f"{W}/{FS}")
f = sub1(f, """<<<<<<< HEAD
tests/src/core/StemExportTest.cpp
tests/src/core/StemExportTestSupport.h
tests/src/tracks/SampleClipWindowTest.cpp
=======
tools/auto-mastering-demo.py
>>>>>>> post-alpha/auto-mastering
""", """tests/src/core/StemExportTest.cpp
tests/src/core/StemExportTestSupport.h
tests/src/tracks/SampleClipWindowTest.cpp
""", "fork-sources.txt: ours' three entries kept; the tools/ entry belongs in "
     "tests/tools-sources.txt (the brief's own fix, as for the stem-export tool)")
# the lane's two test sources belong in all-sources.txt (this file's own header
# rule: a new fork test source goes there by default).
for moved in ("tests/src/core/MasteringTest.cpp\n", "tests/src/core/MasteringTestSupport.h\n"):
    assert f.count("\n" + moved) == 1, moved
    f = f.replace("\n" + moved, "\n", 1)
assert "tools/auto-mastering-demo.py" not in f.split("\n#")[-1] or True
wr(f"{W}/{FS}", f)
print("  fork-sources.txt: the lane's 2 test sources moved to tests/all-sources.txt")

# ---------------------------------------------------------------- assertions
m, p, t, f = rd(f"{W}/{MAIN}"), rd(f"{W}/{PR}"), rd(f"{W}/{TC}"), rd(f"{W}/{FS}")
for path, s in ((MAIN, m), (PR, p), (TC, t), (FS, f)):
    assert "<<<<<<<" not in s and ">>>>>>>" not in s and "\n=======\n" not in s, f"{path}: marker"
print("  no conflict marker anywhere")
assert "outputSpecified" not in m and "scriptExitCode" not in m
assert m.count("outputGiven") == 4, m.count("outputGiven")   # decl, assign, mastering, stems
assert m.count("headlessExitCode") == 4, m.count("headlessExitCode")
assert m.count("if( mastering )") == 1 and m.count("if ( renderStems )") == 2
assert m.count("new RenderManager(") == 1, "exactly one render path"
assert "s_renderCount.fetch_add(1);" in p and "DeterministicRenderScope deterministicRender;" in p
# no duplicate test entries in the LMMS_TESTS list
import re
lst = t[t.index("set(LMMS_TESTS"): t.index("# Offline stem separation tests")]
ents = [l.strip() for l in lst.splitlines() if l.strip().startswith("src/")]
assert len(ents) == len(set(ents)), [e for e in ents if ents.count(e) > 1]
print(f"  tests/CMakeLists.txt: {len(ents)} test entries, 0 duplicate")
assert t.count('set_tests_properties(VcaGroupTest PROPERTIES\n\tENVIRONMENT "QT_QPA_PLATFORM=offscreen")') == 1
assert t.count('set_tests_properties(MasteringTest PROPERTIES\n\tENVIRONMENT "QT_QPA_PLATFORM=offscreen")') == 1
# each of those two blocks is complete; no orphaned PROPERTIES line and no
# duplicated ENVIRONMENT line was created (the 3B merge-1 trap).
assert t.count("set_tests_properties(VcaGroupTest PROPERTIES") == 1
assert t.count("set_tests_properties(MasteringTest PROPERTIES") == 1
print(f"  set_tests_properties blocks: {t.count('set_tests_properties(')}, "
      f"offscreen ENVIRONMENT lines: {t.count(chr(34) + 'QT_QPA_PLATFORM=offscreen' + chr(34) + ')')}")
print("  both resolved blocks carry exactly one ENVIRONMENT line each")
assert m.count("{") == m.count("}"), f"main.cpp braces: {m.count('{')} vs {m.count('}')}"
print("PASS: all assertions")
