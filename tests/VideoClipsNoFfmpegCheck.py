#!/usr/bin/env python3
"""The multi-clip video tests must compile when the build has no FFmpeg.

Cluster ci-78cca41150908714, hosted ``build / macos-x86_64``: the video test
sources are registered for every build, but VideoTestClip.h defines
``videotest::makeClip`` only inside its ``#ifdef ZENE_HAVE_FFMPEG`` block. The
tests called the helper unguarded, so the job without FFmpeg stopped the build:

    tests/src/core/VideoClipsTest.cpp:530:19: error: no member named 'makeClip'
        in namespace 'videotest'

VideoClipsTest and its GUI sibling VideoClipsGuiTest share that needClips() call
and both must compile in the same configuration. This check compiles each real
translation unit through its own line in build/compile_commands.json, first as
configured and then against a shadow lmmsconfig.h with ZENE_HAVE_FFMPEG removed
- the hosted shape. The shadow is proved in both directions with fixtures before
the translation units are compiled. Exit codes: 0 = every compile passes, 1 =
the test-source defect, 2 = the check cannot run.
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

SOURCES = (
    os.path.join("tests", "src", "core", "VideoClipsTest.cpp"),
    os.path.join("tests", "src", "gui", "VideoClipsGuiTest.cpp"),
)
CONFIG = "lmmsconfig.h"
MARKER = "ZENE_HAVE_FFMPEG"
HEADER = os.path.join("tests", "src", "core", "VideoTestClip.h")

# An unguarded caller: these are the three makeClip spellings the video tests use.
PROBE = """#include "lmmsconfig.h"
#include "VideoTestClip.h"
int main()
{
\tconst QString path;
\treturn videotest::makeClip(path) ||
\t       videotest::makeClip(path, videotest::Palette::RedBlue) ||
\t       videotest::makeClip(path, videotest::Palette::GreenYellow);
}
"""

# The no-FFmpeg fallback branch, from its #else to the matching #endif.
FALLBACK_RE = re.compile(
    r"#else\s+namespace videotest\s*\{.*?#endif // " + MARKER, re.S)

WINDOWS_COMMAND_RE = re.compile(r"(?:^|[\s=\"'])[A-Za-z]:[\\/]|\\\\[^\"']")


def fail(message):
    print("VideoClipsNoFfmpegCheck: " + message, file=sys.stderr)
    return 2


def split_command(entry):
    """The recorded compile line as an argument list (response files stay literal)."""
    if entry.get("command"):
        command = entry["command"]
        if WINDOWS_COMMAND_RE.search(command):
            escaped_quote = "\uE000"
            command = command.replace('\\"', escaped_quote)
            lexer = shlex.shlex(command, posix=True)
            lexer.whitespace_split = True
            lexer.commenters = ""
            lexer.escape = ""
            return [arg.replace(escaped_quote, '"') for arg in lexer]
        return shlex.split(command)
    return list(entry.get("arguments") or [])


def compile_flags(entry):
    """The recorded command without its output/depfile arguments or compile action."""
    args = split_command(entry)
    out, index = [], 0
    while index < len(args):
        arg = args[index]
        if arg in ("-o", "-MF", "-MT", "/Fo"):
            index += 2
            continue
        if arg.startswith("/Fo") or arg in ("-MD", "-MMD", "-MP", "-c", "/c"):
            index += 1
            continue
        out.append(arg)
        index += 1
    return out


def is_msvc(compiler):
    """True for the MSVC driver family: cl.exe and clang-cl."""
    return os.path.basename(compiler.replace("\\", "/")).lower() in (
        "cl", "cl.exe", "clang-cl", "clang-cl.exe")


def switch(compiler, msvc, other):
    """MSVC's spelling of a switch on cl/clang-cl, the gcc/clang one elsewhere."""
    return msvc if is_msvc(compiler) else other


def include_dirs(args):
    out, index = [], 0
    while index < len(args):
        arg = args[index]
        if arg in ("-I", "-isystem", "/I", "/external:I") and index + 1 < len(args):
            out.append(args[index + 1])
            index += 2
            continue
        if arg.startswith(("-I", "/I", "/external:I")):
            value = arg[len("/external:I"):] if arg.lower().startswith("/external:i") else arg[2:]
            if value:
                out.append(value)
        elif arg.startswith("-isystem") and len(arg) > len("-isystem"):
            out.append(arg[len("-isystem"):])
        index += 1
    return out


def run(command, directory, timeout=600):
    return subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=timeout)


def errors_of(result):
    output = (result.stderr or "") + "\n" + (result.stdout or "")
    return [line.strip() for line in output.splitlines() if "error" in line.lower()]


def tu_command(entry, includes):
    """The recorded compile line, made syntax-only and preceded by @includes."""
    args = compile_flags(entry)
    return ([args[0], switch(args[0], "/Zs", "-fsyntax-only")]
            + [switch(args[0], "/I", "-I") + path for path in includes] + args[1:])


def fixture_command(compiler, includes, source):
    return ([compiler, switch(compiler, "/Zs", "-fsyntax-only")]
            + [switch(compiler, "/I", "-I") + path for path in includes] + [source])


