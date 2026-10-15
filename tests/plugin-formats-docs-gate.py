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
    SoundFont2) from the plug-ins and libraries we have TESTED (MIT fixtures only) - and a Kontakt
    library is neither hosted nor verified, only external to the project.

WHAT IT CHECKS (all file reads; no network, no instance, no build):

  1. The named GUI location is REAL: `src/gui/CommandLists.cpp` defines the "Plug-in Modules" list
     over `plugin.scan_cache_list` with the `plugin.scan_cache_quarantine_add` / `_remove` actions, and
     `src/gui/shell/ShellCommandMenus.cpp` registers it. A document may not name a location that does
     not exist.
  2. The named commands are on the agent surface: the committed command snapshot carries all four ids.
  3. README.md names **Project tools > Plug-in Modules...**, states that format loading and tested
     third-party compatibility are different claims, names **Kontakt** as an external, unverified
     library case, and no longer carries the stale "not a GUI" / hand-edited-JSON claim. It also
     advertises the **Linux** format list (each named format has a loader in the tree - see
     LINUX_FORMATS), scopes that list to Linux, and states that format availability varies by platform
     instead of implying VST2 hosting on macOS.
  4. docs/KNOWN-LIMITATIONS.md has the **Linux** plug-in formats section, names **Plug-in Modules** as
     the per-file scan-status and quarantine location, keeps a format-loading vs third-party-tested
     separation, names **Kontakt**, scopes the advertised format list to Linux (availability varies by
     platform), and no longer says the per-file records are only on the socket.
  5. The platform qualification is real: our own `CMakeLists.txt` disables VST hosting in the Apple
     build, so the README may not claim VST2/Vestige there.
  6. data/manual/index.html names the same list.

Usage:  python3 tests/plugin-formats-docs-gate.py [--root DIR]
Exit:   0 every check holds; 1 a mismatch or a stale claim; 2 usage/setup error.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

REQUIRED_COMMANDS = (
    "plugin.scan_cache_list",
    "plugin.scan_cache_quarantine_add",
    "plugin.scan_cache_quarantine_remove",
    "plugin.rescan",
)

