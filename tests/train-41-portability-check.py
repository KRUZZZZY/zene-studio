#!/usr/bin/env python3
"""Compile focused portability regression probes from the configured build."""

import argparse
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


def fail(message):
    raise RuntimeError(message)


def require_file(path):
    if not path.is_file():
        fail(f"required source or build file is missing: {path}")


def run(command, cwd=None):
    print("+", shlex.join([str(part) for part in command]), flush=True)
    result = subprocess.run(command, cwd=cwd, check=False)
    if result.returncode:
        fail(f"compiler command failed with exit code {result.returncode}")


def compile_database(build):
    path = build / "compile_commands.json"
    require_file(path)
    with path.open(encoding="utf-8") as stream:
        entries = json.load(stream)
    if not isinstance(entries, list):
        fail(f"invalid compilation database: {path}")
    return entries


def entry_for(entries, root, filename):
    wanted = (root / filename).resolve()
    matches = [item for item in entries if Path(item.get("file", "")).resolve() == wanted]
    if not matches:
        fail(f"no compile_commands.json entry for {wanted}")
    entry = matches[0]
    command = entry.get("arguments")
    if command is None:
        command = shlex.split(entry.get("command", ""))
    if not command:
        fail(f"compile database entry has no command for {wanted}")
    return entry, command


def syntax_command(entry, command, extra=()):
    cleaned = []
    skip = False
    paired = {"-o", "-MF", "-MT", "-MQ"}
    for token in command:
        if skip:
            skip = False
            continue
        if token in paired:
            skip = True
        elif token in {"-c", "-MD", "-MMD", "-MP"}:
            continue
        elif token.startswith(("-o", "-MF", "-MT", "-MQ")) and token not in {"-o"}:
            continue
        else:
            cleaned.append(token)
    if not cleaned:
        fail("compile command has no compiler")
    cleaned.extend(["-fsyntax-only", *extra])
    return cleaned