def source_path(root, entry):
    path = entry["file"]
    return os.path.normpath(path if os.path.isabs(path) else os.path.join(entry["directory"], path))


def find_entry(entries, root, relative):
    wanted = os.path.normpath(os.path.join(root, relative))
    for entry in entries:
        if os.path.normcase(source_path(root, entry)) == os.path.normcase(wanted):
            return entry
    return None


def find_config(entry):
    """The lmmsconfig.h the recorded command actually sees."""
    for include in include_dirs(compile_flags(entry)):
        directory = include if os.path.isabs(include) else os.path.join(entry["directory"], include)
        candidate = os.path.normpath(os.path.join(directory, CONFIG))
        if os.path.isfile(candidate):
            return candidate
    return None


def moc_include(entry, source, tempdir, moc):
    """Generate the .moc the test TU includes itself; (directory or None, detail)."""
    match = re.search(r'#include\s+"([\w.+-]+\.moc)"',
                      open(source, encoding="utf-8", errors="replace").read())
    if match is None:
        return None, ""
    if not moc or not os.path.isfile(moc):
        return None, "includes %s but no usable moc was given" % match.group(1)
    output = os.path.join(tempdir, match.group(1))
    command = [moc, source, "-o", output]
    for directory in include_dirs(compile_flags(entry)):
        command += ["-I", directory]
    produced = run(command, entry["directory"])
    if produced.returncode != 0:
        detail = (produced.stderr or produced.stdout).strip().splitlines()
        return None, "moc failed: " + (detail[-1] if detail else "no output")
    return tempdir, ""


def write_shadow(real, shadow):
    """Copy lmmsconfig.h without its ZENE_HAVE_FFMPEG define; return the define count."""
    text = open(real, encoding="utf-8", errors="replace").read()
    text, removed = re.subn(r"^[ \t]*#[ \t]*define[ \t]+" + MARKER + r"\b.*$",
                            "/* shadow: " + MARKER + " removed for the no-FFmpeg check */",
                            text, flags=re.M)
    if removed:
        with open(os.path.join(shadow, CONFIG), "w", encoding="utf-8") as handle:
            handle.write(text)
    return removed


def prove_shadow(entry, shadow, tempdir):
    """Fixtures prove the shadow removes the define and the configured header keeps it."""
    compiler = compile_flags(entry)[0]
    recorded = include_dirs(compile_flags(entry))
    base = os.path.join(tempdir, "fixture-configured.cpp")
    strict = os.path.join(tempdir, "fixture-shadowed.cpp")
    with open(base, "w", encoding="utf-8") as handle:
        handle.write('#include "%s"\n#ifndef %s\n#error configured header lost %s\n#endif\n'
                     % (CONFIG, MARKER, MARKER))
    with open(strict, "w", encoding="utf-8") as handle:
        handle.write('#include "%s"\n#ifdef %s\n#error shadow header kept %s\n#endif\n'
                     % (CONFIG, MARKER, MARKER))
    configured = run(fixture_command(compiler, recorded, base), entry["directory"])
    shadowed = run(fixture_command(compiler, [shadow] + recorded, strict), entry["directory"])
    print("shadow removes %s: %s" % (MARKER, "yes" if shadowed.returncode == 0 else "NO"))
    print("configured header defines %s: %s" % (MARKER, "yes" if configured.returncode == 0 else "NO"))
    if shadowed.returncode or configured.returncode:
        for result in (configured, shadowed):
            for line in errors_of(result):
                print("  " + line)
        return False
    return True


def compile_tu(entry, includes, label):
    result = run(tu_command(entry, includes), entry["directory"])
    print("%s: %s (exit %d)" % (label, "PASS" if result.returncode == 0 else "FAIL", result.returncode))
    if result.returncode:
        for line in errors_of(result):
            print("  " + line)
    return result.returncode == 0


def prepare_tu(entry, source, tempdir, moc):
    """The generated .moc include directory for a test TU; (includes, error)."""
    moc_dir, detail = moc_include(entry, source, tempdir, moc)
    if moc_dir is None and detail:
        return None, detail
    return ([moc_dir] if moc_dir else []), ""


def load_entries(build):
    """The build's compile_commands.json; (entries, error)."""
    commands = os.path.join(build, "compile_commands.json")
    if not os.path.isfile(commands):
        return None, "no compile_commands.json in " + build
    entries = json.load(open(commands))
    if not isinstance(entries, list) or not entries:
        return None, "invalid or empty compile_commands.json in " + build
    return entries, ""


def collect_sources(entries, root):
    """(relative, entry, source) for every TU the check compiles; (found, error)."""
    found = []
    for relative in SOURCES:
        entry = find_entry(entries, root, relative)
        if entry is None:
            return None, "no compile_commands.json entry for " + os.path.join(root, relative)
        source = os.path.join(root, relative)
        if not os.path.isfile(source):
            return None, "missing source " + source
        found.append((relative, entry, source))
    return found, ""


