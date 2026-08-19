# phi-transport-api

Header-only API for PHI transport plugin development (Qt plugin).

## Purpose

`phi-transport-api` defines the public SDK surface for building transport plugins
that connect external protocols to `phi-core`.

Examples of transport plugin types:

- WebSocket transport (the phi UI and any other client speaking phi's WS protocol)
- CLI transport (`phi-cli` over a local socket)
- MQTT transport - phi as an MQTT endpoint external clients read state from and
  send commands to. Not the device-side broker path: a broker that carries device
  traffic (`zigbee2mqtt/...`) is an adapter, and is already covered by
  `phi-adapter-z2m`.
- Matter bridge - phi appears as a Matter bridge that an external controller
  commissions. The Thread Border Router it may sit behind is an adapter, not a
  transport.

## Southbound vs Northbound: which plane does it belong to?

The protocol name does not decide it, the direction does:

| | Plane | phi's role |
| --- | --- | --- |
| **Adapter** (`phi-adapter-sdk`) | southbound | phi is the *client* of a device or device network, consuming state and issuing device commands |
| **Transport** (this package) | northbound | phi is the *endpoint* a client or controller talks to, exposing phi's own model and accepting commands about it |

The same protocol legitimately appears on both planes, which is why the rule is
about direction:

| Case | Plane | Why |
| --- | --- | --- |
| Subscribe to `zigbee2mqtt/...` to learn about devices | adapter | phi consumes a device network |
| Publish phi's devices on `phi/...` and accept commands there | transport | phi is the endpoint |
| Talk to `otbr-agent` to reach Thread devices | adapter | phi joins a device network |
| Appear as a Matter bridge a controller commissions | transport | phi exposes its own model |

Edge case worth stating: a Matter bridge touches both sides - it exposes phi's
devices northbound, but needs network access to be commissioned. It stays a
transport (its purpose is the northbound exposure) and takes network access from
the Border Router adapter rather than speaking the device network itself.

## Stability: source API, not ABI

This is a **source-level** API. The package is Apache-2.0 and public, which means
anyone may read, fork, build and ship a transport - it does **not** mean binary
compatibility, and none is promised, now or later:

- A transport is built against the `phi-core` release it targets, and rebuilt for
  the next one. That is the whole compatibility contract.
- A prebuilt transport from another release is refused at load, twice over: the Qt
  plugin IID carries the interface version, and `phi-core` compares
  `apiVersion()` against `kTransportApiVersion`. Both produce a clear log line and
  a skipped plugin - never a plugin bound to a vtable that no longer matches.
- The reason is structural, not laziness: transport plugins are loaded **into
  core's process** and their signatures are Qt types, so a Qt upgrade alone would
  break any ABI promise we made.
- Source compatibility is the thing we do keep an eye on, and it is the reason
  adding a field to `LogEntry` is cheap while adding a virtual to
  `TransportInterface` costs an IID bump.

Qt in the contract is being reduced from the data path outwards: `topic`, payloads
and config are UTF-8 text (see `jsontext.h` and PROTOCOLL.md 6.7), while identity
strings and the diagnostics types (`LogEntry`, `Error`) are still Qt. The remaining
Qt dependency is the plugin model itself - `QObject`, `QThread`, `QPluginLoader` -
and a transport that should not depend on Qt at all is better served by running out
of process than by a C ABI, since the wire contract for that already exists on the
adapter plane.

If you want an extension point with a *binary* contract, use the adapter plane:
`phi-adapter-sdk` runs out of process, is Qt-free, has a versioned wire protocol
with golden-wire fixtures, and does not care what language or toolchain you use.

## When a transport should be its own process

The criterion is what the transport *brings with it*, not who writes it:

- A socket plus some JSON (WS, CLI, MQTT) belongs in process. It is small, it uses
  the same Qt event loop, and the front door benefits from the missing hop.
- A transport that drags in a foreign runtime - its own event loop, threading and
  logging, as a Matter SDK would - should be a separate process. Not for trust
  reasons but because two event loops in one process is a debugging tax you pay
  forever.

For the Matter bridge this is **open**: it is the first realistic case, and how
such a transport would attach (a second sidecar kind reusing the adapter IPC, or
something narrower) has not been decided.

## Architecture Contract

- Transport plugins are loaded as Qt plugins.
- `phi-core` remains the only valid backend facade for API calls.
- Auth messages are processed and validated in `phi-core`.
- Transport plugins should focus on transport framing, session handling, and protocol I/O.
- One transport plugin instance per plugin type is supported.
- Core facade injection is owned by the transport manager in `phi-core`.
- Transport runtime configuration is passed into `start(const QJsonObject &config, ...)` by `phi-core`.
- `phi-core` resolves that config from transport-specific JSON config in two layers:
  - `/etc/phi/transports/<plugin>.json` as the default base config
  - `/var/lib/phi/transports/<plugin>/current/config.json` as the runtime override
- Transport plugins must not read or write independent config files on their own.
- Transport plugins must not invent side JSON config files, compatibility shims, or silent fallbacks without prior user approval.
- Transport plugin lifecycle semantics are:
  - `start`: start stopped instance with freshly resolved config
  - `stop`: stop instance without unloading plugin binary
  - `restart`: stop + start on already loaded plugin binary
  - `reload`: unload/load plugin binary, then start with freshly resolved config

## Protocol Contract

- Canonical transport protocol specification: `PROTOCOLL.md`
- WebSocket-specific supplement lives in `phi-transport-ws/PROTOCOL.md`

## Public Headers

- `jsontext.h`
  - UTF-8 JSON text as the data-path representation, plus the helpers that assemble
    an envelope by concatenation (`jsonQuoted`, `jsonField`, `withJsonField`).
  - **Qt-free** - the first piece of the contract that does not depend on Qt.
  - Covered by `transport_jsontext_tests`.
- `logentry.h`
  - Shared header-only runtime log/incident value type for the in-process
    `transport -> core` boundary.
  - Defines `LogEntry`, `LogLevel`, `LogCategory`, `LogSourceType`.
  - Includes small inline helpers for incident/category handling and JSON
    conversion.
- `transporttypes.h`
  - Shared DTOs for sync/async core call results and public/upstream transport error payloads.
  - Internal runtime logging may use `LogEntry`, but external transport payloads
    continue to use `Error`.
  - Public error payloads use `message` as the canonical text field.
  - Result types are `SyncResult` and `AsyncResult` (`CmdId`-based correlation).
  - Error origin metadata is injected by `TransportManager` in `phi-core`.
- `corefacade.h`
  - Abstract facade that transport plugins use to call into core logic.
  - Async submits return `accepted + cmdId + error` for ACK/result correlation.
- `transportinterface.h`
  - `TransportInterface`: the QObject-based plugin interface. Pure - it holds no
    data members of its own, so its layout is not part of the plugin contract and
    only the vtable is.
  - `TransportPluginBase`: header-only convenience base that holds the core facade
    and the `writeLog` / `callCoreSync` / `callCoreAsync` helpers, and implements
    `apiVersion()`. It is compiled into the plugin alone - core never sees the
    type - so adding state here is not a contract change. Deriving from it is
    optional.
  - Core facade is attached by manager friendship (not by plugin callers).
  - Async core command completions are delivered via `onCoreAsyncResult(cmdId, payload)`.
  - `kTransportApiVersion` / `PHI_TRANSPORT_INTERFACE_IID` carry the interface
    version; see "Stability" above and the version gate in `PROTOCOLL.md`.

## Minimal Plugin Skeleton

```cpp
#include <QtPlugin>
#include <phi/transport/api/transportinterface.h>

// Note the base class: TransportPluginBase, not TransportInterface. It brings the
// core facade, the log/call helpers and apiVersion() - the latter must report
// kTransportApiVersion, and reporting it from the constant means a rebuild is all
// it takes to stay loadable.
class WsTransportPlugin final : public phicore::transport::TransportPluginBase
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PHI_TRANSPORT_INTERFACE_IID)
    Q_INTERFACES(phicore::transport::TransportInterface)

public:
    using phicore::transport::TransportPluginBase::TransportPluginBase;

    QString pluginType() const override { return QStringLiteral("ws"); }
    QString displayName() const override { return QStringLiteral("WebSocket"); }
    QString description() const override { return QStringLiteral("WebSocket transport"); }

    bool start(const QJsonObject &config, QString *errorString) override;
    void stop() override;
};
```

## CMake Package

This repository installs:

- headers to `include/phi/transport/api`
- CMake package config under `lib/cmake/phi-transport-api`

Imported target:

- `phicore::transport-api`

## Transport README Template

Use the common transport documentation template from:

- `TRANSPORT_README_TEMPLATE.md`

All `phi-transport-*` repositories should follow this structure to keep
documentation consistent.

## License

Licensed under Apache License 2.0. See `LICENSE`.
