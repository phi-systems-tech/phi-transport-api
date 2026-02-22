# phi-transport-<name>

## Overview

Short user-facing summary of what this transport plugin provides.

## Supported Protocols / Endpoints

- List protocol(s) and endpoint types (for example WebSocket, MQTT, CLI).
- Mention whether this is server-side, client-side, or both.

## Network Exposure

- Default bind/interface behavior.
- LAN-only / WAN-capable expectations.

## Authentication & Security

- Which auth model is used.
- How credentials/tokens are handled.
- TLS/secure transport notes.

## Known Issues

- List confirmed limitations in user terms.

## License

See `LICENSE`.

---

## Developer Documentation

### Purpose

Describe plugin scope and transport boundaries.

### Features

- Session handling
- Request/response framing
- Event streaming
- Reconnect behavior

### Runtime Model

- Runs as `TransportInterface` Qt plugin.
- Exactly one plugin instance per transport plugin type.
- Plugin thread model and long-running I/O behavior.

### Core Integration Contract

- All core calls go through `callCoreSync` / `callCoreAsync`.
- Do not access core registries/managers directly.
- Auth is validated in `phi-core`.

### Protocol Contract

- Incoming command envelope format.
- ACK and async result correlation model.
- Error mapping strategy.

### Runtime Requirements

- Required `phi-core` version/range.
- Required network services, ports, certificates.

### Build Requirements

- Build tools and required libraries.
- Qt modules used.

### Configuration

- Config file location and ownership.
- Required and optional fields.
- Minimal config example:

```json
{
  "host": "0.0.0.0",
  "port": 5022,
  "tls": false
}
```

### Build

```bash
cmake -S . -B build
cmake --build build --parallel
```

Notes:

- By default, this project may expect `phi-transport-api` at `../phi-transport-api`.
- Alternatively install `phi-transport-api-dev` and set `CMAKE_PREFIX_PATH`.

### Installation

- Output shared library name.
- Deployment location (`/opt/phi/plugins/transports/` or distro equivalent).

### Observability

- Logging category names.
- Metrics/counters exposed.

### Troubleshooting

- Common error -> cause -> fix.

### Maintainers

- Phi Systems Tech team

### Issue Tracker

- `https://github.com/phi-systems-tech/phi-transport-<name>/issues`

### Releases / Changelog

- Releases: `https://github.com/phi-systems-tech/phi-transport-<name>/releases`
- Tags: `https://github.com/phi-systems-tech/phi-transport-<name>/tags`