def pkg_config(packages):
    tool = shutil.which("pkg-config")
    if tool is None:
        fail("required tool is missing: pkg-config")
    result = subprocess.run([tool, "--cflags", *packages], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        fail(f"pkg-config {' '.join(packages)} failed: {result.stderr.strip()}")
    return shlex.split(result.stdout)


def mode_linux(root, qt_major="6"):
    header = root / "tests/src/core/MountNamespaceTestSupport.h"
    require_file(header)
    compiler = shutil.which("c++")
    if compiler is None:
        fail("required tool is missing: c++")
    source = '#include "MountNamespaceTestSupport.h"\nint main() { return lmms::testsupport::enterOwnMountNamespace(); }\n'
    with tempfile.TemporaryDirectory(prefix="train41-linux-") as temporary:
        probe = Path(temporary) / "mount_probe.cpp"
        probe.write_text(source, encoding="utf-8")
        command = [compiler, "-std=c++20", "-O2", "-D_FORTIFY_SOURCE=2",
                   "-Werror=unused-result", "-fPIC", *pkg_config(["Qt" + qt_major + "Core"]),
                   "-I" + str(root / "tests/src/core"), "-c", str(probe), "-o", str(Path(temporary) / "probe.o")]
        run(command, cwd=root)


def mode_eq(root, build):
    source = root / "plugins/Eq/EqSpectrumView.cpp"
    require_file(source)
    compiler = shutil.which("c++")
    if compiler is None:
        fail("required tool is missing: c++")
    flags = pkg_config(["Qt5Widgets", "Qt5Xml", "Qt5Network"])
    command = [compiler, "-std=c++20", "-fPIC", *flags, "-I" + str(root / "include"),
               "-I" + str(build), "-I" + str(build / "src"),
               "-I" + str(root / "plugins/Eq"), "-I" + str(build / "plugins/Eq"),
               "-I/usr/include/cairo", "-DPLUGIN_NAME=eq", "-fsyntax-only", str(source)]
    run(command, cwd=root)


def signature_parts(source):
    text = source.read_text(encoding="utf-8")
    constant = re.search(r"constexpr\s+bool\s+sigactionIsNoexcept\s*=\s*[^;]+;", text)
    declaration = re.search(r'extern\s+"C"\s+int\s+sigaction[^;{]+\{', text)
    if declaration is None:
        fail(f"could not extract actual sigaction declaration/constant from {source}")
    return constant.group(0) if constant else "", declaration.group(0)[:-1].rstrip()


def mode_darwin_signature(root):
    source = root / "tests/src/core/CrashReporterArmTest.cpp"
    require_file(source)
    constant, definition = signature_parts(source)
    compiler = shutil.which("c++")
    if compiler is None:
        fail("required tool is missing: c++")
    with tempfile.TemporaryDirectory(prefix="train41-sigaction-") as temporary:
        probe = Path(temporary) / "sigaction_contract.cpp"
        for is_noexcept in (True, False):
            prior = " noexcept" if is_noexcept else ""
            code = ("struct sigaction;\nextern \"C\" int sigaction(int, const struct sigaction*, "
                    f"struct sigaction*){prior};\n{constant}\n{definition} {{ return 0; }}\n")
            probe.write_text(code, encoding="utf-8")
            run([compiler, "-std=c++20", "-fsyntax-only", str(probe)], cwd=root)


def mode_darwin_tu(root, build):
    entries = compile_database(build)
    entry, command = entry_for(entries, root, "tests/src/core/CrashReporterArmTest.cpp")
    with tempfile.TemporaryDirectory(prefix="train41-signal-shadow-") as temporary:
        shadow = Path(temporary)
        (shadow / "signal.h").write_text(
            "#include_next <signal.h>\n#define sigemptyset(set) (0)\n", encoding="utf-8")
        args = syntax_command(entry, command, ["-I" + str(shadow)])
        run(args, cwd=entry["directory"])


def mode_nam(root, build):
    entries = compile_database(build)
    entry, command = entry_for(entries, root, "tests/src/core/NamModelLoaderTest.cpp")
    with tempfile.TemporaryDirectory(prefix="train41-filesystem-shadow-") as temporary:
        shadow = Path(temporary) / "filesystem"
        shadow.mkdir()
        (shadow / "filesystem").write_text("#error <filesystem> is forbidden in this test\n",
                                           encoding="utf-8")
        args = syntax_command(entry, command, ["-I" + str(shadow)])
        run(args, cwd=entry["directory"])


def source_headers(source):
    headers = []
    for match in re.finditer(r"^\s*#\s*include\s*([<\"])([^>\"]+)[>\"]", source.read_text(encoding="utf-8"), re.M):
        delimiter, name = match.groups()
        if name.endswith(".moc"):
            continue
        if delimiter == '"':
            candidate = source.parent / name
            if candidate.is_file():
                headers.append(candidate.resolve())
                continue
        headers.append(("<" + name + ">") if delimiter == "<" else '"' + name + '"')
    return list(dict.fromkeys(headers))


def mode_render(root, build, moc):
    entries = compile_database(build)
    entry, command = entry_for(entries, root, "tests/src/core/RenderSourceGuardTest.cpp")
    source = root / "tests/src/core/RenderSourceGuardTest.cpp"
    with tempfile.TemporaryDirectory(prefix="train41-win-guard-") as temporary:
        preinclude = Path(temporary) / "headers_then_win32.h"
        lines = []
        for header in source_headers(source):
            lines.append(f'#include "{header}"' if isinstance(header, Path) else f"#include {header}")
        lines.append("#define _WIN32 1")
        preinclude.write_text("\n".join(lines) + "\n", encoding="utf-8")
        run([moc, "-D_WIN32=1", str(source), "-o",
             str(Path(temporary) / "RenderSourceGuardTest.moc")], cwd=root)
        args = syntax_command(entry, command,
                              ["-include", str(preinclude), "-Werror=unused-function"])
        args.insert(1, "-I" + temporary)
        args.remove("-fsyntax-only")
        args.extend(["-c", "-o", str(Path(temporary) / "render.o")])
        run(args, cwd=entry["directory"])


MODES = {
    "linux": mode_linux,
    "eq": mode_eq,
    "darwin-signature": mode_darwin_signature,
    "darwin-tu": mode_darwin_tu,
    "nam": mode_nam,
    "render": mode_render,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--moc", default="/usr/lib/qt6/moc")
    parser.add_argument("--qt-major", choices=["5", "6"], default="6")
    parser.add_argument("--mode", choices=["all", *MODES], default="all")
    args = parser.parse_args()
    root = args.source_root.resolve()
    build = args.build_dir.resolve()
    require_file(root / "tests/src/core/MountNamespaceTestSupport.h")
    selected = MODES if args.mode == "all" else {args.mode: MODES[args.mode]}
    for name, check in selected.items():
        print(f"[{name}]", flush=True)
        if name == "linux":
            check(root, args.qt_major)
        elif name == "render":
            check(root, build, args.moc)
        elif name in {"eq", "darwin-tu", "nam"}:
            check(root, build)
        else:
            check(root)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"portability check: {error}", file=sys.stderr)
        sys.exit(1)
