# Transport Protocol Contract (v1 Draft)

This document defines the protocol contract between transport plugins and the
outside world, and the mapping to `CoreFacade`.

Scope:
- `phi-transport-api` plugin contract
- Wire semantics for `sync.*`, `cmd.*`, `event.*`
- Correlation and error behavior

Non-scope:
- Concrete WS-only implementation details
- UI-specific behavior

## 1. Roles

- `CoreApi`:
  - internal core facade
  - domain-focused, not wire-protocol focused
- `CoreFacade` (transport API):
  - stable plugin-facing contract
  - used by all transport plugins (WS/MQTT/CLI/...)
- Transport plugins:
  - parse/serialize their own wire protocol
  - map wire commands to `CoreFacade` calls

## 2. Message Classes

### `sync.*`

Definition:
- synchronous request/response operation

Required behavior:
- exactly one immediate `sync.response` for same `cid`
- no `cmd.ack`
- no later async result for this request

### `cmd.*`

Definition:
- asynchronous command contract

Required behavior:
- immediate `cmd.ack` with same `cid`
- if `accepted=false`: no later `cmd.response`
- if `accepted=true`: exactly one later `cmd.response` for same `cid`

### `event.*`

Definition:
- core-generated events pushed to clients

Required behavior:
- no request needed
- forwarded by transport
- no ACK/response correlation

## 3. Hard Rule: Prefix Defines Semantics

No exceptions:
- `sync.*` is always sync
- `cmd.*` is always async

This rule is stronger than operation type ("read" vs "write").
If a call must be async, it must be named `cmd.*`.

Implication:
- translation fetch with optional WAN lookup must be async
- therefore it should be `cmd.tr.get` (not `sync.tr.get`)

## 4. Correlation Model

- clients correlate by `cid`
- internal `CmdId` is core-internal and not exposed as wire id
- transport plugin keeps mapping:
  - internal `CmdId` -> `{connection, cid, cmdTopic}`

Ordering:
- for accepted async commands:
  - `cmd.ack` first
  - `cmd.response` later (can be very soon, but still after ACK)

## 5. Error Model

Use `protocol.error` only for protocol-level issues:
- invalid JSON/envelope
- missing required envelope fields
- unsupported message type

Use command/sync payload errors for domain/business failures:
- invalid args
- permission denied
- adapter unavailable
- timeout/failure

`cmd.response` payload should include:
- `status` (int)
- `statusName` (string)
- `error` (`null` or object `{msg, params?, ctx?, originType?, originId?}`)
- `tsMs` (int64)
- optional: `resultValue`, `finalValue`, `resultType`, `resultTypeName`

## 6. Operation Classification (Draft)

This is the first draft for operation placement.

### Sync (`sync.*`)

Policy:
- only minimal fast-path operations
- must stay lightweight and bounded
- must not be used for broad topology reads

- `sync.hello.get`
- `sync.ping.get`
- `sync.settings.get`
- `sync.settings.set`
- `sync.settings.user.get`
- `sync.settings.user.set`
- `sync.users.enabled.set`
- `sync.users.flags.set`
- `sync.users.delete.set`

### Async (`cmd.*`)

Policy:
- all topology reads (`list/get`) are async by default
- all commands with side effects are async
- keeps transport threads and core thread decoupled from blocking read RPC patterns

- `cmd.channel.invoke`
- `cmd.device.effect.invoke`
- `cmd.scene.invoke`
- `cmd.adapter.action.invoke`
- `cmd.adapter.create`
- `cmd.adapter.update`
- `cmd.adapter.delete`
- `cmd.adapter.start`
- `cmd.adapter.stop`
- `cmd.adapter.restart`
- `cmd.adapter.reload`
- `cmd.adapters.discover`
- `cmd.adapters.discoverAll`
- `cmd.adapters.list`
- `cmd.devices.list`
- `cmd.rooms.list`
- `cmd.groups.list`
- `cmd.room.get`
- `cmd.group.get`
- `cmd.room.create`
- `cmd.group.create`
- `cmd.scene.create`
- `cmd.scene.scope.assign`
- `cmd.scenes.list`
- `cmd.automations.list`
- `cmd.automation.create`
- `cmd.automation.update`
- `cmd.automation.delete`
- `cmd.automation.run`
- `cmd.cron.job.list`
- `cmd.cron.job.create`
- `cmd.cron.job.update`
- `cmd.cron.job.delete`
- `cmd.device.user.update`
- `cmd.channel.user.update`
- `cmd.users.list`
- `cmd.adapters.factories.list`
- `cmd.adapter.config.layout.get`
- `cmd.adapter.action.layout.get`
- `cmd.tr.get`
- `cmd.tr.set`

Note:
- some of these can be "fast" internally, but still stay async for wire-stability.

## 7. Migration Notes

Current systems may still use legacy topic names.
For v1 cleanup:

1. make naming and semantics consistent
2. move async translation access to `cmd.tr.get`
3. keep temporary compatibility mapping only where necessary
4. remove compatibility mapping after client migration

## 8. Open Decisions (to review together)

1. Should discovery remain async-stream style only, or offer sync snapshot mode too?
2. Should auth remain fully `sync.*`, or include async flows for external providers?

## 9. Decision Log

### 2026-02-22

- `list/get` remains `cmd.*` (async) for v1.
- Rationale:
  - avoids blocking-style cross-thread request handling for larger reads
  - protects core runtime responsiveness under load
  - keeps command semantics uniform for all transports
