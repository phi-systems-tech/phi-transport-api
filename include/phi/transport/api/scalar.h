#pragma once

// Placeholder values for translatable messages.
//
// A log message or error carries an English base string with `%1`, `%2`
// placeholders and an ordered list of values to substitute. Those values are
// rendered (a text sink) or serialized (the wire) - nothing branches on their
// type - but keeping them typed means the producer does not have to build JSON
// at every call site, and the escaping happens once, here.
//
// Deliberately Qt-free, and deliberately the same shape as the adapter
// contract's `phicore::adapter::v1::ScalarValue`. The two planes never exchange
// scalars - a transport does not forward an adapter's params - so this is one
// idea written twice rather than one value described two ways, and neither
// plane has to include the other's headers.

#include "jsontext.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

namespace phicore::transport {

using Scalar = std::variant<std::monostate, bool, std::int64_t, double, std::string>;
using ScalarList = std::vector<Scalar>;

namespace detail {

/// Shortest round-trip form, locale-independent - unlike std::to_string, which
/// follows the C locale and would emit a comma as the decimal separator.
[[nodiscard]] inline std::string doubleToText(double value)
{
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc())
        return "0";
    return std::string(buffer, result.ptr);
}

} // namespace detail

/// JSON value text for one scalar. Non-finite doubles become `null`: JSON has no
/// NaN or Infinity, and emitting one produces a frame no parser accepts.
[[nodiscard]] inline JsonText scalarToJson(const Scalar &value)
{
    return std::visit(
        [](const auto &entry) -> JsonText {
            using T = std::decay_t<decltype(entry)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return JsonText("null");
            } else if constexpr (std::is_same_v<T, bool>) {
                return JsonText(entry ? "true" : "false");
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::to_string(entry);
            } else if constexpr (std::is_same_v<T, double>) {
                return std::isfinite(entry) ? detail::doubleToText(entry) : JsonText("null");
            } else {
                return jsonQuoted(entry);
            }
        },
        value);
}

/// JSON array text for a whole placeholder list.
[[nodiscard]] inline JsonText scalarListToJson(const ScalarList &values)
{
    JsonText out("[");
    bool first = true;
    for (const Scalar &value : values) {
        if (!first)
            out += ',';
        first = false;
        out += scalarToJson(value);
    }
    out += ']';
    return out;
}

/// Rendering for a text sink substituting `%1`. An absent value renders empty,
/// so a missing placeholder leaves a gap rather than the word "null".
[[nodiscard]] inline std::string scalarToText(const Scalar &value)
{
    return std::visit(
        [](const auto &entry) -> std::string {
            using T = std::decay_t<decltype(entry)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return std::string();
            } else if constexpr (std::is_same_v<T, bool>) {
                return std::string(entry ? "true" : "false");
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::to_string(entry);
            } else if constexpr (std::is_same_v<T, double>) {
                return std::isfinite(entry) ? detail::doubleToText(entry) : std::string();
            } else {
                return entry;
            }
        },
        value);
}

} // namespace phicore::transport
