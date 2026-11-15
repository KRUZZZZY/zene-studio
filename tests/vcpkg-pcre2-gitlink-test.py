#!/usr/bin/env python3
"""vcpkg-pcre2-gitlink-test.py - regression for cluster ci-b38398fa7833e0a4.

WHAT FAILED (hosted msvc-x64, run 43569206372 job 132369662829). Configure installs the
vcpkg manifest; one dependency of lilv is sord, which depends on pcre2 with the `jit`
feature. vcpkg's pcre2 portfile resolves the deps/sljit gitlink of a tagged release by
downloading

    https://api.github.com/repos/PCRE2Project/pcre2/contents/deps/sljit?ref=pcre2-10.49

from the unauthenticated GitHub REST API (60 requests/hour/IP). The runner's shared
address was answered 403, vcpkg retries only 408/429/5xx, and the job died:

    Downloading https://api.github.com/.../deps/sljit?ref=pcre2-10.49 -> ...gitlink.json
    error: curl operation failed with response code 403.
    error: Not a transient network error, won't retry download from https://api.github.com/...
    error: building pcre2:x64-windows failed with BUILD_FAILED

THE FIX. The response is a build INPUT, not a live query, so it is committed beside an
overlay pcre2 portfile (`.github/vcpkg/ports/pcre2/`, selected by
`vcpkg-configuration.json`) and verified against the same SHA512 the pinned vcpkg pins.
Only the tagged-release fetch moved: the parse, the gitlink validation, the sljit source
pin and the pcre2/sljit tarball downloads (github.com, not the API) are upstream's.

WHAT THIS TEST DOES. It resolves the pcre2 port the repository would install the way
vcpkg does - the overlay port when `vcpkg-configuration.json` names one and the port
exists, otherwise vcpkg's builtin port (the verbatim pinned copy under
tests/data/vcpkg-pcre2/) - and executes that portfile's real text in `cmake -P` with the
network, the package build and the install stubbed to a transcript. The one behaviour
under test is which URLs a tagged release requests.

Assertions:
  1. the repository configuration selects the overlay pcre2 port;
  2. a tagged release requests NO api.github.com URL; it reads the committed gitlink,
     hash-checks it, and keeps the upstream parse and validation;
  3. the sljit source is still pinned to the commit in that file, and the pcre2 and
     sljit tarballs are still requested from github.com;
  4. the overlay's vcpkg.json is byte-identical in meaning to the pinned builtin one
     (same version 10.49#2, same features), so the dependency graph did not move;
  5. the three upstream patches are byte-identical to the pinned commit's;
  6. `--head` still queries the API (its gitlink is whatever the branch says today);
  7. RED CONTROLS, each of which must fail: the pinned builtin portfile (no overlay)
     dies with the CI's own 403 on that exact URL; a tampered committed gitlink fails
     the SHA512 check; a missing committed gitlink fails loudly.

Deterministic: no network, no wall clock, no sleeps. Inputs are committed files; the
only seam is the CMake stub table (URL -> action) built below.

Exit codes: 0 = pass, 1 = regression, 2 = harness/setup error.
"""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
OVERLAY_DIR = ROOT / ".github" / "vcpkg" / "ports"
CONFIG = ROOT / "vcpkg-configuration.json"
BUILTIN_PORTFILE = HERE / "data" / "vcpkg-pcre2" / "builtin-portfile-177e817512.cmake"
BUILTIN_MANIFEST = HERE / "data" / "vcpkg-pcre2" / "builtin-vcpkg-json-177e817512.json"
GITLINK_NAME = "sljit-gitlink-pcre2-10.49.json"
API_URL = "https://api.github.com/repos/PCRE2Project/pcre2/contents/deps/sljit?ref=pcre2-10.49"
SLJIT_SHA = "de0259c7aaf36aa40cba8014f3fad3edde9307f9"
GITLINK_SHA512 = ("719fa1d8d8e1e749ce6f7370db32818268f84b1376ed9367fd5290ab440d8bea"
                  "7a6a25a68e55045a45da68b0ce7a55daa0d7acd2f1b11774319b50268c02441c")
PCRE2_SHA512 = ("8257285809a9a4afb441869936740ae5ff04cc4bd95f86c8e4b9fd41f3b6a194e88d0aaa1c2cfc0"
                "1b8845984993736fa5e77201c82021842acc87451d8f62f8b")
