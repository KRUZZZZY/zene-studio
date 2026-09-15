#!/usr/bin/env python3
"""The Lua API surface ratchet: the version + compatibility policy, enforced.

docs/LUA-COMPATIBILITY-POLICY.md states the policy; this script is what makes it
a fact rather than a promise:

  * the API surface (every `zene.*` function and every registered class member)
    is derived from the REGISTRATION SOURCES, which are the only place it is
    defined - there is no second, hand-written list to drift;
  * docs/lua-api-surface.txt is the committed surface of the version the build
    reports. A surface change that is not recorded there fails this test, in
    either direction: a REMOVED name (a breaking change, which the policy says
    needs a MAJOR bump) and an ADDED name (additive, which the policy says
    needs a MINOR bump, so the recorded version must move with it);
  * the recorded version is checked against ZENE_LUA_API_VERSION_MAJOR/MINOR in
    CMakeLists.txt, so a version bump with an unchanged surface, or a surface
    change with an unchanged version, is caught here.

Usage:
  tests/lua-api-surface.py --write     # regenerate docs/lua-api-surface.txt
  tests/lua-api-surface.py --check     # what ctest LuaApiSurface runs
  tests/lua-api-surface.py --check --manifest <path> --root <tree>
"""

import argparse
import re
import sys
from pathlib import Path

# The sources that register the Lua surface. A file named here but absent is a
# failure: a renamed registration source would otherwise silently empty the scan.
SOURCES = [
    "src/core/ScriptBindings.cpp",
    "src/core/ScriptDawBindings.cpp",
    "src/core/ScriptDawEffects.cpp",
    "src/core/ScriptDawEdit.cpp",
]

CLASS_BEGIN = re.compile(r'\.beginClass<[^>]+>\(\s*"([^"]+)"\s*\)')
CLASS_END = re.compile(r"\.endClass\(\)")
NAMESPACE_BEGIN = re.compile(r'\.beginNamespace\(\s*"([^"]+)"\s*\)')
NAMESPACE_END = re.compile(r"\.endNamespace\(\)")
ADD_FUNCTION = re.compile(r'\.addFunction\(\s*"([^"]+)"')
# zene.apiSurface is registered with the raw Lua C API (it walks the namespace
# table it is being installed into), so it is not an addFunction call.
RAW_NAMESPACE_FUNCTIONS = {"apiSurface": "src/core/ScriptDawBindings.cpp"}

POLICY_HINT = """\
The Lua API surface changed. docs/LUA-COMPATIBILITY-POLICY.md section 2:
  * an ADDED name is an additive change  -> bump ZENE_LUA_API_VERSION_MINOR in
    CMakeLists.txt, then run tests/lua-api-surface.py --write;
  * a REMOVED or RENAMED name is a breaking change -> bump
    ZENE_LUA_API_VERSION_MAJOR, which makes every existing script fail the
    `--! zene-api` header gate with a message naming this build, then run
    tests/lua-api-surface.py --write.
Never hand-edit docs/lua-api-surface.txt: it is a derived file.
"""


def open_block(line, state):
    """A line that may open the namespace block or a class block."""
    began = NAMESPACE_BEGIN.search(line)
    if began:
        state["namespace"] = began.group(1)
        return []
    started = CLASS_BEGIN.search(line)
    if started:
        state["class"] = started.group(1)
    return []


def scan_class_body(line, state):
    """Entries one line inside a class block declares."""
    if CLASS_END.search(line):
        state["class"] = None
        return []
    member = ADD_FUNCTION.search(line)
    return [f"class {state['class']}: {member.group(1)}"] if member else []


def scan_namespace_body(line, state):
    """Entries one line inside the namespace block declares."""
    if NAMESPACE_END.search(line):
        state["namespace"] = None
        return []
    function = ADD_FUNCTION.search(line)
    return [f"{state['namespace']}: {function.group(1)}"] if function else []


def scan_line(line, state):
    if state["class"] is not None:
        return scan_class_body(line, state)
    if state["namespace"] is None:
        return open_block(line, state)
    return scan_namespace_body(line, state)


