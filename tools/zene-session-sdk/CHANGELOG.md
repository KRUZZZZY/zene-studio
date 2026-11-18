# Changelog

## 0.1.0 (2026-11, Zene 0.5.0 cycle, Q050-334.2)

First release.

- `ZeneClient`: connect over the local control socket or named pipe, check the protocol revision, call any registered
  command, with typed arguments and results for the common read and edit commands.
- `Subscription`: ordered project-change events, `onEvent`, `onEnd`, an async iterator and `unsubscribe()`.
- `ZeneClient.resync()`: subscribe, then reread, and report how many changes were missed.
- `LiveSession`: a self-recovering view that handles overflow and reconnects with back-off.
- Typed errors: `ZeneCommandError`, `ZeneConnectionError`, `ZeneTimeoutError` and `ZeneProtocolError`.
