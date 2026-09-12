#!/usr/bin/env python3
"""Merge-train 3B, merge #1 (post-alpha/rename-complete) conflict resolver.

Five conflicts, one of them product code:

  src/core/ScriptBindings.cpp      semantic: theirs renames the Lua-facing global
                                   namespace ("lmms" -> "zene") and adds a
                                   backward-compat alias at the end of the file;
                                   ours carries the ScriptApi function surface the
                                   branch predates.  Resolution = BOTH: theirs'
                                   namespace line + ours' five ScriptApi functions.
                                   The C++ `lmms::` namespace, the LMMS_* macros and
                                   the plugin entry symbol are untouched.
  tests/all-sources.txt            entry union; ours' header is kept (it is the
                                   newer, pinned-collation, verified one).
  tests/CMakeLists.txt             3 hunks: two entry unions, one comment+call
                                   block where BOTH tests need offscreen Qt.
  tests/upstream-modifications.txt entry union by path key, with the reason rules.
  tests/file-length-baseline.tsv   handled after this script, via the gate's own
                                   --reanchor-file valve (the merged line count).
"""
import os
import sys

W = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-integration"
S = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/.merge3b/sides/merge1"


def rd(p):
    return open(p, encoding="utf-8").read()


def lines(p):
    return rd(p).splitlines()


def strip_markers(text):
    out = []
    for l in text.splitlines():
        if l.startswith("<<<<<<<") or l.startswith(">>>>>>>") or l.rstrip() == "=======":
            continue
        out.append(l)
    return out


def replace_conflict(text, ours_txt, theirs_txt, want_ours, want_theirs):
    """Replace the single conflict region whose two sides are exactly ours_txt/theirs_txt."""
    marker = f"<<<<<<< HEAD\n{ours_txt}=======\n{theirs_txt}>>>>>>> post-alpha/rename-complete\n"
    assert marker in text, "conflict region not found verbatim"
    replacement = (ours_txt if want_ours else "") + (theirs_txt if want_theirs else "")
    return text.replace(marker, replacement, 1)


# ---------------------------------------------------------------- 1. ScriptBindings
def resolve_scriptbindings():
    p = os.path.join(W, "src/core/ScriptBindings.cpp")
    t = rd(p)
    ours = ('\t\t.beginNamespace("lmms")\n'
            '\t\t\t.addFunction("version", +[]() -> QString { return ScriptApi::version(); })\n'
            '\t\t\t.addFunction("apiVersion", +[]() -> QString { return ScriptApi::fullVersion(); })\n'
            '\t\t\t.addFunction("apiVersionMajor", +[]() -> int { return ScriptApi::major(); })\n'
            '\t\t\t.addFunction("apiVersionMinor", +[]() -> int { return ScriptApi::minor(); })\n'
            '\t\t\t.addFunction("apiStability", +[]() -> QString { return ScriptApi::stability(); })\n')
    theirs = ('\t\t.beginNamespace("zene")\n'
              '\t\t\t.addFunction("version", +[]() -> std::string { return std::string("0.1"); })\n')
    sub = ('\t\t.beginNamespace("zene")\n'
           '\t\t\t.addFunction("version", +[]() -> QString { return ScriptApi::version(); })\n'
           '\t\t\t.addFunction("apiVersion", +[]() -> QString { return ScriptApi::fullVersion(); })\n'
           '\t\t\t.addFunction("apiVersionMajor", +[]() -> int { return ScriptApi::major(); })\n'
           '\t\t\t.addFunction("apiVersionMinor", +[]() -> int { return ScriptApi::minor(); })\n'
           '\t\t\t.addFunction("apiStability", +[]() -> QString { return ScriptApi::stability(); })\n')
    marker = f"<<<<<<< HEAD\n{ours}=======\n{theirs}>>>>>>> post-alpha/rename-complete\n"
    assert marker in t, "ScriptBindings conflict not found verbatim"
    t = t.replace(marker, sub, 1)
    open(p, "w", encoding="utf-8").write(t)
    assert "<<<<<<<" not in t and ">>>>>>>" not in t
    # assertions on the resolved content
    assert t.count('.beginNamespace("zene")') == 1, "namespace rename not applied"
    assert '.beginNamespace("lmms")' not in t, "old namespace left behind"
    for fn in ("ScriptApi::version()", "ScriptApi::fullVersion()", "ScriptApi::major()",
               "ScriptApi::minor()", "ScriptApi::stability()"):
        assert fn in t, f"lost ScriptApi function {fn}"
    assert 'lua_setglobal(L, "lmms")' in t, "compat alias block (theirs) lost"
    body = t.split("luabridge::getGlobalNamespace(L)")[1].split(".addFunction(\"ticksPerBar\"")[0]
    assert 'std::string("0.1")' not in body, "hardcoded old version string survived"
    print(f"  ScriptBindings.cpp: {len(t.splitlines())} lines, zene namespace + 5 ScriptApi fns + alias")


