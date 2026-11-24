#!/usr/bin/env python3
"""Focused regression checks for train 39 portability fixes.

Usage: python3 tests/train-39-portability-check.py [all|vst2|sfz|qt5|rnnoise|disk|unused-result]
Optional paths/tools: --source-root, --build-dir, --cc, --cxx.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


def run(command: list[str], *, cwd: pathlib.Path | None = None) -> subprocess.CompletedProcess[str]:
	return subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
	                      stderr=subprocess.PIPE, check=False)


def report(label: str, result: subprocess.CompletedProcess[str]) -> bool:
	if result.returncode == 0:
		print(f"PASS {label}")
		return True
	print(f"FAIL {label} (exit {result.returncode})")
	output = (result.stdout + result.stderr).strip()
	print(output if output else "command produced no diagnostic")
	return False


def require_file(path: pathlib.Path, label: str) -> bool:
	if path.is_file():
		return True
	print(f"FAIL {label}: required file missing: {path}")
	return False


def expect_rejected(cxx: str, flags: list[str], source: pathlib.Path, output: pathlib.Path,
                    label: str, detail: str) -> bool:
	result = run([cxx, *flags, str(source), "-o", str(output)])
	if result.returncode == 0:
		print(f"FAIL {label}: {detail}")
		return False
	print(f"PASS {label}")
	return True


def test_vst2(root: pathlib.Path, cc: str) -> bool:
	source = root / "tests/data/vst2-test-effect/vst2-test-effect.c"
	return require_file(source, "VST2 source") and report(
		"VST2 gnu89 snprintf declaration", run([
			cc, "-std=gnu89", "-Werror=implicit-function-declaration", "-fsyntax-only",
			"-I" + str(root / "tests/data/vst2-test-instrument"), str(source),
		]))


def pkg_flags(package: str, option: str) -> tuple[list[str], str | None]:
	result = run(["pkg-config", option, package])
	if result.returncode:
		return [], (result.stderr.strip() or f"pkg-config {option} {package} failed")
	return shlex.split(result.stdout), None


def test_sfz(root: pathlib.Path, cc: str) -> bool:
	source = root / "plugins/SfzPlayer/sfizz/external/st_audiofile/src/st_audiofile_sndfile.c"
	if not require_file(source, "SFZ source"):
		return False
	cflags, error = pkg_flags("sndfile", "--cflags")
	if error:
		print(f"FAIL SFZ sndfile flags: {error}")
		return False
	base = [cc, "-std=c11", "-Werror=pointer-arith", "-DST_AUDIO_FILE_USE_SNDFILE",
	        *cflags, "-fsyntax-only", str(source)]
	checks = [report("SFZ sndfile const pointer compile", run(base))]
	with tempfile.TemporaryDirectory(prefix="train39-sfz-") as temp:
		dirpath = pathlib.Path(temp)
		harness = dirpath / "memory-vio.c"
		binary = dirpath / "memory-vio"
		quoted_source = str(source).replace("\\", "\\\\").replace('"', '\\"')
		harness.write_text(
			f'#include "{quoted_source}"\n'
			"int main(void) {\n"
			"  const char storage[] = \"prefixabcdefsuffix\";\n"
			"  struct st_audio_file af = {0}; char out[8] = {0};\n"
			"  af.data = storage + 6; af.size = 6; af.offset = 2;\n"
			"  if (sndfile_vio_get_filelen(&af) != 6 || sndfile_vio_tell(&af) != 2) return 1;\n"
			"  if (sndfile_vio_read(out, 3, &af) != 3 || memcmp(out, \"cde\", 3) || af.offset != 5) return 2;\n"
			"  if (sndfile_vio_read(out + 3, 8, &af) != 1 || out[3] != 'f' || af.offset != 6) return 3;\n"
			"  if (sndfile_vio_read(out, 1, &af) != 0 || af.offset != 6) return 4;\n"
			"  af.offset = 0; if (sndfile_vio_seek(4, SEEK_SET, &af) || af.offset != 4) return 5;\n"
			"  if (sndfile_vio_seek(1, SEEK_CUR, &af) || af.offset != 5) return 6;\n"
			"  if (sndfile_vio_seek(0, SEEK_SET, &af) || af.offset != 0) return 7;\n"
			"  return 0;\n}\n", encoding="utf-8")
		ldflags, ld_error = pkg_flags("sndfile", "--libs")
		if ld_error:
			print(f"FAIL SFZ behavior harness flags: {ld_error}")
			return False
		built = run([cc, "-std=c11", "-Werror=pointer-arith", "-DST_AUDIO_FILE_USE_SNDFILE",
		            *cflags, str(harness), *ldflags, "-o", str(binary)])
		checks.append(report("SFZ memory callback harness build", built))
		if built.returncode == 0:
			checks.append(report("SFZ offset/chunk/EOF memory callbacks", run([str(binary)])))
	return all(checks)


def test_qt5(root: pathlib.Path, cxx: str, build: pathlib.Path) -> bool:
	source = root / "plugins/CarlaBase/CarlaVst2Discovery.cpp"
	if not require_file(source, "Carla discovery source"):
		return False
	flags: list[str] = []
	for package in ("Qt5Widgets", "Qt5Xml", "Qt5Network"):
		part, error = pkg_flags(package, "--cflags")
		if error:
			print(f"FAIL Qt5 flags: {error}")
			return False
		flags.extend(part)
	for relative in ("include", "build", "build/src", "build/plugins/CarlaBase",
	                 "plugins/CarlaBase/carla/source/includes",
	                 "plugins/CarlaBase/carla/source/backend",
	                 "plugins/CarlaBase/carla/source/utils"):
		path = build / relative.removeprefix("build/") if relative.startswith("build/") else root / relative
		if relative == "build":
			path = build
		flags.extend(["-I", str(path)])
	return report("Qt5 Carla discovery syntax", run([
		cxx, "-std=gnu++17", "-fsyntax-only", *flags, str(source),
	]))


def test_rnnoise(root: pathlib.Path, build: pathlib.Path, clang: str | None) -> bool:
	header = build / "tests/rnnoise-test-export-definition.h"
	if not require_file(header, "generated RNNoise export definition"):
		return False
	if not clang:
		print("FAIL RNNoise Windows import/export probe: clang is required")
		return False
	with tempfile.TemporaryDirectory(prefix="train39-rnnoise-") as temp:
		fixture = pathlib.Path(temp) / "cdecl.c"
		fixture.write_text(
			f'#include "{header}"\n'
			"#if ZENE_RNNOISE_TEST_IS_EXPORT\n"
			"# define API __declspec(dllexport)\n"
			"#else\n"
			"# define API __declspec(dllimport)\n"
			"#endif\n"
			"API int rnnoise_exported_data = 7;\n"
			"API int rnnoise_exported_function(void) { return rnnoise_exported_data; }\n",
			encoding="utf-8")
		cmd = [clang, "--target=x86_64-pc-windows-msvc", "-fsyntax-only", str(fixture)]
		actual = run(cmd)
		checks = [report("RNNoise generated Windows export definition", actual)]
		fixture.write_text(fixture.read_text().replace(f'#include "{header}"',
		                   "#define ZENE_RNNOISE_TEST_IS_EXPORT 0"), encoding="utf-8")
		negative = run(cmd)
		if negative.returncode == 0:
			print("FAIL RNNoise negative import control unexpectedly compiled")
			checks.append(False)
		else:
			print("PASS RNNoise negative import control rejected exported definitions")
		return all(checks)


def test_unused_result(root: pathlib.Path, cxx: str, qt_include: list[str]) -> bool:
	header = root / "tests/src/core/MountNamespaceTestSupport.h"
	if not require_file(header, "mount namespace test support header"):
		return False
	# -Werror=unused-variable makes the compile fail when a caller keeps
	# writeNamespaceHandshakeByte's result in `notified` but drops it instead of
	# combining it into the caller's own return value.
	flags = ["-std=c++20", "-O2", "-D_FORTIFY_SOURCE=3",
	         "-Werror=unused-result", "-Werror=unused-variable", "-c"]
	qt_flags: list[str] = []
	for directory in qt_include:
		qt_flags.extend(["-I", directory])
	if not qt_flags:
		for package in ("Qt6Core", "Qt5Core"):
			part, error = pkg_flags(package, "--cflags")
			if not error:
				qt_flags = part
				break
		if not qt_flags:
			print("FAIL unused-result Qt flags: neither Qt6Core nor Qt5Core is available")
			return False
	with tempfile.TemporaryDirectory(prefix="train39-unused-result-") as temp:
		dirpath = pathlib.Path(temp)
		checks: list[bool] = []

		# The control makes the probe falsifiable: under these flags, a discarded write
		# result must be rejected, which is exactly what the hosted job reported.
		ignored = dirpath / "ignored-write.cpp"
		ignored.write_text(
			"#include <unistd.h>\n"
			"void probe()\n"
			"{\n"
			"\tchar byte = 0;\n"
			"\t(void)::write(1, &byte, 1);\n"
			"}\n", encoding="utf-8")
		control = run([cxx, *flags, str(ignored), "-o", str(dirpath / "ignored-write.o")])
		if control.returncode == 0:
			print("FAIL unused-result control: a discarded write compiled, so the probe cannot detect the defect")
			checks.append(False)
		else:
			print("PASS unused-result control: a discarded write result is rejected")
			checks.append(True)

		# The exact historical shape: both handshakes as they were before the fix, a
		# (void) cast on each ::write. Hosted linux-arm64 rejected both (historical
		# MountNamespaceTestSupport.h lines 78 and 110); if this fixture compiles, the
		# probe has stopped pinning that regression.
		historic = dirpath / "historic-unchecked-handshakes.cpp"
		historic.write_text(
			"#include <unistd.h>\n"
			"bool mapOwnUidIntoNamespace(int writeEnd)\n"
			"{\n"
			"\tchar done = 1;\n"
			"\t(void)::write(writeEnd, &done, 1);\n"
			"\treturn true;\n"
			"}\n"
			"bool enterOwnMountNamespace(int toChildWrite)\n"
			"{\n"
			"\tchar go = 1;\n"
			"\t(void)::write(toChildWrite, &go, 1);\n"
			"\treturn true;\n"
			"}\n", encoding="utf-8")
		checks.append(expect_rejected(
			cxx, flags, historic, dirpath / "historic-unchecked-handshakes.o",
			"unused-result historical handshakes",
			"both unchecked (void)::write calls compiled, so the historical regression is not pinned"))

		# ...while a checked write is accepted, so the flags reject the discarded result
		# and not write() itself.
		checked = dirpath / "checked-write.cpp"
		checked.write_text(
			"#include <unistd.h>\n"
			"void probe()\n"
			"{\n"
			"\tchar byte = 0;\n"
			"\tconst bool written = ::write(1, &byte, 1) == 1;\n"
			"\t(void)written;\n"
			"}\n", encoding="utf-8")
		checks.append(report("unused-result checked write fixture",
			run([cxx, *flags, str(checked), "-o", str(dirpath / "checked-write.o")])))

		# A caller that stores the notification but never propagates it is still a
		# discarded result. The hosted compile of the real header must reject that
		# shape; -Werror=unused-variable is what turns the unused local into an error.
		dropped = dirpath / "dropped-handshake-result.cpp"
		dropped.write_text(
			"#include <unistd.h>\n"
			"#include <cerrno>\n"
			"static bool writeNamespaceHandshakeByte(int fd, char value)\n"
			"{\n"
			"\tssize_t sent;\n"
			"\tdo { sent = ::write(fd, &value, 1); } while (sent < 0 && errno == EINTR);\n"
			"\treturn sent == 1;\n"
			"}\n"
			"bool probe(int writeEnd)\n"
			"{\n"
			"\tchar done = 1;\n"
			"\tconst bool notified = writeNamespaceHandshakeByte(writeEnd, done);\n"
			"\treturn true;\n"
			"}\n", encoding="utf-8")
		checks.append(expect_rejected(
			cxx, flags, dropped, dirpath / "dropped-handshake-result.o",
			"unused-result dropped handshake notification",
			"a stored but unpropagated handshake result compiled, so propagation is not pinned"))

		propagated = dirpath / "propagated-handshake-result.cpp"
		propagated.write_text(
			dropped.read_text(encoding="utf-8").replace(
				"\treturn true;\n}\n", "\treturn notified;\n}\n"), encoding="utf-8")
		checks.append(report("unused-result propagated handshake represented",
			run([cxx, *flags, str(propagated), "-o", str(dirpath / "propagated-handshake-result.o")])))

		# The real header, compiled in a translation unit that uses both helpers: the
		# hosted linux-arm64 build stopped on these two ignored write() results (lines
		# 78 and 110), and (void) did not silence -Wunused-result under fortify.
		probe = dirpath / "mount-namespace-probe.cpp"
		probe.write_text(
			'#include "MountNamespaceTestSupport.h"\n'
			"\n"
			"int main()\n"
			"{\n"
			"\tint descriptors[2] = {-1, -1};\n"
			"\t(void)lmms::testsupport::mapOwnUidIntoNamespace(1, descriptors[0], descriptors[1]);\n"
			"\t(void)lmms::testsupport::enterOwnMountNamespace();\n"
			"\treturn 0;\n"
			"}\n", encoding="utf-8")
		checks.append(report("mount namespace helper checks every write result",
			run([cxx, *flags, "-I", str(header.parent), *qt_flags, str(probe),
			     "-o", str(dirpath / "mount-namespace-probe.o")])))
		return all(checks)


def linux_jobs_have_strip_flag(workflow: str) -> tuple[bool, str]:
	jobs = re.split(r"(?m)^  ([A-Za-z0-9_-]+):\s*$", workflow)
	blocks = {jobs[i]: jobs[i + 1] for i in range(1, len(jobs), 2)}
	for name in ("linux-x86_64", "linux-arm64"):
		block = blocks.get(name)
		if block is None:
			return False, f"workflow has no {name} job"
		match = re.search(r"(?ms)^\s+CMAKE_OPTS:\s*>-\s*\n((?:^\s{8,}.*\n?)*)", block)
		if not match or "-DZENE_TEST_STRIP_DEBUG=ON" not in match.group(1):
			return False, f"{name} CMAKE_OPTS lacks -DZENE_TEST_STRIP_DEBUG=ON"
	return True, "both native Linux jobs carry the strip-debug linker option"


def inspect_executable(readelf: str, output: pathlib.Path, label: str) -> bool:
	symbols = run([readelf, "-sW", str(output)])
	sections = run([readelf, "-SW", str(output)])
	if symbols.returncode or sections.returncode:
		return report(f"Linux disk {label} ELF inspection", symbols if symbols.returncode else sections)
	has_symbol = bool(re.search(r"\bGLOBAL\b.*\bretained_global\b", symbols.stdout))
	has_debug = bool(re.search(r"\s\.debug_[A-Za-z0-9_.]+\s", sections.stdout))
	expected_debug = label == "unstripped"
	valid = has_symbol and has_debug == expected_debug
	if valid:
		print(f"PASS Linux disk {label} retains global symbol; debug sections {('present' if has_debug else 'absent')}")
	else:
		print(f"FAIL Linux disk {label}: symbol={has_symbol}, debug_sections={has_debug}")
	return report(f"Linux disk {label} probe execution", run([str(output)])) and valid


def test_disk(root: pathlib.Path, cc: str, build: pathlib.Path) -> bool:
	workflow_path = root / ".github/workflows/build.yml"
	if not require_file(workflow_path, "build workflow"):
		return False
	ok, message = linux_jobs_have_strip_flag(workflow_path.read_text(encoding="utf-8"))
	if not ok:
		print(f"FAIL Linux disk workflow option: {message}")
		return False
	print(f"PASS Linux disk workflow option: {message}")
	header = build / "tests/train39-test-link-option.h"
	if not require_file(header, "generated test link option"):
		return False
	requested = "#define ZENE_TEST_STRIP_DEBUG_REQUESTED 1" in header.read_text()
	actual = "#define ZENE_TEST_LINK_STRIPS_DEBUG 1" in header.read_text()
	if actual != requested:
		print("FAIL Linux disk test link option does not match the requested configuration")
		return False
	readelf = shutil.which("readelf")
	if not readelf:
		print("FAIL Linux disk ELF check: readelf is required")
		return False
	with tempfile.TemporaryDirectory(prefix="train39-disk-") as temp:
		dirpath = pathlib.Path(temp)
		source = dirpath / "probe.c"
		source.write_text("int retained_global = 17; int main(void) { return retained_global != 17; }\n",
		                  encoding="utf-8")
		plain, stripped = dirpath / "plain", dirpath / "stripped"
		checks: list[bool] = []
		for label, output, extra in (("unstripped", plain, []),
		                             ("strip-debug", stripped, ["-Wl,--strip-debug"])):
			built = run([cc, "-g", str(source), *extra, "-o", str(output)])
			checks.append(report(f"Linux disk {label} probe link", built))
			if built.returncode:
				continue
			checks.append(inspect_executable(readelf, output, label))

		return all(checks)


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("mode", nargs="?", choices=("all", "vst2", "sfz", "qt5", "rnnoise", "disk",
	                                                "unused-result"), default="all")
	parser.add_argument("--source-root", type=pathlib.Path,
	                    default=pathlib.Path(__file__).resolve().parents[1])
	parser.add_argument("--build-dir", type=pathlib.Path)
	parser.add_argument("--cc", default=os.environ.get("CC") or shutil.which("clang") or "cc")
	parser.add_argument("--cxx", default=os.environ.get("CXX") or "c++")
	parser.add_argument("--clang", default=shutil.which("clang"))
	parser.add_argument("--qt-include", nargs="*", default=[])
	return parser.parse_args()


def main() -> int:
	args = parse_args()
	root = args.source_root.resolve()
	build = (args.build_dir or root / "build").resolve()
	selected = ("vst2", "sfz", "qt5", "rnnoise", "disk",
	            "unused-result") if args.mode == "all" else (args.mode,)
	runners = {
		"vst2": lambda: test_vst2(root, args.cc),
		"sfz": lambda: test_sfz(root, args.cc),
		"qt5": lambda: test_qt5(root, args.cxx, build),
		"rnnoise": lambda: test_rnnoise(root, build, args.clang),
		"disk": lambda: test_disk(root, args.cc, build),
		"unused-result": lambda: test_unused_result(root, args.cxx, args.qt_include),
	}
	results = [runners[name]() for name in selected]
	print(f"RESULT {sum(results)}/{len(results)} groups passed")
	return 0 if all(results) else 1


if __name__ == "__main__":
	sys.exit(main())
