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
- By default, one instance per transport plugin type is supported (`maxInstances() == 1`).

## Public Headers

- `transporttypes.h`
  - Shared DTOs for sync call responses and error payloads.
- `corefacade.h`
  - Abstract facade that transport plugins use to call into core logic.
- `transportplugin.h`
  - Main plugin interface plus `TransportPluginBase` helper.

## Minimal Plugin Skeleton

```cpp
#include <QObject>
#include <QtPlugin>
#include <phi/transport/api/transportplugin.h>

class WsTransportPlugin final : public QObject, public phi::transport::api::TransportPluginBase
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PhiTransportPlugin_iid)
    Q_INTERFACES(phi::transport::api::TransportPlugin)

public:
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

- `phi::transport-api`

## License

Licensed under Apache License 2.0. See `LICENSE`.
