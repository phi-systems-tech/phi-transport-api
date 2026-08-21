# Transport Protocol Contract (v1)

This document defines the protocol contract between transport plugins and the
outside world, and the mapping to `CoreFacade`.

This file is the canonical protocol source of truth for `phi-transport-*`,
`phi-core`, and protocol clients.

Scope:
- `phi-transport-api` plugin contract
- Wire semantics for `sync.*`, `cmd.*`, `event.*`, `stream.*`
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

### Correlation ID (`cid`)

Definition:
- client-generated correlation identifier per request

Required behavior:
- client sends one `cid` per request envelope (`sync.*` and `cmd.*`)
- transport echoes the same `cid` in `sync.response`, `cmd.ack`, and `cmd.response`
- `cid` uniqueness is required per connection (not globally)
- `cid` is wire-level only and not identical to internal core `CmdId`
- for streaming commands, `cid` is mandatory for the start phase (`cmd.*` -> `cmd.ack` -> `cmd.response` with `streamId`)

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
- Fast-path (quasi sync) completion is allowed when processing is immediate:
  - still route as `cmd.*`,
  - emit `cmd.ack` and then `cmd.response` as fast as possible.

V1 strictness:
- `cmd.*` requests MUST NOT be serviced through sync paths.
- A transport/plugin fallback to `sync.*` is not part of v1 and MUST NOT be used.
- Unsupported or unprocessable command topics must terminate as command rejection (`accepted=false` in `cmd.ack`) and must not emit an implicit sync-style completion.

### `event.*`

Definition:
- core-generated events pushed to clients

Required behavior:
- no request needed
- forwarded by transport
- no ACK/response correlation

### `stream.*`

Definition:
- server-driven stream lifecycle for long-running `cmd.*` operations

Required behavior:
- stream messages are emitted only after an accepted `cmd.*`
- stream payloads carry `streamId` and `cmd` (no `cid`)
- sequence is `stream.open` -> `stream.data` (0..n) -> `stream.end`
- on stream failure, `stream.error` is emitted before `stream.end`
- stream start handshake is `cid`-correlated via command responses; stream lifecycle messages are `streamId`-correlated

Long-running action runs:
- `cmd.adapter.action.invoke` remains a normal action command with one `cmd.response`
- when an action starts an observable long-running run, the action result may
  return stream attachment metadata such as:
  - `runId`
  - `streamKind` (recommended generic kind: `adapter.run`)
  - optional `streamParams`
  - optional `abortActionId`
  - optional `abortParams`
  - optional `batch`
- `streamKind` must be treated as domain data returned by the action result; it
  is not implied by the action id
- for adapter-owned observable runs, phi-core must route
  `kind = "adapter.run"` to the addressed adapter selected by
  `target.adapterId`
- target routing should evolve namespace-based:
  - `adapter.*` -> adapter-owned stream routing
  - future families such as `camera.*`, `audio.*`, `system.*` may route elsewhere
- a permissive non-discovery fallback to adapter routing is transitional behavior
  and must not become the long-term stream routing model
- live progress is not streamed implicitly through the action response itself
- clients must attach explicitly through `cmd.stream.start`
- `cmd.stream.stop` stops only the stream observation
- aborting the underlying run is a separate domain operation (for example a
  dedicated action or action mode)
- if present, `abortActionId` and `abortParams` are the canonical action
  attachment data for cancelling the run

## 3. Hard Rule: Prefix Defines Semantics

No exceptions:
- `sync.*` is always sync
- `cmd.*` is always async

This rule is stronger than operation type ("read" vs "write").
If a call must be async, it must be named `cmd.*`.

Canonical placement in v1:
- `cmd.settings.get` / `cmd.settings.set`
- `cmd.settings.user.get` / `cmd.settings.user.set`
- `cmd.users.enabled.set` / `cmd.users.flags.set` / `cmd.users.delete.set`
- `cmd.tr.get` / `cmd.tr.set`
- old `sync.settings.*`, `sync.users.*`, `sync.tr.*` variants are not part of the v1 transport contract

