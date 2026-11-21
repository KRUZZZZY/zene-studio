// @zene-studio/session-sdk: the public surface. Everything a client needs is exported from here.

export { SDK_VERSION } from "./version.ts";
export { ZeneClient, SOCKET_PATH_ENV } from "./client.ts";
export type { CallOptions, ConnectOptions, ResyncOptions, ResyncResult, SubscribeOptions } from "./client.ts";
export { Subscription } from "./subscription.ts";
export type { EventIteratorOptions, SubscriptionEnd, SubscriptionEndReason } from "./subscription.ts";
export { LiveSession } from "./live-session.ts";
export type { LiveSessionOptions, SyncInfo, SyncReason } from "./live-session.ts";
export { ZeneCommandError, ZeneConnectionError, ZeneError, ZeneProtocolError, ZeneTimeoutError } from "./errors.ts";
export type { ZeneErrorCode } from "./errors.ts";
export type { Unlisten } from "./listeners.ts";
export { PROTOCOL_VERSION } from "./protocol.ts";
export type * from "./protocol.ts";
