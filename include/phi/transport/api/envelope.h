#pragma once

// The client-facing envelope: what a transport puts on its wire.
//
// `type`, `topic`, `cid` and the ack / protocol-error / sync-response payloads
// are protocol surface - a client parses them - so every transport speaking the
// phi protocol has to produce the same shapes. They existed as copies in
// phi-transport-ws and phi-transport-cli, which is how those two drifted; this
// header is the single place they come from now.
//
// Qt-free, and compiled into the plugin alone: phi-core never sees these
// functions, so adding to this header is not a change to the interface that the
// version gate protects.

#include "jsontext.h"
#include "transporttypes.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <string_view>

namespace phicore::transport {

// Envelope `type` values.
inline constexpr std::string_view kEnvelopeTypeCmd = "cmd";
inline constexpr std::string_view kEnvelopeTypeEvent = "event";
inline constexpr std::string_view kEnvelopeTypeResponse = "response";
inline constexpr std::string_view kEnvelopeTypeError = "error";

// Envelope `topic` values a transport produces on its own, as opposed to the
// ones it forwards from core.
inline constexpr std::string_view kTopicCmdAck = "cmd.ack";
inline constexpr std::string_view kTopicCmdResponse = "cmd.response";
inline constexpr std::string_view kTopicSyncResponse = "sync.response";
inline constexpr std::string_view kTopicProtocolError = "protocol.error";

/// Error code used for an ack that carries a failure from core.
inline constexpr std::string_view kAckErrorCodeCore = "core_error";

// Protocol error codes and their messages. A frame that never reached core is
// still an answer a client has to recognise, so the wording is shared: two
// transports describing the same malformed frame differently is a protocol
// difference, not a cosmetic one.
inline constexpr std::string_view kErrorCodeInvalidJson = "invalid_json";
inline constexpr std::string_view kErrorCodeMissingCid = "missing_cid";
inline constexpr std::string_view kErrorCodeInvalidType = "invalid_type";
inline constexpr std::string_view kErrorCodeMissingTopic = "missing_topic";
inline constexpr std::string_view kErrorCodeUnknownTopic = "unknown_topic";

inline constexpr std::string_view kMessageInvalidJson = "Payload must be a valid JSON object.";
inline constexpr std::string_view kMessageMissingCid = "Commands must include numeric 'cid'.";
inline constexpr std::string_view kMessageInvalidType = "Only messages with type='cmd' are supported.";
inline constexpr std::string_view kMessageMissingTopic = "Missing command topic.";

/**
 * @brief Assemble `{"type":..,"topic":..,["cid":..,]"payload":{..}}`.
 *
 * Pure concatenation: `payloadJson` is spliced in as text, so an event that came
 * from core reaches the wire without being parsed on the way. An empty payload
 * becomes `{}`; `cid` is omitted where there is nothing to correlate, which is
 * what distinguishes an event from a response.
 */
[[nodiscard]] inline JsonText makeEnvelope(std::string_view type,
                                           std::string_view topic,
                                           std::optional<CmdId> cid,
                                           std::string_view payloadJson)
{
    JsonText out("{");
    out += jsonField("type", jsonQuoted(type));
    out += ',';
    out += jsonField("topic", jsonQuoted(topic));
    if (cid.has_value()) {
        out += ',';
        out += jsonField("cid", std::to_string(*cid));
    }
    out += ',';
    out += jsonField("payload", payloadJson);
    out += '}';
    return out;
}

/**
 * @brief The `cmd.ack` payload: whether core took the command.
 *
 * `error` is present and `null` on acceptance rather than absent, so a client
 * reads one shape in both cases.
 */
[[nodiscard]] inline JsonText makeAckPayload(bool accepted,
                                             std::string_view cmdTopic,
                                             std::string_view errorMessage = {},
                                             std::string_view errorCode = kAckErrorCodeCore)
{
    JsonText out("{");
    out += jsonField("accepted", accepted ? "true" : "false");
    out += ',';
    out += jsonField("cmd", jsonQuoted(cmdTopic));
    out += ',';
    if (errorMessage.empty()) {
        out += jsonField("error", "null");
    } else {
        out += jsonField("error",
                         jsonObject({{"code", jsonQuoted(errorCode)},
                                     {"message", jsonQuoted(errorMessage)}}));
    }
    out += '}';
    return out;
}

/**
 * @brief The `protocol.error` payload for a frame that never reached core -
 * malformed JSON, a missing `cid`, an unknown envelope type.
 */
[[nodiscard]] inline JsonText makeProtocolErrorPayload(std::string_view code,
                                                       std::string_view message)
{
    return jsonObject({{"code", jsonQuoted(code)},
                       {"message", jsonQuoted(message)}});
}

/**
 * @brief The `sync.response` payload for a call core rejected.
 *
 * Only `message` and `ctx` are surfaced. The rest of `Error` - level, category,
 * flags, source - describes how core would log the failure, not what the caller
 * asked for, and putting it on the wire would make every rejection a diagnostic
 * dump.
 */
[[nodiscard]] inline JsonText makeSyncRejectionPayload(
    const std::optional<Error> &error,
    std::string_view fallbackMessage = "Sync call rejected")
{
    const std::string_view message =
        (error.has_value() && !error->message.empty()) ? std::string_view(error->message)
                                                       : fallbackMessage;
    const JsonText errorObject = (error.has_value() && !error->ctx.empty())
        ? jsonObject({{"message", jsonQuoted(message)}, {"ctx", jsonQuoted(error->ctx)}})
        : jsonObject({{"message", jsonQuoted(message)}});
    return jsonObject({{"accepted", "false"}, {"error", errorObject}});
}

/**
 * @brief The `sync.response` payload for an accepted call: core's payload with
 * the topic it answers added, so a client can correlate without holding on to
 * the request.
 *
 * Core's payload is spliced, not re-encoded.
 */
[[nodiscard]] inline JsonText makeSyncResponsePayload(std::string_view payloadJson,
                                                      std::string_view syncTopic)
{
    return withJsonField(payloadJson, "sync", jsonQuoted(syncTopic));
}

/// Read a `cid` that arrived as a JSON number. Rejects negatives and NaN, both
/// of which would otherwise convert into an arbitrary correlation id.
[[nodiscard]] inline std::optional<CmdId> cidFromNumber(double value)
{
    // Written as a rejection rather than `value >= 0` so NaN falls out too.
    if (!(value >= 0.0))
        return std::nullopt;
    if (value > static_cast<double>(std::numeric_limits<CmdId>::max()))
        return std::nullopt;
    return static_cast<CmdId>(value);
}

/// Read a `cid` that arrived as a JSON string - digits only, surrounding
/// whitespace tolerated. Anything else is a malformed cid, which is worth
/// rejecting rather than turning into a zero that correlates to nothing.
[[nodiscard]] inline std::optional<CmdId> cidFromString(std::string_view text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    const auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    };
    while (begin < end && isSpace(text[begin]))
        ++begin;
    while (end > begin && isSpace(text[end - 1]))
        --end;
    if (begin >= end)
        return std::nullopt;