## 4. Correlation Model

- clients correlate command lifecycle by `cid`
- clients correlate stream lifecycle by `streamId`
- internal `CmdId` is core-internal and not exposed as wire id
- transport plugin keeps mapping:
  - internal `CmdId` -> `{connection, cid, cmdTopic}`

Streaming correlation sequence:
- client sends `cmd.*` with `cid`
- server returns `cmd.ack` with same `cid`
- server returns `cmd.response` with same `cid` and `streamId`
- client uses `streamId` (not `cid`) to correlate `stream.open/data/error/end`

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
- `error` (`null` or object `{message, params?, ctx?, level?, levelName?, category?, categoryName?, flags?, flagNames?, fields?, tsMs?, sourceType?, sourceTypeName?, sourceId?}`)
- `tsMs` (int64)
- optional: `resultValue`, `finalValue`, `resultType`, `resultTypeName`, `rollbackValue`

Runtime incidents/logging:
- core/runtime logging uses `LogEntry` for internal runtime transport
- public/upstream transport payloads remain `Error` objects
- public `Error` payloads use `message` as the canonical text field; `msg` is
  reserved for `cmd.tr.get` / `cmd.tr.set` only
- `LogEntry` lives as a shared header-only type in `phi-transport-api`, so
  `phi-core` and transport plugins can use one common in-process model
- `LogEntry` remains a value-type/header-only contract; small helper functions
  such as JSON conversion helpers may remain inline/header-only as well
- the external transport wire remains JSON-based; `LogEntry` is the shared typed
  model behind that JSON, not a public JSON replacement
- the in-process `transport -> core` boundary is moving to typed DTOs and shared
  value types such as `LogEntry`; JSON remains only at the external transport
  edges (for example WebSocket, MQTT, HTTP)
- current core-side logging implementation uses:
  - `LogEntry` as the DTO
  - a central core logging facade
  - facade instantiated in `main.cpp` and injected into `CoreServices`
  - a dedicated log worker thread
  - bounded queue with backpressure/drop policy
  - interchangeable sinks
  - convenience macros in `phi-core`, not `phi-transport-api`
  - automatic `file` / `line` / `func` capture only for `Trace` / `Debug`
- `LogEntry` carries:
  - `level:uint8` - one shared numbering across the planes: `1 = Trace`,
    `2 = Debug`, `3 = Info`, `4 = Warn`, `5 = Error`. Identical to
    `phicore::adapter::sdk::LogLevel` and to the adapter IPC wire, so a level never
    needs translating when it crosses a plane. Both headers `static_assert` these
    values; a renumber fails to compile.
  - `category:uint8` (`0x80` reserved as incident flag)
  - `message:utf8` (stored as `QByteArray`)
  - `params:any[]`
  - `ctx:utf8` (stored as `QByteArray`)
  - `fields:object`
  - `tsMs:int64`
  - `sourceType:uint8`
  - `sourceId:utf8` (stored as `QByteArray`)
- category code ranges:
  - `0..63` reserved for shared/public base categories, also usable by adapters
  - `64..127` reserved for core/runtime-local extensions, not available to adapters
- `incident` is derived from `category & 0x80`; it is not a separate source field
- human-readable names such as `levelName`, `categoryName`, and `flagNames` are
  presentation-only and must not be treated as canonical transport fields
- `sourceType` / `sourceId` is the canonical naming

`rollbackValue` rule:
- `rollbackValue` is optional in schema, but mandatory by context for `cmd.channel.invoke` when `status != Success`.
- Value must be the last authoritative channel value known by core (pre-command value).
- For non-channel commands, `rollbackValue` must be omitted.

## 5.1 Streamed Run Output

When a long-running action exposes a follow-up stream, the streamed payload
should be treated as structured run output rather than raw logs.

