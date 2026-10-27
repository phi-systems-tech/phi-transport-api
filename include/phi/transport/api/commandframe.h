#pragma once

// Reading a client's command frame: the parse and checks every transport that
// speaks the phi protocol makes before a command reaches core.
//
// They were copies in phi-transport-ws and phi-transport-cli, and the copies
// had drifted (WS trimmed the topic at both ends, the CLI only at the end) and
// shared a crash: nlohmann's value() throws when a member is there with another
// type, so {"type": 5} - a frame anyone on the LAN could send before logging
// in - ended the WS sidecar, and the same line on the CLI socket ended core.
// Everything here reads by type and never throws on what a client sent.
//
// Built on nlohmann::json, unlike envelope.h (text only): a target that
// includes it links nlohmann_json, as both transports already do.

#include "envelope.h"

#include <phi/runtime/str.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace phicore::transport {

/// A `cid` as a client may send it: a JSON number or a string of digits.
[[nodiscard]] inline std::optional<CmdId> readCid(const nlohmann::json &value)
{
    if (value.is_number())
        return cidFromNumber(value.get<double>());
    if (value.is_string())
        return cidFromString(value.get_ref<const std::string &>());
    return std::nullopt;
}

/// The string member `key` of `object`; empty when `object` is not an object
/// or the member is missing or not a string.
[[nodiscard]] inline std::string stringMember(const nlohmann::json &object, const char *key)
{
    if (!object.is_object())
        return {};
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

/// A command frame that passed the checks.
struct CommandFrame {
    CmdId cid = 0;
    /// Trimmed at both ends.
    std::string topic;
    /// The `payload` object; an empty object when absent or not an object.
    nlohmann::json payload = nlohmann::json::object();
};

/// Why a frame was refused, as the protocol error a client gets back; `cid`
/// is set when the frame carried a readable one.
struct FrameError {
    std::optional<CmdId> cid;
    std::string_view code;
    std::string_view message;
};

/// Parse and check one frame: a JSON object, a readable `cid`, `type` "cmd"
/// and a non-empty `topic`, in that order (the order decides which error a
/// client sees for a frame with several faults).
[[nodiscard]] inline std::variant<CommandFrame, FrameError> parseCommandFrame(std::string_view text)
{
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (!doc.is_object())
        return FrameError{std::nullopt, kErrorCodeInvalidJson, kMessageInvalidJson};

    const auto cidIt = doc.find("cid");
    const std::optional<CmdId> cid = cidIt != doc.end() ? readCid(*cidIt) : std::nullopt;
    if (!cid)
        return FrameError{std::nullopt, kErrorCodeMissingCid, kMessageMissingCid};

    if (stringMember(doc, "type") != kEnvelopeTypeCmd)
        return FrameError{cid, kErrorCodeInvalidType, kMessageInvalidType};

    CommandFrame frame;
    frame.cid = *cid;
    frame.topic = phi::str::trimmed(stringMember(doc, "topic"));
    if (frame.topic.empty())
        return FrameError{cid, kErrorCodeMissingTopic, kMessageMissingTopic};

    const auto payloadIt = doc.find("payload");
    if (payloadIt != doc.end() && payloadIt->is_object())
        frame.payload = *payloadIt;
    return frame;
}

/// The payload of a `cmd.response`: core's result with `cmd` set to the
/// command's topic and `error: null` added when core sent no error. The one
/// outbound path that parses - deciding "absent" from raw text would be a
/// substring guess - and it is user-driven, so the parse is cheap enough.
[[nodiscard]] inline std::string makeCmdResponsePayload(std::string_view cmdTopic,
                                                        std::string_view payloadJson)
{
    nlohmann::json out = nlohmann::json::parse(payloadJson, nullptr, false);
    if (!out.is_object())
        out = nlohmann::json::object();
    out["cmd"] = std::string(cmdTopic);
    if (!out.contains("error"))
        out["error"] = nullptr;
    return out.dump();
}

} // namespace phicore::transport