def prepare_all(found, tempdir, moc):
    """Compile each TU as configured; (prepared, error)."""
    prepared = []
    for relative, entry, source in found:
        includes, detail = prepare_tu(entry, source, tempdir, moc)
        if includes is None:
            return None, detail
        if not compile_tu(entry, includes, relative + " configured"):
            return None, relative + " does not compile as configured"
        prepared.append((relative, entry, includes))
    return prepared, ""


def shadow_compiles(prepared, shadow):
    """Compile each prepared TU without the macro; the failing relatives."""
    failed = []
    for relative, entry, includes in prepared:
        if not compile_tu(entry, [shadow] + includes, relative + " without " + MARKER):
            failed.append(relative)
    return failed


def force_no_ffmpeg(real, directory):
    """Write a lmmsconfig.h that includes the real one and then undefines the macro."""
    with open(os.path.join(directory, CONFIG), "w", encoding="utf-8") as handle:
        handle.write('#include "%s"\n#undef %s\n'
                     % (real.replace("\\", "/"), MARKER))


def remove_fallback(real, directory):
    """Copy VideoTestClip.h with the no-FFmpeg makeClip fallback removed."""
    text = open(real, encoding="utf-8", errors="replace").read()
    stripped, removed = FALLBACK_RE.subn("#endif // " + MARKER, text)
    if removed != 1:
        return False
    with open(os.path.join(directory, "VideoTestClip.h"), "w", encoding="utf-8") as handle:
        handle.write(stripped)
    return True


def probe_command(entry, includes, source):
    """The recorded compile line for the probe instead of its own translation unit."""
    args = compile_flags(entry)
    recorded = os.path.normcase(os.path.normpath(
        entry["file"] if os.path.isabs(entry["file"])
        else os.path.join(entry["directory"], entry["file"])))

    def is_recorded(arg):
        path = arg if os.path.isabs(arg) else os.path.join(entry["directory"], arg)
        return os.path.normcase(os.path.normpath(path)) == recorded

    return ([args[0], switch(args[0], "/Zs", "-fsyntax-only")]
            + [switch(args[0], "/I", "-I") + path for path in includes]
            + [arg for arg in args[1:] if not is_recorded(arg)] + [source])


def probe_make_clip(entry, root, tempdir):
    """Compile an unguarded no-FFmpeg makeClip probe; a negative control removes the fallback."""
    real_config = find_config(entry)
    if real_config is None:
        print("FAIL: could not find %s for the makeClip probe" % CONFIG)
        return False
    forced = os.path.join(tempdir, "probe-config")
    os.makedirs(forced)
    force_no_ffmpeg(real_config, forced)
    source = os.path.join(tempdir, "make-clip-no-ffmpeg.cpp")
    with open(source, "w", encoding="utf-8") as handle:
        handle.write(PROBE)
    core = os.path.dirname(os.path.join(root, HEADER))
    positive = run(probe_command(entry, [core, forced], source), entry["directory"])
    print("makeClip probe without %s: %s (exit %d)"
          % (MARKER, "PASS" if positive.returncode == 0 else "FAIL", positive.returncode))
    if positive.returncode:
        for line in errors_of(positive):
            print("  " + line)
        return False
    broken = os.path.join(tempdir, "probe-no-fallback")
    os.makedirs(broken)
    header = os.path.join(root, HEADER)
    if not os.path.isfile(header) or not remove_fallback(header, broken):
        print("FAIL: could not remove the makeClip fallback for the negative control")
        return False
    negative = run(probe_command(entry, [broken, core, forced], source), entry["directory"])
    print("makeClip probe with the fallback removed: %s (exit %d)"
          % ("FAIL" if negative.returncode else "PASS - unexpected", negative.returncode))
    if negative.returncode == 0:
        print("FAIL: the probe compiled without the fallback; it does not detect the defect")
        return False
    return True


def check(root, build, moc):
    entries, error = load_entries(build)
    if entries is None:
        return fail(error)
    found, error = collect_sources(entries, root)
    if found is None:
        return fail(error)
    with tempfile.TemporaryDirectory(prefix="videoclips-noffmpeg-") as tempdir:
        if not probe_make_clip(found[0][1], root, tempdir):
            print("FAIL: the no-FFmpeg makeClip probe failed")
            return 1
        prepared, error = prepare_all(found, tempdir, moc)
        if prepared is None:
            return fail(error)
        real = find_config(found[0][1])
        if real is None:
            return fail("could not find " + CONFIG + " on the recorded include path")
        shadow = os.path.join(tempdir, "shadow")
        os.makedirs(shadow)
        if not write_shadow(real, shadow):
            print("this build's %s already lacks %s; the configured compiles are the no-FFmpeg proof"
                  % (CONFIG, MARKER))
            return 0
        if not prove_shadow(found[0][1], shadow, tempdir):
            return fail("the shadow header does not take effect")
        failed = shadow_compiles(prepared, shadow)
        if failed:
            print("FAIL: %s do not compile without %s" % (", ".join(failed), MARKER))
            return 1
        print("PASS: every video clip test compiles in a build without " + MARKER)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--moc", default="")
    args = parser.parse_args()
    root = os.path.abspath(args.source_root)
    build = os.path.abspath(args.build_dir)
    return check(root, build, args.moc)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        sys.exit(fail(str(error)))