Recommended generic follow-up kind:

- `adapter.run`

Meaning:

- one adapter-owned observable run instance
- identified by `runId`
- attachable after the initial action result
- reusable by test adapters and non-test adapters alike

Recommended `stream.data` shape for run output:

- `entryType:string`
- `level?:int`
- `levelName?:string`
- `message?:string`
- `tsMs?:int64`
- `fields?:object`

Recommended `entryType` values:

- `step`
- `metric`
- `assertion`
- `info`
- `warn`
- `error`
- `summary`

This allows clients to render:

- progress timelines
- metrics
- assertions
- final summaries

without coupling test output to core runtime logging.

## 6. Operation Classification (v1)

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
- `cmd.transport.reload`
- `cmd.transport.restart`
- `cmd.transport.start`
- `cmd.transport.stop`
- `cmd.adapter.update`
- `cmd.stream.start`
- `cmd.stream.stop`
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
- `cmd.device.group.set`
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
- must not perform translation lookup operations that may hit external services

- `sync.hello.get`
- `sync.auth.bootstrap.set`
- `sync.auth.login.set`
- `sync.auth.logout.set`
- `sync.ping.get`

### Event (`event.*`)

Policy:
- push-only topics emitted by core and forwarded by transports
- no request/response pairing

- `event.adapter.added`
- `event.adapter.connectionStateChanged`
- `event.error`
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

Runtime incidents:
- `event.error` is the generic transport topic for incident-bearing runtime events
- the source is described in payload fields (`sourceType`, `sourceId`, optional domain ids such as `adapterId`)

### Stream (`stream.*`)

Policy:
- server-emitted topics only
- tied to accepted async commands
- lifecycle and chunk transport for long-running operations

- `stream.open`
- `stream.data`
- `stream.end`
- `stream.error`

Note:
- some `cmd.*` operations can be fast internally, but still stay async for wire-stability.

## 6.4 Transport Payload Contract (v1)

### 6.4.1 `cmd.*` request payload

Note:
- This table covers external transport client <-> core topics.
- It is not the adapter runtime sidecar IPC contract.
- Sidecar IPC is defined in the dedicated adapter contract documentation.

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
| `cmd.transport.reload` | `pluginType:string` | none |
| `cmd.transport.restart` | `pluginType:string` | none |
| `cmd.transport.start` | `pluginType:string` | none |
| `cmd.transport.stop` | `pluginType:string` | none |
| `cmd.adapter.update` | `adapterId:int` | `pluginType:string`, `externalId:string`, `name:string`, `meta:object`, `metaUser:object`, `metaRuntime:object` |
| `cmd.stream.start` | `kind:string`, `params:object` | `target:object` |
| `cmd.stream.stop` | `streamId:string` | none |
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
| `cmd.device.group.set` | `deviceId:int`, `groupId:int` | `add:bool` (default `true`) |
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

### 6.4.1.1 `cmd.adapter.reload` contract (v1)

Wire-level scope:

- `cmd.adapter.reload` targets plugin runtime by `pluginType:string`.
- `cmd.adapter.start|stop|restart` target adapter instances by `adapterId:int`.
- Client-side fan-out restarts must resolve adapter ids first (for example via `cmd.adapters.list`) and then send per-instance commands.

Required behavior:

- Normal async lifecycle applies: `cmd.ack` then `cmd.response`.
- Validation failures return `cmd.ack` with `accepted=false`.
- Execution failures return `cmd.response` with `status != Success` and structured `error`.
- Successful reload returns `cmd.response` with `status == Success`.

Out of scope for transport contract:

- Internal reload strategy (`phi-core` quiesce/reload/rollback details).
- Adapter state recovery policy.

### 6.4.1.2 `cmd.transport.reload|restart|start|stop` contract (v1)

Wire-level scope:

- `cmd.transport.reload|restart|start|stop` target transport plugin runtime by `pluginType:string`.
- They do not target adapter instances and do not operate on adapter ids.

