# oop-hosting fixtures

Instruments for the "run in a separate process" toggle
(post-alpha/oop-hosting; the report is `docs/OOP-HOSTING.md`).

| file | what it is |
|---|---|
| `zyn-separate-process-off.mmp` | one ZynAddSubFx instrument track, `<zynaddsubfx separateprocess="0"/>`, 12 patterns / 204 notes |
| `zyn-separate-process-on.mmp` | the same project with `separateprocess="1"` - the only difference between the two files |
| `observe-render.sh` | renders a project and reports, from `/proc/<pid>/exe` (never from a command-line match), whether a `RemoteZynAddSubFx` child process of the render host exists while it runs |
| `kill-loop-experiment.sh` | renders the ON project while SIGKILLing that client whenever it appears; used for the isolation and sensitivity evidence |
| `compare-renders.py` | two WAVs -> sha256, per-sample max abs delta, dB relative to peak/RMS, count of differing samples (reads float32 WAV, which Python's `wave` module refuses) |

Provenance of the project: the track is the "Pad" track of the shipped demo
`data/projects/demos/Ashore.mmpz` (its `<track>`, `<pattern>`s and notes are
kept), with the saved PADsynth patch dropped so the instrument uses Zyn's
built-in default patch, `vol` raised to 100 and the mixer channel set to 0. The
PADsynth patch is deliberately not used here: it is not reproducible
run-to-run on this box (see `docs/OOP-HOSTING.md`, "What is not proven").

Usage (from the worktree, `build/` configured and built):

```sh
export LMMS_PLUGIN_DIR=$PWD/build/plugins
bash tests/data/oop-hosting/observe-render.sh "$PWD" \
    "$PWD/tests/data/oop-hosting/zyn-separate-process-on.mmp" \
    /tmp/on.wav /tmp/on.log "TOGGLE ON"
```

The scripts write only to the output paths they are given; they are test
instruments, not build inputs.
