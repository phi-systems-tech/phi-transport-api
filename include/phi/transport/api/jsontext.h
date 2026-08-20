// UTF-8 JSON text as the transport data-path representation.
//
// The transport boundary passes payloads as JSON text rather than as a parsed
// document: the wire is text on both ends, so the representation costs nothing
// where it matters, and a transport can later move out of process without its
// contract changing. See README ("Stability") and PROTOCOLL.md 6.7.
//
// Deliberately Qt-free - this is the first piece of the contract that no longer
// depends on Qt.
#pragma once

#include <cstddef>
#include <initializer_list>
#include <utility>
#include <string>
#include <string_view>

namespace phicore::transport {

/// UTF-8 JSON text. Object-shaped unless a field says otherwise.
using JsonText = std::string;

/// An empty JSON object, for payload slots that carry nothing.
[[nodiscard]] inline JsonText emptyJsonObject()
{
    return JsonText("{}");
}

/// `true` if `json` holds nothing but whitespace or an empty object.
[[nodiscard]] inline bool isEmptyJsonObject(std::string_view json)
{
    for (const char c : json) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '{' || c == '}')
            continue;
        return false;
    }
    return true;
}

/// A JSON string literal for `text`, quoted and escaped.
[[nodiscard]] inline JsonText jsonQuoted(std::string_view text)
{
    JsonText out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                static const char *digits = "0123456789abcdef";
                out += "\\u00";
                out.push_back(digits[(static_cast<unsigned char>(c) >> 4) & 0x0f]);
                out.push_back(digits[static_cast<unsigned char>(c) & 0x0f]);
            } else {
                // UTF-8 continuation bytes pass through unchanged.
                out.push_back(c);
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

/**
 * @brief Nest `payloadJson` under `key` inside a fresh JSON object.
 *
 * Pure concatenation: nesting an object text under a key needs no parsing and no
 * escaping of the payload, which is what makes a text boundary cheap for envelope
 * assembly. An empty or blank payload becomes `{}`.
 */
[[nodiscard]] inline JsonText jsonField(std::string_view key, std::string_view valueJson)
{
    JsonText out = jsonQuoted(key);
    out += ':';
    out += valueJson.empty() ? std::string_view("{}") : valueJson;
    return out;
}

/**
 * @brief Assemble an object from members whose values are already JSON text.
 *
 * For the common case of a handful of structured log fields:
 * `jsonObject({{"host", jsonQuoted(host)}, {"port", std::to_string(port)}})`.
 * Values are inserted verbatim, so a string value must be quoted by the caller -
 * an unquoted one is a caller bug, not something to guess at.
 */
[[nodiscard]] inline JsonText jsonObject(
    std::initializer_list<std::pair<std::string_view, std::string_view>> members)
{
    JsonText out("{");
    bool first = true;
    for (const auto &[key, valueJson] : members) {
        if (!first)
            out += ',';
        first = false;
        out += jsonField(key, valueJson);
    }
    out += '}';
    return out;
}

/**
 * @brief Insert `key: valueJson` as the first member of the object text `objectJson`.
 *
 * For the case where a transport augments a payload it did not build - the WS/CLI
 * sync response adds its own `sync` field to core's payload. Existing members are
 * kept and not re-parsed. A blank or empty input is treated as `{}`.
 */
[[nodiscard]] inline JsonText withJsonField(std::string_view objectJson,
                                            std::string_view key,
                                            std::string_view valueJson)
{
    JsonText out("{");
    out += jsonField(key, valueJson);
    if (!isEmptyJsonObject(objectJson)) {
        const std::size_t open = objectJson.find('{');
        const std::size_t close = objectJson.rfind('}');
        if (open != std::string_view::npos && close != std::string_view::npos && close > open) {
            out += ',';
            out += objectJson.substr(open + 1, close - open - 1);
        }
    }
    out += '}';
    return out;
}

} // namespace phicore::transport
