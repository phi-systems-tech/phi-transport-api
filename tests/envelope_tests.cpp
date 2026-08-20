// Contract tests for the client-facing envelope.
//
// These shapes are what a client parses, so they are pinned byte for byte: the
// point of moving them out of the transports (F-61) is that there is now one
// answer to "what does an ack look like", and a change to it has to be made
// here, deliberately, rather than in one transport and not the other.
#include "phi/transport/api/envelope.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const char *what, const std::string &detail = {})
{
    if (condition)
        return;
    ++g_failures;
    std::printf("FAIL %s%s%s\n", what, detail.empty() ? "" : ": ", detail.c_str());
}

void checkEqual(const std::string &actual, const std::string &expected, const char *what)
{
    check(actual == expected, what, "expected " + expected + ", got " + actual);
}

using namespace phicore::transport;

void testEnvelope()
{
    checkEqual(makeEnvelope(kEnvelopeTypeResponse, "cmd.ack", CmdId{7}, "{\"a\":1}"),
               "{\"type\":\"response\",\"topic\":\"cmd.ack\",\"cid\":7,\"payload\":{\"a\":1}}",
               "response envelope carries the cid");
    // An event correlates to nothing, so it has no cid at all rather than a zero.
    checkEqual(makeEnvelope(kEnvelopeTypeEvent, "event.channel.stateChanged", std::nullopt, "{\"v\":2}"),
               "{\"type\":\"event\",\"topic\":\"event.channel.stateChanged\",\"payload\":{\"v\":2}}",
               "event envelope omits the cid");
    // A missing payload must still be a valid object on the wire.
    checkEqual(makeEnvelope(kEnvelopeTypeError, "protocol.error", std::nullopt, ""),
               "{\"type\":\"error\",\"topic\":\"protocol.error\",\"payload\":{}}",
               "empty payload becomes {}");
    // The topic comes from a client in the sync case, so it gets escaped.
    checkEqual(makeEnvelope(kEnvelopeTypeResponse, "a\"b", CmdId{1}, "{}"),
               "{\"type\":\"response\",\"topic\":\"a\\\"b\",\"cid\":1,\"payload\":{}}",
               "topic is escaped");
}

void testAckPayload()
{
    checkEqual(makeAckPayload(true, "cmd.invokeChannel"),
               "{\"accepted\":true,\"cmd\":\"cmd.invokeChannel\",\"error\":null}",
               "accepted ack");
    checkEqual(makeAckPayload(false, "cmd.invokeChannel", "Command rejected"),
               "{\"accepted\":false,\"cmd\":\"cmd.invokeChannel\","
               "\"error\":{\"code\":\"core_error\",\"message\":\"Command rejected\"}}",
               "rejected ack");
    checkEqual(makeAckPayload(false, "cmd.x", "boom", "custom_code"),
               "{\"accepted\":false,\"cmd\":\"cmd.x\","
               "\"error\":{\"code\":\"custom_code\",\"message\":\"boom\"}}",
               "custom error code");
    // error is present-but-null on success: one shape for the client either way.
    check(makeAckPayload(true, "cmd.x").find("\"error\":null") != std::string::npos,
          "accepted ack still carries an error field");
}

void testProtocolErrorPayload()
{
    checkEqual(makeProtocolErrorPayload("invalid_json", "Payload must be a valid JSON object."),
               "{\"code\":\"invalid_json\",\"message\":\"Payload must be a valid JSON object.\"}",
               "protocol error payload");
    checkEqual(makeProtocolErrorPayload("unknown_topic", "Unknown command topic: a\"b"),
               "{\"code\":\"unknown_topic\",\"message\":\"Unknown command topic: a\\\"b\"}",
               "message is escaped");
}

void testUnknownTopicPayload()
{
    checkEqual(makeUnknownTopicPayload("cmd.nope"),
               "{\"code\":\"unknown_topic\",\"message\":\"Unknown command topic: cmd.nope\"}",
               "unknown topic payload echoes the topic");
}