Required behavior:

- Normal async lifecycle applies: `cmd.ack` then `cmd.response`.
- Validation failures return `cmd.ack` with `accepted=false`.
- Execution failures return `cmd.response` with `status != Success` and structured `error`.
- Successful transport lifecycle commands return `cmd.response` with `status == Success`.

Runtime config behavior:

- Immediately before `start`, `restart`, or `reload`, `phi-core` resolves the effective transport config from:
  - `/etc/phi/transports/<plugin>.json` as the default base config
  - `/var/lib/phi/transports/<plugin>/current/config.json` as the runtime override
- There is no implicit polling or automatic reload on file changes.

Semantic difference:

- `start` starts a previously stopped transport instance with freshly resolved config.
- `stop` stops the running transport instance without unloading the transport plugin binary.
- `restart` is the hard lifecycle path and performs `stop()` followed by `start(config)` on the already loaded plugin binary.
- `reload` is the code reload path and performs stop, plugin unload, plugin load, and `start(config)` so an updated transport plugin binary can be activated without restarting `phi-core`.

Out of scope for transport contract:

- Internal implementation details for stop/start/load sequencing.

### 6.4.1.3 `cmd.stream.start|stop` contract (v1)

Wire-level scope:

- `cmd.stream.start` opens one long-running stream session for one stream `kind`.
- `cmd.stream.stop` closes one open stream session by `streamId`.
- `kind` is the semantic stream source/scope selector.
- `target` is optional and carries kind-specific addressing when needed.

Initial v1 `target` rules:

- `kind = "adapter.discover"`
  - no `target` required
  - `target` SHOULD be omitted
- `kind = "network.discover"`
  - no `target` required
  - `target` SHOULD be omitted
- adapter-bound stream kinds
  - `target` is required
  - `target.adapterId:int > 0`
  - top-level `adapterId` is not part of the contract

Required behavior:

- Normal async lifecycle applies: `cmd.ack` then `cmd.response`.
- Validation failures return `cmd.ack` with `accepted=false`.
- Successful `start` returns `cmd.response` with:
  - `status == Success`
  - `streamId:string` in payload (`resultValue` or operation payload field)
- `stop` is idempotent:
  - stopping an already-ended stream should still return `Success`.

Initial discovery payload shapes:

- `kind = "adapter.discover"`
  - top-level fields:
    - `plugin:string`
    - `provider:string`
    - `externalId:string|null`
    - `label:string`
    - `hostname:string|null`
    - `ip:string|null`
    - `port:int|null`
    - `service:string|null`
    - `signal:string|null`
    - `meta:object`
  - meaning:
    - `plugin` identifies the adapter type that can handle the candidate
    - `provider` identifies how the candidate was discovered (`mdns`, `ssdp`, `manual`, ...)

- `kind = "network.discover"`
  - top-level fields:
    - `provider:string`
    - `externalId:string|null`
    - `label:string`
    - `hostname:string|null`
    - `ip:string|null`
    - `port:int|null`
    - `service:string|null`
    - `signal:string|null`
    - `meta:object`
  - meaning:
    - `provider` identifies how the network finding was discovered
    - `plugin` is intentionally omitted

Discovery payload naming rules:

- Public payload field names use:
  - `plugin` instead of `pluginType`
  - `provider` instead of raw/internal provider plugin identifiers
  - `externalId` instead of `discoveredExternalId`
  - `service` instead of `serviceType`
- `meta` may contain provider-specific extra data, but must not duplicate top-level
  fields such as `plugin`, `provider`, `externalId`, `ip`, `port`, or `service`.

Reserved `kind` values (initial v1 set):

- `adapter.discover`
- `network.discover`
- `raw.discover`
- `adapter.log`
- `camera.live`

Extensibility rule:

