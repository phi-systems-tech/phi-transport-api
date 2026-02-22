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
- ACK meaning:
  - `accepted=true`: command was syntactically valid and accepted for async core processing
  - `accepted=false`: command was rejected before execution (invalid/missing fields, unsupported topic, etc.)
- if `accepted=false`: no later `cmd.response`
- if `accepted=true`: exactly one later `cmd.response` for same `cid`
- execution failures are returned via `cmd.response` (`status != Success` plus `error`)

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
  - response may represent success or failure

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

For `cmd.*`:
- pre-execution validation rejection => `cmd.ack` with `accepted=false` (no later response)
- accepted command that later fails in execution => `cmd.response` with error payload

`cmd.response` payload should include:
- `status` (int)
- `statusName` (string)
- `error` (`null` or object `{msg, params?, ctx?, originType?, originId?}`)
- `tsMs` (int64)
- optional: `resultValue`, `finalValue`, `resultType`, `resultTypeName`

## 6. Operation Classification (Draft)

This is the first draft for operation placement.

### Command (`cmd.*`)

Policy:
- all topology reads (`list/get`) are async by default
- all commands with side effects are async
- keeps transport threads and core thread decoupled from blocking read RPC patterns

- `cmd.adapter.action.invoke`
- `cmd.adapter.action.layout.get`
- `cmd.adapter.config.layout.get`
- `cmd.adapter.create`
- `cmd.adapter.delete`
- `cmd.adapter.reload`
- `cmd.adapter.restart`
- `cmd.adapter.start`
- `cmd.adapter.stop`
- `cmd.adapter.update`
- `cmd.adapters.discover`
- `cmd.adapters.discoverAll`
- `cmd.adapters.factories.list`
- `cmd.adapters.list`
- `cmd.automation.create`
- `cmd.automation.delete`
- `cmd.automation.run`
- `cmd.automation.update`
- `cmd.automations.list`
- `cmd.channel.invoke`
- `cmd.channel.user.update`
- `cmd.cron.job.create`
- `cmd.cron.job.delete`
- `cmd.cron.job.list`
- `cmd.cron.job.update`
- `cmd.device.effect.invoke`
- `cmd.device.user.update`
- `cmd.devices.list`
- `cmd.group.create`
- `cmd.group.get`
- `cmd.groups.list`
- `cmd.room.create`
- `cmd.room.get`
- `cmd.rooms.list`
- `cmd.scene.create`
- `cmd.scene.invoke`
- `cmd.scene.scope.assign`
- `cmd.scenes.list`
- `cmd.settings.get`
- `cmd.settings.set`
- `cmd.settings.user.get`
- `cmd.settings.user.set`
- `cmd.tr.get`
- `cmd.tr.set`
- `cmd.users.delete.set`
- `cmd.users.enabled.set`
- `cmd.users.flags.set`
- `cmd.users.list`

### Sync (`sync.*`)

Policy:
- only minimal fast-path operations
- must stay lightweight and bounded
- must not be used for broad topology reads
- must not perform settings/user persistence operations

- `sync.hello.get`
- `sync.ping.get`

### Event (`event.*`)

Policy:
- push-only topics emitted by core and forwarded by transports
- no request/response pairing

- `event.adapter.added`
- `event.adapter.connectionStateChanged`
- `event.adapter.error`
- `event.adapter.removed`
- `event.adapter.updated`
- `event.automation.notification`
- `event.automations.changed`
- `event.channel.stateChanged`
- `event.device.added`
- `event.device.changed`
- `event.device.removed`
- `event.group.removed`
- `event.group.updated`
- `event.room.removed`
- `event.room.updated`

Note:
- some `cmd.*` operations can be fast internally, but still stay async for wire-stability.

## 6.1 Required Payload By Topic

### `cmd.*` request payload

