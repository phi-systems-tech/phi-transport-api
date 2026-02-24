# phi-transport-api

Header-only API for PHI transport plugin development (Qt plugin).

## Purpose

`phi-transport-api` defines the public SDK surface for building transport plugins
that connect external protocols to `phi-core`.

Examples of transport plugin types:

- WebSocket transport
- MQTT transport
- CLI transport
- Future protocol bridges (for example Thread-facing gateways)

## Architecture Contract

- Transport plugins are loaded as Qt plugins.
- `phi-core` remains the only valid backend facade for API calls.
- Auth messages are processed and validated in `phi-core`.
- Transport plugins should focus on transport framing, session handling, and protocol I/O.
- One transport plugin instance per plugin type is supported.
- Core facade injection is owned by the transport manager in `phi-core`.

## Protocol Contract

- Canonical transport protocol specification: `PROTOCOLL.md`
- WebSocket-specific supplement lives in `phi-transport-ws/PROTOCOL.md`

## Public Headers

- `transporttypes.h`
  - Shared DTOs for sync/async core call results and `phicore::Error`-aligned error payloads.
  - Result types are `SyncResult` and `AsyncResult` (`CmdId`-based correlation).
  - Error origin metadata is injected by `TransportManager` in `phi-core`.
- `corefacade.h`
  - Abstract facade that transport plugins use to call into core logic.
  - Async submits return `accepted + cmdId + error` for ACK/result correlation.
- `transportinterface.h`
  - Main QObject-based plugin interface for transport implementations.
  - Core facade is injected by manager friendship (not by plugin callers).
  - Async core command completions are delivered via `onCoreAsyncResult(cmdId, payload)`.

## Minimal Plugin Skeleton

```cpp
#include <QtPlugin>
#include <phi/transport/api/transportinterface.h>

class WsTransportPlugin final : public phicore::transport::TransportInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PHI_TRANSPORT_INTERFACE_IID)
    Q_INTERFACES(phicore::transport::TransportInterface)

public:
    using phicore::transport::TransportInterface::TransportInterface;

    QString pluginType() const override { return QStringLiteral("ws"); }
    QString displayName() const override { return QStringLiteral("WebSocket"); }
    QString description() const override { return QStringLiteral("WebSocket transport"); }
    QString apiVersion() const override { return QStringLiteral("1.0"); }

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
