# phi-transport-api

Header-only, Qt-free API for PHI transport plugin development.

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
- A prebuilt transport from another release is refused at load: the plugin exports
  its API version as a plain string, and `phi-core` reads it **before constructing
  anything**. A mismatch is a clear log line and a skipped plugin - never a plugin
  bound to a vtable that no longer matches.
- The reason is structural, not laziness: transport plugins are loaded **into
  core's process**, and a C++ abstract class across a `dlopen` boundary is only
  sound while both sides are built from the same source release with a compatible
  toolchain. That is exactly what we promise, and nothing more.
- Source compatibility is the thing we do keep an eye on, and it is the reason
  adding a field to `LogEntry` is cheap while adding a virtual to
  `TransportInterface` costs an API version bump.

The contract is Qt-free as of 1.4.0: `topic`, payloads and config are UTF-8 text
(see `jsontext.h` and PROTOCOLL.md 6.7), the diagnostics types carry `std::string`,
JSON text and `Scalar`, and the plugin model is two exported C functions rather
than a Qt plugin. A plugin may still use Qt internally - the two transports phi
ships do - but it no longer has to.

What Qt-free does **not** mean here: it is still a C++ contract, so a plugin needs
a toolchain compatible with core's. Running out of process - the `sidecar` hosting
below - moves the plugin out of core's fault domain, not out of its toolchain:
the host is a phi-core binary and loads the same `.so`. A transport that should
be free of the toolchain too belongs on the adapter plane, whose wire contract
is versioned for exactly that.

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

How such a transport attaches is decided (2026-09-06): the plugin is the same
`.so` either way, and its configuration says where it runs. With
`"hosting": "sidecar"` in the transport's config, phi-core starts
`phi-transport-host` with the plugin and relays the contract over a Unix
socket - start, stop, the config and the events down; `invokeSync`,
`invokeAsync`, the caller identity and the log up. The plugin cannot tell the
difference, which is what this contract's text-only data path (6.7) was for.
The WS transport was the first to run that way, measured against in process;
see phi-core's `docs/file-structure.md` ("Hosting mode") for the
configuration and `src/core/transporthostprotocol.h` for the frames.

## Architecture Contract

- Transport plugins are plain shared objects, loaded by `dlopen`/`QLibrary` and
  resolved through two exported C entry points (PROTOCOLL.md 6.5).
- `phi-core` remains the only valid backend facade for API calls.
- Auth messages are processed and validated in `phi-core`.
- Transport plugins should focus on transport framing, session handling, and protocol I/O.
- One transport plugin instance per plugin type is supported.
- Core facade injection is owned by the transport manager in `phi-core`.
- Transport runtime configuration is passed into `start(std::string_view configJson, ...)` by `phi-core` as UTF-8 JSON object text.
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
  - `CallerIdentity` says on whose behalf a call is made - `Anonymous`, an
    authenticated `Session`, or a `TrustedLocal` channel whose access is itself
    the credential. Core authorizes from it; see PROTOCOLL.md 6.6.1.
- `envelope.h`
  - The client-facing envelope: `type`/`topic`/`cid` assembly plus the `cmd.ack`,
    `protocol.error` and `sync.response` payload shapes, the protocol error codes
    and their messages, and `cid` parsing.
  - These are protocol surface, not scaffolding: a client parses them, so every
    transport has to produce the same ones. They existed as copies in the two
    shipped transports until those drifted apart (phi-core audit F-61).
  - **Qt-free**, and compiled into the plugin alone.
  - Covered by `transport_envelope_tests`.
- `transportinterface.h`
  - `TransportInterface`: the plugin interface. Pure and not a `QObject` - it holds
    no data members of its own, so its layout is not part of the plugin contract
    and only the vtable is. A plugin that wants Qt inherits `QObject` alongside it.
  - `TransportPluginBase`: header-only convenience base that holds the core facade
    and the `writeLog` / `callCoreSync` / `callCoreAsync` helpers. It is compiled
    into the plugin alone - core never sees the type - so adding to it is not a
    contract change. Deriving from it is optional.
  - `TransportPluginBase::dispatchCommand`: routes one client command - `sync.*`
    synchronously, `cmd.*` as an async submit, anything else an unknown topic -
    and returns a `CommandOutcome` the transport only has to put on its socket.
    Routing lives here so two transports cannot answer the same topic differently;
    there is deliberately no fallback between the two paths.
    Covered by `transport_dispatch_tests`.
  - Core facade is attached by manager friendship (not by plugin callers).
  - Async core command completions are delivered via `onCoreAsyncResult(cmdId, payload)`.
  - `kTransportApiVersion` carries the interface version; see "Stability" above
    and the version gate in `PROTOCOLL.md`.

## Minimal Plugin Skeleton

A plugin is a shared object exporting two C entry points. `PHI_TRANSPORT_PLUGIN`
writes both for you.

```cpp
#include <phi/transport/api/transportinterface.h>

// Note the base class: TransportPluginBase, not TransportInterface. It brings the
// core facade and the log/call helpers. It is compiled into the plugin alone, so
// deriving from it is optional but usually what you want.
class MyTransport final : public phicore::transport::TransportPluginBase
{
public:
    std::string pluginType() const override { return "my"; }
    std::string displayName() const override { return "My transport"; }
    std::string description() const override { return "Example transport"; }

    bool start(std::string_view configJson, std::string *errorString) override;
    void stop() override;
};

PHI_TRANSPORT_PLUGIN(MyTransport)
```

No Qt anywhere. A plugin that *wants* Qt - for a `QWebSocketServer`, say -
inherits `QObject` alongside, QObject first, as Qt requires:

```cpp
class WsTransport final : public QObject, public phicore::transport::TransportPluginBase
{
    Q_OBJECT
    // ...
};
```

phi-core constructs the instance on the transport's own thread, so a `QObject`
built in the constructor already has the right thread affinity. Core deletes it
through the virtual destructor, which runs the plugin's own `operator delete`.

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
