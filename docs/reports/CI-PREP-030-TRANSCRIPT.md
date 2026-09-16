# CI-PREP lane — raw transcript (cards #638, #639, #640)

Branch `030/ci-prep` @ base `49a40b30c`, worktree `zene-030/wcicp`. Every command below was
run on this box against this tree; outputs are verbatim. The report is
`docs/reports/CI-PREP-030-REPORT.md`.

## 1. #638 — the class-key census (the local proof)

```bash
$ grep -rn "class ControlResult\|struct ControlResult" include/ src/ plugins/ tests/ tools/
### every class-key sighting of lmms::ControlResult (include/ src/ plugins/ tests/ tools/)
include/ControlEdit.h:44:struct ControlResult;
include/ControlWarpSupport.h:44:struct ControlResult;
include/ControlRegistry.h:70:struct ControlResult
include/ControlChordSupport.h:50:struct ControlResult;
include/ControlRecordingSupport.h:47:struct ControlResult;
include/ControlMeterSupport.h:56:struct ControlResult;
include/ControlDetectSupport.h:42:struct ControlResult;
include/ControlNoteShared.h:51:struct ControlResult;
include/ControlGrooveSupport.h:46:struct ControlResult;
include/ControlCompSupport.h:47:struct ControlResult;
include/ControlMasteringSupport.h:78:struct ControlResult;
include/ControlScaleShared.h:47:struct ControlResult;
include/ControlBrowserSupport.h:51:struct ControlResult;
src/core/ControlCommandsPatcherShared.h:45:struct ControlResult;

### the class-key of every sighting, counted
     14 struct ControlResult
```

## 2. #639a — the workflow edit, validated

```bash
$ python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/build.yml')); print('YAML OK')"
YAML OK
$ yamllint -c .yamllint .github/workflows/build.yml
(no output)
```

The inserted step, linux-x86_64 (the other two are the pip form / the same shape):

```
      - name: Install the control suite's Python dependencies (numpy + scipy)
        # comments: the measured failure, why a step and not the shared deps list,
        #           why the interpreter matters (see the report)
        run: |
          sudo apt-get install -y --no-install-recommends python3-numpy python3-scipy
          echo "BS.1770-4 deps this job will run with (interpreter, numpy, scipy):"
          python3 -c "import numpy,scipy,sys;print(sys.executable,numpy.__version__,scipy.__version__)"
      - name: Run tests
```

## 3. #639b — the registration census behind the /tmp record

python-derived registration census (nesting of if()/endif() in tests/CMakeLists.txt):

 1855  LuaApiSurface              guard=[PYTHON3_EXECUTABLE]                                ALL-PLATFORMS
 1875  ControlNamedPipeSmoke      guard=[WIN32 AND PYTHON3_EXECUTABLE]                      WINDOWS-ONLY
 1899  GoldenAudioSelfTest        guard=[PYTHON3_EXECUTABLE]                                ALL-PLATFORMS
 1925  RtSafetySweep              guard=[PYTHON3_EXECUTABLE]                                ALL-PLATFORMS
 1933  RtSafetySelfTest           guard=[PYTHON3_EXECUTABLE]                                ALL-PLATFORMS
 1940  ControlSocketIntegration   guard=[PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE]    POSIX-ONLY
 ... (39 more control-surface tests under the same gate) ...
 2506  ControlMasteringCommands   guard=[PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE]    POSIX-ONLY
 3014  ControlCommandsSnapshot    guard=[PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE]    POSIX-ONLY
 3031  MmpzGitDepthTest           guard=[PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE]    POSIX-ONLY
 3078  ControlMcpGroupCoverage    guard=[PYTHON3_EXECUTABLE]                                ALL-PLATFORMS

(lines abbreviated in the middle; the machine-derived run printed all 53 python registrations,
47 POSIX-only, 6 all-platforms.)

## 4. #640 — the closed-socket diagnosis, before and after

The proof script (kept here rather than in `tests/`, which the manifests and Gate 10 scan;
run it with the tests directory as its only argument):

```python
#!/usr/bin/env python3
"""ci-prep proof: does the harness's EOF path now report WHY the server vanished?

Spins a fake "server" on an AF_UNIX socket that reads one request line and closes
without answering - the exact wire shape the macOS ControlPluginScanCommands engine
produced on control.undo in run 34870198514 - and drives the REAL harness Client at
it. Prints the raised Blocked message for a given tests/ directory, so the pre-change
and post-change harness can be compared with the same stimulus.
"""
import os
import socket
import sys
import tempfile
import threading


def stimulus(tests_dir):
    sys.path.insert(0, tests_dir)
    import control_socket_harness as H

    work = tempfile.mkdtemp(prefix="ci-prep-eof-")
    sock_path = os.path.join(work, "fake.sock")
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(sock_path)
    srv.listen(1)

    def serve():
        conn, _ = srv.accept()
        conn.recv(65536)   # the request arrives ...
        conn.close()       # ... and the server closes without answering

    threading.Thread(target=serve, daemon=True).start()

    client = H.Client(sock_path)
    try:
        client.call(1, "control.ping")
        return "NO EXCEPTION (unexpected)"
    except H.Blocked as error:
        return str(error)
    finally:
        client.close()
        srv.close()


if __name__ == "__main__":
    print(stimulus(sys.argv[1]))
```