- `kind` is a string token by contract (not numeric enum on wire).
- Unknown `kind` must return `NotSupported`/validation rejection.
- When adding new reserved `kind` values, the same additions must be mirrored in
  the v1 adapter contract documentation (`phi-adapter-sdk`) so core/transport/sdk
  keep one aligned stream-kind vocabulary.

Naming rule:

- The command topic is intentionally generic.
- Stream mechanism is modeled by `cmd.stream.start|stop`.
- Functional meaning is modeled by `kind`, not by the topic path.
- New stream-capable subsystems must not introduce topic families such as
  `cmd.adapters.stream.*`, `cmd.transports.stream.*`, etc.

Migration note:

- Older implementation branches may still use `cmd.adapters.stream.start|stop`.
- That adapter-scoped naming is considered obsolete and must be migrated to the
  generic `cmd.stream.start|stop` topic family.
- Adapter-bound stream requests must use `target.adapterId`.
- Top-level `adapterId` is obsolete for `cmd.stream.start`.

### 6.4.2 `sync.*` request payload

| Topic | Required payload fields | Optional payload fields |
| --- | --- | --- |
| `sync.hello.get` | none | `version:int`, `clientName:string`, `clientVersion:string`, `clientId:string`, `authToken:string` |
| `sync.auth.bootstrap.set` | `username:string`, `password:string`, `clientId:string` | none |
| `sync.auth.login.set` | `username:string`, `password:string`, `clientId:string` | none |
| `sync.auth.logout.set` | `token:string` | none |
| `sync.ping.get` | none | none |

### 6.4.3 `event.*` payload (server -> client)

| Topic | Required payload fields | Optional payload fields |
| --- | --- | --- |
| `event.adapter.added` | `adapter:object` | none |
| `event.adapter.connectionStateChanged` | `adapterId:int`, `connected:bool` | `lastStateChangeMs:int64` |
| `event.error` | `message:string` | `adapterId:int`, `params:any[]`, `ctx:string`, `sourceType:int`, `sourceTypeName:string`, `sourceId:string`, `level:int`, `levelName:string`, `category:int`, `categoryName:string`, `flags:int`, `flagNames:string[]`, `fields:object`, `tsMs:int64` |
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

### 6.4.4 `stream.*` payload (server -> client)

| Topic | Required payload fields | Optional payload fields |
| --- | --- | --- |
| `stream.open` | `streamId:string`, `cmd:string`, `kind:string`, `contentType:string` | `contentEncoding:string`, `meta:object` |
| `stream.data` | `streamId:string`, `cmd:string`, `seq:int64`, `tsMs:int64` | operation-specific chunk fields (for example discovery candidate fields, log fields, media chunk fields) |
| `stream.end` | `streamId:string`, `cmd:string`, `reason:string` | none |
| `stream.error` | `streamId:string`, `cmd:string`, `message:string` | `params:any[]`, `ctx:string`, `sourceType:int`, `sourceTypeName:string`, `sourceId:string`, `level:int`, `levelName:string`, `category:int`, `categoryName:string`, `flags:int`, `flagNames:string[]`, `fields:object`, `tsMs:int64` |

Stream lifecycle rules (v1):

- For one accepted stream start:
  - exactly one `stream.open`
  - zero or more `stream.data`
  - exactly one terminal `stream.end`
- On stream failure, `stream.error` must be emitted before `stream.end`.
- After `stream.end`, no additional `stream.data` may be emitted for that `streamId`.

## 6.5 Plugin Loading and Version Gate (normative)

A transport plugin is a plain shared object. `phi-core` loads it and resolves two
exported C entry points:

| Symbol | Signature | Purpose |
| --- | --- | --- |
| `phi_transport_api_version` | `const char *()` | The interface version the plugin was built against. |
| `phi_transport_create` | `TransportInterface *()` | Creates one instance. |

`PHI_TRANSPORT_PLUGIN(Type)` in `transportinterface.h` exports both.

The C++ transport interface is a **source-level** API. There is no binary
compatibility promise, so a transport built against a different header MUST be
refused rather than loaded.