def scan_source(path: Path, root: Path):
    """Every entry one registration source declares, as a list of strings."""
    state = {"namespace": None, "class": None}
    entries = []
    for line in path.read_text(encoding="utf-8").splitlines():
        entries += scan_line(line, state)
    return str(path.relative_to(root)), entries


def scan_tree(root: Path):
    """The whole surface, sorted, or a list of the sources that went missing."""
    entries = []
    missing = []
    for name in SOURCES:
        path = root / name
        if not path.is_file():
            missing.append(name)
            continue
        _, found = scan_source(path, root)
        entries.extend(found)
    for name, source in RAW_NAMESPACE_FUNCTIONS.items():
        entries.append(f"zene: {name}")
    return sorted(entries), missing


def read_version(root: Path):
    """The build's API version, from the one place that defines it."""
    text = (root / "CMakeLists.txt").read_text(encoding="utf-8")

    def number(name):
        found = re.search(rf"{name}\s+\"?(\d+)\"?\s*\)", text, re.IGNORECASE)
        return found.group(1) if found else None

    major = number("ZENE_LUA_API_VERSION_MAJOR")
    minor = number("ZENE_LUA_API_VERSION_MINOR")
    if major is None or minor is None:
        raise SystemExit("lua-api-surface: CMakeLists.txt declares no ZENE_LUA_API_VERSION_*")
    return f"{major}.{minor}"


def render(version: str, entries) -> str:
    head = [
        "# The Lua API surface of this build, per version. DERIVED FILE - do not hand-edit.",
        "#",
        "# Producer: tests/lua-api-surface.py --write (registered as the ctest LuaApiSurface,",
        "# which re-derives this file and fails on any drift). Enforcement and policy:",
        "# docs/LUA-COMPATIBILITY-POLICY.md section 2.",
        "#",
        f"# The surface is derived from the registration sources: {', '.join(SOURCES)}.",
        "# Entries: '<namespace>: <function>' for a namespace function, 'class <Name>: <method>'",
        "# for a class member.",
        f"api {version}",
    ]
    return "\n".join(head + entries) + "\n"


def parse_manifest(text: str):
    version = None
    entries = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if stripped.startswith("api "):
            version = stripped.split(None, 1)[1]
            continue
        entries.append(stripped)
    return version, entries


def report_drift(current, recorded):
    """Print what changed, in the terms the policy is written in."""
    added = [name for name in current if name not in recorded]
    removed = [name for name in recorded if name not in current]
    for name in removed:
        print(f"REMOVED (breaking): {name}")
    for name in added:
        print(f"ADDED (additive):   {name}")
    if not added and not removed:
        print("(no entry-level difference: the recorded API version is the difference)")


def check(root: Path, manifest: Path) -> int:
    current, missing = scan_tree(root)
    if missing:
        print(f"lua-api-surface: registration source(s) missing: {', '.join(missing)}")
        return 1
    version = read_version(root)
    if not manifest.is_file():
        print(f"lua-api-surface: no manifest at {manifest}; run --write")
        return 1
    recorded_version, recorded = parse_manifest(manifest.read_text(encoding="utf-8"))
    if current == recorded and version == recorded_version:
        print(f"lua-api-surface: {len(current)} entries match {manifest.name} (api {version})")
        return 0
    print(f"lua-api-surface: {manifest} does not describe this tree.")
    if version != recorded_version:
        print(f"  recorded api {recorded_version}, CMakeLists says {version}")
    report_drift(current, recorded)
    print(POLICY_HINT)
    return 1


def write(root: Path, manifest: Path) -> int:
    current, missing = scan_tree(root)
    if missing:
        print(f"lua-api-surface: registration source(s) missing: {', '.join(missing)}")
        return 1
    version = read_version(root)
    manifest.write_text(render(version, current), encoding="utf-8")
    print(f"lua-api-surface: wrote {len(current)} entries to {manifest} (api {version})")
    return 0


def main() -> int:
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=str(here.parent))
    parser.add_argument("--manifest", default=None)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--check", action="store_true")
    group.add_argument("--write", action="store_true")
    args = parser.parse_args()
    root = Path(args.root).resolve()
    manifest = Path(args.manifest).resolve() if args.manifest else root / "docs/lua-api-surface.txt"
    return write(root, manifest) if args.write else check(root, manifest)


if __name__ == "__main__":
    sys.exit(main())