    CmdId value = 0;
    for (std::size_t i = begin; i < end; ++i) {
        const char c = text[i];
        if (c < '0' || c > '9')
            return std::nullopt;
        const CmdId digit = static_cast<CmdId>(c - '0');
        if (value > (std::numeric_limits<CmdId>::max() - digit) / 10)
            return std::nullopt;
        value = value * 10 + digit;
    }
    return value;
}

/**
 * @brief The `protocol.error` payload for a topic no transport can route.
 *
 * The topic is echoed because a client that got here usually sent a typo, and
 * the frame it sent is the only place that typo exists.
 */
[[nodiscard]] inline JsonText makeUnknownTopicPayload(std::string_view topic)
{
    std::string message("Unknown command topic: ");
    message.append(topic);
    return makeProtocolErrorPayload(kErrorCodeUnknownTopic, message);
}

/**
 * @brief What a transport should answer one client command with.
 *
 * Routing a command is the protocol's business, not a transport's, so the
 * decision is made once (see `TransportPluginBase::dispatchCommand`) and the
 * transport is left with what only it knows: which client asked, and how to
 * frame the answer.
 */
struct CommandOutcome {
    enum class Kind : std::uint8_t {
        SyncResponse,  ///< send as `sync.response` with the client's cid
        Ack,           ///< send as `cmd.ack` with the client's cid
        ProtocolError, ///< send as `protocol.error` with the client's cid
    };

    Kind kind = Kind::ProtocolError;
    /// Ready to send; already carries whatever the kind requires.
    JsonText payloadJson;
    /// Non-zero only for an accepted async command: the transport remembers the
    /// client under this id until `onCoreAsyncResult` brings the answer.
    CmdId cmdId = 0;
};

/**
 * @brief The envelope a `CommandOutcome` travels in: `{type, topic}`.
 *
 * Which answer goes out as which envelope is a protocol decision, so it is made
 * here rather than in a switch statement per transport - the transport is left
 * with putting the text on its socket.
 */
[[nodiscard]] inline std::pair<std::string_view, std::string_view> envelopeFor(
    CommandOutcome::Kind kind)
{
    switch (kind) {
    case CommandOutcome::Kind::SyncResponse:
        return {kEnvelopeTypeResponse, kTopicSyncResponse};
    case CommandOutcome::Kind::Ack:
        return {kEnvelopeTypeResponse, kTopicCmdAck};
    case CommandOutcome::Kind::ProtocolError:
        break;
    }
    return {kEnvelopeTypeError, kTopicProtocolError};
}

} // namespace phicore::transport
