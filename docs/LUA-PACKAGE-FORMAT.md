# Lua script package format (v0)

> **Status:** the *format* is defined and the discovery scanner is implemented and
> tested; the UI and the user package directory are not. See "What is not done".
> Implementation: `include/ScriptPackage.h`, `src/core/ScriptPackage.cpp`.
> Policy this inherits: `docs/LUA-COMPATIBILITY-POLICY.md`.

## 1. What a package is

A package is a **directory** holding one entry script. Nothing else about the
layout is prescribed:

```
<scripts root>/<name>/package.lua     the entry point (required)
<scripts root>/<name>/<module>.lua    sibling modules, loaded with require (section 1a)
<scripts root>/<name>/<dir>/<m>.lua   modules in sub-directories (require("dir.m"))
```

`package.lua` is an ordinary v0 script: it needs the
`--! lmms-api <major>.<minor>` header, runs with the same sandbox, budget and
command queue as a loose script, and is run with
`ScriptEngine::runFile(entryPath)` — the package adds *discovery*, not a second
execution path.

## 1a. Importing sibling modules (`require`)

A script **file** run with `ScriptEngine::runFile` (a loose script or a package
entry) gets a sandboxed `require`. Source: `include/ScriptImport.h`,
`src/core/ScriptImport.cpp`.

* **Root.** The directory that holds the entry script: the package directory for
  `package.lua`, the script's own directory for a loose script. Nothing outside
  that directory's canonical path can be imported. A package cannot import from
  a sibling package; a loose script sitting in the scripts root can name the
  packages beneath it (`require("arp.package")`), since they are inside its root.
* **Names.** `require("util")` loads `<root>/util.lua`; `require("lib.util")`
  loads `<root>/lib/util.lua`. Each dot-separated part is an identifier
  (`[A-Za-z_][A-Za-z0-9_]*`, at most 64 characters; 128 bytes in all). Refused
  with a clear error: `..`, `/` or `\` separators, absolute paths (`/x`, `\x`,
  `C:x`, `\\server\share`), empty parts, NUL bytes, and file extensions.
* **Symlinks and races.** The candidate's canonical path (symlinks resolved)
  must lie under the canonical root, so a symlink inside the root that points
  outside it is refused. The file is opened only after that check, then
  re-checked: the path must still resolve to the same canonical file and, on
  POSIX, the open descriptor must be the same device/inode as the path, so a
  swap between check and open is refused.
* **Semantics like Lua's `require`.** The module chunk runs once per script run
  with the module name and its root-relative path as `...`; its return value
  (or `true`) is cached and returned by later `require` calls of that name. A
  fresh run starts with an empty cache. Module errors propagate to the caller
  and are not cached.
* **Cycles and bounds.** An import cycle is refused (`import cycle: a -> b -> a`).
  Limits: nesting depth 16, 64 distinct modules per run, 256 KiB per module.
  Modules are loaded as text only; precompiled bytecode is refused.
* **Sandbox unchanged.** Modules run in the same globals as the entry script:
  `os`, `io`, `package`, `dofile`, `loadfile`, `load`, `loadstring` and `module`
  stay absent, and the same instruction and memory budgets apply across modules.
  Imported modules do not need the `--! lmms-api` header; the entry script's
  header gates the whole run.
* **`runString` has no `require`.** A script run from memory (the `script.run`
  source argument, livecode, the schedule clock) has no directory to confine
  imports to, so `require` is absent there.

Tests: `ScriptImportTest` (every refusal above, the symlink and swap cases, the
cache, packages and loose scripts, the sandbox inside modules).

## 2. Naming

* A package name is the directory name. Lowercase letters, digits, `_` and `-`,
  starting with a letter, at most 64 characters:
  `^[a-z][a-z0-9_-]*$`.
* The name is both a path component and (for a future device/package API) an
  identifier, so it refuses dots, spaces, separators and upper case. `..`,
  `arp/../etc` and `note gen` are not names.
* Loose scripts stay legal: `data/scripts/hello.lua` is not a package and does
  not need to become one.

## 3. The optional manifest

A package may declare itself in the entry script (first 4 KB, same window as the
version header):

```lua
--! lmms-api 0.1
--! lmms-package arp 0.2
```

`--! lmms-package <name> [<version>]` — the name must equal the directory name or
it is ignored (the directory is what a host lists and runs); the version is free
text and may be omitted. A package with no manifest is valid and reports an empty
version: an absent manifest must not invent one.

## 4. Discovery

```cpp
#include "ScriptPackage.h"

const QVector<lmms::ScriptPackage> packages =
    lmms::ScriptPackages::scan(scriptsRoot);
```

* `scan()` lists immediate subdirectories of `scriptsRoot`, keeps those that
  validate as a name and contain `package.lua`, and returns them sorted by name.
* Anything else is **skipped, not reported as broken**: a typo'd directory, a
  half-copied package or a loose script must not break a listing of the rest.
* `isValidName()` and `ScriptPackages::EntryFileName` are exposed so a caller
  that *does* want to explain a skip can.
* `parseManifest()` extracts the manifest from arbitrary source text.

Where the scripts root is: the product ships its example scripts in the data
directory (`data/scripts/`, installed by `data/scripts/CMakeLists.txt`). There is
**no user package directory yet** — see below.

## 5. What is not done (honest list)

1. **No user package directory.** `ConfigManager` has no scripts path
   (`include/ConfigManager.h` has `dataDir()`/`workingDir()` and typed
   sub-directories for presets, samples, projects, LADSPA, SF2, GIG — not
   scripts), so there is nowhere a user's packages are looked for. Adding one is
   a config-surface decision, not a script-API one.
2. **No UI or CLI listing.** `MainWindow::runScript()` (`src/gui/MainWindow.cpp`)
   is a file-open dialog; it does not offer packages, and no `--list-scripts`
   action exists. The scanner is host-side only, so nothing about this adds to
   the frozen v0 Lua surface.
3. **No install/upgrade tooling**, no signature/trust story, and no per-package
   dependency declaration.
4. **No engine-side package awareness at run time.** A package's entry is run by
   path; the engine does not know or care that it came from a package, which is
   deliberate for v0 (one execution path, one sandbox). The import root is simply
   the entry script's directory (section 1a).

Tests: `ScriptStabilisationTest::packageNamesAreValidated`,
`packagesAreDiscoveredAndTheirEntryScriptsRun` (including the proof that a
package entry cannot bypass the version gate), `manifestParsingIsStrict`; imports: `ScriptImportTest`.
