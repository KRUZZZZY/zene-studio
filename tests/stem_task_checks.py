"""Q050-010: a stem job through app.tasks, and its decode and resample on the worker, not on the caller."""

import os
import struct
import time
import wave

from stem_commands_lib import SAMPLE_RATE, poll_until

# Long enough at 48 kHz that decoding and resampling it takes seconds.
LONG_SOURCE_SECONDS = 180


def check_stem_task(session, recorder, job):
    """Q050-010: a stem job through app.tasks, under the id its start reply named."""
    task_id = job.get("task")
    recorder.check("stem.job_start names its app.tasks id", task_id == "stem-%d" % job["job_id"], repr(job)[:200])
    listed = [t for t in session.result("app.tasks").get("tasks", []) if t.get("id") == task_id]
    recorder.check("app.tasks lists the stem job as a stem_split task",
                   len(listed) == 1 and listed[0].get("kind") == "stem_split", repr(listed)[:200])
    cancel = session.result("app.task_cancel", {"id": task_id})
    recorder.check("app.task_cancel cancels the stem job",
                   cancel.get("state") in ("cancel_requested", "cancelled"), repr(cancel)[:200])
    poll_until(session, job["job_id"], ("completed", "failed", "cancelled"))
    listed = [t for t in session.result("app.tasks").get("tasks", []) if t.get("id") == task_id]
    recorder.check("after the cancel app.tasks shows the stem task cancelled",
                   len(listed) == 1 and listed[0].get("state") == "cancelled", repr(listed)[:200])


def check_resample_is_not_on_the_caller(session, recorder, outdir):
    """Q050-010: job_start answers before a long source at another rate is decoded and resampled: that work, which
    takes seconds here, is the worker's. The surface answers a ping while it runs, and a cancel stops it."""
    source = os.path.join(outdir, "long-48k.wav")
    with wave.open(source, "wb") as handle:
        handle.setnchannels(2)
        handle.setsampwidth(2)
        handle.setframerate(48000)
        block = b"".join(struct.pack("<hh", (i * 37) % 2000 - 1000, (i * 53) % 2000 - 1000) for i in range(48000))
        for _ in range(LONG_SOURCE_SECONDS):
            handle.writeframes(block)
    before = time.monotonic()
    answer = session.call("stem.job_start", {"source": source})
    seconds = time.monotonic() - before
    ok = answer.get("ok") is True
    reply = answer.get("result") or {}
    recorder.check("job_start on a %d s 48 kHz source answers in under a second" % LONG_SOURCE_SECONDS,
                   ok and seconds < 1.0, "%.2f s %r" % (seconds, repr(reply)[:160]))
    recorder.check("its reply promises the length at the model's rate",
                   ok and abs(reply.get("frames", 0) - LONG_SOURCE_SECONDS * SAMPLE_RATE) <= 1, repr(reply)[:200])
    recorder.check("the surface answers a ping while the worker decodes", session.result("control.ping").get("error") is None, "")
    if ok:
        check_stem_task(session, recorder, reply)
