#pragma once

// Result and error types exchanged across the transport boundary.
//
// Qt-free, like the log record: these define what an error looks like on the
// wire, and an implementation should be able to read that without Qt.

#include "jsontext.h"
#include "logentry.h"
#include "scalar.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace phicore::transport {

using CmdId = std::uint64_t;

enum class ErrorFlag : std::uint16_t {
    None     = 0,
    Incident = 1 << 0,
};

inline constexpr std::uint16_t operator|(ErrorFlag lhs, ErrorFlag rhs)
{
    return static_cast<std::uint16_t>(lhs) | static_cast<std::uint16_t>(rhs);
}

[[nodiscard]] inline constexpr bool hasErrorFlag(std::uint16_t flags, ErrorFlag flag)
{
    return (flags & static_cast<std::uint16_t>(flag)) != 0;
}

/// Names for the flags that have one. Unknown bits are carried but unnamed.
[[nodiscard]] inline std::vector<std::string_view> errorFlagNames(std::uint16_t flags)
{
    std::vector<std::string_view> out;
    if (hasErrorFlag(flags, ErrorFlag::Incident))
        out.push_back("incident");
    return out;
}

struct Error {
    /// English base string with `%1`, `%2` placeholders (translation key). UTF-8.
    std::string message;
    /// Ordered values for those placeholders.
    ScalarList params;
    /// Optional hint for translation engines. UTF-8.
    std::string ctx;
    LogLevel level = LogLevel::Error;
    std::uint8_t category = static_cast<std::uint8_t>(LogCategory::Internal);
    std::uint16_t flags = 0;
    /// Structured extras as JSON object text; empty when there are none.
    JsonText fields;
    std::int64_t tsMs = 0;
    std::uint8_t sourceType = static_cast<std::uint8_t>(LogSourceType::Unknown);
    std::string sourceId;
};

struct SyncResult {
    bool accepted = false;
    /// Result payload as UTF-8 JSON object text; `{}` or empty when there is none.
    JsonText payloadJson;
    std::optional<Error> error;
};

struct AsyncResult {
    bool accepted = false;
    CmdId cmdId = 0; // internal core command id; valid when accepted=true
    std::optional<Error> error;
};

/**
 * @brief Encode an error as JSON object text.
 *
 * An `Error` without a message encodes to `{}` and everything else it carries -
 * level, category, flags, fields - goes with it. That is deliberate: "no error"
 * is expressed by `SyncResult::error` being an empty optional, so a messageless
 * Error is a half-filled struct rather than a renderable error. Pinned by
 * `transport_types_tests`.
 *
 * The `*Name` members are for readers of the wire; they are derived from the
 * numeric values and carry no information of their own.
 *
 * There is no decoding counterpart, for the same reason as `logEntryToJson`:
 * nothing in phi decodes an error back into an `Error`.
 */
[[nodiscard]] inline JsonText errorToJson(const Error &error)
{
    if (error.message.empty())
        return JsonText("{}");

    JsonText out("{\"message\":");
    out += jsonQuoted(error.message);
    if (!error.params.empty()) {
        out += ",\"params\":";
        out += scalarListToJson(error.params);
    }
    if (!error.ctx.empty()) {
        out += ",\"ctx\":";
        out += jsonQuoted(error.ctx);
    }
    out += ",\"level\":";
    out += std::to_string(static_cast<int>(error.level));
    out += ",\"levelName\":";
    out += jsonQuoted(logLevelName(error.level));
    out += ",\"category\":";
    out += std::to_string(static_cast<int>(error.category));
    out += ",\"categoryName\":";
    out += jsonQuoted(logCategoryName(error.category));
    out += ",\"flags\":";
    out += std::to_string(static_cast<int>(error.flags));
    const std::vector<std::string_view> flagNames = errorFlagNames(error.flags);
    if (!flagNames.empty()) {
        out += ",\"flagNames\":[";
        bool first = true;
        for (const std::string_view name : flagNames) {
            if (!first)
                out += ',';
            first = false;
            out += jsonQuoted(name);
        }
        out += ']';
    }
    if (!isEmptyJsonObject(error.fields)) {
        out += ",\"fields\":";
        out += error.fields;
    }
    if (error.tsMs > 0) {
        out += ",\"tsMs\":";
        out += std::to_string(error.tsMs);
    }
    if (error.sourceType != static_cast<std::uint8_t>(LogSourceType::Unknown)) {
        out += ",\"sourceType\":";
        out += std::to_string(static_cast<int>(error.sourceType));
        out += ",\"sourceTypeName\":";
        out += jsonQuoted(logSourceTypeName(static_cast<LogSourceType>(error.sourceType)));
    }
    if (!error.sourceId.empty()) {
        out += ",\"sourceId\":";
        out += jsonQuoted(error.sourceId);
    }
    out += '}';
    return out;
}

/// `true` when `errorToJson` would produce nothing renderable, i.e. the caller
/// should emit `null` rather than an empty object.
[[nodiscard]] inline bool isRenderableError(const Error &error)
{
    return !error.message.empty();
}

} // namespace phicore::transport
