#!/usr/bin/env python3
"""Fail when VideoClipsTest.cpp compiles only while FFmpeg is enabled.

Cluster ci-9b6002ebebcf807a (2026-11-15). The hosted macos-arm64 job has no FFmpeg
(ZENE_WANT_FFMPEG=ON, but pkg-config found no libav*), so lmmsconfig.h does not define
ZENE_HAVE_FFMPEG. The original fixture did not declare videotest::makeClip there. VideoClipsTest's
needClips() called it unconditionally and that build stopped at:

    tests/src/core/VideoClipsTest.cpp:530:19: error: no member named 'makeClip' in namespace 'videotest'

VideoTrackTest already guards the same call with #ifdef ZENE_HAVE_FFMPEG and keeps its
runtime skip for a build without the library. This check recompiles the real translation
unit from this build's own compile_commands.json with a shadow lmmsconfig.h that does not
define ZENE_HAVE_FFMPEG - the configuration the macOS job builds - so the defect is red on
a Linux host that has FFmpeg.

The real fixture now retains a fallback API; a forced include renames it only in
this compiler model, preserving the accepted missing-helper contract for both callers.
Driver flags and Windows command parsing are shared with VideoClipsNoFfmpegCheck.

Two probes run first: the shadow configuration must really lack ZENE_HAVE_FFMPEG, and
videotest::makeClip must really be undeclared there. A check that cannot fail fails here.

Deterministic: it compiles the recorded command against a fixed shadow header; no clock,
sleep, network or configure.

Usage: no-ffmpeg-video-clips-compile-check.py --source-root <tree> --build-dir <build>
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

from VideoClipsNoFfmpegCheck import split_command, switch

TARGET = "tests/src/core/VideoClipsTest.cpp"
CONFIG_PROBE = """#include "lmmsconfig.h"
#ifdef ZENE_HAVE_FFMPEG
#error the shadow configuration still defines ZENE_HAVE_FFMPEG
#endif
int main() { return 0; }
"""
ABSENT_PROBE = """#include <QString>
#include "VideoTestClip.h"
int main() { return videotest::makeClip(QStringLiteral("x")) ? 0 : 1; }
"""


def run(command, cwd, timeout=300):
	return subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=timeout)


def source_of(entry):
	path = entry["file"]
	if not os.path.isabs(path):
		path = os.path.join(entry["directory"], path)
	return os.path.normpath(path)


def include_position(command):
	"""Where to put the shadow -I so it wins over every recorded include directory."""
	for index, argument in enumerate(command[1:], 1):
		if argument == "-I" or argument.startswith("-I") or argument == "-isystem" or argument.startswith(("-isystem", "/I", "/external:I")):
			return index
	return len(command)


def syntax_command(entry, shadow):
	"""The recorded compile line, with the shadow include and no output/compile action."""
	cleaned = []
	skip = False
	for argument in split_command(entry):
		if skip:
			skip = False
			continue
		if argument in ("-o", "-MF", "-MT", "-MQ", "/Fo"):
			skip = True
			continue
		if argument in ("-c", "-MD", "-MMD", "-MP", "/c"):
			continue
		if argument.startswith(("-o", "-MF", "-MT", "-MQ", "/Fo")):
			continue
		cleaned.append(argument)
	if not cleaned:
		return None
	cleaned.insert(include_position(cleaned), switch(cleaned[0], "/I", "-I") + shadow)
	cleaned.extend([switch(cleaned[0], "/Zs", "-fsyntax-only"), switch(cleaned[0], "/FI", "-include"), os.path.join(shadow, "hide-clip-helper.h")])
	return cleaned


def load_entry(build_dir, source_root, target=TARGET):
	path = os.path.join(os.path.abspath(build_dir), "compile_commands.json")
	if not os.path.isfile(path):
		return None, "no compile_commands.json in " + os.path.abspath(build_dir)
	with open(path, encoding="utf-8") as stream:
		entries = json.load(stream)
	wanted = os.path.normpath(os.path.join(os.path.abspath(source_root), target))
	for entry in entries:
		if source_of(entry) == wanted:
			return entry, ""
	return None, target + " is not in compile_commands.json (was it renamed?)"


def shadow_header(build_dir, shadow):
	"""A copy of the build's lmmsconfig.h with ZENE_HAVE_FFMPEG off."""
	path = os.path.join(os.path.abspath(build_dir), "lmmsconfig.h")
	if not os.path.isfile(path):
		return "no lmmsconfig.h in " + os.path.abspath(build_dir)
	with open(path, encoding="utf-8") as stream:
		lines = stream.readlines()
	written = False
	for index, line in enumerate(lines):
		if line.strip() == "#define ZENE_HAVE_FFMPEG":
			lines[index] = "/* #undef ZENE_HAVE_FFMPEG */\n"
			written = True
	with open(os.path.join(shadow, "lmmsconfig.h"), "w", encoding="utf-8") as stream:
		stream.writelines(lines)
	return "" if written else "the build's lmmsconfig.h does not enable FFmpeg (checking that configuration)"