# ---------------------------------------------------------------- 2. all-sources.txt
def resolve_all_sources():
    p = os.path.join(W, "tests/all-sources.txt")
    t = rd(p)
    for _ in range(2):
        i = t.index("<<<<<<< HEAD")
        j = t.index("=======\n", i)
        k = t.index(">>>>>>> post-alpha/rename-complete\n", j)
        ours = t[i + len("<<<<<<< HEAD\n"):j]
        theirs = t[j + len("=======\n"):k]
        union = sorted(set(strip_markers(ours) + strip_markers(theirs)), key=lambda x: x.encode())
        t = t[:i] + "\n".join(union) + "\n" + t[k + len(">>>>>>> post-alpha/rename-complete\n"):]
    open(p, "w", encoding="utf-8").write(t)
    assert "<<<<<<<" not in t
    ent = [l for l in t.splitlines() if l.strip() and not l.lstrip().startswith("#")]
    assert len(ent) == len(set(ent)), "duplicate entry"
    for n in ("tests/src/core/ConfigMigrationTest.cpp", "tests/src/core/DataFileFormatTest.cpp",
              "tests/src/core/PluginLogoResourceTest.cpp"):
        assert n in ent, f"lost {n}"
    assert "LC_ALL=C sort" in t, "kept an unpinned collation header"
    print(f"  all-sources.txt: {len(ent)} entries, 0 duplicates, ours' pinned header kept")


# ---------------------------------------------------------------- 3. tests/CMakeLists.txt
BOTH = ("# StemExportTest drives the real render path (Engine + RenderManager +\n"
        "# ProjectRenderer + AudioFileWave) on a project it writes to a temporary\n"
        "# directory, so it needs the same headless Qt platform as the test above.\n"
        "set_tests_properties(StemExportTest PROPERTIES\n"
        "\tENVIRONMENT \"QT_QPA_PLATFORM=offscreen\")\n"
        "\n"
        "# PluginLogoResourceTest loads a QPixmap through the product's own artwork search\n"
        "# path, so it needs a QGuiApplication and therefore a Qt platform plugin.  It\n"
        "# exists because a resource that does not resolve is silent: embed::loadSvgPixmap\n"
        "# warns and returns a 1x1 transparent pixmap, so nothing fails while every plugin\n"
        "# dialog renders blank.\n"
        "set_tests_properties(PluginLogoResourceTest PROPERTIES\n"
        "\tENVIRONMENT \"QT_QPA_PLATFORM=offscreen\")\n")


def resolve_cmake():
    p = os.path.join(W, "tests/CMakeLists.txt")
    t = rd(p)
    n_entries = 0
    for _ in range(2):  # hunk 1 and 2: entry union
        i = t.index("<<<<<<< HEAD")
        j = t.index("=======\n", i)
        k = t.index(">>>>>>> post-alpha/rename-complete\n", j)
        ours = strip_markers(t[i + len("<<<<<<< HEAD\n"):j])
        theirs = strip_markers(t[j + len("=======\n"):k])
        union = sorted(set(ours + theirs), key=lambda x: x.encode())
        n_entries += len(set(theirs) - set(ours))
        t = t[:i] + "\n".join(union) + "\n" + t[k + len(">>>>>>> post-alpha/rename-complete\n"):]
    # hunk 3: the comment+set_tests_properties block - BOTH tests need offscreen Qt
    i = t.index("<<<<<<< HEAD")
    j = t.index("=======\n", i)
    k = t.index(">>>>>>> post-alpha/rename-complete\n", j)
    ours = t[i + len("<<<<<<< HEAD\n"):j]
    theirs = t[j + len("=======\n"):k]
    tail = t[k + len(">>>>>>> post-alpha/rename-complete\n"):]
    assert tail.startswith("\tENVIRONMENT \"QT_QPA_PLATFORM=offscreen\")\n"), "hunk 3 tail unexpected"
    # The shared closing line is the TAIL, not part of either side: both blocks above
    # already carry their own copy of it, so the tail must be CONSUMED, not re-appended.
    # (Appending it duplicated the line and broke `cmake` with a parse error at
    # tests/CMakeLists.txt:223 - see the labelled fix-up commit for merge 3B #1.)
    t = t[:i] + BOTH + tail.split("\n", 1)[1]
    open(p, "w", encoding="utf-8").write(t)
    assert "<<<<<<<" not in t
    assert t.count("set_tests_properties(StemExportTest PROPERTIES") == 1
    assert t.count("set_tests_properties(PluginLogoResourceTest PROPERTIES") == 1
    print(f"  CMakeLists.txt: +{n_entries} entries, both offscreen-Qt blocks kept")