Rules:

- `phi_transport_api_version` MUST return `phicore::transport::kTransportApiVersion`,
  the constant rather than a literal, so a rebuild is sufficient.
- `phi-core` MUST read the version **before** calling `phi_transport_create`. The
  check exists to prevent using a vtable that no longer matches, so it cannot be
  the instance that answers it - by then the object has been constructed and its
  vtable used.
- A shared object that does not export `phi_transport_api_version` MUST be
  refused. This is also what a pre-1.4 Qt-plugin transport looks like, so the
  message MUST say "rebuild against this release" rather than "not a plugin".
- On mismatch `phi-core` MUST log the reason with the expected version, skip the
  plugin, and keep running. It MUST NOT fall back to loading it anyway.
- `kTransportApiVersion` MUST be bumped for every change a plugin has to react to
  in source: a virtual added, removed or reordered, and DTO fields (`LogEntry`,
  `Error`) even when the vtable is untouched. `TransportInterface` deliberately
  holds no data members, so member changes are not a concern - they live in
  `TransportPluginBase`, which exists only inside the plugin binary.
- Version skew is therefore always a load-time failure, never a runtime surprise.

Lifetime:

- `phi-core` creates the instance **on the transport's own thread**, so a `QObject`
  a plugin builds in its constructor has the right affinity from the start.
- `phi-core` destroys it through the virtual destructor, on that same thread, and
  only then unloads the library. The deleting destructor in the plugin's vtable
  runs the plugin's own `operator delete`.

## 6.6 Transport Threading and Blocking (normative)

The thread:

- `phi-core` creates one thread per loaded transport and owns it. A **Qt event
  loop runs on it**. A plugin neither creates that thread nor starts a loop on it.
- The instance is constructed on that thread (6.5), and `start(...)`, `stop(...)`
  and both `onCore*` callbacks are called on it. They are never called
  concurrently - one thread serves all of them - so a plugin's own state needs no
  locking against core.
- That event loop is why the transports phi ships work without ever calling
  `exec()`: `QWebSocketServer`, `QLocalServer` and `QTimer` need a running loop and
  get core's. A plugin MAY rely on it.
- A plugin that brings its own runtime - a foreign SDK loop, its own `poll()` -
  MUST run it on a thread it starts in `start(...)` and joins in `stop(...)`. It
  MUST NOT take over the thread core provides, because `start(...)` has to return.

Blocking:

- `start(...)` and `stop(...)` are called with core's thread **waiting** for them
  to return. That is the one way a transport can stall core, and it is not about
  the work it does afterwards - it is about how long it stays inside those two
  calls. They MUST return promptly: bind the socket, arm the listener, leave the
  rest to the event loop.
- `event.*` deliveries and async results are **posted**, not waited on. Blocking
  inside `onCoreEvent`/`onCoreAsyncResult` delays that transport's own I/O and
  queues its pending events; it does not stall core or another transport.
- `CoreFacade::invokeSync(topic, payload, timeoutMs)`:
  - `timeoutMs` bounds the **handoff to core's thread**, not the work itself. Once
    core has picked the call up, it runs to completion.
  - `timeoutMs` MUST be greater than zero; core rejects anything else instead of
    substituting a default.
  - Exceeding it yields `accepted=false` with an error. It MUST NOT turn into an
    unbounded wait.
- `CoreFacade::invokeAsync(...)` describes asynchronous *result delivery*. The
  submit itself reports `accepted`/`cmdId` synchronously and therefore waits for
  core's thread, bounded by the same budget as `invokeSync`'s default.
- A transport MUST NOT block core's thread, and MUST NOT perform long blocking
  work inside the `onCore*` callbacks - they run on the thread that also serves
  its protocol I/O.

## 6.6.1 Caller Identity (normative)

Authentication is the transport's: it owns the login procedure and its own
connections. Authorization is core's: the user capabilities describe policy about
core's resources, and a transport gate can only answer "authenticated", never
"may this user manage adapters".