| Topic | Required payload fields | Optional payload fields |
| --- | --- | --- |
| `cmd.adapter.action.invoke` | `actionId:string`; if `scope="instance"`: `adapterId:int`; if `scope="factory"`: `pluginType:string` | `scope:string` (default `factory`), `params:object`, `externalId:string`, `name:string`, `meta:object`, `metaUser:object`, `metaRuntime:object` |
| `cmd.adapter.action.layout.get` | `pluginType:string`, `actionId:string` | `scope:string` (default `instance`), `adapterId:int`, `discoveredId:string`, `meta:object` |
| `cmd.adapter.config.layout.get` | `pluginType:string` | `adapterId:int`, `discoveredId:string`, `meta:object` |
| `cmd.adapter.create` | `pluginType:string` | `externalId:string`, `name:string`, `meta:object`, `metaUser:object`, `metaRuntime:object` |
| `cmd.adapter.delete` | `adapterId:int` | none |
| `cmd.adapter.reload` | `pluginType:string` | none |
| `cmd.adapter.restart` | `adapterId:int` | none |
| `cmd.adapter.start` | `adapterId:int` | none |
| `cmd.adapter.stop` | `adapterId:int` | none |
| `cmd.adapter.update` | `adapterId:int` | `pluginType:string`, `externalId:string`, `name:string`, `meta:object`, `metaUser:object`, `metaRuntime:object` |
| `cmd.adapters.discover` | none | `pluginTypes:string[]` |
| `cmd.adapters.discoverAll` | none | `pluginTypes:string[]` |
| `cmd.adapters.factories.list` | none | none |
| `cmd.adapters.list` | none | none |
| `cmd.automation.create` | `automation:object` | none |
| `cmd.automation.delete` | `automationId:int` | none |
| `cmd.automation.run` | `automationId:int`, `triggerNodeId:int` | none |
| `cmd.automation.update` | `automation:object` (must include `id>0`) | none |
| `cmd.automations.list` | none | none |
| `cmd.channel.invoke` | `channelId:int`, `value:any` | none |
| `cmd.channel.user.update` | `channelId:int` | `name:string`, `metaUser:object` |
| `cmd.cron.job.create` | `expression:string`, `payload:object` with `payload.source:string`, `payload.owner:string` | additional fields inside `payload` |
| `cmd.cron.job.delete` | `jobId:int` | none |
| `cmd.cron.job.list` | none | none |
| `cmd.cron.job.update` | `jobId:int`, `expression:string`, `payload:object` with `payload.source:string`, `payload.owner:string` | additional fields inside `payload` |
| `cmd.device.effect.invoke` | `deviceId:int` and one of `effect:int` or `effectId:string` | `params:object` |
| `cmd.device.user.update` | `deviceId:int` | `name:string`, `roomId:int` (`0` allowed for unassign), `metaUser:object` |
| `cmd.devices.list` | none | `adapterId:int` (filter) |
| `cmd.group.create` | `name:string` | `zone:string` |
| `cmd.group.get` | `groupId:int` | none |
| `cmd.groups.list` | none | none |
| `cmd.room.create` | `name:string` | `zone:string` |
| `cmd.room.get` | `roomId:int` | none |
| `cmd.rooms.list` | none | none |
| `cmd.scene.create` | `name:string` | `description:string` |
| `cmd.scene.invoke` | `sceneId:int` | `action:string` |
| `cmd.scene.scope.assign` | `sceneId:int` | `roomId:int`, `groupId:int` (normally at least one > 0) |
| `cmd.scenes.list` | none | none |
| `cmd.settings.get` | `key:string` | none |
| `cmd.settings.set` | `key:string`, `value:any` | none |
| `cmd.settings.user.get` | `key:string` | `userId:int` (admin-only override target) |
| `cmd.settings.user.set` | `key:string`, `value:any` | `userId:int` (admin-only override target) |
| `cmd.tr.get` | `locale:string`, `msg:string` | `ctx:string`, `hash:string` |
| `cmd.tr.set` | `locale:string`, `msg:string`, `value:string` | `ctx:string` |
| `cmd.users.delete.set` | `userId:int` | none |
| `cmd.users.enabled.set` | `userId:int`, `enabled:bool` | none |
| `cmd.users.flags.set` | `userId:int`, `flags:int` | none |
| `cmd.users.list` | none | none |

### `sync.*` request payload

| Topic | Required payload fields | Optional payload fields |
| --- | --- | --- |
| `sync.hello.get` | none | `version:int`, `clientName:string`, `clientVersion:string`, `clientId:string`, `authToken:string` |
| `sync.ping.get` | none | none |

### `event.*` payload (server -> client)

| Topic | Required payload fields | Optional payload fields |
| --- | --- | --- |
| `event.adapter.added` | `adapter:object` | none |
| `event.adapter.connectionStateChanged` | `adapterId:int`, `connected:bool` | `lastStateChangeMs:int64` |
| `event.adapter.error` | `adapterId:int`, `message:string` | `params:any[]`, `ctx:string`, `originType:int`, `originId:string` |
| `event.adapter.removed` | `adapter:object` | none |
| `event.adapter.updated` | `adapter:object` | none |
| `event.automation.notification` | `automationId:int`, `nodeId:int`, `message:string`, `payload:any`, `tsMs:int64` | none |
| `event.automations.changed` | `automations:object[]` | none |
| `event.channel.stateChanged` | `channelId:int`, `value:any`, `tsMs:int64` | `valueName:string` |
| `event.device.added` | `adapter:object`, `device:object`, `channels:object[]` | none |
| `event.device.changed` | `adapter:object`, `device:object`, `channels:object[]` | none |
| `event.device.removed` | `adapter:object`, `device:object` | none |
| `event.group.removed` | `group:object` | none |
| `event.group.updated` | `group:object` | none |
| `event.room.removed` | `room:object` | none |
| `event.room.updated` | `room:object` | none |

## 7. Version-1 Policy

v1 has no backward-compatibility layer for protocol topic semantics.

- no deprecated aliases
- no sync/cmd dual-topic support
- one canonical topic per operation class

If an operation is async, it is exposed only as `cmd.*`.

## 8. Open Decisions (to review together)

1. Should discovery remain async-stream style only, or offer sync snapshot mode too?
2. Should auth remain fully `sync.*`, or include async flows for external providers?

## 9. Decision Log

### 2026-02-22

- `list/get` remains `cmd.*` (async) for v1.
- settings/user-settings/users operations move to `cmd.*` (async) for v1.
- no deprecated topic aliases and no backward-compatibility shim in v1.
- Rationale:
  - avoids blocking-style cross-thread request handling for larger reads
  - protects core runtime responsiveness under load
  - keeps command semantics uniform for all transports