# ---------------------------------------------------------------- 4. the ledger
def parse_key(text):
    d = {}
    for l in text.splitlines():
        if not l.strip() or l.startswith("#"):
            continue
        k, _, v = l.partition("\t")
        if k.strip():
            d.setdefault(k, v)
    return d


def resolve_ledger():
    p = os.path.join(W, "tests/upstream-modifications.txt")
    t = rd(p)
    i = t.index("<<<<<<< HEAD")
    j = t.index("=======\n", i)
    k = t.index(">>>>>>> post-alpha/rename-complete\n", j)
    ours_lines = t[i + len("<<<<<<< HEAD\n"):j].splitlines()
    theirs_lines = t[j + len("=======\n"):k].splitlines()
    tail = t[k + len(">>>>>>> post-alpha/rename-complete\n"):].splitlines()
    # NB: git's auto-merged prefix (t[:i]) is part of the INTEGRATION side: t[:i]+ours == :2:
    # exactly (verified).  The full ours-side set is therefore prefix+hunk, not the hunk alone.
    ours_all = t[:i].splitlines() + ours_lines + tail
    out_lines = list(ours_all)
    O = parse_key("\n".join(ours_all))
    T = parse_key("\n".join(theirs_lines))
    B = parse_key(rd(os.path.join(S, "upstream-modifications.txt.base")))
    assert set(O) & set(T), "conflict sides do not overlap - wrong hunk"

    # --- shared paths whose reason differs
    n_super, n_base, n_union = 0, 0, 0
    for idx, l in enumerate(out_lines):
        if l.startswith("#") or not l.strip():
            continue
        path, _, reason = l.partition("\t")
        if path not in T or T[path] == reason:
            continue
        tn = T[path]
        if tn == B.get(path):
            n_base += 1                      # branch never touched this reason
        elif tn in reason:
            n_super += 1                     # theirs extends ours -> superset
        elif reason in tn:
            out_lines[idx] = path + "\t" + tn
            n_super += 1
        else:
            out_lines[idx] = path + "\t" + reason + "; " + tn
            n_union += 1

    # --- theirs-only entries, kept in their own banner groups
    bodies = {"upstream-modifications.txt.theirs": theirs_lines}
    new = [x for x in T if x not in O]
    appended = []
    if new:
        # walk theirs' own line order, carrying each entry's banner section with it
        block, banner = [], []
        for l in bodies["upstream-modifications.txt.theirs"]:
            if l.strip() == "":
                continue
            if l.startswith("#"):
                if l.startswith("# ---"):
                    if [b for b in block if not b.startswith("#")]:
                        appended += [""] + banner + block
                        block = []
                    banner = [l]
                else:
                    banner.append(l)
                continue
            path, _, _ = l.partition("\t")
            if path in new:
                block.append(l)
        if [b for b in block if not b.startswith("#")]:
            appended += [""] + banner + block
    out_lines += appended

    body = "\n".join(out_lines) + "\n"
    open(p, "w", encoding="utf-8").write(body)

    # --- assertions
    R = parse_key(body)
    assert not any(l.startswith("<<<<<<<") or l.startswith(">>>>>>>") or l.rstrip() == "======="
                   for l in out_lines), "conflict marker"
    missing = (set(O) | set(T)) - set(R)
    assert not missing, f"entries lost: {sorted(missing)}"
    dup = [l.split("\t")[0] for l in body.splitlines()
           if l.strip() and not l.startswith("#")]
    assert len(dup) == len(set(dup)), "duplicate path"
    assert all(v.strip() for v in R.values()), "blank reason"
    assert not any(";;" in v for v in R.values()), "';;' in a reason"
    lines_out = [l for l in body.splitlines() if l.strip() and not l.startswith("#")]
    print(f"  upstream-modifications.txt: {len(R)} entries "
          f"(ours {len(O)} + theirs-only {len(new)}), reasons: {n_base} kept-ours(newer), "
          f"{n_super} superset, {n_union} clause-union; 0 lost, 0 dup, 0 blank")


if __name__ == "__main__":
    which = sys.argv[1:] or ["sb", "all", "cmake", "ledger"]
    print("resolving merge 1 conflicts:")
    if "sb" in which:
        resolve_scriptbindings()
    if "all" in which:
        resolve_all_sources()
    if "cmake" in which:
        resolve_cmake()
    if "ledger" in which:
        resolve_ledger()
    print("OK")
