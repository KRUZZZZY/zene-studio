# @zene-studio/session-sdk

A typed TypeScript client for a running Zene Studio session. A program can connect to the open project, read it
and edit it through the commands Zene already registers, and follow every change as it happens. Those changes can
come from the program, another client, a script or the musician working in Zene.

- Version **0.1.0** (`SDK_VERSION`), control protocol **1** (`PROTOCOL_VERSION`). Semantic versioning: a breaking
  change to anything exported raises the major number. See `CHANGELOG.md`.
- Node >= 22.6. No runtime dependencies. GPL-2.0-or-later, like Zene.
- Local only. The SDK connects to the AF_UNIX socket (Linux, macOS) or named pipe (Windows) that Zene opens when
  it is started with `--control-socket <path>`. Nothing listens on the network.

## Install and build

```sh
cd tools/zene-session-sdk
npm ci            # the pinned TypeScript compiler and Node types (development only)
npm run build     # dist/: JavaScript and .d.ts, what "exports" publishes
npm run typecheck # src/, examples/ and test/, strict
```

You can also run the TypeScript sources directly: `node --experimental-strip-types --conditions=zene-source ...`
resolves `@zene-studio/session-sdk` to `src/index.ts`.

## Connect

```ts
import { ZeneClient } from "@zene-studio/session-sdk";

// zene --control-socket /tmp/zene.sock      (Windows: --control-socket \\.\pipe\zene)
const client = await ZeneClient.connect({ path: "/tmp/zene.sock" }); // or set $ZENE_CONTROL_SOCKET
console.log(client.version);                                         // Zene's version string
```

`connect()` asks `control.version` and refuses an instance that speaks a different protocol revision with a
`ZeneProtocolError`. Options: `connectTimeoutMs` (10 s), `callTimeoutMs` (30 s per call), `checkProtocol` (true).

## Read and edit

Every registered command is available through `call()`. `control.commands_list` lists them all with their argument
and result schemas. The commands below have typed arguments and results in `KnownCommands`; every other command
takes and returns a `JsonObject`.

```ts
const state = await client.call("project.get_state");            // ProjectState: tracks, tempo, file...
const added = await client.call("track.add", { type: "instrument", name: "Pad" });
await client.call("track.rename", { track: added.track, name: "Warm pad" });
await client.call("control.undo");                                // edits are normal undo steps
const anything = await client.call("mixer.get_state");            // JsonObject
```

An edit made this way works exactly like one made from Zene's menus. It goes through the same command, refusals
and undo history, and each successful edit is one undo step.

## Subscribe to changes

```ts
const sub = await client.subscribe({ queueLimit: 256 });
sub.onEvent((event) => {
  // event.seq rises by exactly one per change; event.kind: "edit" | "undo" | "redo" | "project_replaced"
  // event.command / event.args identify a command's change; source "journal" is an edit made in the interface
  console.log(event.seq, event.kind, event.command, event.args);
});
sub.onEnd((end) => console.log("ended:", end.reason));

for await (const event of sub.events()) { /* the same changes as an async iterator */ }

await sub.unsubscribe(); // events already on their way arrive first; none after
```

A connection holds one subscription; asking for a second is refused `busy`. Subscribing changes nothing in the
project, is not an undo step and is never saved. A reply always reaches `call()` and an event always reaches the
subscription, even when the two arrive interleaved.

## Errors

Every failure is a `ZeneError` with a `code`:

| Class | `code` | Meaning |
|---|---|---|
| `ZeneCommandError` | `command` | Zene refused the command. `kind` is one of `not_found`, `requires`, `invalid_args`, `busy`, `refused`, `irreversible`. Nothing changed. |
| `ZeneConnectionError` | `connection` | Nothing listens at the path, or the connection closed. Calls still waiting are rejected with it. |
| `ZeneTimeoutError` | `timeout` | No reply within the call's bound. The connection stays open, and an edit may still have happened, so reread before you retry it. |
| `ZeneProtocolError` | `protocol` | Another protocol revision, or a line the SDK cannot read (the connection is closed). |

## Resync: overflow and reconnect

Zene never waits for a slow client and never queues without limit. A subscriber that falls more than
`queueLimit` changes (at most 4096, and 1 MiB) behind gets the changes Zene was already holding for it. It then gets
one `events_overflow` notice, and its subscription ends. Missed changes are **never** replayed. The rule is the
same for every gap:

1. The subscription ends with `onEnd({ reason })`:
   - `overflow`: `end.notice` names `first_missed_seq` and `missed`.
   - `disconnected`: the connection closed.
   - `gap`: a defensive check found a sequence break; Zene promises never to send one.
2. Subscribe again **first**, then reread the state, so that no change falls between the read and the first new
   event. `client.resync(read, { since })` does both in that order. `missed` tells you how many changes you
   lost. It is `null` when there is no `since`, and also when Zene was restarted in between (`sessionChanged`); then
   treat everything as unknown.

```ts
let sub = await client.subscribe();
sub.onEnd(async (end) => {
  if (end.reason === "overflow") {
    const again = await client.resync((c) => c.call("project.get_state"), { since: sub.cursor });
    console.log(`missed ${again.missed}; fresh state has ${again.state.tracks?.length} tracks`);
    sub = again.subscription;
  }
});
```

`LiveSession` does all of this for you. It connects, subscribes and reads. When a gap appears it recovers on its
own, reconnecting with back-off if it lost the connection:

```ts
import { LiveSession, type ProjectState } from "@zene-studio/session-sdk";

const view = await LiveSession.start<ProjectState>({
  path: "/tmp/zene.sock",
  read: (client) => client.call("project.get_state"),
});
view.onChange((event) => { /* apply it, or reread */ });
view.onStale((end) => { /* view.state no longer reflects every change: end.reason */ });
view.onSync((sync) => { /* fresh sync.state; sync.missed changes were not delivered */ });
await view.client?.call("track.add", { type: "sample" });
await view.stop();
```

No change is lost while you set up. A change that arrives before a subscription's first `onEvent` listener
attaches is held, and so is one that arrives before a `LiveSession`'s first `onChange` listener. That covers a change
in the same read as the subscribe reply, and one made while `resync()` or a sync was reading the state. The listener
gets the held changes first, in order, one microtask after it attaches, and `cursor` counts them. A subscription
with no listener that holds 8192 changes ends with reason `overflow`, and a `LiveSession` with no `onChange`
listener stops holding at that point.

Events that arrive after a resync can describe changes that `state` already shows, because the read ran after the
subscription started. Apply them idempotently, or reread.

## Example

`examples/live-view.ts` is a complete program. It follows the project, makes one edit and sees that edit's event,
handles a typed refusal, and recovers by itself. It typechecks with the package (`npm run typecheck`) and runs
against a real Zene in `ctest -R ControlSessionSdk`:

```sh
ZENE_CONTROL_SOCKET=/tmp/zene.sock node --experimental-strip-types --conditions=zene-source examples/live-view.ts --once
```

## Tests

From the Zene build tree (`build/tests`):

- `ctest -R ZeneSessionSdkTypecheck` typechecks src/, the example and the test, then builds the package. It reports
  Skipped until `npm ci` has run here.
- `ctest -R ControlSessionSdk` starts a real headless Zene. It runs `test/integration.ts` (connect, read, edit,
  event, unsubscribe, recovery after overflow and after a dropped connection) and then the example.

The wire contract underneath is documented in `docs/050-features/Q050-334.1.md` in the Zene tree.