def probe_source(command, entry, probe):
	"""The real compile line, with the translation unit replaced by a probe.

	The probe lives in the build tree, so the real source directory is added for the quoted
	include of VideoTestClip.h that the original translation unit gets from its own location.
	"""
	source = source_of(entry)
	for index, argument in enumerate(command):
		if os.path.normpath(argument) == source:
			command[index] = probe
			command.insert(1, switch(command[0], "/I", "-I") + os.path.dirname(source))
			return command
	return None


def error_lines(result):
	lines = [line for line in (result.stderr or result.stdout).splitlines() if "error" in line.lower()]
	return "; ".join(lines[:3]) or "compiler exited %d" % result.returncode


def hide_clip_helper(source_root, shadow):
	"""Keep the accepted missing-helper model even when the real fixture has a fallback API."""
	header = os.path.join(os.path.abspath(source_root), "tests/src/core/VideoTestClip.h").replace("\\", "/")
	with open(os.path.join(shadow, "hide-clip-helper.h"), "w", encoding="utf-8") as stream:
		stream.write('#define makeClip zene_test_hidden_makeClip\n#include "' + header + '"\n#undef makeClip\n')


def check_gui(build_dir, source_root, shadow):
	entry, error = load_entry(build_dir, source_root, "tests/src/gui/VideoClipsGuiTest.cpp")
	if entry is None:
		print("FAIL:", error)
		return 1
	result = run(syntax_command(entry, shadow), entry["directory"])
	if result.returncode:
		print("FAIL: GUI calls an unavailable video helper:", error_lines(result))
		return 1
	print("PASS: VideoClipsGuiTest compiles with the clip helper unavailable")
	return 0


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source-root", required=True)
	parser.add_argument("--build-dir", required=True)
	options = parser.parse_args()

	entry, error = load_entry(options.build_dir, options.source_root)
	if entry is None:
		print("FAIL:", error)
		return 1
	command = split_command(entry)
	if not command:
		print("FAIL:", TARGET, "has no compile command")
		return 1
	compiler = command[0]

	with tempfile.TemporaryDirectory(prefix="zene-no-ffmpeg-video-", dir=os.path.abspath(options.build_dir)) as temporary:
		shadow = os.path.join(temporary, "shadow")
		os.makedirs(shadow)
		hide_clip_helper(options.source_root, shadow)
		note = shadow_header(options.build_dir, shadow)
		if note.startswith("no "):
			print("FAIL:", note)
			return 1
		if note:
			print("NOTE:", note)

		config = os.path.join(temporary, "config.cpp")
		with open(config, "w", encoding="utf-8") as stream:
			stream.write(CONFIG_PROBE)
		config_result = run([compiler, switch(compiler, "/std:c++20", "-std=c++20"), switch(compiler, "/Zs", "-fsyntax-only"), switch(compiler, "/I", "-I") + shadow, config], temporary)
		if config_result.returncode != 0:
			print("FAIL: the shadow header still enables FFmpeg:", error_lines(config_result))
			return 1
		print("PASS: the shadow configuration does not define ZENE_HAVE_FFMPEG")

		absent = os.path.join(temporary, "absent.cpp")
		with open(absent, "w", encoding="utf-8") as stream:
			stream.write(ABSENT_PROBE)
		absent_line = probe_source(syntax_command(entry, shadow), entry, absent)
		if absent_line is None:
			print("FAIL: could not find the translation unit in the recorded compile line")
			return 1
		absent_result = run(absent_line, entry["directory"])
		if absent_result.returncode == 0:
			print("FAIL: videotest::makeClip is declared without ZENE_HAVE_FFMPEG, so this check cannot detect the defect")
			return 1
		if "makeClip" not in (absent_result.stderr + absent_result.stdout):
			print("FAIL: the no-FFmpeg probe failed for a different reason:", error_lines(absent_result))
			return 1
		print("PASS: videotest::makeClip is not declared without ZENE_HAVE_FFMPEG")

		result = run(syntax_command(entry, shadow), entry["directory"])
		if result.returncode != 0:
			print("FAIL:", TARGET, "does not compile without FFmpeg:", error_lines(result))
			return 1
		print("PASS:", TARGET, "compiles without FFmpeg (its clip cases keep their runtime skip)")
		return check_gui(options.build_dir, options.source_root, shadow)


if __name__ == "__main__":
	sys.exit(main())