For core to decide, it has to know whose call it is, so the identity is a
parameter on `invokeSync`/`invokeAsync` - not a key inside the caller's payload,
for the same reason the plugin type stopped being one (F-40).

| Kind | Meaning |
| --- | --- |
| `Anonymous` | The transport established no identity. Only the pre-auth topics are reachable: `sync.hello.get`, `sync.auth.bootstrap.set`, `sync.auth.login.set`, `sync.auth.logout.set`, `sync.ping.get`. |
| `Session` | A client the transport authenticated. `sessionToken` names the session core issued; `clientId` is the client it was issued for. |
| `TrustedLocal` | A channel whose access is the credential - a unix socket whose permissions decide who may connect. The transport asserts this and is responsible for it being true. |

Rules:

- A transport MUST pass the identity it actually established. `Anonymous` is the
  honest answer when it has none; core then refuses everything else, which is the
  intended outcome rather than a failure.
- `TrustedLocal` MUST NOT be asserted by a transport whose channel is reachable
  beyond the machine. `phi-transport-cli` qualifies because its socket is
  permission-restricted; a network transport does not.
- Core decides from the identity alone. It does not read tokens out of payloads,
  so a client cannot grant itself a capability by putting one there.
- Where a transport gets a session from is its own business: `phi-transport-ws`
  currently reads it from the frame it already parses, and will take it from its
  per-connection state once it authenticates its own connections. Core will not
  notice the difference.

## 6.7 Transport Data Path (normative)

The data path across the plugin boundary is **UTF-8 JSON text**, not a parsed
document:

- `topic` is plain UTF-8 text; `payloadJson`, `configJson` and `SyncResult::payloadJson`
  are UTF-8 JSON object text. An empty string is equivalent to `{}`.
- Core serializes an event **once** for the whole fan-out, and every transport
  receives the same text. A transport MUST be able to forward it to its wire without
  parsing - which is why an envelope nests the payload under a key rather than
  merging into it. Nesting is concatenation; merging would force a parse.
- `phi/transport/api/jsontext.h` carries the helpers for that assembly
  (`jsonQuoted`, `jsonField`, `withJsonField`) and is Qt-free. `withJsonField` exists
  for the one legitimate case of augmenting a payload the transport did not build.
- `invokeAsync` takes the plugin type as a parameter. It used to travel as a
  `__phiTransportPluginType` key inside the caller's payload, which is a hidden
  channel in a namespace the caller owns and is no longer permitted.

Rationale, and the trade-off stated plainly:

- The wire is text on both ends, so text is the representation the boundary would
  arrive at anyway. It also means a transport can later move out of process without
  its contract changing - the representation no longer has to be renegotiated.
- Outbound it is cheaper than a document boundary: one serialization in core plus a
  concatenation per client, instead of one full serialization per client.
- Inbound it costs one extra serialize/parse pair, because a transport parses its
  own frame to read the envelope and then hands the payload on as text. That sits on
  the command path (user actions), not on the event path (device traffic), which is
  the trade that was accepted.
- Identity (`pluginType`, `displayName`, ...) and diagnostics (`LogEntry`, `Error`,
  `errorString`) are still Qt types. Converting those is a separate step; it does not
  affect the data path.

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

### 2026-08-21 (later)

- The caller's identity crosses the boundary as a parameter (`CallerIdentity`),
  and the version gate moves to 1.6.0.
- Rationale:
  - authentication at the edge and authorization in core is the split we settled
    on, and the second half was impossible: nothing told core whose call it was,
    so the capabilities it hands to clients were enforced nowhere
  - a payload key would have been a hidden channel in a namespace the caller
    owns - the objection that removed `__phiTransportPluginType` in F-40
  - `TrustedLocal` lets a transport state what it actually knows: the CLI
    transport's socket permissions *are* the credential, and without a way to say
    so, local tooling would have needed credentials for a channel that is already
    privileged