SLJIT_SHA512 = ("e5cc01f941877558f19859aedc9079c155f752a92e6d94fc5fb716e483b8fcb3a8495a597"
                "4bbb545d6d9a947ca97ebb36bfe278c4dc083afeb48b8ccab36895b")
PATCH_SHA512 = {
    "pcre2-10.35_fix-uwp.patch":
        "92f231765fea9868c39552009fbe1096279e0dd9b1f121f5184ff945e0b0c7a0d6e2167976db2e2b5bbdfaf231175894fcf9221c4d94fc7d436e400847fda209",
    "no-static-suffix.patch":
        "7d3eb8b71f0880efc0dd2f391d89c2a3bc4d380a335401692161f4d050d224e4ce1a827f936b23c2127118364c8c7c7e6f1270ba25161de2039be0c7d65dcb10",
    "fix-msvc-pdb-install.patch":
        "824a6abee766eee1a4d5732d1a62d4bacbe56f0a372b6d81cd0a8ed94c6cf7014d2bb099e22ecd5d26421c349479d99cbd3cceed86e6358b2f33e199887856a0",
}

CMAKELIB = r'''
# Stub half of a vcpkg install. The portfile under test is the only real code that runs;
# every vcpkg_* helper it calls is replaced by one that records what was asked for, and
# the network answers exactly like the hosted runner did.
cmake_minimum_required(VERSION 3.20)
set(VERSION 10.49)
set(PORT pcre2)
set(TARGET_TRIPLET x64-windows)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_TARGET_IS_ANDROID OFF)
set(VCPKG_BUILD_TYPE "")
# The manifest requests pcre2 with the jit feature (sord->pcre2[jit]); FEATURES is what
# the portfile tests before it resolves the gitlink at all.
set(FEATURES jit)
set(PCRE2_SUPPORT_JIT OFF)
set(CURRENT_BUILDTREES_DIR "${WORK}/buildtrees/pcre2")
set(CURRENT_PACKAGES_DIR "${WORK}/packages/pcre2_x64-windows")
file(MAKE_DIRECTORY "${CURRENT_BUILDTREES_DIR}" "${CURRENT_PACKAGES_DIR}/bin"
     "${WORK}/downloads" "${WORK}/src")
file(WRITE "${TRANSCRIPT_FILE}" "")

function(tx key value)
    string(REPLACE "\n" " " value "${value}")
    file(APPEND "${TRANSCRIPT_FILE}" "TRANSCRIPT|${key}|${value}\n")
endfunction()

# vcpkg's own downloader, reduced to the two behaviours this regression is about: the
# API answers 403 non-transiently, and a github.com archive URL is a valid source.
function(vcpkg_download_distfile out_var)
    cmake_parse_arguments(PARSE_ARGV 1 arg "SKIP_SHA512;SILENT_EXIT;QUIET;ALWAYS_REDOWNLOAD"
        "FILENAME;SHA512" "URLS;HEADERS")
    foreach(url IN LISTS arg_URLS)
        if(url MATCHES "^https://api[.]github[.]com/")
            tx("API-FETCH" "${url}")
            message(FATAL_ERROR
                "error: curl operation failed with response code 403.\n"
                "error: Not a transient network error, won't retry download from ${url}\n"
                "CMake Error: Download failed, halting portfile.")
        endif()
        tx("DOWNLOAD" "${url} -> ${arg_FILENAME}")
    endforeach()
    set(path "${WORK}/downloads/${arg_FILENAME}")
    file(WRITE "${path}" "stub:${arg_URLS}")
    set(${out_var} "${path}" PARENT_SCOPE)
endfunction()

function(vcpkg_from_github)
    cmake_parse_arguments(PARSE_ARGV 0 arg "USE_TARBALL_API"
        "OUT_SOURCE_PATH;REPO;REF;SHA512;HEAD_REF;GITHUB_HOST;AUTHORIZATION_TOKEN;FILE_DISAMBIGUATOR"
        "PATCHES")
    tx("FROM-GITHUB" "${arg_REPO}@${arg_REF} sha512=${arg_SHA512} patches=${arg_PATCHES}")
    foreach(patch IN LISTS arg_PATCHES)
        if(IS_ABSOLUTE "${patch}" AND EXISTS "${patch}")
            file(SHA512 "${patch}" patch_sha512)
            get_filename_component(patch_name "${patch}" NAME)
            tx("PATCH-BYTES" "${patch_name} ${patch_sha512}")
        endif()
    endforeach()
    set(dir "${WORK}/src/${arg_REPO}-${arg_REF}")
    file(MAKE_DIRECTORY "${dir}")
    file(WRITE "${dir}/README.zene-stub" "${arg_REPO} ${arg_REF}")
    set(${arg_OUT_SOURCE_PATH} "${dir}" PARENT_SCOPE)
endfunction()

function(vcpkg_check_features)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "OUT_FEATURE_OPTIONS" "FEATURES")
    set(opts "")
    set(jit OFF)
    set(i 0)
    list(LENGTH arg_FEATURES n)
    while(i LESS n)
        list(GET arg_FEATURES ${i} feature)
        math(EXPR j "${i} + 1")
        list(GET arg_FEATURES ${j} definition)
        if(feature STREQUAL "jit")
            set(jit ON)
            list(APPEND opts "-D${definition}=ON")
        endif()
        math(EXPR i "${i} + 2")
    endwhile()
    set(PCRE2_SUPPORT_JIT "${jit}" PARENT_SCOPE)
    if(arg_OUT_FEATURE_OPTIONS)
        set(${arg_OUT_FEATURE_OPTIONS} "${opts}" PARENT_SCOPE)
    endif()
endfunction()

function(vcpkg_cmake_configure)
    tx("CONFIGURE" "${ARGN}")
endfunction()

function(vcpkg_cmake_install)
    # Both configurations ship the tool the portfile moves into tools/.
    foreach(subdir "bin" "debug/bin")
        file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/${subdir}")
        file(WRITE "${CURRENT_PACKAGES_DIR}/${subdir}/pcre2-config" "#!/bin/sh\nexit 0\n")
    endforeach()
    tx("INSTALL" "ok")
endfunction()

function(vcpkg_cmake_config_fixup)
    tx("CONFIG-FIXUP" "${ARGN}")
endfunction()

function(vcpkg_replace_string path match replace)
    tx("REPLACE-STRING" "${path} :: ${match} -> ${replace}")
    if(NOT EXISTS "${path}")
        file(WRITE "${path}" "stub header\n")
    endif()
endfunction()

function(vcpkg_copy_pdbs)
endfunction()

function(vcpkg_fixup_pkgconfig)
    tx("FIXUP-PKGCONFIG" "ok")
endfunction()

function(vcpkg_clean_executables_in_bin)
    tx("CLEAN-EXES" "${ARGN}")
endfunction()

function(vcpkg_install_copyright)
    tx("COPYRIGHT" "${ARGN}")
endfunction()

include("${PORTFILE}")

tx("SOURCE-DIR" "${SOURCE_PATH}")
set(sljit_marker "${SOURCE_PATH}/deps/sljit/README.zene-stub")
if(EXISTS "${sljit_marker}")
    file(READ "${sljit_marker}" sljit_contents)
    tx("SLJIT-IN-SOURCE" "${sljit_contents}")
else()
    tx("SLJIT-IN-SOURCE" "MISSING")
endif()
tx("DONE" "ok")
'''


