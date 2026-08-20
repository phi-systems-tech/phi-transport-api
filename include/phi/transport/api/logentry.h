#pragma once

// The structured log record that crosses the transport boundary.
//
// Qt-free: this is the schema of a log frame, so an implementation that does not
// use Qt - an out-of-process transport, or an in-process plugin built against a
// different toolkit - can read what a log record contains without a Qt shim.

#include "jsontext.h"
#include "scalar.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace phicore::transport {

// Shared log vocabulary.
//
// These values are the phi log numbering: identical on the adapter IPC wire and in
// phicore::adapter::sdk::LogLevel, so a value never needs translating when it
// crosses a plane. Two divergent numberings once made every adapter log level
// arrive one step off in core (F-36/F-39); the static_asserts below are what keeps
// them from drifting apart again - a renumber fails to compile.
enum class LogLevel : std::uint8_t {
    Trace = 1,
    Debug = 2,
    Info  = 3,
    Warn  = 4,
    Error = 5,
};

static_assert(static_cast<std::uint8_t>(LogLevel::Trace) == 1, "log vocabulary: Trace is 1");
static_assert(static_cast<std::uint8_t>(LogLevel::Debug) == 2, "log vocabulary: Debug is 2");
static_assert(static_cast<std::uint8_t>(LogLevel::Info) == 3, "log vocabulary: Info is 3");
static_assert(static_cast<std::uint8_t>(LogLevel::Warn) == 4, "log vocabulary: Warn is 4");
static_assert(static_cast<std::uint8_t>(LogLevel::Error) == 5, "log vocabulary: Error is 5");

// 0..63 is the shared range that adapters may also use; 64..127 is reserved for
// core-local extensions. Bit 0x80 is the incident flag.
enum class LogCategory : std::uint8_t {
    Internal    = 0,
    Lifecycle   = 1,
    Discovery   = 2,
    Network     = 3,
    Protocol    = 4,
    Device      = 5,
    Config      = 6,
    Performance = 7,
    Security    = 8,
    Database    = 9,

    Transport  = 64,
    Automation = 65,
    Auth       = 66,
    Storage    = 67,
    Plugin     = 68,
    Api        = 69,
    System     = 70,
};

static_assert(static_cast<std::uint8_t>(LogCategory::Internal) == 0, "log vocabulary: Internal is 0");
static_assert(static_cast<std::uint8_t>(LogCategory::Database) == 9, "log vocabulary: Database is 9");
static_assert(static_cast<std::uint8_t>(LogCategory::Transport) == 64,
              "log vocabulary: core-local categories start at 64");

enum class LogSourceType : std::uint8_t {
    Unknown    = 0,
    Core       = 1,
    Adapter    = 2,
    WebSocket  = 3,
    Cli        = 4,
    Transport  = 5,
    Automation = 6,
    Database   = 7,
};

struct LogEntry
{
    LogLevel      level = LogLevel::Info;
    std::uint8_t  category = static_cast<std::uint8_t>(LogCategory::Internal);
    /// English base string with `%1`, `%2` placeholders. UTF-8.
    std::string   message;
    /// Ordered values for those placeholders.
    ScalarList    params;
    /// Optional hint for translation engines. UTF-8.
    std::string   ctx;
    /// Structured extras as JSON object text; empty when there are none.
    JsonText      fields;
    std::int64_t  tsMs = 0;
    LogSourceType sourceType = LogSourceType::Core;
    std::string   sourceId;
};

inline constexpr std::uint8_t kLogIncidentFlag = 0x80;

[[nodiscard]] inline constexpr bool isIncident(std::uint8_t category)
{
    return (category & kLogIncidentFlag) != 0;
}

[[nodiscard]] inline constexpr std::uint8_t baseCategory(std::uint8_t category)
{
    return static_cast<std::uint8_t>(category & 0x7f);
}