void testEnvelopeFor()
{
    const auto sync = envelopeFor(CommandOutcome::Kind::SyncResponse);
    check(sync.first == kEnvelopeTypeResponse && sync.second == kTopicSyncResponse,
          "sync outcome travels as a response/sync.response");
    const auto ack = envelopeFor(CommandOutcome::Kind::Ack);
    check(ack.first == kEnvelopeTypeResponse && ack.second == kTopicCmdAck,
          "ack outcome travels as a response/cmd.ack");
    const auto error = envelopeFor(CommandOutcome::Kind::ProtocolError);
    check(error.first == kEnvelopeTypeError && error.second == kTopicProtocolError,
          "protocol error travels as an error/protocol.error");
}

void testSyncRejectionPayload()
{
    Error error;
    error.message = "Permission denied";
    checkEqual(makeSyncRejectionPayload(error),
               "{\"accepted\":false,\"error\":{\"message\":\"Permission denied\"}}",
               "rejection without ctx");

    error.ctx = "sync.settings.get";
    checkEqual(makeSyncRejectionPayload(error),
               "{\"accepted\":false,\"error\":{\"message\":\"Permission denied\","
               "\"ctx\":\"sync.settings.get\"}}",
               "rejection with ctx");

    // A rejection core did not describe still has to render as one.
    checkEqual(makeSyncRejectionPayload(std::nullopt),
               "{\"accepted\":false,\"error\":{\"message\":\"Sync call rejected\"}}",
               "rejection without an error object");

    // Level, category and flags describe core's own logging, not the caller's
    // problem, and must not leak into the response.
    Error noisy;
    noisy.message = "Nope";
    noisy.level = LogLevel::Error;
    noisy.category = makeCategory(LogCategory::Security, true);
    noisy.sourceId = "core";
    const JsonText rendered = makeSyncRejectionPayload(noisy);
    check(rendered.find("category") == std::string::npos, "category stays out of the rejection");
    check(rendered.find("sourceId") == std::string::npos, "sourceId stays out of the rejection");
}

void testSyncResponsePayload()
{
    checkEqual(makeSyncResponsePayload("{\"ok\":true}", "sync.hello.get"),
               "{\"sync\":\"sync.hello.get\",\"ok\":true}",
               "core payload is spliced, not re-encoded");
    checkEqual(makeSyncResponsePayload("", "sync.hello.get"),
               "{\"sync\":\"sync.hello.get\"}",
               "empty core payload");
}

void testCidFromNumber()
{
    check(cidFromNumber(0.0) == CmdId{0}, "zero is a cid");
    check(cidFromNumber(42.0) == CmdId{42}, "positive number");
    check(!cidFromNumber(-1.0).has_value(), "negative is rejected");
    check(!cidFromNumber(std::nan("")).has_value(), "NaN is rejected");
    check(!cidFromNumber(1e30).has_value(), "out-of-range is rejected");
}

void testCidFromString()
{
    check(cidFromString("42") == CmdId{42}, "digits");
    check(cidFromString(" 42 ") == CmdId{42}, "surrounding whitespace is tolerated");
    check(!cidFromString("").has_value(), "empty is rejected");
    check(!cidFromString("   ").has_value(), "blank is rejected");
    check(!cidFromString("abc").has_value(), "non-digits are rejected");
    check(!cidFromString("4a").has_value(), "trailing garbage is rejected");
    check(!cidFromString("-1").has_value(), "negative text is rejected");
    // A cid that does not fit is malformed, not a wrapped-around correlation id.
    check(!cidFromString("99999999999999999999999").has_value(), "overflow is rejected");
    check(cidFromString("18446744073709551615") == CmdId{18446744073709551615ULL},
          "the largest cid still parses");
}

} // namespace

int main()
{
    testEnvelope();
    testAckPayload();
    testProtocolErrorPayload();
    testUnknownTopicPayload();
    testEnvelopeFor();
    testSyncRejectionPayload();
    testSyncResponsePayload();
    testCidFromNumber();
    testCidFromString();

    if (g_failures == 0) {
        std::printf("envelope_tests: all passed\n");
        return 0;
    }
    std::printf("envelope_tests: %d failure(s)\n", g_failures);
    return 1;
}