def sha512(path):
    return hashlib.sha512(Path(path).read_bytes()).hexdigest()


class Checker:
    """Collects PASS/FAIL lines so helpers can assert without passing a list around."""

    def __init__(self):
        self.failures = []

    def check(self, condition, label, detail=""):
        if condition:
            print(f"  PASS {label}")
        else:
            print(f"  FAIL {label}{': ' + detail if detail else ''}")
            self.failures.append(label)
        return bool(condition)


def effective_portfile(overlay_ports):
    """The port vcpkg resolves: the overlay when the configuration names one and the port
    is there, else the builtin registry's (the pinned copy kept as a fixture)."""
    for entry in overlay_ports:
        port_dir = (ROOT / entry / "pcre2").resolve()
        if (port_dir / "vcpkg.json").is_file() and (port_dir / "portfile.cmake").is_file():
            return port_dir / "portfile.cmake", port_dir, "overlay"
    return BUILTIN_PORTFILE, None, "builtin"


def run_portfile(portfile, work, use_head=False, head_version="0123456789abcdef0123456789abcdef01234567"):
    work = Path(work)
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    transcript = work / "transcript.txt"
    harness = work / "harness.cmake"
    # Model Windows text-mode file(WRITE) even when this regression runs on Unix.
    # Explicit NEWLINE_STYLE UNIX generation must still produce the pinned bytes.
    windows_write = r'''
macro(file operation)
    if("${operation}" STREQUAL "WRITE")
        set(windows_content "${ARGV2}")
        if("${ARGV1}" MATCHES "zene-patches/")
            string(REPLACE "\n" "\r\n" windows_content "${ARGV2}")
        endif()
        _file(WRITE "${ARGV1}" "${windows_content}")
    elseif("${operation}" STREQUAL "CONFIGURE")
        if("${ARGV}" MATCHES "NEWLINE_STYLE")
            _file(${ARGV})
        else()
            _file(${ARGV} NEWLINE_STYLE CRLF)
        endif()
    else()
        _file(${ARGV})
    endif()
endmacro()
'''
    harness.write_text(windows_write + CMAKELIB)
    # -D before -P: cmake applies options in order, and the script needs them at entry.
    cmd = ["cmake", f"-DPORTFILE={portfile}", f"-DWORK={work}", f"-DTRANSCRIPT_FILE={transcript}",
           f"-DVCPKG_USE_HEAD_VERSION={'ON' if use_head else 'OFF'}",
           f"-DVCPKG_HEAD_VERSION={head_version}",
           "-P", str(harness)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    entries = {}
    if transcript.is_file():
        for line in transcript.read_text().splitlines():
            if line.startswith("TRANSCRIPT|"):
                _, key, value = line.split("|", 2)
                entries.setdefault(key, []).append(value)
    return proc, entries


def load_config(checker):
    """The repository's vcpkg configuration, or {} when it does not exist. A missing or
    unparsable configuration is a FAIL, not a harness error: without it vcpkg falls back to
    the builtin registry, which is exactly the state that fetched the API at run time."""
    print("repository configuration")
    config = {}
    if CONFIG.is_file():
        try:
            config = json.loads(CONFIG.read_text())
            checker.check(str(CONFIG.parent) == str(ROOT),
                          "configuration sits beside vcpkg.json (vcpkg reads it there)")
        except ValueError as exc:
            checker.check(False, "vcpkg-configuration.json is valid JSON", str(exc))
    overlay_ports = config.get("overlay-ports") or []
    checker.check(bool(overlay_ports), "vcpkg-configuration.json names overlay port(s)", str(config))
    return overlay_ports


def api_calls_outside_head(text):
    """Line numbers of non-comment api.github.com mentions that are not inside the
    `if(VCPKG_USE_HEAD_VERSION)` block. The head branch may query: its gitlink is whatever
    the branch points at today and cannot be committed."""
    api_lines = {n for n, line in enumerate(text.splitlines(), 1)
                 if "api.github.com" in line and not line.lstrip().startswith("#")}
    inside, depth, head_region = set(), 0, False
    for n, line in enumerate(text.splitlines(), 1):
        if re.match(r"\s*if\(VCPKG_USE_HEAD_VERSION\)", line):
            head_region, depth = True, 1
            continue
        if head_region:
            depth += line.count("if(") - line.count("endif()")
            if depth <= 0:
                head_region = False
            elif n in api_lines:
                inside.add(n)
    return sorted(api_lines - inside)


def check_overlay_files(checker, port_dir):
    overlay_manifest = json.loads((port_dir / "vcpkg.json").read_text())
    builtin_manifest = json.loads(BUILTIN_MANIFEST.read_text())
    checker.check(overlay_manifest == builtin_manifest,
                  "overlay pcre2 manifest is the pinned one (10.49#2, jit unchanged)",
                  f"{overlay_manifest} != {builtin_manifest}")
    for name, expected in PATCH_SHA512.items():
        patch = port_dir / (name + ".json")
        content = json.loads(patch.read_text())["content"] if patch.is_file() else ""
        checker.check(hashlib.sha512(content.encode("utf-8")).hexdigest() == expected,
                      f"upstream patch {name} is byte-identical to the pinned commit")
    gitlink = port_dir / GITLINK_NAME
    checker.check(gitlink.is_file() and sha512(gitlink) == GITLINK_SHA512,
                  "committed gitlink response matches the SHA512 vcpkg pins for pcre2-10.49")
    outside = api_calls_outside_head((port_dir / "portfile.cmake").read_text())
    checker.check(not outside, "no api.github.com request outside the --head branch", str(outside))


def check_tagged_release(checker, portfile, tmp):
    print("tagged release (the path the hosted job takes)")
    proc, entries = run_portfile(portfile, Path(tmp) / "tagged")
    combined = proc.stdout + proc.stderr
    checker.check(proc.returncode == 0, "portfile completes", combined[-800:])
    checker.check(not entries.get("API-FETCH"), "requests no unauthenticated api.github.com URL",
                  str(entries.get("API-FETCH")))
    checker.check(entries.get("SLJIT-IN-SOURCE") == [f"zherczeg/sljit {SLJIT_SHA}"],
                  "sljit source is pinned to the gitlink's commit", str(entries.get("SLJIT-IN-SOURCE")))
    checker.check(sorted(entries.get("PATCH-BYTES", [])) ==
                  sorted(f"{name} {sha}" for name, sha in PATCH_SHA512.items()),
                  "the real portfile passes all three byte-identical patches to vcpkg")
    github = " ".join(entries.get("FROM-GITHUB", []))
    checker.check(f"PCRE2Project/pcre2@pcre2-10.49 sha512={PCRE2_SHA512}" in github,
                  "pcre2 tarball is the pinned tag and hash", github)
    checker.check(f"zherczeg/sljit@{SLJIT_SHA} sha512={SLJIT_SHA512}" in github,
                  "sljit tarball is the pinned commit and hash", github)
    checker.check(any("PCRE2_SUPPORT_JIT=ON" in c for c in entries.get("CONFIGURE", [])),
                  "the jit feature still reaches the build", str(entries.get("CONFIGURE")))


def check_builtin_control(checker, tmp):
    """RED CONTROL: the pinned builtin portfile is the repository without the overlay, and
    it must reproduce the CI's own failure - the API URL and the 403 verdict."""
    print("control: the pinned builtin portfile, i.e. the repository without the overlay")
    proc, entries = run_portfile(BUILTIN_PORTFILE, Path(tmp) / "builtin")
    combined = proc.stdout + proc.stderr
    checker.check(proc.returncode != 0, "the builtin portfile cannot complete (this is the CI failure)")
    checker.check(entries.get("API-FETCH") == [API_URL],
                  "it downloads exactly the URL the CI log names", str(entries.get("API-FETCH")))
    checker.check("curl operation failed with response code 403" in combined
                  and "Not a transient network error" in combined,
                  "with the CI's own 403 non-transient verdict", combined[-500:])


def check_head_control(checker, portfile, tmp):
    print("control: --head still resolves the gitlink from the API")
    proc, entries = run_portfile(portfile, Path(tmp) / "head", use_head=True)
    api = entries.get("API-FETCH", [])
    checker.check(proc.returncode != 0 and bool(api),
                  "a --head build queries the API (it cannot be pre-committed)", str(api))
    checker.check(any("/contents/deps/sljit?ref=0123456789abcdef0123456789abcdef01234567" in u for u in api),
                  "with the head ref in the URL", str(api))


def check_tampered_control(checker, port_dir, tmp):
    print("control: tampered committed gitlink")
    copy = Path(tmp) / "tampered"
    shutil.copytree(port_dir, copy)
    payload = json.loads((copy / GITLINK_NAME).read_text())
    payload["submodule_git_url"] = "https://evil.example/sljit.git"
    (copy / GITLINK_NAME).write_text(json.dumps(payload, indent=2) + "\n")
    proc, entries = run_portfile(copy / "portfile.cmake", Path(tmp) / "tampered-work")
    combined = proc.stdout + proc.stderr
    checker.check(proc.returncode != 0 and "SHA512" in combined,
                  "the hash check rejects a modified gitlink", combined[-400:])
    checker.check(not entries.get("SLJIT-IN-SOURCE"), "nothing was installed from it")


def check_missing_control(checker, port_dir, tmp):
    print("control: missing committed gitlink")
    copy = Path(tmp) / "missing"
    shutil.copytree(port_dir, copy)
    (copy / GITLINK_NAME).unlink()
    proc, entries = run_portfile(copy / "portfile.cmake", Path(tmp) / "missing-work")
    combined = proc.stdout + proc.stderr
    checker.check(proc.returncode != 0 and "gitlink response is missing" in combined,
                  "a missing gitlink fails loudly instead of silently fetching", combined[-400:])


def check_literal_patch_control(checker, port_dir, tmp):
    print("control: literal template tokens survive patch materialization")
    copy = Path(tmp) / "literal-patch"
    shutil.copytree(port_dir, copy)
    name = next(iter(PATCH_SHA512))
    patch = copy / (name + ".json")
    payload = json.loads(patch.read_text())
    payload["content"] += "@ZENE_LITERAL_TOKEN@;${ZENE_LITERAL_TOKEN}\n"
    expected = hashlib.sha512(payload["content"].encode()).hexdigest()
    patch.write_text(json.dumps(payload) + "\n")
    port = copy / "portfile.cmake"
    text = port.read_text().replace(PATCH_SHA512[name], expected)
    port.write_text('set(ZENE_LITERAL_TOKEN "must-not-expand")\n' + text)
    proc, entries = run_portfile(port, Path(tmp) / "literal-patch-work")
    checker.check(proc.returncode == 0 and
                  f"{name} {expected}" in entries.get("PATCH-BYTES", []),
                  "literal @ and dollar tokens keep their exact hashed bytes",
                  (proc.stdout + proc.stderr)[-500:])


def check_patch_control(checker, port_dir, tmp, missing):
    label = "missing" if missing else "tampered"
    print(f"control: {label} encoded upstream patch")
    copy = Path(tmp) / ("patch-" + label)
    shutil.copytree(port_dir, copy)
    patch = copy / (next(iter(PATCH_SHA512)) + ".json")
    if missing:
        patch.unlink()
    else:
        payload = json.loads(patch.read_text())
        payload["content"] += "tampered\n"
        patch.write_text(json.dumps(payload) + "\n")
    proc, entries = run_portfile(copy / "portfile.cmake", Path(tmp) / (label + "-patch-work"))
    checker.check(proc.returncode != 0, f"a {label} upstream patch fails closed")
    checker.check(not entries.get("FROM-GITHUB"), "no dependency source is used after patch failure")


def main():
    if not shutil.which("cmake"):
        print("vcpkg-pcre2-gitlink-test: cmake not found - SKIP", file=sys.stderr)
        return 77
    fixtures = [BUILTIN_PORTFILE, BUILTIN_MANIFEST, HERE / "data" / "vcpkg-pcre2" / "usage"]
    missing = [str(f) for f in fixtures if not f.is_file()]
    if missing:
        print(f"vcpkg-pcre2-gitlink-test: missing fixture(s) {', '.join(missing)}", file=sys.stderr)
        return 2

    checker = Checker()
    portfile, port_dir, origin = effective_portfile(load_config(checker))
    checker.check(origin == "overlay", "the repository resolves pcre2 from the overlay port",
                  f"resolved {portfile}")
    if port_dir is not None:
        check_overlay_files(checker, port_dir)

    with tempfile.TemporaryDirectory(prefix="vcpkg-pcre2-") as tmp:
        check_tagged_release(checker, portfile, tmp)
        check_builtin_control(checker, tmp)
        check_head_control(checker, portfile, tmp)
        if port_dir is not None:
            check_tampered_control(checker, port_dir, tmp)
            check_missing_control(checker, port_dir, tmp)
            check_literal_patch_control(checker, port_dir, tmp)
            check_patch_control(checker, port_dir, tmp, missing=False)
            check_patch_control(checker, port_dir, tmp, missing=True)

    print()
    if checker.failures:
        print(f"RESULT: FAIL - {len(checker.failures)} check(s): " + "; ".join(checker.failures))
        return 1
    print("RESULT: PASS - a tagged pcre2 release is installed without an api.github.com request")
    return 0


if __name__ == "__main__":
    sys.exit(main())
