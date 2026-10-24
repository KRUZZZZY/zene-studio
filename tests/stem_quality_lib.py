#!/usr/bin/env python3
"""Q050-062.1: the stem-separation quality setting, checked over the socket
and through the reference CLI. Called from control-stem-commands.py.

`stem.job_start` takes an optional `quality` (fast / standard / high: 25% /
50% / 75% segment overlap); every job object - the start reply, `stem.job_status`
and `stem.job_cancel` - reports the effective `quality`, `overlap` and
`hop_frames`. A caller that omits it gets `standard`, the overlap every backend
used before the setting existed. The CLI (tools/stem_split_cli.py --quality)
uses the same table; its `info` line reports the hop and the number of segments
the run takes, which is how this file sees the setting change the inference.
"""

import json
import os
import struct
import subprocess
import tempfile

from stem_commands_lib import CLI_PATH, SEGMENT_FRAMES, STUB_MODEL, error_kind, poll_until, write_source_wav

QUALITIES = ["fast", "standard", "high"]
# overlap fraction and hop at the model's own 343980-frame segment
# (floor(segment * (4 - quarters) / 4), include/StemSeparation/StemTypes.h)
EXPECTED = {
    "fast": (0.25, 257985),
    "standard": (0.5, SEGMENT_FRAMES // 2),
    "high": (0.75, 85995),
}
SOURCE_SECONDS = 2


def reports_quality(job, quality):
    overlap, hop = EXPECTED[quality]
    return (job.get("quality") == quality and job.get("overlap") == overlap
            and job.get("hop_frames") == hop)


def job_facts(job):
    return "quality=%r overlap=%r hop=%r" % (job.get("quality"), job.get("overlap"), job.get("hop_frames"))


def first_job(session, job_id):
    jobs = session.result("stem.job_status", {"job_id": job_id}).get("jobs") or []
    return jobs[0] if jobs else {}


def check_state_lists_qualities(session, recorder):
    state = session.result("stem.get_state")
    recorder.check("stem.get_state lists the qualities and the default",
                   state.get("qualities") == QUALITIES and state.get("default_quality") == "standard",
                   "qualities=%r default=%r" % (state.get("qualities"), state.get("default_quality")))


def check_job_at(session, recorder, source, quality):
    """Starts a job (quality None = omitted) and checks the start reply, the
    status poll and the finished job all carry the effective setting."""
    args = {"source": source}
    if quality is not None:
        args["quality"] = quality
    effective = quality or "standard"
    label = "quality %s" % (quality or "omitted")
    ok, started = session.ok_and_result("stem.job_start", args)
    recorder.check("%s: stem.job_start reports the effective setting" % label,
                   ok and reports_quality(started, effective), job_facts(started) if ok else repr(started)[:200])
    if not ok:
        return
    job_id = started.get("job_id")
    recorder.check("%s: stem.job_status reports the same setting" % label,
                   reports_quality(first_job(session, job_id), effective),
                   job_facts(first_job(session, job_id)))
    state, seen, seconds = poll_until(session, job_id, ("completed", "failed", "cancelled"))
    finished = first_job(session, job_id)
    recorder.check("%s: the job completes and still reports it" % label,
                   state == "completed" and reports_quality(finished, effective),
                   "state=%r after %.1fs %s error=%r" % (state, seconds, job_facts(finished),
                                                         finished.get("error")))


def check_unknown_quality_refused(session, recorder, source):
    before = session.result("stem.job_status").get("count")
    refused = session.result("stem.job_start", {"source": source, "quality": "ultra"})
    after = session.result("stem.job_status").get("count")
    recorder.check("an unknown quality is refused typed and creates no job",
                   error_kind(refused) == "invalid_args" and before == after,
                   "kind=%r jobs %r -> %r" % (error_kind(refused), before, after))


def cli_run(python, env, workdir, quality):
    """Runs the CLI on a one-second f32 mix at a 16384-frame segment; returns
    (exit code, info line, done line). quality None omits --quality."""
    mix = os.path.join(workdir, "cli-mix.f32")
    if not os.path.exists(mix):
        with open(mix, "wb") as handle:
            handle.write(struct.pack("<%df" % (2 * 44100), *([0.1, -0.1] * 44100)))
    out_dir = os.path.join(workdir, "cli-%s" % (quality or "omitted"))
    command = [python, CLI_PATH, "--model", STUB_MODEL, "--input", mix, "--in-format", "f32",
               "--in-rate", "44100", "--out-dir", out_dir, "--segment", "16384", "--progress-json"]
    if quality is not None:
        command += ["--quality", quality]
    run = subprocess.run(command, capture_output=True, text=True, env=env, timeout=300)
    lines = [json.loads(line) for line in run.stdout.splitlines() if line.startswith("{")]
    info = next((line for line in lines if line.get("type") == "info"), {})
    done = next((line for line in lines if line.get("type") == "done"), {})
    return run.returncode, info, done


def check_cli(recorder, python, env):
    """A one-second mix padded to 68676 frames: 6 / 9 / 17 segments at a hop of
    12288 / 8192 / 4096; omitting --quality is the legacy 9."""
    expected = {None: ("standard", 8192, 9), "fast": ("fast", 12288, 6),
                "standard": ("standard", 8192, 9), "high": ("high", 4096, 17)}
    workdir = tempfile.mkdtemp(prefix="zstem-quality-cli-")
    for quality, (name, hop, chunks) in expected.items():
        code, info, done = cli_run(python, env, workdir, quality)
        recorder.check("the CLI at --quality %s runs %d segments of hop %d" % (quality or "omitted", chunks, hop),
                       code == 0 and info.get("quality") == name and info.get("hop") == hop
                       and info.get("chunks") == chunks and done.get("chunks") == chunks,
                       "exit=%r info=%r done.chunks=%r" % (code, info, done.get("chunks")))


def check_quality(session, recorder, outdir, python, cli_env):
    check_state_lists_qualities(session, recorder)
    source = os.path.join(outdir, "quality-mix.wav")
    write_source_wav(source, seconds=SOURCE_SECONDS)
    check_unknown_quality_refused(session, recorder, source)
    for quality in (None, "fast", "high"):
        check_job_at(session, recorder, source, quality)
    check_cli(recorder, python, cli_env)