# The Linux format list the docs advertise, each paired with a tree artifact that provides it. The gate
# reads the artifact, so the advertised list is verified against the build rather than only asserted,
# and a format that no longer has a loader is caught. (VST2 is LMMS's Vestige host.)
LINUX_FORMATS = (
    ("VST3", "plugins/Vst3Effect/CMakeLists.txt"),
    ("CLAP", "plugins/ClapEffect/CMakeLists.txt"),
    ("VST2", "plugins/VstEffect/CMakeLists.txt"),
    ("LV2", "plugins/Lv2Effect/CMakeLists.txt"),
    ("LADSPA", "plugins/LadspaEffect/CMakeLists.txt"),
    ("SoundFont2", "plugins/Sf2Player/CMakeLists.txt"),
    ("GUS", "plugins/Patman/Patman.cpp"),
)


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def flat(text):
    """Collapse all whitespace so a phrase matches across a soft line wrap."""
    return re.sub(r"\s+", " ", text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=os.path.dirname(HERE))
    args = parser.parse_args()
    root = os.path.abspath(args.root)

    failures = []

    def check(ok, msg):
        print(("PASS " if ok else "FAIL ") + msg)
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

    # 3. README.md: the GUI location, the separation, Kontakt, no stale claim.
    readme = read(path("README.md"))
    freadme = flat(readme)
    check("Plug-in Modules" in readme,
          "README.md: names the Plug-in Modules list")
    check("formats it is built with" in freadme,
          "README.md: says the build loads the formats it is built with")
    check("tested third-party compatibility" in freadme or "tested compatibility" in freadme,
          "README.md: states loading a format is not the same claim as tested third-party compatibility")
    check("Kontakt" in readme,
          "README.md: names Kontakt as the library case")
    check("no third-party plug-in of any kind has" in freadme,
          "README.md: says no third-party plug-in of any kind has been tested")
    check("not a GUI" not in freadme,
          "README.md: no stale 'not a GUI' claim about the scan cache")
    check("hand-writing an entry in the cache file" not in freadme,
          "README.md: no stale hand-edited-cache claim")
    # The advertised format list is grounded in the build, scoped to Linux, and does not overstate.
    for name, artifact in LINUX_FORMATS:
        check(name in readme and os.path.isfile(path(artifact)),
              "README.md: advertises %s (loader %s exists)" % (name, artifact))
    check("On **Linux**" in freadme,
          "README.md: scopes the advertised format list to Linux")
    check("availability varies by platform" in freadme,
          "README.md: states format availability varies by platform")
    check("the other platforms alike" not in freadme,
          "README.md: no 'on Linux and the other platforms alike' overstatement")
    check("VST2" in readme and "macOS build has no VST2" in freadme,
          "README.md: says a macOS build has no VST2/Vestige path")

    # 4. KNOWN-LIMITATIONS.md: the Linux formats section, the GUI location, the separation, Kontakt,
    #    no stale socket-only claim.
    kl = read(path("docs/KNOWN-LIMITATIONS.md"))
    fkl = flat(kl)
    check("Plug-in formats and libraries on Linux" in kl,
          "docs/KNOWN-LIMITATIONS.md: has the Linux plug-in formats section")
    check("Plug-in Modules" in kl,
          "docs/KNOWN-LIMITATIONS.md: names the Plug-in Modules list as the GUI location")
    check("per-file scan status and quarantine" in fkl,
          "docs/KNOWN-LIMITATIONS.md: names the list for per-file scan status and quarantine controls")
    check("Third-party compatibility is unverified" in fkl,
          "docs/KNOWN-LIMITATIONS.md: separates format loading from tested third-party compatibility")
    check("no Kontakt runtime and has verified no Kontakt library" in fkl,
          "docs/KNOWN-LIMITATIONS.md: states no Kontakt runtime and no verified Kontakt library")
    check("per-file records are still only on the socket" not in fkl,
          "docs/KNOWN-LIMITATIONS.md: no stale 'per-file records still only on the socket' claim")
    check("no GUI for it yet" not in fkl,
          "docs/KNOWN-LIMITATIONS.md: no stale 'no GUI for it yet' claim")
    # The advertised Linux format list and its platform scope.
    for name, _artifact in LINUX_FORMATS:
        check(name in kl,
              "docs/KNOWN-LIMITATIONS.md: advertises %s" % name)
    check("Linux build loads" in fkl,
          "docs/KNOWN-LIMITATIONS.md: scopes the advertised format list to the Linux build")
    check("availability varies by platform" in fkl,
          "docs/KNOWN-LIMITATIONS.md: states format availability varies by platform")

    # 5. The platform qualification is grounded in our own build: VST hosting off in the Apple build.
    cmake = read(path("CMakeLists.txt"))
    apple = re.search(r"IF\(LMMS_BUILD_APPLE\)(.*?)ELSE\(LMMS_BUILD_APPLE\)", cmake, re.S)
    check(apple is not None and "SET(WANT_VST OFF)" in apple.group(1),
          "CMakeLists.txt: VST hosting is disabled in the Apple build, so the docs must not promise it")

    # 6. The manual names the same location.
    manual = read(path("data/manual/index.html"))
    check("Plug-in Modules" in manual,
          "data/manual/index.html: names the Plug-in Modules list")

    print()
    if failures:
        print("RESULT: FAIL - %d plug-in-formats doc claim(s) are stale or unnamed" % len(failures))
        return 1
    print("RESULT: PASS - format loading and tested third-party compatibility are documented apart,"
          " and the Plug-in Modules GUI location is named and real")
    return 0


if __name__ == "__main__":
    sys.exit(main())