- Consequences: core refuses every non-pre-auth topic from an `Anonymous` caller;
  transports pass what they established rather than what a client claims.

### 2026-08-21

- The thread a transport runs on is part of the contract, not an internal detail
  of core: core creates one per transport, owns it, and runs a Qt event loop on it
  (6.6).
- Rationale:
  - it was already true and written down nowhere, so "can I write a transport
    without Qt" was answerable only by reading `phi-core`
  - the shipped transports depend on that loop without saying so - they never call
    `exec()`, and their servers and timers need one. A rule everything relies on
    and nothing states is the kind that breaks on the first plugin that does not
  - it decides the shape of a non-Qt transport: own runtime means own thread,
    started in `start()` and joined in `stop()`
  - `start()`/`stop()` are the only calls core waits on, which makes them the only
    place a transport can stall core - worth stating next to the blocking rules
- Consequences: none in code. The contract now describes what phi-core already
  does (phi-core audit F-62).

### 2026-08-19 (later)

- The transport data path is UTF-8 JSON text (topic, payload, config), not
  `QJsonObject`.
- Rationale:
  - the wire is text on both ends, so the boundary representation matches it
  - a transport can move out of process later without its contract changing
  - the event fan-out gets cheaper: core serializes once for all transports
- Accepted cost: one extra serialize/parse pair on the inbound command path.
- The plugin type became a parameter of `invokeAsync` instead of a magic key inside
  the payload.
- Still Qt: identity strings and the diagnostics types (`LogEntry`, `Error`). A
  fully Qt-free header is a later step, and it is not what unlocks process mobility.

### 2026-08-19

- The C++ transport interface is a public **source** API, not a binary one:
  Apache-2.0 and readable by anyone, built against the `phi-core` release it
  targets, rebuilt for the next.
- Rationale:
  - transport plugins load into core's process and their signatures are Qt types,
    so a Qt upgrade alone would break any ABI promise
  - the extension point that *does* carry a binary/wire contract already exists:
    the out-of-process adapter SDK
- Consequences: interface made stateless (state moved to `TransportPluginBase`),
  IID bumped to `/2.0`, `apiVersion()` turned from decoration into a load gate.
- Plane assignment is decided by direction, not by protocol name: devices and
  device networks are adapters (southbound), clients and controllers talking to
  core are transports (northbound). A Matter bridge is a transport; the Thread
  Border Router behind it is an adapter.

### 2026-08-20

- The transport contract is Qt-free (1.4.0). `TransportInterface` is a pure
  abstract class; a plugin is a shared object exporting `phi_transport_api_version`
  and `phi_transport_create`.
- Rationale:
  - the diagnostics types and the plugin model were the last places Qt was
    *required* rather than merely convenient; a third party could not write a
    transport without it
  - a C++ abstract class over `dlopen` is exactly as sound as the source-API rule
    we already committed to - the usual ABI objection only bites when binary
    compatibility is promised, and it is not
  - the version gate gets stronger: an exported string is read before anything is
    constructed, where `apiVersion()` could only answer after the vtable was
    already in use
- Consequences: core owns a `TransportHost` QObject per transport, since the
  plugin can no longer be moved to a thread or posted to; the instance is created
  on the transport thread rather than moved there; `Q_PLUGIN_METADATA`,
  `Q_INTERFACES` and the IID are gone, and the package no longer depends on Qt.
- Not solved by this: a plugin still needs a C++ toolchain compatible with core's.
  Language independence remains the out-of-process route.

### 2026-02-22

- `list/get` remains `cmd.*` (async) for v1.
- settings/user-settings/users operations move to `cmd.*` (async) for v1.
- no deprecated topic aliases and no backward-compatibility shim in v1.
- Rationale:
  - avoids blocking-style cross-thread request handling for larger reads
  - protects core runtime responsiveness under load
  - keeps command semantics uniform for all transports
