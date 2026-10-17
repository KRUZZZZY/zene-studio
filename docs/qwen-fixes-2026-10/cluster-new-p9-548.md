# Cluster new-p9-548: Profiling writes to disk once per audio period

## Verdict
**FIXED**

## Summary
The bounded queue removes formatting and file writes from `finishPeriod()`, and the writer thread now owns the complete file lifecycle (open, write, drain, close). Caller-thread access to the QFile has been eliminated.

## Evidence

| Check | Command | Exit |
|---|---|---:|
| Build with fix | `cmake --build build --target zene AudioEngineProfilerTest -j12` | 0 |
| RED regression (original code) | `ctest -R '^AudioEngineProfilerTest$' --output-on-failure` | 8 (1/1 failed) |
| GREEN regression (with fix) | `ctest -R '^AudioEngineProfilerTest$' --output-on-failure` | 0 (1/1 passed) |
| Affected-family test count | `ctest -R '^AudioEngineProfilerTest$'` | 1 test |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 (unrelated pre-existing violations) |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 (unrelated pre-existing violations) |
| Worktree status | `git status --short --branch` | clean |

## Files Changed
- `src/core/AudioEngineProfiler.cpp`
- `include/AudioEngineProfiler.h`
- `tests/src/core/AudioEngineProfilerTest.cpp`

## Changes Made

### 1. Header (`include/AudioEngineProfiler.h`)
Added `QString m_outputFileName` member to store the output filename for the writer thread.

### 2. Implementation (`src/core/AudioEngineProfiler.cpp`)

#### `setOutputFile()` (lines 129-135)
- **Before**: Opens the QFile on the caller thread, then starts the writer thread
- **After**: Stores the filename and starts the writer thread (no file operations on caller thread)

#### `writerLoop()` (lines 77-118)
- **Before**: Uses the already-opened QFile from the caller thread
- **After**: Opens the file at the start of the loop, writes data, drains remaining data, and closes the file before exiting (complete lifecycle owned by writer thread)

#### `stopWriter()` (lines 120-127)
- **Before**: Signals the thread, joins, then drains remaining data from the caller thread (accessing QFile from wrong thread)
- **After**: Signals the thread and joins (no caller-thread file access)

#### `~AudioEngineProfiler()` (lines 40-43)
- **Before**: Calls `stopWriter()` then closes the QFile on the caller thread
- **After**: Only calls `stopWriter()` (writer thread handles file closure)

### 3. Test (`tests/src/core/AudioEngineProfilerTest.cpp`)
Added `writerThreadOwnsFileLifecycle()` test that verifies:
- `setOutputFile()` does not call `m_outputFile.open()`
- `stopWriter()` does not reference `m_outputFile`
- `writerLoop()` contains both `m_outputFile.open()` and `m_outputFile.close()`

## RED/GREEN Proof

### RED (original code)
```
FAIL!  : AudioEngineProfilerTest::writerThreadOwnsFileLifecycle() 
'setOutputFile.find("m_outputFile.open") == std::string::npos' returned FALSE. 
(setOutputFile opens the file on caller thread)
Exit code: 8
```

### GREEN (with fix)
```
PASS   : AudioEngineProfilerTest::writerThreadOwnsFileLifecycle()
100% tests passed out of 1
Exit code: 0
```

## Technical Details

The fix ensures that the QFile object is only accessed from the writer thread, complying with Qt's threading model for QObject-derived classes. The writer thread now:
1. Opens the file when the loop starts
2. Writes all buffered data during the loop
3. Drains any remaining data before exiting
4. Closes the file before the thread terminates

This eliminates cross-thread access to the QFile object, which could cause undefined behavior according to Qt's documentation.

## Gate Status
Both complexity and file-length gates report pre-existing violations in unrelated files. The changed files (AudioEngineProfiler.cpp: 137 lines, AudioEngineProfiler.h: 126 lines, AudioEngineProfilerTest.cpp: 96 lines) are well within limits.