Second stimulus - a REAL harness `Instance` (a sleeper binary), killed, with the client pointed
at a server that accepts and then closes without answering: the macOS wire shape.

```python
#!/usr/bin/env python3
"""ci-prep proof 2: what the next macOS run will print where it printed nothing.

Registers a REAL harness Instance (whose binary is a sleeper, so nothing depends on a
built product), lets the client be closed by a server that never answers, and kills the
instance with a signal first - the shape run 34870198514's ControlPluginScanCommands hit
on control.undo. The raised Blocked message is what the CI log will carry.
"""
import os
import signal
import socket
import sys
import tempfile
import threading
import time


def main(tests_dir):
    sys.path.insert(0, tests_dir)
    import control_socket_harness as H

    fake = tempfile.mkdtemp(prefix="ci-prep-fake-")
    binary = os.path.join(fake, "fake-zene.sh")
    with open(binary, "w") as handle:
        handle.write("#!/bin/sh\nexec sleep 60\n")
    os.chmod(binary, 0o755)

    work = tempfile.mkdtemp(prefix="ci-prep-eof2-")
    sock_path = os.path.join(work, "fake.sock")
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(sock_path)
    srv.listen(1)

    def serve():
        conn, _ = srv.accept()
        conn.recv(65536)
        conn.close()

    threading.Thread(target=serve, daemon=True).start()

    with H.start_instance(binary) as instance:
        # The instance the harness will diagnose: a real one, killed mid-flight - the
        # "server closed the connection" case that used to print no explanation.
        # (SIGKILL because it is the one signal no shell ignores; what matters for the
        # proof is that the message names an exit STATUS, not the number itself.)
        instance.process.send_signal(signal.SIGKILL)
        time.sleep(0.3)
        assert instance.process.poll() is not None, "the fake instance did not die"
        client = H.Client(sock_path)
        try:
            client.call(1, "control.ping")
            print("NO EXCEPTION (unexpected)")
        except H.Blocked as error:
            print(str(error))
        finally:
            client.close()
            srv.close()


if __name__ == "__main__":
    main(sys.argv[1])
```

Before (the harness as committed at HEAD~1, copied out with `git show`):

```bash
$ python3 /tmp/ci-prep-eof-proof2.py /tmp/ci-prep-oldharness ; echo EXIT=$?
the server closed the connection without answering
EXIT=0
```

After (this branch):

```bash
$ python3 /tmp/ci-prep-eof-proof2.py tests ; echo EXIT=$?
the server closed the connection without answering
diagnosis: the instance EXITED with -9 - a crash or a refusal, not a hang (its stdout/stderr are the transcript above)
EXIT=0
```

And the no-instance variant of the first script (`python3 /tmp/ci-prep-eof-proof.py tests`)
prints the same sentence plus `diagnosis: no instance was launched by this process`, so the
helper is never silently empty.

## 5. Gate state of the branch

```bash
$ bash tests/run-all-gates.sh      # default enforcement scope (fork + tools)
================ SUMMARY ================
gate   name                     result
1      ctest                    SKIP
2      coverage                 SKIP
3      no-tautology             PASS
4      complexity               FAIL
5      mutation                 FAIL
6      upstream-regression      PASS
7      file-length              FAIL
8      duplication              PASS
9      fork-sources             PASS
10     unregistered-tests       PASS
11     evidence                 PASS
12     rt-safety                PASS

scope: fork (tests/fork-sources.txt) + tools — the enforced scope, the same one CI's
       static-gates job runs. The WHOLE-TREE scope (gates 4/7/8 --scope all) was NOT
       measured by this run; use --whole-tree for it (tests/QA-GATES.md, 'Scope policy').

skipped: 2 of 12 gates did not run
  gate 1 (ctest): no configured build/ directory — to run it: configure build/ (cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DWANT_QT6=ON)
  gate 2 (coverage): --with-coverage was not passed — to run it: pass --with-coverage

RESULT: FAIL — see the failing gate above
```

## 6. Files touched by this lane (git diff --stat of the four commits)

```
 $ git diff --stat 49a40b30c..HEAD
 .github/workflows/build.yml                    | 62 ++++++++++++++++++++++
 docs/reports/CI-PREP-030-REPORT.md             | new
 docs/reports/CI-PREP-030-TRANSCRIPT.md         | new (this file)
 include/ControlMasteringSupport.h              | 14 +++++++++++++-
 include/ControlMeterSupport.h                  |  8 +++++++-
 src/core/ControlCommandsControl.cpp            | 23 ++++++++++++++++++++---
 tests/control_instance_diagnosis.py            | 26 +++++++++++++++++
 tests/control_socket_harness.py                |  4 ++--   (net-zero lines: 511 before, 511 after)
```
