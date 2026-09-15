#!/usr/bin/env python3
"""For a list of workspace doc basenames, print the head of the workspace file
and every citation line in the product tree."""
import subprocess
import sys

PRODUCT = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wdoc5"
WS = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research"

CANDIDATES = [
    ("SPEC-dynamic-routing.md", "mixer/SPEC-dynamic-routing.md"),
    ("PART-D-SIDECHAIN.md", "PART-D-SIDECHAIN.md"),
    ("WASM-SANDBOX.md", "WASM-SANDBOX.md"),
    ("NEURAL-AMP.md", "NEURAL-AMP.md"),
    ("PATCHER-MVP.md", "PATCHER-MVP.md"),
    ("RECORDING-PROTOTYPE.md", "RECORDING-PROTOTYPE.md"),
    ("A16-STATUS-MEASURED.md", "ableton-gap/A16-STATUS-MEASURED.md"),
    ("GIT-FRIENDLY-MMPZ.md", "GIT-FRIENDLY-MMPZ.md"),
    ("SPEC-two-track-recording.md", "specs/SPEC-two-track-recording.md"),
    ("VST3-LICENSING.md", "plugin-hosting/VST3-LICENSING.md"),
    ("SPEC-stable-ids.md", "ableton-gap/SPEC-stable-ids.md"),
]


def main():
    for base, wspath in CANDIDATES:
        print("=" * 110)
        print(f"### {base}   workspace:{wspath}")
        try:
            head = open(f"{WS}/{wspath}").read().split("\n")[:12]
            print("  --- workspace head ---")
            for l in head:
                print("   | " + l[:170])
        except FileNotFoundError:
            print("   !! not in workspace")
        out = subprocess.run(
            ["git", "grep", "-I", "-n", "-F", base, "--", "src", "include", "plugins",
             "modules", "tests", "CMakeLists.txt", "cmake", "tools", "docs", "recording"],
            cwd=PRODUCT, capture_output=True, text=True).stdout
        lines = [l for l in out.split("\n") if l]
        print(f"  --- {len(lines)} citing line(s) in the product tree ---")
        for l in lines[:40]:
            print("   > " + l[:190])
        if len(lines) > 40:
            print(f"   ... {len(lines)-40} more")


if __name__ == "__main__":
    main()