[[nodiscard]] inline constexpr std::uint8_t makeCategory(LogCategory category, bool incident = false)
{
    const std::uint8_t value = static_cast<std::uint8_t>(category);
    return incident ? static_cast<std::uint8_t>(value | kLogIncidentFlag) : value;
}

[[nodiscard]] inline constexpr LogCategory categoryEnum(std::uint8_t category)
{
    return static_cast<LogCategory>(category & 0x7f);
}

[[nodiscard]] inline constexpr std::string_view logLevelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace: return "trace";
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warn: return "warn";
    case LogLevel::Error: return "error";
    }
    return "info";
}

[[nodiscard]] inline constexpr std::string_view logCategoryName(std::uint8_t category)
{
    switch (categoryEnum(category)) {
    case LogCategory::Internal: return "internal";
    case LogCategory::Lifecycle: return "lifecycle";
    case LogCategory::Discovery: return "discovery";
    case LogCategory::Network: return "network";
    case LogCategory::Protocol: return "protocol";
    case LogCategory::Device: return "device";
    case LogCategory::Config: return "config";
    case LogCategory::Performance: return "performance";
    case LogCategory::Security: return "security";
    case LogCategory::Database: return "database";
    case LogCategory::Transport: return "transport";
    case LogCategory::Automation: return "automation";
    case LogCategory::Auth: return "auth";
    case LogCategory::Storage: return "storage";
    case LogCategory::Plugin: return "plugin";
    case LogCategory::Api: return "api";
    case LogCategory::System: return "system";
    }
    return "internal";
}

[[nodiscard]] inline constexpr std::string_view logSourceTypeName(LogSourceType sourceType)
{
    switch (sourceType) {
    case LogSourceType::Unknown: return "unknown";
    case LogSourceType::Core: return "core";
    case LogSourceType::Adapter: return "adapter";
    case LogSourceType::WebSocket: return "ws";
    case LogSourceType::Cli: return "cli";
    case LogSourceType::Transport: return "transport";
    case LogSourceType::Automation: return "automation";
    case LogSourceType::Database: return "database";
    }
    return "unknown";
}

/**
 * @brief Encode a log record as JSON object text.
 *
 * Assembled by concatenation: `fields` is already object text and is nested
 * without being parsed, which is the same reason the data path is text
 * (PROTOCOLL.md 6.7). Optional members are omitted when empty, so a reader must
 * treat absence as the default rather than as zero.
 *
 * There is no decoding counterpart. Nothing in phi decodes a log record back
 * into a `LogEntry` - the readers are phi-ui and the log store, which consume
 * the JSON - and a decoder here would mean carrying a JSON parser in a
 * header-only API for no caller.
 */
[[nodiscard]] inline JsonText logEntryToJson(const LogEntry &entry)
{
    // The key set is exactly what the Qt implementation emitted. Unlike
    // errorToJson this record carries no derived *Name members; readers compute
    // them from the numeric values, and adding them here would be a wire change
    // dressed up as a refactor.
    JsonText out("{\"level\":");
    out += std::to_string(static_cast<int>(entry.level));
    out += ",\"category\":";
    out += std::to_string(static_cast<int>(entry.category));
    out += ",\"message\":";
    out += jsonQuoted(entry.message);
    if (!entry.params.empty()) {
        out += ",\"params\":";
        out += scalarListToJson(entry.params);
    }
    if (!entry.ctx.empty()) {
        out += ",\"ctx\":";
        out += jsonQuoted(entry.ctx);
    }
    if (!isEmptyJsonObject(entry.fields)) {
        out += ",\"fields\":";
        out += entry.fields;
    }
    if (entry.tsMs > 0) {
        out += ",\"tsMs\":";
        out += std::to_string(entry.tsMs);
    }
    out += ",\"sourceType\":";
    out += std::to_string(static_cast<int>(entry.sourceType));
    if (!entry.sourceId.empty()) {
        out += ",\"sourceId\":";
        out += jsonQuoted(entry.sourceId);
    }
    out += '}';
    return out;
}

} // namespace phicore::transport
