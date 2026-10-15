#!/usr/bin/env python3
"""plugin-formats-docs-gate.py - the docs keep format LOADING and tested third-party COMPATIBILITY apart.

WHY. Q050-014 (R-instruments-11 / R-instruments-26 / R-ui-19) is a documentation-truth unit. Two
current-facing claims had drifted, and one gap was open:

  * README.md said the plugin scan cache was "a JSON data layer, not a GUI (quarantining a plugin
    means hand-writing an entry in the cache file)" - stale since `CommandLists::pluginModules()` and
    its "Plug-in Modules" list landed (src/gui/CommandLists.cpp, src/gui/shell/ShellCommandMenus.cpp);
  * docs/KNOWN-LIMITATIONS.md said "the cache's per-file records are still only on the socket" - the
    same stale claim, with `plugin.scan_cache_list` no longer the only way to see a record;
  * neither page separated the formats this build LOADS (VST3 / CLAP / VST2 / LV2 / LADSPA /
    SoundFont2 / GUS) from the plug-ins and libraries we have TESTED (MIT fixtures only) - and a
    Kontakt library is neither hosted nor verified, only external to the project.

WHAT IT CHECKS (all file reads; no network, no instance, no build):

  1. The named GUI location is REAL: `src/gui/CommandLists.cpp` defines the "Plug-in Modules" list
     over `plugin.scan_cache_list` with the `plugin.scan_cache_quarantine_add` / `_remove` actions, and
     `src/gui/shell/ShellCommandMenus.cpp` registers it. A document may not name a location that does
     not exist.
  2. The named commands are on the agent surface: the committed command snapshot carries all four ids.
  3. README.md names **Project tools > Plug-in Modules...**, states that format loading and tested
     third-party compatibility are different claims, names **Kontakt** as an external, unverified
     library case, and no longer carries the stale "not a GUI" / hand-edited-JSON claim.
  4. docs/KNOWN-LIMITATIONS.md has the **Linux** plug-in formats section, names **Plug-in Modules** as
     the per-file scan-status and quarantine location, keeps a format-loading vs third-party-tested
     separation, names **Kontakt**, and no longer says the per-file records are only on the socket.
  5. The advertised Linux format lists are *scoped*: each format name in LINUX_FORMATS must appear in
     the README plug-in-formats paragraph AND in the KNOWN-LIMITATIONS Linux section (not merely
     somewhere else in the document), and both sections must scope the list to Linux and say
     availability varies by platform, so the list cannot pass on an unrelated mention.
  6. Each advertised format is grounded in the build: its mapped loader source *registers* the
     plug-in (a `Plugin::Descriptor` symbol), not merely exists - a loader that stopped registering
     would catch a stale claim that a file exists somewhere.
  7. The platform qualification is real: our own `CMakeLists.txt` disables VST hosting in the Apple
     build, so the README may not claim VST2/Vestige there.
  8. data/manual/index.html names the same list.

`--self-test` is the gate's own red/green control: it builds a throwaway root (symlinks to every real
input, with README.md / KNOWN-LIMITATIONS.md mutated) and asserts the gate fails when a format is
removed from a *scoped* list while its other mentions stay intact - the failure mode checks 5 exists
to catch.

Usage:  python3 tests/plugin-formats-docs-gate.py [--root DIR] [--self-test]
Exit:   0 every check holds; 1 a mismatch or a stale claim; 2 usage/setup error.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

REQUIRED_COMMANDS = (
    "plugin.scan_cache_list",
    "plugin.scan_cache_quarantine_add",
    "plugin.scan_cache_quarantine_remove",
    "plugin.rescan",
)

# The Linux format list the docs advertise, each paired with the loader source that REGISTERS it in
# the build. The gate reads the registered symbol, so the advertised list is verified against the
# build - not only against the existence of a path - and a loader that stopped registering a format
# is caught. (VST2 is LMMS's Vestige host; GUS is the Patman instrument.)
LINUX_FORMATS = (
    ("VST3", "plugins/Vst3Effect/Vst3Effect.cpp", "vst3effect_plugin_descriptor"),
    ("CLAP", "plugins/ClapEffect/ClapEffect.cpp", "clapeffect_plugin_descriptor"),
    ("VST2", "plugins/VstEffect/VstEffect.cpp", "vsteffect_plugin_descriptor"),
    ("LV2", "plugins/Lv2Effect/Lv2Effect.cpp", "lv2effect_plugin_descriptor"),
    ("LADSPA", "plugins/LadspaEffect/LadspaEffect.cpp", "ladspaeffect_plugin_descriptor"),
    ("SoundFont2", "plugins/Sf2Player/Sf2Player.cpp", "sf2player_plugin_descriptor"),
    ("GUS", "plugins/Patman/Patman.cpp", "patman_plugin_descriptor"),
)

# The two bounded regions that advertise the Linux format list. A format named only outside its
# region does NOT satisfy the claim; the region is what the reader reads as "the list".
README_FORMATS_START = "**Plug-in formats vs. tested compatibility"
README_FORMATS_END = "**AI DSP**"
KL_FORMATS_START = "## Plug-in formats and libraries on Linux"
KL_FORMATS_END = "## What this alpha cannot do at all"

# Every path the gate reads, for the self-test to mirror into a throwaway root.
FIXED_INPUTS = (
    "src/gui/CommandLists.cpp",
    "src/gui/shell/ShellCommandMenus.cpp",
    "tools/mcp-zene-control/zene_control/commands_snapshot.json",
    "README.md",
    "docs/KNOWN-LIMITATIONS.md",
    "CMakeLists.txt",
    "data/manual/index.html",
)


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def flat(text):
    """Collapse all whitespace so a phrase matches across a soft line wrap."""
    return re.sub(r"\s+", " ", text)


def section(text, start_marker, end_marker):
    """The bounded region [start, end) that advertises a list, or "" when a marker is gone."""
    i = text.find(start_marker)
    if i < 0:
        return ""
    j = text.find(end_marker, i + len(start_marker))
    if j < 0:
        return ""
    return text[i:j]


def run_checks(root, report):
    """Run every check against `root`; `report(ok, msg)` records them. Returns the failure count."""
    failures = []

    def check(ok, msg):
        report(ok, msg)
        if not ok:
            failures.append(msg)

    def path(rel):
        return os.path.join(root, rel)

    # 1. The named GUI location exists in the code.
    lists = read(path("src/gui/CommandLists.cpp"))
    check('spec("Plug-in Modules", "plugin.scan_cache_list"' in lists,
          "src/gui/CommandLists.cpp: the 'Plug-in Modules' list is built over plugin.scan_cache_list")
    check("plugin.scan_cache_quarantine_add" in lists,
          "src/gui/CommandLists.cpp: the list offers plugin.scan_cache_quarantine_add")
    check("plugin.scan_cache_quarantine_remove" in lists,
          "src/gui/CommandLists.cpp: the list offers plugin.scan_cache_quarantine_remove")
    menus = read(path("src/gui/shell/ShellCommandMenus.cpp"))
    check("CommandLists::pluginModules()" in menus,
          "src/gui/shell/ShellCommandMenus.cpp: the Plug-in Modules list is registered in a menu")

    # 2. The named commands are on the agent surface.
    snapshot = read(path("tools/mcp-zene-control/zene_control/commands_snapshot.json"))
    for cid in REQUIRED_COMMANDS:
        check('"%s"' % cid in snapshot,
              "commands_snapshot.json: the agent surface carries %s" % cid)

    # The bounded format regions, used for every format-name claim below.
    readme = read(path("README.md"))
    freadme = flat(readme)
    kl = read(path("docs/KNOWN-LIMITATIONS.md"))
    fkl = flat(kl)
    readme_formats = flat(section(freadme, README_FORMATS_START, README_FORMATS_END))
    kl_formats = flat(section(fkl, KL_FORMATS_START, KL_FORMATS_END))
    check(readme_formats != "", "README.md: has the plug-in formats paragraph (the Linux list region)")
    check(kl_formats != "", "docs/KNOWN-LIMITATIONS.md: has the Linux plug-in formats section region")

    # 3. README.md: the GUI location, the separation, Kontakt, no stale claim.
    check("Plug-in Modules" in readme,
          "README.md: names the Plug-in Modules list")
    check("formats it is built with" in readme_formats,
          "README.md plug-in formats paragraph: says the build loads the formats it is built with")
    check("tested third-party compatibility" in readme_formats or "tested compatibility" in readme_formats,
          "README.md plug-in formats paragraph: states loading a format is not the same claim as tested"
          " third-party compatibility")
    check("Kontakt" in readme,
          "README.md: names Kontakt as the library case")
    check("no third-party plug-in of any kind has" in readme_formats,
          "README.md plug-in formats paragraph: says no third-party plug-in of any kind has been tested")
    check("not a GUI" not in freadme,
          "README.md: no stale 'not a GUI' claim about the scan cache")
    check("hand-writing an entry in the cache file" not in freadme,
          "README.md: no stale hand-edited-cache claim")
    # The advertised format list is scoped to the Linux paragraph, not just somewhere in the README.
    for name, _artifact, _reg in LINUX_FORMATS:
        check(name in readme_formats,
              "README.md plug-in formats paragraph: advertises %s" % name)
    check("On **Linux**" in readme_formats,
          "README.md plug-in formats paragraph: scopes the advertised format list to Linux")
    check("availability varies by platform" in readme_formats,
          "README.md plug-in formats paragraph: states format availability varies by platform")
    check("the other platforms alike" not in freadme,
          "README.md: no 'on Linux and the other platforms alike' overstatement")
    check("VST2" in readme_formats and "macOS build has no VST2" in readme_formats,
          "README.md plug-in formats paragraph: says a macOS build has no VST2/Vestige path")

    # 4. KNOWN-LIMITATIONS.md: the Linux formats section, the GUI location, the separation, Kontakt,
    #    no stale socket-only claim.
    check("Plug-in formats and libraries on Linux" in kl,
          "docs/KNOWN-LIMITATIONS.md: has the Linux plug-in formats section")
    check("Plug-in Modules" in kl,
          "docs/KNOWN-LIMITATIONS.md: names the Plug-in Modules list as the GUI location")
    check("per-file scan status and quarantine" in fkl,
          "docs/KNOWN-LIMITATIONS.md: names the list for per-file scan status and quarantine controls")
    check("Third-party compatibility is unverified" in kl_formats,
          "docs/KNOWN-LIMITATIONS.md plug-in formats section: separates format loading from tested"
          " third-party compatibility")
    check("no Kontakt runtime and has verified no Kontakt library" in kl_formats,
          "docs/KNOWN-LIMITATIONS.md plug-in formats section: states no Kontakt runtime and no verified"
          " Kontakt library")
    check("per-file records are still only on the socket" not in fkl,
          "docs/KNOWN-LIMITATIONS.md: no stale 'per-file records still only on the socket' claim")
    check("no GUI for it yet" not in fkl,
          "docs/KNOWN-LIMITATIONS.md: no stale 'no GUI for it yet' claim")
    # The advertised Linux format list is scoped to that section, not just somewhere in the page.
    for name, _artifact, _reg in LINUX_FORMATS:
        check(name in kl_formats,
              "docs/KNOWN-LIMITATIONS.md plug-in formats section: advertises %s" % name)
    check("Linux build loads" in kl_formats,
          "docs/KNOWN-LIMITATIONS.md plug-in formats section: scopes the advertised list to the Linux"
          " build")
    check("availability varies by platform" in kl_formats,
          "docs/KNOWN-LIMITATIONS.md plug-in formats section: states format availability varies by"
          " platform")

    # 5. Each advertised format is grounded in the build: its loader source registers the plug-in.
    for name, artifact, reg in LINUX_FORMATS:
        artifact_path = path(artifact)
        check(os.path.isfile(artifact_path) and reg in read(artifact_path),
              "loader %s registers %s (the %s loader is real)" % (artifact, reg, name))

    # 6. The platform qualification is grounded in our own build: VST hosting off in the Apple build.
    cmake = read(path("CMakeLists.txt"))
    apple = re.search(r"IF\(LMMS_BUILD_APPLE\)(.*?)ELSE\(LMMS_BUILD_APPLE\)", cmake, re.S)
    check(apple is not None and "SET(WANT_VST OFF)" in apple.group(1),
          "CMakeLists.txt: VST hosting is disabled in the Apple build, so the docs must not promise it")

    # 7. The manual names the same location.
    manual = read(path("data/manual/index.html"))
    check("Plug-in Modules" in manual,
          "data/manual/index.html: names the Plug-in Modules list")

    return failures


def mirror_inputs(root, dest):
    """Symlink every input the gate reads from `root` into `dest`, so only the docs are mutated."""
    rels = list(FIXED_INPUTS) + [artifact for _n, artifact, _r in LINUX_FORMATS]
    for rel in rels:
        src = os.path.join(root, rel)
        dst = os.path.join(dest, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.lexists(dst):
            os.remove(dst)
        os.symlink(src, dst)


def rewrite(dest, rel, text):
    dst = os.path.join(dest, rel)
    if os.path.lexists(dst):
        os.remove(dst)
    with open(dst, "w", encoding="utf-8") as fh:
        fh.write(text)


def _selftest_case(root, rel, doc, region):
    """Drop LADSPA from one scoped region, keep its other mentions, and run the gate in a mirrored
    root. Return (ok, detail): ok is True only when the gate exits 1 AND names the missing format."""
    mutated = doc.replace(region, region.replace("LADSPA", "", 1), 1)
    if mutated == doc or "LADSPA" not in mutated:
        return False, "the other document mentions did not stay intact"
    tmp = tempfile.mkdtemp(prefix="plugin-formats-selftest-")
    try:
        mirror_inputs(root, tmp)
        rewrite(tmp, rel, mutated)
        proc = subprocess.run([sys.executable, os.path.abspath(__file__), "--root", tmp],
                              capture_output=True, text=True)
        out = proc.stdout + proc.stderr
        named = any(line.startswith("FAIL") and "advertises LADSPA" in line
                    for line in out.splitlines())
        return proc.returncode == 1 and named, "gate exit %d, names LADSPA=%s" % (proc.returncode, named)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def self_test():
    """Prove checks 5 are not tautological: a format dropped from a scoped list fails the gate.

    Each case removes the scoped mention of one format, while its other document mentions stay put.
    A document-wide substring check would pass; the scoped check must not.
    """
    root = os.path.dirname(HERE)
    print("SELF-TEST: removing a format from a scoped list while its other mentions stay intact")

    readme = read(os.path.join(root, "README.md"))
    kl = read(os.path.join(root, "docs/KNOWN-LIMITATIONS.md"))
    cases = (
        ("README.md", readme, section(readme, README_FORMATS_START, README_FORMATS_END)),
        ("docs/KNOWN-LIMITATIONS.md", kl, section(kl, KL_FORMATS_START, KL_FORMATS_END)),
    )

    failures = 0
    for rel, doc, region in cases:
        ok, detail = _selftest_case(root, rel, doc, region)
        print(("PASS " if ok else "FAIL ")
              + "self-test: %s scoped-list LADSPA removal -> %s" % (rel, detail))
        if not ok:
            failures += 1

    print()
    if failures:
        print("RESULT: FAIL - the scoped format check let a missing format through")
        return 1
    print("RESULT: PASS - a format removed from a scoped list fails the gate even when other"
          " mentions remain")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=os.path.dirname(HERE))
    parser.add_argument("--self-test", action="store_true",
                        help="run the gate's own red/green control instead of the tree")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    root = os.path.abspath(args.root)

    def report(ok, msg):
        print(("PASS " if ok else "FAIL ") + msg)

    failures = run_checks(root, report)

    print()
    if failures:
        print("RESULT: FAIL - %d plug-in-formats doc claim(s) are stale or unnamed" % len(failures))
        return 1
    print("RESULT: PASS - format loading and tested third-party compatibility are documented apart,"
          " the Plug-in Modules GUI location is named and real, and the Linux format lists are scoped"
          " and grounded in registered loaders")
    return 0


if __name__ == "__main__":
    sys.exit(main())
